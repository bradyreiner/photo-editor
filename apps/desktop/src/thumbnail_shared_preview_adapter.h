#pragma once

#include "editor_preview_controller.h"
#include "thumbnail_worker_renderer_factory.h"

namespace photo_editor::desktop {

[[nodiscard]] RenderResult execute_preview_render_request(
    const RenderRequest& request);

[[nodiscard]] inline ThumbnailAdjustmentSharedRenderResult
render_thumbnail_shared_preview_request(
    const ThumbnailAdjustmentSharedRenderRequest& request) {
  const auto result = execute_preview_render_request(RenderRequest{
      .source_asset_id = request.asset_id,
      .file_path = request.file_path,
      .render_reason = request.render_reason,
      .interaction_mode = request.interaction_mode,
      .render_mode = request.render_mode,
      .render_priority = request.render_priority,
      .dirty_domain = request.dirty_domain,
      .dirty_stages = request.dirty_stages,
      .full_invalidation = true,
      .state = request.state,
      .full_resolution = false,
      .max_edge = request.max_edge,
      .jpeg_quality = request.jpeg_quality,
      .generation = 1,
      .render_latency_budget_ms = request.render_latency_budget_ms,
      .async_job = request.job,
      .mask_alpha_trace_fields = request.mask_alpha_trace_fields,
      .use_in_memory_preview = true,
  });
  return ThumbnailAdjustmentSharedRenderResult{
      .image = result.preview_image,
      .render_mode = result.render_mode,
      .backend = result.backend,
      .publish_path = result.publish_path,
      .adjustment_pipeline_elapsed_ms =
          result.adjustment_pipeline_elapsed_ms,
      .success = result.success,
  };
}

}  // namespace photo_editor::desktop
