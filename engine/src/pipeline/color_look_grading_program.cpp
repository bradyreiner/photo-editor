#include "color_look_compilation_runtime.h"

#include "color_math_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace photo_editor::pipeline {

void compile_look_grading_program(
    CompiledLookStageProgram* compiled,
    const ColorAdjustmentState& state) {
  if (compiled == nullptr) return;
  compiled->grading_hue = {
      static_cast<float>(wrap_unit(state.grading_shadows_hue)),
      static_cast<float>(wrap_unit(state.grading_midtones_hue)),
      static_cast<float>(wrap_unit(state.grading_highlights_hue))};
  compiled->grading_saturation = {
      static_cast<float>(clamp_signed_percent(state.grading_shadows_saturation) / 100.0),
      static_cast<float>(clamp_signed_percent(state.grading_midtones_saturation) / 100.0),
      static_cast<float>(clamp_signed_percent(state.grading_highlights_saturation) / 100.0)};
  compiled->grading_luminance = {
      static_cast<float>(clamp_signed_percent(state.grading_shadows_luminance) / 100.0),
      static_cast<float>(clamp_signed_percent(state.grading_midtones_luminance) / 100.0),
      static_cast<float>(clamp_signed_percent(state.grading_highlights_luminance) / 100.0)};
  compiled->grading_amount =
      static_cast<float>(std::clamp(state.grading_amount / 100.0, 0.0, 1.0));
  compiled->grading_balance =
      static_cast<float>(clamp_signed_percent(state.grading_balance) / 100.0);
  const auto nonzero = [](float value) { return std::abs(value) > 1.0e-9F; };
  const auto adds_chroma = std::ranges::any_of(compiled->grading_saturation, nonzero);
  if (compiled->grading_amount > 1.0e-9F &&
      (adds_chroma || std::ranges::any_of(compiled->grading_luminance, nonzero))) {
    compiled->active_mask |= CompiledLookStageProgram::kColorGrading;
    if (adds_chroma) {
      compiled->active_mask |= CompiledLookStageProgram::kColorGradingAddsChroma;
    }
  }
}

void apply_compiled_look_grading(
    const CompiledLookStageProgram& compiled,
    float skin_scale,
    float* lightness,
    float* a,
    float* b) {
  const auto balance_shift = compiled.grading_balance * 0.18F;
  const auto selection_lightness = std::clamp(*lightness, 0.0F, 1.0F);
  const auto unit = [](float edge0, float edge1, float value) {
    const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
  };
  const auto shadow = 1.0F - unit(
      0.15F - balance_shift, 0.45F - balance_shift, selection_lightness);
  const auto highlight = unit(
      0.55F - balance_shift, 0.85F - balance_shift, selection_lightness);
  const std::array<float, 3> weights{
      shadow, std::max(0.0F, 1.0F - shadow - highlight), highlight};
  for (auto grade = std::size_t{0}; grade < weights.size(); ++grade) {
    const auto scaled_weight = weights[grade] * compiled.grading_amount;
    const auto intensity = std::abs(compiled.grading_saturation[grade]) *
                           0.18F * scaled_weight * skin_scale;
    const auto hue = compiled.grading_hue[grade] * 6.28318530717958647692F;
    *a += intensity * std::cos(hue);
    *b += intensity * std::sin(hue);
    *lightness += compiled.grading_luminance[grade] * 0.20F *
                  scaled_weight * skin_scale;
  }
}

}  // namespace photo_editor::pipeline
