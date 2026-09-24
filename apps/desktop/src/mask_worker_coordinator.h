#pragma once
#include "ai_inpaint_control.h"

#include <atomic>
#include <memory>
#include <utility>

#include <QFutureWatcher>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "async_mask_job.h"
#include "active_mask_preview_worker.h"
#include "brush_preview_compositor.h"
#include "edit_recipe_state.h"
#include "vision_segmentation.h"

namespace photo_editor::desktop {

using EditState = EditRecipeState;

struct SmartSelectPreviewRequest final {
  AsyncMaskJob job;
  QString file_path;
  QString asset_id;
  EditState state;
  QVariantList points;
  int generation = 0;
};

struct SmartSelectPreviewResult final {
  AsyncMaskJob job;
  QVariantMap preview_mask;
  QVector<int> instance_ids;
  int generation = 0;
  bool success = false;
};

struct SmartSelectCommitRequest final {
  AsyncMaskJob job;
  QString file_path;
  QString asset_id;
  EditState state;
  QVariantList points;
  QString intent = QStringLiteral("selection");
  bool create_new = false;
  int generation = 0;
  int state_revision = 0;
};

struct SmartSelectCommitResult final {
  SmartSelectCommitRequest request;
  QImage source_image;
  SegmentationResult segmentation_result;
  QString worker_thread;
  qint64 elapsed_ms = 0;
  bool decode_success = false;
  bool success = false;
};

struct BrushRasterCommitRequest final {
  AsyncMaskJob job;
  QString file_path;
  QString asset_id;
  EditState state;
  int mask_index = -1;
  QString mask_id;
  QVariantMap mask;
  QVector<BrushPreviewPoint> live_points;
  QVector<QVector<BrushPreviewPoint>> live_strokes;
  std::shared_ptr<std::atomic<int>> latest_generation;
  int generation = 0;
  EditState undo_state;
  bool undo_state_valid = false;
  QString undo_label;
  double brush_spacing = 0.75;
  double feather = 0.0;
  double auto_mask_tolerance = 0.18;
  bool auto_mask = false;
  bool protect_edges = false;
  qint64 live_preview_elapsed_ms = 0;
  bool live_preview_within_latency_budget = true;
  int coalesced_stroke_count = 1;
  int state_revision = 0;
};

struct BrushRasterCommitResult final {
  BrushRasterCommitRequest request;
  QVariantMap mask;
  QRectF dirty_region;
  QRect changed_raster_rect;
  QString worker_thread;
  qint64 elapsed_ms = 0;
  int source_decode_edge = 0;
  int source_decode_width = 0;
  int source_decode_height = 0;
  qint64 auto_mask_candidate_pixels = 0;
  qint64 auto_mask_edge_evaluation_pixels = 0;
  bool can_incrementally_update = false;
  bool source_decoded = false;
  bool source_decode_bounded = false;
  bool should_rerender = false;
  bool cancelled = false;
  bool success = false;
};

struct InpaintApplyRequest final {
  AsyncMaskJob job;
  QString file_path;
  QString asset_id;
  EditState state;
  QVariantList mask_stack;
  QVariantMap active_mask;
  int active_mask_index = -1;
  QString active_mask_id;
  QString retouch_mode;
  QString prompt;
  QString quality_mode = QStringLiteral("preview");
  int seed = 1;
  double blend_amount = 1.0;
  int generation = 0;
  int state_revision = 0;
  std::shared_ptr<AiInpaintControl> control;
};

struct InpaintApplyResult final {
  InpaintApplyRequest request;
  QVariantList next_stack;
  QVariantMap telemetry;
  QString status_suffix;
  QString error_message;
  QString worker_thread;
  qint64 elapsed_ms = 0;
  bool changed = false;
  bool has_inpaint_patch = false;
  bool success = false;
};

struct InpaintRegenerateRequest final {
  AsyncMaskJob job;
  QString file_path;
  QString asset_id;
  EditState state;
  QVariantMap mask;
  int mask_index = -1;
  QString mask_id;
  QString retouch_mode;
  QString prompt;
  QString quality_mode = QStringLiteral("preview");
  QString success_status_template;
  int fallback_seed = 1;
  double fallback_blend_amount = 1.0;
  int target_candidate_index = 0;
  int generation = 0;
  int state_revision = 0;
  std::shared_ptr<AiInpaintControl> control;
};

struct InpaintRegenerateResult final {
  InpaintRegenerateRequest request;
  QVariantMap mask;
  QVariantMap telemetry;
  QString error_message;
  QString worker_thread;
  qint64 elapsed_ms = 0;
  int next_seed = 1;
  bool success = false;
};

struct ActiveMaskOutlineRequest final {
  AsyncMaskJob job;
  QString cache_key;
  QString file_path;
  EditState state;
  QVariantList preview_masks;
  QVariantMap active_mask;
  int preview_edge = 0;
  double outline_tolerance_pixels = 1.5;
  double outline_display_scale = 1.0;
  int state_revision = 0;
  bool show_original_refinement = false;
  bool refinement_edge_mode = false;
  bool pixel_selection_inspection = false;
};

struct ActiveMaskOutlineResult final {
  ActiveMaskOutlineRequest request;
  QVariantList paths;
  QVariantMap trace_fields;
  QString source;
  QString worker_thread;
  qint64 elapsed_ms = 0;
  bool success = false;
};

class MaskWorkerCoordinator final {
 public:
  MaskWorkerCoordinator() {
    outline_worker_pool_.setMaxThreadCount(1);
    outline_worker_pool_.setExpiryTimeout(30000);
  }

  [[nodiscard]] QFutureWatcher<SmartSelectPreviewResult>& smartSelectPreviewWatcher() {
    return smart_select_preview_watcher_;
  }
  [[nodiscard]] const QFutureWatcher<SmartSelectPreviewResult>& smartSelectPreviewWatcher() const {
    return smart_select_preview_watcher_;
  }
  [[nodiscard]] QFutureWatcher<SmartSelectCommitResult>& smartSelectCommitWatcher() {
    return smart_select_commit_watcher_;
  }
  [[nodiscard]] const QFutureWatcher<SmartSelectCommitResult>& smartSelectCommitWatcher() const {
    return smart_select_commit_watcher_;
  }
  [[nodiscard]] QFutureWatcher<BrushRasterCommitResult>& brushRasterCommitWatcher() {
    return brush_raster_commit_watcher_;
  }
  [[nodiscard]] const QFutureWatcher<BrushRasterCommitResult>& brushRasterCommitWatcher() const {
    return brush_raster_commit_watcher_;
  }
  [[nodiscard]] QFutureWatcher<InpaintApplyResult>& inpaintApplyWatcher() {
    return inpaint_apply_watcher_;
  }
  [[nodiscard]] const QFutureWatcher<InpaintApplyResult>& inpaintApplyWatcher() const {
    return inpaint_apply_watcher_;
  }
  [[nodiscard]] QFutureWatcher<InpaintRegenerateResult>& inpaintRegenerateWatcher() {
    return inpaint_regenerate_watcher_;
  }
  [[nodiscard]] const QFutureWatcher<InpaintRegenerateResult>& inpaintRegenerateWatcher() const {
    return inpaint_regenerate_watcher_;
  }
  [[nodiscard]] QFutureWatcher<ActiveMaskOutlineResult>& activeMaskOutlineWatcher() const {
    return active_mask_outline_watcher_;
  }
  [[nodiscard]] QThreadPool* outlineWorkerPool() const {
    return &outline_worker_pool_;
  }
  [[nodiscard]] ActiveMaskPreviewWorker& activeMaskPreviewWorker() const { return active_mask_preview_worker_; }

  void setPendingActiveMaskOutline(ActiveMaskOutlineRequest request) const {
    pending_active_mask_outline_request_ = std::move(request);
    active_mask_outline_request_pending_ = true;
  }
  [[nodiscard]] bool activeMaskOutlinePending() const {
    return active_mask_outline_request_pending_;
  }
  [[nodiscard]] ActiveMaskOutlineRequest takePendingActiveMaskOutline() const {
    active_mask_outline_request_pending_ = false;
    return std::exchange(pending_active_mask_outline_request_, {});
  }
  [[nodiscard]] const AsyncMaskJob& latestActiveMaskOutlineJob() const {
    return latest_active_mask_outline_job_;
  }
  void setLatestActiveMaskOutlineJob(AsyncMaskJob job) const {
    latest_active_mask_outline_job_ = std::move(job);
  }
  void recordActiveMaskOutlineStarted() const {
    ++active_mask_outline_worker_started_count_;
  }
  void recordActiveMaskOutlineCompleted() const {
    ++active_mask_outline_worker_completed_count_;
  }
  void recordActiveMaskOutlineStale() const {
    ++active_mask_outline_stale_result_count_;
  }
  [[nodiscard]] int activeMaskOutlineWorkerStartedCount() const {
    return active_mask_outline_worker_started_count_;
  }
  [[nodiscard]] int activeMaskOutlineWorkerCompletedCount() const {
    return active_mask_outline_worker_completed_count_;
  }
  [[nodiscard]] int activeMaskOutlineStaleResultCount() const {
    return active_mask_outline_stale_result_count_;
  }

  [[nodiscard]] const SmartSelectPreviewRequest& pendingSmartSelectPreview() const {
    return pending_smart_select_preview_request_;
  }
  [[nodiscard]] SmartSelectPreviewRequest& pendingSmartSelectPreview() {
    return pending_smart_select_preview_request_;
  }
  void setPendingSmartSelectPreview(SmartSelectPreviewRequest request) {
    pending_smart_select_preview_request_ = std::move(request);
    smart_select_preview_request_pending_ = true;
  }
  void clearPendingSmartSelectPreview() {
    pending_smart_select_preview_request_ = {};
    smart_select_preview_request_pending_ = false;
  }
  void markSmartSelectPreviewDispatched() {
    smart_select_preview_request_pending_ = false;
  }
  [[nodiscard]] bool smartSelectPreviewPending() const {
    return smart_select_preview_request_pending_;
  }
  [[nodiscard]] bool& smartSelectPreviewPending() {
    return smart_select_preview_request_pending_;
  }

  [[nodiscard]] const SmartSelectCommitRequest& pendingSmartSelectCommit() const {
    return pending_smart_select_commit_request_;
  }
  [[nodiscard]] SmartSelectCommitRequest& pendingSmartSelectCommit() {
    return pending_smart_select_commit_request_;
  }
  void setPendingSmartSelectCommit(SmartSelectCommitRequest request) {
    pending_smart_select_commit_request_ = std::move(request);
    smart_select_commit_request_pending_ = true;
  }
  void clearPendingSmartSelectCommit() {
    pending_smart_select_commit_request_ = {};
    smart_select_commit_request_pending_ = false;
  }
  void markSmartSelectCommitDispatched() {
    smart_select_commit_request_pending_ = false;
  }
  [[nodiscard]] bool smartSelectCommitPending() const {
    return smart_select_commit_request_pending_;
  }
  [[nodiscard]] bool& smartSelectCommitPending() {
    return smart_select_commit_request_pending_;
  }

  [[nodiscard]] const AsyncMaskJob& latestSmartSelectPreviewJob() const {
    return latest_smart_select_preview_job_;
  }
  [[nodiscard]] AsyncMaskJob& latestSmartSelectPreviewJob() {
    return latest_smart_select_preview_job_;
  }
  void setLatestSmartSelectPreviewJob(AsyncMaskJob job) {
    latest_smart_select_preview_job_ = std::move(job);
  }
  [[nodiscard]] const AsyncMaskJob& latestSmartSelectCommitJob() const {
    return latest_smart_select_commit_job_;
  }
  [[nodiscard]] AsyncMaskJob& latestSmartSelectCommitJob() {
    return latest_smart_select_commit_job_;
  }
  void setLatestSmartSelectCommitJob(AsyncMaskJob job) {
    latest_smart_select_commit_job_ = std::move(job);
  }
  [[nodiscard]] const AsyncMaskJob& latestBrushRasterJob() const {
    return latest_brush_raster_job_;
  }
  [[nodiscard]] AsyncMaskJob& latestBrushRasterJob() {
    return latest_brush_raster_job_;
  }
  void setLatestBrushRasterJob(AsyncMaskJob job) {
    latest_brush_raster_job_ = std::move(job);
  }
  [[nodiscard]] const AsyncMaskJob& latestInpaintJob() const {
    return latest_inpaint_job_;
  }
  [[nodiscard]] AsyncMaskJob& latestInpaintJob() {
    return latest_inpaint_job_;
  }
  void setLatestInpaintJob(AsyncMaskJob job) {
    latest_inpaint_job_ = std::move(job);
  }
  [[nodiscard]] AsyncMaskJob& latestRefineEdgePreviewJob() const {
    return latest_refine_edge_preview_job_;
  }
  void setLatestRefineEdgePreviewJob(AsyncMaskJob job) const {
    latest_refine_edge_preview_job_ = std::move(job);
  }

  [[nodiscard]] const std::shared_ptr<std::atomic<int>>& latestBrushRasterGeneration() const {
    return latest_brush_raster_generation_;
  }

 private:
  QFutureWatcher<SmartSelectPreviewResult> smart_select_preview_watcher_;
  QFutureWatcher<SmartSelectCommitResult> smart_select_commit_watcher_;
  QFutureWatcher<BrushRasterCommitResult> brush_raster_commit_watcher_;
  QFutureWatcher<InpaintApplyResult> inpaint_apply_watcher_;
  QFutureWatcher<InpaintRegenerateResult> inpaint_regenerate_watcher_;
  mutable QFutureWatcher<ActiveMaskOutlineResult> active_mask_outline_watcher_;
  mutable ActiveMaskPreviewWorker active_mask_preview_worker_;
  mutable QThreadPool outline_worker_pool_;
  SmartSelectPreviewRequest pending_smart_select_preview_request_;
  SmartSelectCommitRequest pending_smart_select_commit_request_;
  AsyncMaskJob latest_smart_select_preview_job_;
  AsyncMaskJob latest_smart_select_commit_job_;
  AsyncMaskJob latest_brush_raster_job_;
  std::shared_ptr<std::atomic<int>> latest_brush_raster_generation_ =
      std::make_shared<std::atomic<int>>(0);
  AsyncMaskJob latest_inpaint_job_;
  mutable AsyncMaskJob latest_refine_edge_preview_job_;
  mutable AsyncMaskJob latest_active_mask_outline_job_;
  mutable ActiveMaskOutlineRequest pending_active_mask_outline_request_;
  mutable bool active_mask_outline_request_pending_ = false;
  mutable int active_mask_outline_worker_started_count_ = 0;
  mutable int active_mask_outline_worker_completed_count_ = 0;
  mutable int active_mask_outline_stale_result_count_ = 0;
  bool smart_select_preview_request_pending_ = false;
  bool smart_select_commit_request_pending_ = false;
};

}  // namespace photo_editor::desktop
