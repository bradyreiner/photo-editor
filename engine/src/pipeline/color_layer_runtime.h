#pragma once

#include "color_working_space_runtime.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <functional>
#include <vector>

namespace photo_editor::pipeline {

using PhotoLayerCommonAdjustment =
    std::function<void(ColorPixel*, const PhotoLayer&)>;

[[nodiscard]] bool has_photo_layers(
    const std::vector<PhotoLayer>& layers);
[[nodiscard]] bool photo_layer_requires_gamut_fit(
    const PhotoLayer& layer,
    const PipelineColorContext& context);
[[nodiscard]] ColorAdjustmentState photo_layer_color_adjustment_state(
    const PhotoLayer& layer);

bool apply_photo_layers(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<PhotoLayer>& layers,
    const PhotoLayerCommonAdjustment& apply_common_adjustment,
    // Opt in only for callbacks implementing the standard layer adjustment
    // fields. Arbitrary callbacks retain the complete scalar execution path.
    bool common_adjustment_uses_layer_fields = false,
    bool common_adjustment_thread_safe = false,
    CancellationToken cancellation = {}, std::string* error = nullptr);

// Standard pipeline callback with immutable per-layer preparation and worker-safe execution.
bool apply_standard_photo_layers(
    std::vector<ColorPixel>* pixels, int width, int height,
    const PipelineColorContext& context, PhotographicRenderingModel rendering_model,
    const std::vector<PhotoLayer>& layers,
    CancellationToken cancellation = {}, std::string* error = nullptr);

}  // namespace photo_editor::pipeline
