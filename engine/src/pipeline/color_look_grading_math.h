#pragma once

#if defined(__METAL_VERSION__)
#define PHOTO_EDITOR_GRADING_COS(value) cos(value)
#define PHOTO_EDITOR_GRADING_SIN(value) sin(value)
#else
#include <cmath>
#define PHOTO_EDITOR_GRADING_COS(value) std::cos(value)
#define PHOTO_EDITOR_GRADING_SIN(value) std::sin(value)
#endif

namespace photo_editor {
namespace pipeline {

struct ColorLookGradingBandContribution {
  float tint_a;
  float tint_b;
  float desaturation;
  float lightness_delta;
};

// hue_turns is the compiled OKLab angle in turns. weight_amount_skin is the
// product of tonal weight, overall grading amount, and protection scale.
// Positive saturation adds a modest tint; negative saturation removes chroma.
inline ColorLookGradingBandContribution
color_look_grading_band_contribution(
    float hue_turns,
    float saturation_norm,
    float luminance_norm,
    float weight_amount_skin) {
  const auto signed_strength = saturation_norm * weight_amount_skin;
  const auto hue_radians = hue_turns * 6.28318530717958647692F;
  const auto tint_strength = 0.18F * (signed_strength > 0.0F ? signed_strength : 0.0F);
  ColorLookGradingBandContribution result;
  result.tint_a = tint_strength * PHOTO_EDITOR_GRADING_COS(hue_radians);
  result.tint_b = tint_strength * PHOTO_EDITOR_GRADING_SIN(hue_radians);
  result.desaturation = signed_strength < 0.0F ? -signed_strength : 0.0F;
  result.lightness_delta = luminance_norm * 0.20F * weight_amount_skin;
  return result;
}

}  // namespace pipeline
}  // namespace photo_editor

#undef PHOTO_EDITOR_GRADING_COS
#undef PHOTO_EDITOR_GRADING_SIN
