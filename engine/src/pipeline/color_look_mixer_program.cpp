#include "color_look_compilation_runtime.h"

#include "color_mixer_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

constexpr auto kTwoPi = 6.28318530717958647692F;

[[nodiscard]] float wrap_radians_float(float value) {
  value = std::fmod(value, kTwoPi);
  return value < 0.0F ? value + kTwoPi : value;
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
  if (chroma <= 0.0F) return;
  auto hue = wrap_radians_float(std::atan2(*b, *a));
  if ((compiled.active_mask & CompiledLookStageProgram::kPerChannelMixer) != 0U) {
    // A hue-dependent absolute L offset turns tiny chroma variations into
    // large light/dark speckles. Bound that offset by the chromatic signal,
    // using the same pre-mixer C for every channel (including overlaps).
    // This is a signal-size guard, not a sensor-noise estimate or image blur.
    const auto luminance_chroma_scale =
        (compiled.active_mask & CompiledLookStageProgram::kLegacyCreativeSemantics) != 0U
            ? 1.0F : std::min(1.0F, chroma / 0.12F);
    const auto selection_rgb = color_selection_rgb(compiled, input);
    const auto confidence = static_cast<float>(color_selection_confidence(selection_rgb));
    if (confidence <= 0.0F) return;
    const auto selection_hue = rgb_hue_for_mixer_selection(selection_rgb);
    if (selection_hue >= 0.0F) {
      const auto hue_scale = compiled.mixer_model == 2U ? 0.16F : 0.22F;
      const auto saturation_scale = compiled.mixer_model == 3U ? 0.72F :
                                    compiled.mixer_model == 2U ? 0.92F : 1.15F;
      const auto luminance_scale = compiled.mixer_model == 2U ? 0.14F :
                                   compiled.mixer_model == 3U ? 0.20F : 0.30F;
      for (auto channel = std::size_t{0}; channel < kColorMixerChannelCount; ++channel) {
        if (compiled.mixer_hue[channel] == 0.0F && compiled.mixer_saturation[channel] == 0.0F &&
            compiled.mixer_luminance[channel] == 0.0F && compiled.selective_cyan[channel] == 0.0F &&
            compiled.selective_magenta[channel] == 0.0F && compiled.selective_yellow[channel] == 0.0F &&
            compiled.selective_black[channel] == 0.0F) continue;
        const auto weight = static_cast<float>(color_mixer_channel_weight(selection_hue, channel)) * skin_scale * confidence;
        if (weight <= 0.0F) continue;
        hue = wrap_radians_float(hue + weight * compiled.mixer_hue[channel] * hue_scale * kTwoPi);
        chroma *= std::max(0.0F, 1.0F + weight * compiled.mixer_saturation[channel] * saturation_scale);
        *lightness += weight * compiled.mixer_luminance[channel] * luminance_scale *
                      luminance_chroma_scale;
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
