#pragma once

#include "photo_editor/pipeline/render_executor.h"

#include <cstddef>

namespace photo_editor::desktop {

inline constexpr std::size_t kPreviewRenderExecutorCacheBytes =
    384ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kExportRenderExecutorCacheBytes =
    512ULL * 1024ULL * 1024ULL;

[[nodiscard]] photo_editor::pipeline::RenderExecutor&
preview_render_executor();

[[nodiscard]] photo_editor::pipeline::RenderExecutor&
export_render_executor();

void prewarm_default_aces2_output_luts();

}  // namespace photo_editor::desktop
