#pragma once

#include "thumbnail_refresh_queue_service.h"

namespace photo_editor::desktop {

struct ThumbnailAdjustmentSharedRenderRequest final {
  QString asset_id;
  QString file_path;
  EditRecipeState state;
  AsyncMaskJob job;
  QVariantMap mask_alpha_trace_fields;
  QString render_reason;
  QString interaction_mode;
  QString render_mode;
  int render_priority = 0;
  QString dirty_domain;
  QStringList dirty_stages;
  int max_edge = 0;
  int jpeg_quality = 90;
  qint64 render_latency_budget_ms = 0;
};

struct ThumbnailAdjustmentSharedRenderResult final {
  QImage image;
  QString render_mode;
  QString backend;
  QString publish_path;
  qint64 adjustment_pipeline_elapsed_ms = 0;
  bool success = false;
};

struct ThumbnailAdjustmentRendererDependencies final {
  std::function<ThumbnailAdjustmentSharedRenderResult(
      const ThumbnailAdjustmentSharedRenderRequest& request)> shared_render;
};

[[nodiscard]] ThumbnailRefreshQueueRenderers make_thumbnail_worker_renderers(
    ThumbnailAdjustmentRendererDependencies adjustment_renderer_dependencies = {});

}  // namespace photo_editor::desktop
