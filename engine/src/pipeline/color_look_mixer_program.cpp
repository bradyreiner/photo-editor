#include "color_look_compilation_runtime.h"

#include "color_mixer_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

constexpr auto kTwoPi = 6.28318530717958647692F;

[[nodiscard]] float wrap_unit_float(float value) {
  value = std::fmod(value, 1.0F);
  return value < 0.0F ? value + 1.0F : value;
}

[[nodiscard]] float wrap_radians_float(float value) {
  value = std::fmod(value, kTwoPi);
  return value < 0.0F ? value + kTwoPi : value;
}

[[nodiscard]] float rgb_hue(const ColorPixel& pixel) {
  const auto red = std::max(0.0F, pixel.red);
  const auto green = std::max(0.0F, pixel.green);
  const auto blue = std::max(0.0F, pixel.blue);
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto delta = maximum - minimum;
  if (delta <= 1.0e-9F) return -1.0F;
  auto hue = 0.0F;
  if (maximum == red) {
    hue = (green - blue) / delta;
    if (hue < 0.0F) hue += 6.0F;
  } else if (maximum == green) {
    hue = (blue - red) / delta + 2.0F;
  } else {
    hue = (red - green) / delta + 4.0F;
  }
  return hue / 6.0F;
}

[[nodiscard]] float channel_weight(float hue, std::size_t channel) {
  static constexpr std::array<float, 8> centers{
      0.0F, 0.09F, 0.15F, 0.32F, 0.50F, 0.64F, 0.78F, 0.88F};
  static constexpr std::array<float, 8> inner{
      0.05F, 0.04F, 0.04F, 0.10F, 0.06F, 0.08F, 0.06F, 0.05F};
  static constexpr std::array<float, 8> outer{
      0.10F, 0.09F, 0.08F, 0.18F, 0.10F, 0.13F, 0.10F, 0.10F};
  auto distance = std::abs(hue - centers[channel]);
  distance = std::min(distance, 1.0F - distance);
  if (distance <= inner[channel]) return 1.0F;
  if (distance >= outer[channel]) return 0.0F;
  const auto unit = std::clamp(
      (distance - inner[channel]) / (outer[channel] - inner[channel]),
      0.0F, 1.0F);
  return 1.0F - unit * unit * (3.0F - 2.0F * unit);
}

void apply_selective(
    float amount,
    float target_hue,
    float weight,
    float* chroma,
    float* hue) {
  if (std::abs(amount) <= 1.0e-9F) return;
  const auto current_a = *chroma * std::cos(*hue);
  const auto current_b = *chroma * std::sin(*hue);
  const auto direction = target_hue * kTwoPi +
                         (amount < 0.0F ? kTwoPi * 0.5F : 0.0F);
  const auto strength = std::abs(amount) * weight * 0.07F;
  const auto next_a = current_a + std::cos(direction) * strength;
  const auto next_b = current_b + std::sin(direction) * strength;
  *chroma = std::hypot(next_a, next_b);
  *hue = wrap_radians_float(std::atan2(next_b, next_a));
}

}  // namespace

void compile_look_mixer_program(
    CompiledLookStageProgram* compiled,
    const ColorAdjustmentState& state) {
  if (compiled == nullptr) return;
  const auto has_per_channel = has_per_channel_color_mixer_adjustments(state);
  const auto has_legacy = std::abs(state.hue_adjustment) > 1.0e-9 ||
                          std::abs(state.saturation_adjustment) > 1.0e-9 ||
                          std::abs(state.luminance_adjustment) > 1.0e-9;
  if (has_per_channel || has_legacy) {
    compiled->active_mask |= CompiledLookStageProgram::kPerChannelMixer;
    const auto model = normalized_color_mixer_model(state);
    compiled->mixer_model = model == "HSL" ? 1U : model == "HSV" ? 2U :
                             model == "LCH" ? 3U : 0U;
    for (auto channel = std::size_t{0}; channel < kColorMixerChannelCount; ++channel) {
      auto mixer = state.color_mixer_channels[channel];
      if (!has_per_channel && state.color_mixer_channel == kColorMixerChannelNames[channel]) {
        mixer.hue = state.hue_adjustment;
        mixer.saturation = state.saturation_adjustment;
        mixer.luminance = state.luminance_adjustment;
      }
      compiled->mixer_hue[channel] = static_cast<float>(clamp_signed_percent(mixer.hue) / 100.0);
      compiled->mixer_saturation[channel] = static_cast<float>(clamp_signed_percent(mixer.saturation) / 100.0);
      compiled->mixer_luminance[channel] = static_cast<float>(clamp_signed_percent(mixer.luminance) / 100.0);
      compiled->selective_cyan[channel] = static_cast<float>(clamp_signed_percent(mixer.selective_cyan) / 100.0);
      compiled->selective_magenta[channel] = static_cast<float>(clamp_signed_percent(mixer.selective_magenta) / 100.0);
      compiled->selective_yellow[channel] = static_cast<float>(clamp_signed_percent(mixer.selective_yellow) / 100.0);
      compiled->selective_black[channel] = static_cast<float>(clamp_signed_percent(mixer.selective_black) / 100.0);
    }
  }
  compile_look_harmony_program(compiled, state);
}

void apply_compiled_look_mixer(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& input,
    float skin_scale,
    float* lightness,
    float* a,
    float* b) {
  auto chroma = std::hypot(*a, *b);
  if (chroma <= 1.0e-6F) return;
  auto hue = wrap_radians_float(std::atan2(*b, *a));
  if ((compiled.active_mask & CompiledLookStageProgram::kPerChannelMixer) != 0U) {
    const auto selection_hue = compiled.mixer_model == 1U || compiled.mixer_model == 2U
                                   ? rgb_hue(input)
                                   : wrap_unit_float(hue / kTwoPi);
    if (selection_hue >= 0.0F) {
      const auto hue_scale = compiled.mixer_model == 2U ? 0.16F : 0.22F;
      const auto saturation_scale = compiled.mixer_model == 3U ? 0.72F :
                                    compiled.mixer_model == 2U ? 0.92F : 1.15F;
      const auto luminance_scale = compiled.mixer_model == 2U ? 0.14F :
                                   compiled.mixer_model == 3U ? 0.20F : 0.30F;
      for (auto channel = std::size_t{0}; channel < kColorMixerChannelCount; ++channel) {
        const auto weight = channel_weight(selection_hue, channel) * skin_scale;
        if (weight <= 0.0F) continue;
        hue = wrap_radians_float(hue + weight * compiled.mixer_hue[channel] * hue_scale * kTwoPi);
        chroma *= std::max(0.0F, 1.0F + weight * compiled.mixer_saturation[channel] * saturation_scale);
        *lightness += weight * compiled.mixer_luminance[channel] * luminance_scale;
        apply_selective(compiled.selective_cyan[channel], 0.50F, weight, &chroma, &hue);
        apply_selective(compiled.selective_magenta[channel], 0.88F, weight, &chroma, &hue);
        apply_selective(compiled.selective_yellow[channel], 0.15F, weight, &chroma, &hue);
        *lightness -= weight * compiled.selective_black[channel] * 0.18F;
      }
    }
  }
  *a = chroma * std::cos(hue);
  *b = chroma * std::sin(hue);
}

}  // namespace photo_editor::pipeline
