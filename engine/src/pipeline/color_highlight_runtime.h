#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::pipeline {

[[nodiscard]] double highlight_reconstruction_strength(
    const ColorAdjustmentState& state);

[[nodiscard]] bool camera_linear_highlight_reconstruction_enabled(
    const InputTransformSettings& settings);

[[nodiscard]] ColorPixel apply_camera_linear_highlight_reconstruction(
    const ColorPixel& input,
    double strength,
    const HighlightReconstructionAnalysis& analysis);

}  // namespace photo_editor::pipeline
