#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <vector>

namespace photo_editor::pipeline {

[[nodiscard]] ColorPixel sample_bilinear(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double x,
    double y);

void apply_lens_vignette_correction(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state);

[[nodiscard]] bool geometry_corrections_are_identity(
    const ColorAdjustmentState& state);

void apply_geometry_corrections(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state);

}  // namespace photo_editor::pipeline
