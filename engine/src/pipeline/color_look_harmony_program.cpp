#include "color_look_compilation_runtime.h"

#include "color_look_harmony_math.h"
#include "color_look_runtime.h"

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
  // The mixer stage is compiled before grading. Reset the shared semantic bit
  // here so the following grading compile can add its own legacy requirement.
  compiled->active_mask &= ~CompiledLookStageProgram::kLegacyCreativeSemantics;
  compiled->harmony_strength = static_cast<float>(
      std::clamp(state.color_harmony_strength / 100.0, 0.0, 1.0) * 0.65);
  if (!state.color_harmony_enabled || compiled->harmony_strength <= 1.0e-9F) return;
  const auto legacy_semantics =
      state.color_pipeline.photographic_rendering_model !=
      PhotographicRenderingModel::Aces2OcioV1;
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
  if (!legacy_semantics) {
    for (auto& anchor : compiled->harmony_anchors) {
      anchor = wrap_unit(static_cast<float>(
          oklab_hue_for_rgb_hue(static_cast<double>(anchor)) /
          static_cast<double>(kTwoPi)));
    }
  }
  compiled->active_mask |= CompiledLookStageProgram::kColorHarmony;
  if (legacy_semantics) {
    compiled->active_mask |= CompiledLookStageProgram::kLegacyCreativeSemantics;
  }
}

void apply_compiled_look_harmony(
    const CompiledLookStageProgram& compiled,
    float skin_scale,
    float* a,
    float* b) {
  const auto strength = compiled.harmony_strength * skin_scale;
  if (strength == 0.0F || compiled.harmony_anchor_count == 0U) return;
  const auto chroma = std::hypot(*a, *b);
  if (chroma == 0.0F) return;
  const auto hue = std::atan2(*b, *a);
  if ((compiled.active_mask &
       CompiledLookStageProgram::kLegacyCreativeSemantics) != 0U) {
    // Preserve the frozen recipe behavior: palette anchors remain raw UI hue
    // turns, the nearest anchor is selected discretely, and the shortest
    // angular delta is scaled by the original harmony strength.
    const auto normalized_hue = wrap_unit(hue / kTwoPi);
    auto nearest_anchor = compiled.harmony_anchors[0];
    auto nearest_distance = 1.0F;
    for (auto index = 0U; index < compiled.harmony_anchor_count; ++index) {
      const auto anchor = compiled.harmony_anchors[index];
      auto distance = std::abs(normalized_hue - anchor);
      distance = std::min(distance, 1.0F - distance);
      if (distance < nearest_distance) {
        nearest_distance = distance;
        nearest_anchor = anchor;
      }
    }
    auto delta = wrap_radians(nearest_anchor * kTwoPi) - wrap_radians(hue);
    if (delta > kTwoPi * 0.5F) {
      delta -= kTwoPi;
    } else if (delta < -kTwoPi * 0.5F) {
      delta += kTwoPi;
    }
    const auto adjusted_hue = wrap_radians(hue + delta * strength);
    *a = chroma * std::cos(adjusted_hue);
    *b = chroma * std::sin(adjusted_hue);
    return;
  }
  const auto force = photo_editor_color_harmony_circular_force(
      hue, compiled.harmony_anchors.data(), compiled.harmony_anchor_count);
  const auto adjusted_hue = wrap_radians(hue + strength * force);
  *a = chroma * std::cos(adjusted_hue);
  *b = chroma * std::sin(adjusted_hue);
}

}  // namespace photo_editor::pipeline
