#include "color_look_protected_program.h"
#include "color_mixer_runtime.h"
#include "color_look_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

constexpr auto kPi = 3.14159265358979323846F;
constexpr auto kTwoPi = 2.0F * kPi;

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
  const auto unit = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
  return unit * unit * (3.0F - 2.0F * unit);
}

[[nodiscard]] float wrap_unit(float value) {
  value -= std::floor(value);
  return value < 0.0F ? value + 1.0F : value;
}

[[nodiscard]] float wrap_radians(float value) {
  value = std::fmod(value, kTwoPi);
  return value < 0.0F ? value + kTwoPi : value;
}

[[nodiscard]] std::array<float, 3> multiply(
    const ColorMatrix3& matrix,
    const std::array<float, 3>& value) {
  return {
      static_cast<float>(matrix.values[0]) * value[0] + static_cast<float>(matrix.values[1]) * value[1] + static_cast<float>(matrix.values[2]) * value[2],
      static_cast<float>(matrix.values[3]) * value[0] + static_cast<float>(matrix.values[4]) * value[1] + static_cast<float>(matrix.values[5]) * value[2],
      static_cast<float>(matrix.values[6]) * value[0] + static_cast<float>(matrix.values[7]) * value[1] + static_cast<float>(matrix.values[8]) * value[2]};
}

[[nodiscard]] float luminance(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& pixel) {
  return pixel.red * compiled.luminance_coefficients[0] +
         pixel.green * compiled.luminance_coefficients[1] +
         pixel.blue * compiled.luminance_coefficients[2];
}

[[nodiscard]] std::array<float, 3> oklab_to_rgb(
    const CompiledLookStageProgram& compiled,
    float lightness,
    float a,
    float b) {
  const auto l_root = lightness + 0.3963377774F * a + 0.2158037573F * b;
  const auto m_root = lightness - 0.1055613458F * a - 0.0638541728F * b;
  const auto s_root = lightness - 0.0894841775F * a - 1.2914855480F * b;
  const auto l = l_root * l_root * l_root;
  const auto m = m_root * m_root * m_root;
  const auto s = s_root * s_root * s_root;
  return multiply(compiled.xyz_d65_to_rgb, {
      1.2270138511F * l - 0.5577999807F * m + 0.2812561490F * s,
      -0.0405801784F * l + 1.1122568696F * m - 0.0716766787F * s,
      -0.0763812845F * l - 0.4214819784F * m + 1.5861632204F * s});
}

[[nodiscard]] float circular_weight(float value, float center, float radius) {
  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0F - distance);
  if (distance >= radius) return 0.0F;
  const auto unit = 1.0F - distance / radius;
  return unit * unit * (3.0F - 2.0F * unit);
}

[[nodiscard]] float feathered_weight(
    float value,
    float center,
    float radius,
    float softness) {
  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0F - distance);
  if (distance >= radius) return 0.0F;
  const auto feather = softness * radius;
  const auto inner = radius - feather;
  if (distance <= inner || feather <= 1.0e-9F) return 1.0F;
  return 1.0F - smoothstep(inner, radius, distance);
}

}  // namespace

void compile_look_protected_program(
    CompiledLookStageProgram* compiled,
    const ColorAdjustmentState& state) {
  if (compiled == nullptr) return;
  compiled->replacement_strength = static_cast<float>(
      std::clamp(state.color_replacement_strength / 100.0, 0.0, 1.0));
  if (state.color_replacement_enabled && compiled->replacement_strength > 1.0e-9F) {
    compiled->replacement_source_hue = wrap_unit(static_cast<float>(state.color_replacement_source_hue));
    compiled->replacement_target_oklab_hue = static_cast<float>(oklab_hue_for_rgb_hue(
        state.color_replacement_target_hue));
    compiled->replacement_range = static_cast<float>(
        std::clamp(state.color_replacement_range / 100.0, 0.01, 1.0) * 0.50);
    compiled->replacement_softness = static_cast<float>(
        std::clamp(state.color_replacement_softness / 100.0, 0.0, 1.0));
    compiled->replacement_preserve_luminance = state.color_replacement_preserve_luminance ? 1U : 0U;
    compiled->replacement_preserve_saturation = state.color_replacement_preserve_saturation ? 1U : 0U;
    compiled->active_mask |= CompiledLookStageProgram::kColorReplacement;
  }
  compiled->skin_strength = static_cast<float>(
      std::clamp(state.skin_tone_protection_strength / 100.0, 0.0, 1.0));
  if (state.skin_tone_protection_enabled && compiled->skin_strength > 1.0e-9F &&
      compiled->active_mask != 0U) {
    const auto legacy = state.color_pipeline.photographic_rendering_model !=
                        PhotographicRenderingModel::Aces2OcioV1;
    compiled->skin_hue = legacy
        ? wrap_unit(static_cast<float>(state.skin_tone_line_hue))
        : wrap_unit(static_cast<float>(oklab_hue_for_rgb_hue(state.skin_tone_line_hue)) / kTwoPi);
    compiled->skin_range = static_cast<float>(
        std::clamp(state.skin_tone_protection_range / 100.0, 0.01, 1.0) * 0.50);
    compiled->active_mask |= CompiledLookStageProgram::kSkinProtection;
  }
}

float compiled_look_skin_protection_scale(
    const CompiledLookStageProgram& compiled,
    float a,
    float b) {
  if ((compiled.active_mask & CompiledLookStageProgram::kSkinProtection) == 0U) return 1.0F;
  const auto chroma = std::hypot(a, b);
  const auto hue = wrap_unit(std::atan2(b, a) / kTwoPi);
  const auto weight = circular_weight(hue, compiled.skin_hue, compiled.skin_range) *
                      smoothstep(0.02F, 0.12F, chroma) * compiled.skin_strength;
  return 1.0F - 0.95F * weight;
}

void apply_compiled_look_replacement(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& input,
    float skin_scale,
    float* lightness,
    float* a,
    float* b,
    float* luminance_reference,
    float* luminance_weight) {
  if ((compiled.active_mask & CompiledLookStageProgram::kColorReplacement) == 0U) return;
  const auto selection_rgb = color_selection_rgb(compiled, input);
  const auto confidence = static_cast<float>(color_selection_confidence(selection_rgb));
  if (confidence <= 0.0F) return;
  const auto selection_hue = rgb_hue_for_mixer_selection(selection_rgb);
  if (selection_hue < 0.0F) return;
  const auto weight = feathered_weight(
      static_cast<float>(selection_hue), compiled.replacement_source_hue,
      compiled.replacement_range, compiled.replacement_softness) *
      compiled.replacement_strength * skin_scale *
      confidence;
  if (weight <= 1.0e-6F) return;
  auto chroma = std::hypot(*a, *b);
  auto hue = wrap_radians(std::atan2(*b, *a));
  if (compiled.replacement_preserve_luminance != 0U) {
    const auto rgb = oklab_to_rgb(compiled, *lightness, *a, *b);
    *luminance_reference = rgb[0] * compiled.luminance_coefficients[0] +
                           rgb[1] * compiled.luminance_coefficients[1] +
                           rgb[2] * compiled.luminance_coefficients[2];
    *luminance_weight = weight;
  }
  hue = wrap_radians(
      hue + std::remainder(compiled.replacement_target_oklab_hue - hue, kTwoPi) * weight);
  if (compiled.replacement_preserve_saturation == 0U) {
    chroma = std::max(chroma, std::lerp(chroma, 0.16F, weight));
  }
  *a = chroma * std::cos(hue);
  *b = chroma * std::sin(hue);
}

ColorPixel apply_compiled_replacement_luminance(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& output,
    float luminance_reference,
    float luminance_weight) {
  if (luminance_reference < 0.0F || luminance_weight <= 1.0e-6F) return output;
  const auto source_luminance = luminance(compiled, output);
  const auto target = std::lerp(
      source_luminance,
      std::clamp(luminance_reference, 0.0F, compiled.output_headroom),
      std::clamp(luminance_weight, 0.0F, 1.0F));
  const auto maximum = std::max({std::abs(output.red), std::abs(output.green), std::abs(output.blue)});
  auto adjusted = output;
  if (maximum <= 1.0e-9F) {
    adjusted.red = adjusted.green = adjusted.blue = std::max(0.0F, target);
  } else if (source_luminance > 1.0e-6F) {
    const auto scale = std::max(0.0F, target) / source_luminance;
    adjusted.red *= scale;
    adjusted.green *= scale;
    adjusted.blue *= scale;
  }
  const auto adjusted_luminance = std::max(0.0F, luminance(compiled, adjusted));
  const auto nonnegative_source = std::max(0.0F, source_luminance);
  const auto lift_stops = std::log2(
      (adjusted_luminance + 1.0e-6F) / (nonnegative_source + 1.0e-6F));
  if (lift_stops > 0.0F) {
    const auto spread = std::max({
        std::abs(output.red - output.green), std::abs(output.green - output.blue),
        std::abs(output.red - output.blue)});
    const auto attenuation = std::clamp(
        (1.0F - smoothstep(0.012F, 0.16F, nonnegative_source)) *
        smoothstep(0.12F, 1.20F, lift_stops) *
        (1.0F - smoothstep(0.025F, 0.12F, spread)) * 0.56F,
        0.0F, 0.56F);
    adjusted.red = adjusted_luminance + (adjusted.red - adjusted_luminance) * (1.0F - attenuation);
    adjusted.green = adjusted_luminance + (adjusted.green - adjusted_luminance) * (1.0F - attenuation);
    adjusted.blue = adjusted_luminance + (adjusted.blue - adjusted_luminance) * (1.0F - attenuation);
  }
  return adjusted;
}

}  // namespace photo_editor::pipeline
