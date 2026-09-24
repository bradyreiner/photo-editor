#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::pipeline {

// Continue a local-adjustment stack without replacing its original edge
// reference. The reference must not alias the mutable destination.
[[nodiscard]] bool apply_local_adjustments_to_look_pixels_with_context(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const std::vector<ColorPixel>& edge_pixels,
    CancellationToken cancellation = {});

// Range selection uses the pixel at this position in the local-adjustment
// stack; edge refinement uses the immutable pre-layer image. Both surfaces
// must cover the same raster. Invalid surfaces fail closed.
[[nodiscard]] std::vector<std::uint8_t> rasterize_local_mask_set_alpha_with_context(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> range_pixels,
    std::span<const ColorPixel> edge_pixels,
    const ColorPipelineConfig& color_pipeline);

}  // namespace photo_editor::pipeline
