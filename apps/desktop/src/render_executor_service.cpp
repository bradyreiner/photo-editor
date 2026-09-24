#include "render_executor_service.h"

#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/output_rendering.h"

#include <QThreadPool>

#include <mutex>

namespace photo_editor::desktop {

photo_editor::pipeline::RenderExecutor&
preview_render_executor() {
  static photo_editor::pipeline::RenderExecutor executor(
      kPreviewRenderExecutorCacheBytes);
  return executor;
}

photo_editor::pipeline::RenderExecutor&
export_render_executor() {
  static photo_editor::pipeline::RenderExecutor executor(
      kExportRenderExecutorCacheBytes);
  return executor;
}

void prewarm_default_aces2_output_luts() {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  static std::once_flag prewarm_once;
  std::call_once(prewarm_once, []() {
    auto* const pool = QThreadPool::globalInstance();
    if (pool == nullptr) {
      return;
    }
    pool->start([]() {
      auto config = photo_editor::pipeline::default_color_pipeline_config();
      config.photographic_rendering_model =
          photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
      for (const auto quality : {
               photo_editor::pipeline::Aces2OutputLutQuality::Interactive,
               photo_editor::pipeline::Aces2OutputLutQuality::Final,
           }) {
        static_cast<void>(
            photo_editor::pipeline::compile_aces2_output_lut_application(
                config,
                photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
                quality));
      }
    });
  });
#endif
}

}  // namespace photo_editor::desktop
