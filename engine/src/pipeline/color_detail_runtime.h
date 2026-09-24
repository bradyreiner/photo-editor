#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <vector>

namespace photo_editor::pipeline {

[[nodiscard]] bool apply_spatial_detail_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry);

void apply_fine_detail_effects(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry);

}  // namespace photo_editor::pipeline
