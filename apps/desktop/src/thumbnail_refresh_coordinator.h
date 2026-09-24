#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include "thumbnail_refresh_queue_service.h"

namespace photo_editor::desktop {

class ThumbnailRefreshCoordinator final {
 public:
  using StageCallback = std::function<void()>;

  struct AdmissionFacts {
    bool full_preview_running = false;
    bool interactive_preview_running = false;
    bool coalesced_edit_active = false;
    bool mask_refresh_suppressed = false;
    bool live_brush_stroke = false;
    bool brush_commit_preview_points = false;
    bool brush_commit_scheduled = false;
    bool brush_commit_queued = false;
    bool brush_raster_worker_running = false;
    bool brush_raster_commit_active = false;
    bool layer_slider_debounce_active = false;
    bool smart_selection_preview_requested = false;
    bool smart_selection_hover_active = false;
    bool smart_selection_preview_worker_running = false;
    bool smart_selection_commit_pending = false;
    bool smart_selection_commit_requested = false;
    bool smart_selection_commit_worker_running = false;
  };

  [[nodiscard]] static QStringList admissionBlockers(
      const AdmissionFacts& facts) {
    QStringList blockers;
    const auto append_if = [&blockers](bool condition, const char* blocker) {
      if (condition) {
        blockers.append(QString::fromLatin1(blocker));
      }
    };
    append_if(facts.full_preview_running, "full_preview_render");
    append_if(facts.interactive_preview_running, "interactive_preview_render");
    append_if(facts.coalesced_edit_active, "coalesced_edit");
    append_if(facts.mask_refresh_suppressed, "mask_refresh_suppressed");
    append_if(facts.live_brush_stroke, "live_brush_stroke");
    append_if(facts.brush_commit_preview_points, "brush_commit_preview_points");
    append_if(facts.brush_commit_scheduled, "brush_commit_scheduled");
    append_if(facts.brush_commit_queued, "brush_commit_queue");
    append_if(facts.brush_raster_worker_running, "brush_raster_worker");
    append_if(facts.brush_raster_commit_active, "brush_raster_commit_active");
    append_if(facts.layer_slider_debounce_active, "layer_slider_debounce");
    append_if(
        facts.smart_selection_preview_requested,
        "smart_selection_preview_request");
    append_if(facts.smart_selection_hover_active, "smart_selection_hover_timer");
    append_if(
        facts.smart_selection_preview_worker_running,
        "smart_selection_preview_worker");
    append_if(
        facts.smart_selection_commit_pending,
        "smart_selection_commit_pending");
    append_if(
        facts.smart_selection_commit_requested,
        "smart_selection_commit_request");
    append_if(
        facts.smart_selection_commit_worker_running,
        "smart_selection_commit_worker");
    return blockers;
  }

  ThumbnailRefreshCoordinator(
      QObject* context,
      int latency_budget_ms,
      StageCallback dispatch_stage,
      StageCallback completion_stage)
      : context_(context), dispatch_stage_(std::move(dispatch_stage)) {
    timer_.setSingleShot(true);
    timer_.setInterval(latency_budget_ms);
    QObject::connect(
        &timer_,
        &QTimer::timeout,
        context,
        [this]() { dispatch_stage_(); });
    QObject::connect(
        &watcher_,
        &QFutureWatcher<ThumbnailRefreshQueueResult>::finished,
        context,
        [stage = std::move(completion_stage)]() { stage(); });
  }

  void schedule(const QString& reason, const QString& queue_signature) {
    const auto replacing_pending_request =
        pending_ && !signature_.isEmpty() && signature_ != queue_signature;
    const auto replacing_running_request =
        watcher_.isRunning() && !active_signature_.isEmpty() &&
        active_signature_ != queue_signature;
    if (replacing_pending_request || replacing_running_request) {
      ++cancelled_count_;
      cancelled_reason_ = QStringLiteral("superseded_pending_thumbnail_refresh");
      cancelActiveWorker();
    } else if (pending_ && signature_ == queue_signature) {
      reason_ = reason;
      if (!timer_.isActive()) {
        ++scheduled_count_;
        timer_.start();
      }
      return;
    }

    pending_ = true;
    reason_ = reason;
    signature_ = queue_signature;
    pending_generation_ = ++generation_;
    completion_ = {};
    if (!replacing_pending_request && !replacing_running_request) {
      ++scheduled_count_;
    }
    if (!timer_.isActive()) {
      timer_.start();
    }
  }

  void defer() {
    ++preempted_count_;
    timer_.start();
  }

  void dispatchSoon() const {
    QTimer::singleShot(0, context_, [this]() { dispatch_stage_(); });
  }

  [[nodiscard]] bool pending() const { return pending_; }
  [[nodiscard]] const QString& reason() const { return reason_; }
  [[nodiscard]] const QString& signature() const { return signature_; }
  [[nodiscard]] int pendingGeneration() const { return pending_generation_; }
  [[nodiscard]] bool workerRunning() const { return watcher_.isRunning(); }
  [[nodiscard]] bool timerActive() const { return timer_.isActive(); }

  void clearPending() {
    pending_ = false;
    reason_.clear();
    signature_.clear();
    pending_generation_ = 0;
  }

  [[nodiscard]] std::shared_ptr<std::atomic_bool> activate(
      const QString& job_id) {
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    active_job_id_ = job_id;
    active_signature_ = signature_;
    return cancellation_;
  }

  void startWorker(QFuture<ThumbnailRefreshQueueResult> future) {
    ++worker_started_count_;
    watcher_.setFuture(std::move(future));
  }

  [[nodiscard]] ThumbnailRefreshQueueResult workerResult() const {
    return watcher_.result();
  }

  void recordWorkerCompleted() { ++worker_completed_count_; }

  [[nodiscard]] bool resultIsCurrent(
      const ThumbnailRefreshQueueResult& result,
      const QString& asset_id,
      const QString& file_path,
      const QString& state_signature,
      int state_revision) const {
    return !result.cancelled &&
           result.request.job.job_id == active_job_id_ &&
           result.request.queue_signature == active_signature_ &&
           result.request.job.asset_id == asset_id &&
           result.request.job.file_path == file_path &&
           result.request.job.state_signature == state_signature &&
           result.request.job.state_revision == state_revision;
  }

  void recordStaleResult(const ThumbnailRefreshQueueResult& result) {
    ++stale_result_count_;
    if (result.request.job.job_id == active_job_id_) {
      clearActive();
    }
  }

  [[nodiscard]] bool hasSupersedingPendingRequest(
      const ThumbnailRefreshQueueResult& result) const {
    return pending_ && !signature_.isEmpty() &&
           result.request.queue_signature != signature_;
  }

  void complete(const ThumbnailRefreshQueueResult& result) {
    clearPending();
    clearActive();
    completion_ = ThumbnailRefreshQueueCompletion{
        .job_id = result.request.job.job_id,
        .state_signature = result.request.job.state_signature,
        .queue_signature = result.request.queue_signature,
        .reason = result.request.reason,
        .mask_thumbnail_dirty = result.request.mask_thumbnail_dirty,
        .photo_layer_thumbnail_dirty = result.request.photo_layer_thumbnail_dirty,
        .elapsed_ms = result.elapsed_ms,
        .photo_layer_thumbnail_entry_count = result.photo_layer_thumbnail_entry_count,
        .mask_thumbnail_entry_count = result.mask_thumbnail_entry_count,
        .service_contract = result.service_contract,
        .failure_stage = result.failure_stage,
        .failure_detail = result.failure_detail,
        .state_revision = result.request.job.state_revision,
        .mask_thumbnail_failed = result.mask_thumbnail_failed,
        .photo_layer_thumbnail_failed = result.photo_layer_thumbnail_failed,
        .success = result.success,
    };
    ++dispatched_count_;
  }

  void reset() {
    cancelActiveWorker();
    timer_.stop();
    clearPending();
    clearActive();
    completion_ = {};
    stale_warning_signatures_.clear();
  }

  [[nodiscard]] QVariantMap fields(const QStringList& blockers) const {
    const auto job_id = pending_generation_ > 0
                            ? QStringLiteral("thumbnail-refresh-queue-%1")
                                  .arg(pending_generation_)
                            : QString();
    QVariantMap result;
    result.insert(QStringLiteral("thumbnailQueuePending"), pending_);
    result.insert(QStringLiteral("thumbnailQueueJobId"), job_id);
    result.insert(QStringLiteral("thumbnailQueueJobKind"), QStringLiteral("thumbnail_refresh"));
    result.insert(QStringLiteral("thumbnailQueuePriority"), QStringLiteral("low"));
    result.insert(QStringLiteral("thumbnailQueuePriorityRank"), 90);
    result.insert(QStringLiteral("thumbnailQueueReason"), reason_);
    result.insert(QStringLiteral("thumbnailQueueGeneration"), pending_generation_);
    result.insert(QStringLiteral("thumbnailQueueStateSignature"), signature_);
    result.insert(QStringLiteral("thumbnailQueueDeferBlockers"), blockers);
    result.insert(QStringLiteral("thumbnailQueueWorkerRunning"), watcher_.isRunning());
    result.insert(QStringLiteral("thumbnailQueueActiveJobId"), active_job_id_);
    result.insert(QStringLiteral("thumbnailQueueWorkerStartedCount"), worker_started_count_);
    result.insert(QStringLiteral("thumbnailQueueWorkerCompletedCount"), worker_completed_count_);
    result.insert(QStringLiteral("thumbnailQueueStaleResultCount"), stale_result_count_);
    result.insert(
        QStringLiteral("thumbnailQueueDeferBlockerSignature"),
        blockers.join(QLatin1Char(',')));
    result.insert(
        QStringLiteral("thumbnailQueueContract"),
        QStringLiteral("thumbnail-refresh-queue:v1|priority:low|pending:%1|generation:%2|blockers:%3")
            .arg(pending_ ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(pending_generation_),
                 blockers.join(QLatin1Char(','))));
    result.insert(
        QStringLiteral("layerThumbnailServiceOwnershipContract"),
        layer_thumbnail_service_ownership_contract());
    return result;
  }

  [[nodiscard]] const ThumbnailRefreshQueueCompletion& completion() const {
    return completion_;
  }

  [[nodiscard]] bool consumeStaleWarning(const QString& reason) {
    const auto trimmed_reason = reason.trimmed();
    if (trimmed_reason.isEmpty()) {
      return false;
    }
    const auto warning_signature =
        QStringLiteral("%1|generation:%2|queue:%3")
            .arg(trimmed_reason,
                 QString::number(pending_generation_),
                 signature_);
    if (stale_warning_signatures_.contains(warning_signature)) {
      return false;
    }
    if (stale_warning_signatures_.size() > 128) {
      stale_warning_signatures_.clear();
    }
    stale_warning_signatures_.insert(warning_signature);
    return true;
  }

  [[nodiscard]] int scheduledCount() const { return scheduled_count_; }
  [[nodiscard]] int preemptedCount() const { return preempted_count_; }
  [[nodiscard]] int dispatchedCount() const { return dispatched_count_; }
  [[nodiscard]] int cancelledCount() const { return cancelled_count_; }
  [[nodiscard]] const QString& cancelledReason() const { return cancelled_reason_; }
  [[nodiscard]] int workerStartedCount() const { return worker_started_count_; }
  [[nodiscard]] int workerCompletedCount() const { return worker_completed_count_; }
  [[nodiscard]] int staleResultCount() const { return stale_result_count_; }
  [[nodiscard]] const QString& activeJobId() const { return active_job_id_; }
  [[nodiscard]] const QString& activeSignature() const { return active_signature_; }

 private:
  void cancelActiveWorker() {
    if (cancellation_ != nullptr) {
      cancellation_->store(true, std::memory_order_relaxed);
    }
  }

  void clearActive() {
    active_job_id_.clear();
    active_signature_.clear();
    cancellation_.reset();
  }

  QObject* context_ = nullptr;
  StageCallback dispatch_stage_;
  QTimer timer_;
  QFutureWatcher<ThumbnailRefreshQueueResult> watcher_;
  bool pending_ = false;
  QString reason_;
  QString signature_;
  int generation_ = 0;
  int pending_generation_ = 0;
  int scheduled_count_ = 0;
  int preempted_count_ = 0;
  int dispatched_count_ = 0;
  int cancelled_count_ = 0;
  QString cancelled_reason_;
  int worker_started_count_ = 0;
  int worker_completed_count_ = 0;
  int stale_result_count_ = 0;
  QString active_job_id_;
  QString active_signature_;
  ThumbnailRefreshQueueCompletion completion_;
  QSet<QString> stale_warning_signatures_;
  std::shared_ptr<std::atomic_bool> cancellation_;
};

}  // namespace photo_editor::desktop
