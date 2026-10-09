#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace photo_editor::desktop::detail {

struct EncodedLogEvent {
  QByteArray file;
  QByteArray console;
  EncodedLogEvent(QByteArray bytes = {}) : file(std::move(bytes)) {}
};

// A single file owner preserves FIFO admission order. Pending accounting includes
// the active batch, so moving entries out of the queue cannot evade its budget.
// A single oversized record is admitted alone and applies backpressure until it
// finishes. Records are never discarded to meet the queue limit.
template <typename Event>
class ObservabilityWriter final {
 public:
  enum class Result { accepted, stopped, file_error };
  struct Statistics {
    std::size_t pending_bytes = 0;
    std::size_t pending_events = 0;
    std::size_t peak_bytes = 0;
    std::size_t peak_events = 0;
    std::size_t backpressure_waits = 0;
    std::uint64_t accepted = 0;
    std::uint64_t completed = 0;
    bool opened = false;
    bool healthy = true;
  };

  ObservabilityWriter(QString path, std::function<EncodedLogEvent(const Event&)> serialize,
                      std::size_t byte_limit = 8 * 1024 * 1024,
                      std::size_t event_limit = 4096)
      : path_(std::move(path)), serialize_(std::move(serialize)),
        byte_limit_(std::max<std::size_t>(1, byte_limit)),
        event_limit_(std::max<std::size_t>(1, event_limit)),
        thread_([this] { run(); }) {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return ready_; });
  }

  ~ObservabilityWriter() { shutdown(); }

  void shutdown() {
    std::call_once(shutdown_once_, [this] {
      {
        std::lock_guard lock(mutex_);
        closing_ = true;
      }
      work_.notify_one();
      changed_.notify_all();
      thread_.join();
    });
  }

  ObservabilityWriter(const ObservabilityWriter&) = delete;
  ObservabilityWriter& operator=(const ObservabilityWriter&) = delete;

  Result submit(Event&& event, std::size_t bytes, bool immediate) {
    std::unique_lock lock(mutex_);
    const auto has_capacity = [&] {
      return closing_ || stats_.pending_events == 0 ||
          (stats_.pending_events < event_limit_ &&
           bytes <= byte_limit_ - std::min(byte_limit_, stats_.pending_bytes));
    };
    if (!has_capacity()) {
      ++stats_.backpressure_waits;
      changed_.wait(lock, has_capacity);
    }
    if (closing_) return Result::stopped;
    const auto ticket = ++stats_.accepted;
    queue_.push_back({std::move(event), bytes, ticket, immediate});
    if (immediate) ++urgent_pending_;
    stats_.pending_bytes += bytes;
    ++stats_.pending_events;
    stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.pending_bytes);
    stats_.peak_events = std::max(stats_.peak_events, stats_.pending_events);
    work_.notify_one();
    if (!immediate) return Result::accepted;
    changed_.wait(lock, [&] { return stats_.completed >= ticket; });
    return stats_.healthy ? Result::accepted : Result::file_error;
  }

  bool isWriterThread() const { return std::this_thread::get_id() == worker_id_; }

  bool flush() {
    std::unique_lock lock(mutex_);
    const auto ticket = stats_.accepted;
    flush_ticket_ = std::max(flush_ticket_, ticket);
    work_.notify_one();
    changed_.wait(lock, [&] { return stats_.completed >= ticket; });
    return stats_.healthy;
  }

  Statistics statistics() const {
    std::lock_guard lock(mutex_);
    return stats_;
  }

 private:
  struct Entry {
    Event event;
    std::size_t bytes;
    std::uint64_t ticket;
    bool immediate;
  };

  void run() {
    // QFile is created, opened, written, flushed, and destroyed on this thread.
    QFile file(path_);
    bool healthy = file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    bool failure_reported = false;
    {
      std::lock_guard lock(mutex_);
      stats_.opened = healthy;
      stats_.healthy = healthy;
      worker_id_ = std::this_thread::get_id();
      ready_ = true;
    }
    changed_.notify_all();
    for (;;) {
      std::vector<Entry> batch;
      {
        std::unique_lock lock(mutex_);
        work_.wait(lock, [this] { return closing_ || !queue_.empty(); });
        if (queue_.empty()) break;
        // Coalesce sparse routine output without delaying warnings, explicit
        // flushes, shutdown, or producers already at the admission budget.
        work_.wait_for(lock, std::chrono::milliseconds(2), [this] {
          return closing_ || urgent_pending_ > 0 || stats_.completed < flush_ticket_ ||
              queue_.size() >= 64 || stats_.pending_bytes >= byte_limit_ ||
              stats_.pending_events >= event_limit_;
        });
        while (!queue_.empty() && batch.size() < 64) {
          batch.push_back(std::move(queue_.front()));
          queue_.pop_front();
          if (batch.back().immediate) { --urgent_pending_;break; }
        }
      }
      QByteArray output;
      QByteArray console;
      std::size_t bytes = 0;
      for (const auto& entry : batch) {
        auto encoded = serialize_(entry.event);
        output += encoded.file;
        console += encoded.console;
        bytes += entry.bytes;
      }
      if (healthy) {
        qint64 offset = 0;
        while (offset < output.size()) {
          const auto written = file.write(output.constData() + offset, output.size() - offset);
          if (written <= 0) { healthy = false; break; }
          offset += written;
        }
        if (healthy && !file.flush()) healthy = false;
      }
      if (!healthy) {
        // Avoid recursive Qt logging from inside its handler's writer. Preserve
        // the complete batch on stderr, including after a partial file write.
        if (!failure_reported) {
          const auto error = QStringLiteral("Looksy diagnostic log write failed: %1 (%2)\n")
              .arg(path_, file.errorString()).toUtf8();
          std::fwrite(error.constData(), 1, static_cast<std::size_t>(error.size()), stderr);
          failure_reported = true;
        }
        std::fwrite(output.constData(), 1, static_cast<std::size_t>(output.size()), stderr);
        std::fflush(stderr);
      }
      if (!console.isEmpty()) {
        std::fwrite(console.constData(), 1, static_cast<std::size_t>(console.size()), stderr);
        std::fflush(stderr);
      }
      {
        std::lock_guard lock(mutex_);
        stats_.pending_bytes -= bytes;
        stats_.pending_events -= batch.size();
        stats_.completed = batch.back().ticket;
        stats_.healthy = healthy;
      }
      changed_.notify_all();
    }
  }

  const QString path_;
  const std::function<EncodedLogEvent(const Event&)> serialize_;
  const std::size_t byte_limit_;
  const std::size_t event_limit_;
  mutable std::mutex mutex_;
  std::condition_variable work_;
  std::condition_variable changed_;
  std::deque<Entry> queue_;
  Statistics stats_;
  std::size_t urgent_pending_ = 0;
  std::uint64_t flush_ticket_ = 0;
  bool ready_ = false;
  bool closing_ = false;
  std::once_flag shutdown_once_;
  std::thread::id worker_id_;
  std::thread thread_;
};

}  // namespace photo_editor::desktop::detail
