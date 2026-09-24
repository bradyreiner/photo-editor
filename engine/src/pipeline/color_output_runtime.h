#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::pipeline {

[[nodiscard]] bool black_point_compensation_affects_output(
    const ColorPipelineConfig& config);
[[nodiscard]] ColorPixel apply_black_point_compensation(
    const ColorPixel& pixel,
    const ColorPipelineConfig& config);

}  // namespace photo_editor::pipeline

