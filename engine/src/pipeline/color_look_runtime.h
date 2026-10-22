#pragma once

#include "color_working_space_runtime.h"

namespace photo_editor::pipeline {

[[nodiscard]] bool aces_drt_owns_highlight_chroma(
    const ColorAdjustmentState& state);
[[nodiscard]] bool color_look_stage_is_identity(
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength);

[[nodiscard]] bool has_post_tone_color_adjustments(
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength);

[[nodiscard]] double highlight_chroma_compression_scale(
    double lightness,
    double chroma);

// Converts a canonical sRGB/Qt.hsla display hue turn [0, 1) into the angle,
// in radians, of that color in the engine's D65 OKLab axes. This mapping is
// independent of the active working profile and is shared by color controls.
[[nodiscard]] double oklab_hue_for_rgb_hue(double hue_unit);

[[nodiscard]] ColorPixel apply_post_tone_color_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double output_headroom);

}  // namespace photo_editor::pipeline
