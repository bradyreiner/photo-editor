#include "color_look_compilation_runtime.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

constexpr auto kTwoPi = 6.28318530717958647692F;

[[nodiscard]] float wrap_unit(float value) {
  value = std::fmod(value, 1.0F);
  return value < 0.0F ? value + 1.0F : value;
}

[[nodiscard]] float wrap_radians(float value) {
  value = std::fmod(value, kTwoPi);
  return value < 0.0F ? value + kTwoPi : value;
}

}  // namespace

void compile_look_harmony_program(
    CompiledLookStageProgram* compiled,
    const ColorAdjustmentState& state) {
  if (compiled == nullptr) return;
  compiled->harmony_strength = static_cast<float>(
      std::clamp(state.color_harmony_strength / 100.0, 0.0, 1.0) * 0.65);
  if (!state.color_harmony_enabled || compiled->harmony_strength <= 1.0e-9F) return;
  const auto base = wrap_unit(static_cast<float>(state.color_harmony_base_hue));
  if (state.color_harmony_mode == "Analogous") {
    compiled->harmony_anchors = {
        wrap_unit(base - 1.0F / 12.0F), base,
        wrap_unit(base + 1.0F / 12.0F), base};
    compiled->harmony_anchor_count = 3U;
  } else if (state.color_harmony_mode == "Triadic") {
    compiled->harmony_anchors = {
        base, wrap_unit(base + 1.0F / 3.0F),
        wrap_unit(base + 2.0F / 3.0F), base};
    compiled->harmony_anchor_count = 3U;
  } else if (state.color_harmony_mode == "Split Complementary") {
    compiled->harmony_anchors = {
        base, wrap_unit(base + 5.0F / 12.0F),
        wrap_unit(base + 7.0F / 12.0F), base};
    compiled->harmony_anchor_count = 3U;
  } else if (state.color_harmony_mode == "Tetradic") {
    compiled->harmony_anchors = {
        base, wrap_unit(base + 0.25F), wrap_unit(base + 0.50F),
        wrap_unit(base + 0.75F)};
    compiled->harmony_anchor_count = 4U;
  } else {
    compiled->harmony_anchors = {
        base, wrap_unit(base + 0.50F), base, base};
    compiled->harmony_anchor_count = 2U;
  }
  compiled->active_mask |= CompiledLookStageProgram::kColorHarmony;
}

void apply_compiled_look_harmony(
    const CompiledLookStageProgram& compiled,
    float skin_scale,
    float* a,
    float* b) {
  const auto chroma = std::hypot(*a, *b);
  auto hue = wrap_radians(std::atan2(*b, *a));
  const auto normalized_hue = wrap_unit(hue / kTwoPi);
  auto best = compiled.harmony_anchors[0];
  auto best_distance = 1.0F;
  for (auto index = 0U; index < compiled.harmony_anchor_count; ++index) {
    auto distance = std::abs(normalized_hue - compiled.harmony_anchors[index]);
    distance = std::min(distance, 1.0F - distance);
    if (distance < best_distance) {
      best_distance = distance;
      best = compiled.harmony_anchors[index];
    }
  }
  hue = wrap_radians(
      hue + std::remainder(best * kTwoPi - hue, kTwoPi) *
                compiled.harmony_strength * skin_scale);
  *a = chroma * std::cos(hue);
  *b = chroma * std::sin(hue);
}

}  // namespace photo_editor::pipeline
