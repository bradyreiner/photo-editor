#include "photo_editor/pipeline/color_adjustments.h"

#include "color_look_compilation_runtime.h"
#include "color_look_protected_program.h"
#include "color_look_runtime.h"
#include "color_mixer_runtime.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
  const auto unit =
      std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
  return unit * unit * (3.0F - 2.0F * unit);
}

[[nodiscard]] std::array<float, 3> multiply(
    const ColorMatrix3& matrix,
    const std::array<float, 3>& value) {
  return {
      static_cast<float>(matrix.values[0]) * value[0] +
          static_cast<float>(matrix.values[1]) * value[1] +
          static_cast<float>(matrix.values[2]) * value[2],
      static_cast<float>(matrix.values[3]) * value[0] +
          static_cast<float>(matrix.values[4]) * value[1] +
          static_cast<float>(matrix.values[5]) * value[2],
      static_cast<float>(matrix.values[6]) * value[0] +
          static_cast<float>(matrix.values[7]) * value[1] +
          static_cast<float>(matrix.values[8]) * value[2],
  };
}

[[nodiscard]] float native_luminance(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& pixel) {
  return pixel.red * compiled.luminance_coefficients[0] +
         pixel.green * compiled.luminance_coefficients[1] +
         pixel.blue * compiled.luminance_coefficients[2];
}

[[nodiscard]] ColorPixel apply_rgb_mixer(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& pixel) {
  const auto mixed = multiply(
      ColorMatrix3{.values = {
          compiled.rgb_mixer[0], compiled.rgb_mixer[1], compiled.rgb_mixer[2],
          compiled.rgb_mixer[3], compiled.rgb_mixer[4], compiled.rgb_mixer[5],
          compiled.rgb_mixer[6], compiled.rgb_mixer[7], compiled.rgb_mixer[8]}},
      {pixel.red, pixel.green, pixel.blue});
  return {
      .red = std::max(0.0F, mixed[0]),
      .green = std::max(0.0F, mixed[1]),
      .blue = std::max(0.0F, mixed[2]),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] ColorPixel apply_black_and_white(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& pixel) {
  auto gray = native_luminance(compiled, pixel);
  const auto selection_rgb = color_selection_rgb(compiled, pixel);
  const auto confidence = static_cast<float>(color_selection_confidence(selection_rgb));
  const auto hue = confidence > 0.0F ? rgb_hue_for_mixer_selection(selection_rgb) : -1.0;
  if (hue >= 0.0F) {
    for (auto channel = 0; channel < kColorMixerChannelCount; ++channel) {
      const auto weight = confidence * static_cast<float>(color_mixer_channel_weight(hue, static_cast<std::size_t>(channel)));
      gray *= std::max(
          0.0F,
          1.0F + weight *
                     compiled.black_and_white_mixer[
                         static_cast<std::size_t>(channel)] *
                     0.85F);
    }
  }
  gray = std::max(0.0F, gray);
  return {.red = gray, .green = gray, .blue = gray, .alpha = pixel.alpha};
}

[[nodiscard]] ColorPixel apply_saturation_vibrance(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& input) {
  const auto spread = std::max({
      std::abs(input.red - input.green),
      std::abs(input.green - input.blue),
      std::abs(input.red - input.blue)});
  const auto neutral_threshold =
      (compiled.active_mask & CompiledLookStageProgram::kLegacyCreativeSemantics) != 0U
          ? 1.0e-6F : 0.0F;
  if (spread <= neutral_threshold &&
      (compiled.active_mask & CompiledLookStageProgram::kColorGrading) == 0U) {
    return input;
  }
  // Hue-selective edits are exact identities when the source has no reliable color.
  // Avoid a perceptual round trip (and its rounding noise) in that common case.
  if ((compiled.active_mask & (CompiledLookStageProgram::kSaturationVibrance |
                               CompiledLookStageProgram::kColorGrading)) == 0U &&
      color_selection_confidence(color_selection_rgb(compiled, input)) <= 0.0) {
    return input;
  }
  const auto xyz = multiply(
      compiled.rgb_to_xyz_d65, {input.red, input.green, input.blue});
  const auto l = 0.8189330101F * xyz[0] + 0.3618667424F * xyz[1] -
                 0.1288597137F * xyz[2];
  const auto m = 0.0329845436F * xyz[0] + 0.9293118715F * xyz[1] +
                 0.0361456387F * xyz[2];
  const auto s = 0.0482003018F * xyz[0] + 0.2643662691F * xyz[1] +
                 0.6338517070F * xyz[2];
  const auto l_root = std::cbrt(l);
  const auto m_root = std::cbrt(m);
  const auto s_root = std::cbrt(s);
  auto lightness = std::clamp(
      0.2104542553F * l_root + 0.7936177850F * m_root -
          0.0040720468F * s_root,
      0.0F,
      std::cbrt(compiled.output_headroom));
  auto a = 1.9779984951F * l_root - 2.4285922050F * m_root +
           0.4505937099F * s_root;
  auto b = 0.0259040371F * l_root + 0.7827717662F * m_root -
           0.8086757660F * s_root;
  auto chroma = std::hypot(a, b);
  const auto skin_scale = compiled_look_skin_protection_scale(compiled, a, b);
  const auto vibrance_weight = 1.0F - smoothstep(0.035F, 0.24F, chroma);
  chroma *= std::max(
      0.0F, 1.0F + compiled.vibrance_strength * vibrance_weight * skin_scale * 0.82F);
  const auto original_chroma = std::hypot(a, b);
  const auto peak = std::max({input.red, input.green, input.blue});
  const auto over_headroom =
      (compiled.active_mask &
       CompiledLookStageProgram::kHighlightChromaCompressionOverHeadroom) != 0U &&
      static_cast<double>(peak) >
          sanitized_scene_working_headroom(compiled.output_headroom) + 1.0e-5;
  const auto from_tone =
      (compiled.active_mask &
       CompiledLookStageProgram::kHighlightChromaCompressionFromTone) != 0U &&
      !over_headroom && peak >= 0.98F;
  if (over_headroom || from_tone) {
    chroma *= static_cast<float>(highlight_chroma_compression_scale(
        static_cast<double>(lightness), static_cast<double>(original_chroma)));
  }
  auto chroma_scale = original_chroma > 1.0e-9F
                          ? chroma / original_chroma
                          : 0.0F;
  a *= chroma_scale;
  b *= chroma_scale;
  if ((compiled.active_mask & (CompiledLookStageProgram::kPerChannelMixer | CompiledLookStageProgram::kColorHarmony)) != 0U) {
    apply_compiled_look_mixer(compiled, input, skin_scale, &lightness, &a, &b);
    chroma = std::hypot(a, b);
  }
  auto replacement_luminance = -1.0F;
  auto replacement_weight = 0.0F;
  apply_compiled_look_replacement(compiled, input, skin_scale, &lightness, &a, &b,
                                  &replacement_luminance, &replacement_weight);
  if ((compiled.active_mask & CompiledLookStageProgram::kColorHarmony) != 0U)
    apply_compiled_look_harmony(compiled, skin_scale * static_cast<float>(
        color_selection_confidence(color_selection_rgb(compiled, input))), &a, &b);
  chroma = std::hypot(a, b);
  if ((compiled.active_mask & CompiledLookStageProgram::kColorGrading) != 0U) {
    apply_compiled_look_grading(compiled, skin_scale, &lightness, &a, &b);
    chroma = std::hypot(a, b);
  }
  chroma *= compiled.saturation_strength >= 0.0F
                ? std::max(0.0F, 1.0F + compiled.saturation_strength * 0.85F)
                : std::max(0.0F, 1.0F + compiled.saturation_strength);
  const auto graded_chroma = std::hypot(a, b);
  chroma_scale = graded_chroma > 1.0e-9F ? chroma / graded_chroma : 0.0F;
  a *= chroma_scale;
  b *= chroma_scale;
  const auto next_l = lightness + 0.3963377774F * a + 0.2158037573F * b;
  const auto next_m = lightness - 0.1055613458F * a - 0.0638541728F * b;
  const auto next_s = lightness - 0.0894841775F * a - 1.2914855480F * b;
  const auto linear_l = next_l * next_l * next_l;
  const auto linear_m = next_m * next_m * next_m;
  const auto linear_s = next_s * next_s * next_s;
  const auto oklab_rgb = std::array<float, 3>{
      1.2270138511F * linear_l - 0.5577999807F * linear_m +
          0.2812561490F * linear_s,
      -0.0405801784F * linear_l + 1.1122568696F * linear_m -
          0.0716766787F * linear_s,
      -0.0763812845F * linear_l - 0.4214819784F * linear_m +
          1.5861632204F * linear_s,
  };
  const auto rgb = multiply(compiled.xyz_d65_to_rgb, oklab_rgb);
  auto output = apply_compiled_replacement_luminance(
      compiled, ColorPixel{rgb[0], rgb[1], rgb[2], input.alpha},
      replacement_luminance, replacement_weight);
  if (compiled.saturation_strength <= -0.999999F &&
      (compiled.active_mask &
       (CompiledLookStageProgram::kColorGradingAddsChroma |
        CompiledLookStageProgram::kColorReplacement)) == 0U) {
    const auto neutral = native_luminance(compiled, output);
    output.red = neutral;
    output.green = neutral;
    output.blue = neutral;
  }
  return output;
}

}  // namespace

CompiledLookStageProgram compile_look_stage_program(
    const ColorAdjustmentState& state) {
  auto compiled = CompiledLookStageProgram{};
  const auto context = make_pipeline_color_context(state);
  compiled.rgb_to_xyz_d65 = context.rgb_to_xyz_d65;
  compiled.xyz_d65_to_rgb = context.xyz_d65_to_rgb;
  compiled.rgb_to_selection_rgb = context.rgb_to_linear_srgb;
  compiled.luminance_coefficients = {
      static_cast<float>(context.rgb_to_xyz_native.values[3]),
      static_cast<float>(context.rgb_to_xyz_native.values[4]),
      static_cast<float>(context.rgb_to_xyz_native.values[5])};
  compiled.output_headroom = static_cast<float>(sanitized_output_headroom(state));
  if (has_rgb_channel_mixer_adjustments(state)) {
    compiled.active_mask |= CompiledLookStageProgram::kRgbMixer;
  }
  const auto mixer_values = std::array{
      state.rgb_channel_mixer.red_from_red,
      state.rgb_channel_mixer.red_from_green,
      state.rgb_channel_mixer.red_from_blue,
      state.rgb_channel_mixer.green_from_red,
      state.rgb_channel_mixer.green_from_green,
      state.rgb_channel_mixer.green_from_blue,
      state.rgb_channel_mixer.blue_from_red,
      state.rgb_channel_mixer.blue_from_green,
      state.rgb_channel_mixer.blue_from_blue};
  for (auto index = std::size_t{0}; index < mixer_values.size(); ++index) {
    compiled.rgb_mixer[index] = static_cast<float>(mixer_values[index] / 100.0);
  }
  if (state.black_and_white_enabled) {
    compiled.active_mask |= CompiledLookStageProgram::kBlackAndWhite;
  }
  for (auto index = std::size_t{0}; index < state.black_and_white_mixer.size(); ++index) {
    compiled.black_and_white_mixer[index] = static_cast<float>(
        clamp_signed_percent(state.black_and_white_mixer[index]) / 100.0);
  }
  compiled.saturation_strength =
      static_cast<float>(clamp_signed_percent(state.saturation) / 100.0);
  compiled.vibrance_strength =
      static_cast<float>(clamp_signed_percent(state.vibrance) / 100.0);
  if (std::abs(compiled.saturation_strength) > 1.0e-9F ||
      std::abs(compiled.vibrance_strength) > 1.0e-9F) {
    compiled.active_mask |= CompiledLookStageProgram::kSaturationVibrance;
  }
  compile_look_mixer_program(&compiled, state);
  compile_look_grading_program(&compiled, state);
  compile_look_protected_program(&compiled, state);
  if (compiled.active_mask == 0U) {
    return compiled;
  }
  const auto aces2_perceptual =
      photographic_rendering_model_uses_aces2(
          state.color_pipeline.photographic_rendering_model) &&
      state.color_pipeline.render_intent == RenderIntent::Perceptual;
  if (!aces2_perceptual) {
    // The historical look stage applies highlight chroma compression when
    // scene values exceed output headroom. Tone edits also enable its
    // sub-headroom highlight branch.
    compiled.active_mask |=
        CompiledLookStageProgram::kHighlightChromaCompressionOverHeadroom;
    if (std::abs(effective_input_exposure(state)) > 1.0e-9 ||
        std::abs(state.filmic_shoulder) > 1.0e-9 ||
        std::abs(state.hdr_local_tone_mapping) > 1.0e-9) {
      compiled.active_mask |=
          CompiledLookStageProgram::kHighlightChromaCompressionFromTone;
    }
  }
  if (state.color_pipeline.photographic_rendering_model !=
      PhotographicRenderingModel::Aces2OcioV1) {
    compiled.active_mask |= CompiledLookStageProgram::kLegacyCreativeSemantics;
  }
  compiled.cache_identity = look_stage_program_payload_identity(compiled);
  return compiled;
}

ColorPixel apply_compiled_look_stage_program(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& pixel) {
  if (!compiled.ready()) {
    return pixel;
  }
  auto adjusted = pixel;
  if ((compiled.active_mask & CompiledLookStageProgram::kRgbMixer) != 0U) {
    adjusted = apply_rgb_mixer(compiled, adjusted);
  }
  if ((compiled.active_mask & CompiledLookStageProgram::kBlackAndWhite) != 0U) {
    adjusted = apply_black_and_white(compiled, adjusted);
  }
  if ((compiled.active_mask &
       (CompiledLookStageProgram::kSaturationVibrance |
        CompiledLookStageProgram::kColorGrading |
        CompiledLookStageProgram::kPerChannelMixer |
        CompiledLookStageProgram::kColorHarmony |
        CompiledLookStageProgram::kColorReplacement |
        CompiledLookStageProgram::kHighlightChromaCompressionOverHeadroom |
        CompiledLookStageProgram::kHighlightChromaCompressionFromTone)) != 0U) {
    adjusted = apply_saturation_vibrance(compiled, adjusted);
  }
  return adjusted;
}

}  // namespace photo_editor::pipeline
