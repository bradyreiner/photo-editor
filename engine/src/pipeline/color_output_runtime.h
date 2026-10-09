#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <span>

namespace photo_editor::pipeline {

[[nodiscard]] bool color_surface_is_finite(
    std::span<const ColorPixel> pixels);
[[nodiscard]] bool black_point_compensation_affects_output(
    const ColorPipelineConfig& config);
[[nodiscard]] ColorPixel apply_black_point_compensation(
    const ColorPixel& pixel,
    const ColorPipelineConfig& config);

}  // namespace photo_editor::pipeline
