#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::pipeline {

// Recover a single clipped sensor channel only when the two surviving
// channels independently indicate a bright, nearly neutral illuminant.
[[nodiscard]] ColorPixel reconstruct_camera_neutral_highlight(
    const ColorPixel& input,
    const std::array<double, 3>& camera_neutral,
    double strength);

[[nodiscard]] double highlight_reconstruction_strength(
    const ColorAdjustmentState& state);

[[nodiscard]] bool camera_linear_highlight_reconstruction_enabled(
    const InputTransformSettings& settings);

[[nodiscard]] double camera_linear_highlight_reconstruction_strength(
    const InputTransformSettings& settings,
    double highlight_headroom = 1.0);

[[nodiscard]] ColorPixel apply_camera_linear_highlight_reconstruction(
    const ColorPixel& input,
    double strength,
    const HighlightReconstructionAnalysis& analysis,
    double normalized_source_x = -1.0,
    double normalized_source_y = -1.0);

}  // namespace photo_editor::pipeline
