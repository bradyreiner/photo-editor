#pragma once

#include "edit_recipe_state.h"
#include <QFutureWatcher>
#include <QObject>
#include <QPromise>
#include <QThreadPool>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace photo_editor::desktop {

struct ActiveMaskPreviewRequest final {
  QString key;
  QString asset_id;
  QString file_path;
  QString color_signature;
  QString source_revision;
  EditRecipeState state;
  QVariantMap active_mask;
  QString mode;
  QString color;
  int edge = 512;
  int revision = 0;
  bool original = false;
  std::shared_ptr<std::atomic_bool> cancellation = std::make_shared<std::atomic_bool>(false);
};

struct ActiveMaskPreviewResult final {
  ActiveMaskPreviewRequest request;
  QString source;
  QString worker_thread;
  qint64 elapsed_ms = 0;
  bool success = false;
};

// UI-thread admission/publication; the render closure owns only an immutable
// snapshot. QObject destruction disconnects delivery without waiting for a
// decoder/encoder to finish. Cancellation is cooperative at stage boundaries.
class ActiveMaskPreviewWorker final : public QObject {
 public:
  using Render = std::function<ActiveMaskPreviewResult(ActiveMaskPreviewRequest)>;
  using Publish = std::function<bool(const ActiveMaskPreviewResult&)>;

  ActiveMaskPreviewWorker() {
    QObject::connect(&watcher_, &QFutureWatcher<ActiveMaskPreviewResult>::finished, this, [this] {
      handling_completion_ = true;
      const auto result = watcher_.result();
      active_ = false;
      ++completed;
      last_worker_thread = result.worker_thread;
      last_elapsed_ms = result.elapsed_ms;
      const auto current = !result.request.cancellation->load() && result.request.key == wanted_;
      if (current) failed_key_ = result.success ? QString{} : result.request.key;
      if (current &&
          publish_ && publish_(result)) ++published;
      else ++discarded;
      handling_completion_ = false;
      if (pending_ && pending_->key == wanted_) {
        auto next = std::move(*pending_);
        pending_.reset();
        start(std::move(next));
      }
    });
  }
  ~ActiveMaskPreviewWorker() override { stop(); }

  void configure(Render render, Publish publish) { render_ = std::move(render); publish_ = std::move(publish); }
  void want(const QString& key) {
    if (wanted_ == key) return;
    wanted_ = key;
    failed_key_.clear();
    pending_.reset();
    if (active_cancellation_) active_cancellation_->store(true);
  }
  void submit(ActiveMaskPreviewRequest request) {
    want(request.key);
    if (!render_ || request.key == failed_key_) return;
    if (active_ || handling_completion_) {
      if (active_key_ == request.key && active_cancellation_ && !active_cancellation_->load()) return;
      pending_ = std::move(request);
      return;
    }
    start(std::move(request));
  }
  void stop() {
    want({});
    pending_.reset();
    if (active_cancellation_) active_cancellation_->store(true);
  }
  [[nodiscard]] bool running() const { return active_ || handling_completion_; }
  [[nodiscard]] bool pending() const { return pending_.has_value(); }
  [[nodiscard]] const QString& wanted() const { return wanted_; }
  int started = 0;
  int completed = 0;
  int published = 0;
  int discarded = 0;
  QString last_worker_thread;
  qint64 last_elapsed_ms = 0;

 private:
  void start(ActiveMaskPreviewRequest request) {
    active_key_ = request.key;
    active_cancellation_ = request.cancellation;
    ++started;
    active_ = true;
    auto promise = std::make_shared<QPromise<ActiveMaskPreviewResult>>();
    promise->start();
    watcher_.setFuture(promise->future());
    QThreadPool::globalInstance()->start([request = std::move(request), render = render_, promise]() mutable {
      ActiveMaskPreviewResult result;
      result.request = request;
      try { result = render(request); } catch (...) { result.success = false; }
      promise->addResult(std::move(result));
      promise->finish();
    });
  }
  QFutureWatcher<ActiveMaskPreviewResult> watcher_;
  std::optional<ActiveMaskPreviewRequest> pending_;
  QString wanted_;
  QString active_key_;
  QString failed_key_;
  std::shared_ptr<std::atomic_bool> active_cancellation_;
  Render render_;
  Publish publish_;
  bool handling_completion_ = false;
  bool active_ = false;
};

}  // namespace photo_editor::desktop
