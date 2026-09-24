#include "color_look_runtime.h"

#include "color_mixer_runtime.h"

#include <cmath>

namespace photo_editor::pipeline {

bool aces_drt_owns_highlight_chroma(const ColorAdjustmentState& state) {
  return photographic_rendering_model_uses_aces2(
             state.color_pipeline.photographic_rendering_model) &&
         state.color_pipeline.render_intent == RenderIntent::Perceptual;
}

bool color_look_stage_is_identity(
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength) {
  return !has_rgb_channel_mixer_adjustments(state) &&
         !state.black_and_white_enabled &&
         !has_post_tone_color_adjustments(
             state, saturation_strength, vibrance_strength) &&
         aces_drt_owns_highlight_chroma(state);
}

}  // namespace photo_editor::pipeline
