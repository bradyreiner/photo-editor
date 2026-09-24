#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QThreadPool>
#include <functional>
#include <memory>
#include <optional>

namespace photo_editor::desktop {

// Optional, process-wide speculative RAW work: one active task and at most one
// replacement. Superseded recipes are released before they reach the pool.
// Workers own shared state, never the queue object or a controller QObject.
class RawDecodePrewarmQueue final {
 public:
  using Task = std::function<bool(photo_editor::pipeline::CancellationToken)>;
  struct Snapshot {
    int pending = 0;
    bool scheduled = false;
    quint64 started = 0;
    quint64 finished = 0;
    quint64 failed = 0;
    quint64 replaced = 0;
  };

  explicit RawDecodePrewarmQueue(QThreadPool* pool = nullptr) : pool_(pool) {}
  RawDecodePrewarmQueue(const RawDecodePrewarmQueue&) = delete;
  RawDecodePrewarmQueue& operator=(const RawDecodePrewarmQueue&) = delete;
  ~RawDecodePrewarmQueue() {
    QMutexLocker lock(&state_->mutex);
    state_->closed = true;
    cancel_locked(*state_);
  }

  void cancel() {
    QMutexLocker lock(&state_->mutex);
    cancel_locked(*state_);
  }

  void submit(QString key, Task task) {
    if (key.isEmpty() || !task) { cancel(); return; }
    auto shared = state_;
    {
      QMutexLocker lock(&shared->mutex);
      if (shared->closed || (shared->pending && shared->pending->key == key) ||
          (!shared->pending && shared->active_key == key && shared->active_cancel &&
           !shared->active_cancel->load())) return;
      if (shared->active_cancel) shared->active_cancel->store(true);
      if (shared->pending) ++shared->stats.replaced;
      shared->pending = Job{std::move(key), std::move(task), std::make_shared<std::atomic_bool>(false)};
      if (shared->stats.scheduled) return;
      shared->stats.scheduled = true;
    }
    try {
      auto* pool = pool_ != nullptr ? pool_ : QThreadPool::globalInstance();
      pool->start([shared] { drain(shared); }, -1);
    } catch (...) {
      QMutexLocker lock(&shared->mutex);
      shared->pending.reset();
      shared->stats.scheduled = false;
      ++shared->stats.failed;
    }
  }

  [[nodiscard]] Snapshot snapshot() const {
    QMutexLocker lock(&state_->mutex);
    auto result = state_->stats;
    result.pending = state_->pending ? 1 : 0;
    return result;
  }

 private:
  struct Job {
    QString key;
    Task task;
    std::shared_ptr<std::atomic_bool> canceled;
  };
  struct State {
    QMutex mutex;
    std::optional<Job> pending;
    QString active_key;
    std::shared_ptr<std::atomic_bool> active_cancel;
    Snapshot stats;
    bool closed = false;
  };
  static void cancel_locked(State& state) {
    if (state.pending) ++state.stats.replaced;
    state.pending.reset();
    if (state.active_cancel) state.active_cancel->store(true);
  }
  static void drain(const std::shared_ptr<State>& state) {
    for (;;) {
      std::optional<Job> job;
      {
        QMutexLocker lock(&state->mutex);
        if (state->closed || !state->pending) {
          state->stats.scheduled = false;
          return;
        }
        job = std::move(state->pending);
        state->pending.reset();
        state->active_key = job->key;
        state->active_cancel = job->canceled;
        ++state->stats.started;
      }
      bool success = false;
      try {
        const auto cancellation = photo_editor::pipeline::CancellationToken{
            .cancellation_requested = job->canceled.get()};
        if (!cancellation.cancelled()) success = job->task(cancellation);
      } catch (...) {
        // Optional prewarming must not terminate a pool thread or strand the
        // newest request after a decoder exception.
      }
      {
        QMutexLocker lock(&state->mutex);
        ++state->stats.finished;
        if (!success && !job->canceled->load()) ++state->stats.failed;
        state->active_key.clear();
        state->active_cancel.reset();
      }
    }
  }
  std::shared_ptr<State> state_ = std::make_shared<State>();
  QThreadPool* pool_ = nullptr;
};

}  // namespace photo_editor::desktop
