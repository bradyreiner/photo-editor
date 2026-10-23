#include "color_look_runtime.h"

#include "color_math_runtime.h"
#include "color_look_grading_math.h"
#include "color_look_harmony_math.h"
#include "color_mixer_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace photo_editor::pipeline {

constexpr double kPi = std::numbers::pi_v<double>;

[[nodiscard]] bool has_color_grading_adjustments(const ColorAdjustmentState& state) {
  if (clamp_unit(state.grading_amount / 100.0) <= 1e-9) {
    return false;
  }
  const auto has_tint_or_lightness_change =
      std::abs(state.grading_shadows_saturation) > 1e-9 ||
      std::abs(state.grading_shadows_luminance) > 1e-9 ||
      std::abs(state.grading_midtones_saturation) > 1e-9 ||
      std::abs(state.grading_midtones_luminance) > 1e-9 ||
      std::abs(state.grading_highlights_saturation) > 1e-9 ||
      std::abs(state.grading_highlights_luminance) > 1e-9;
  if (state.color_pipeline.photographic_rendering_model ==
      PhotographicRenderingModel::Aces2OcioV1) {
    // A modern hue control only selects the direction of an active tint; it
    // cannot change pixels while every saturation and luminance control is
    // zero. Keep such recipes on the exact identity path.
    return has_tint_or_lightness_change;
  }

  // Preserve the historical renderer's treatment of hue-only grading as an
  // active look, including its perceptual round trip.
  return has_tint_or_lightness_change ||
      std::abs(state.grading_shadows_hue) > 1e-9 ||
      std::abs(state.grading_midtones_hue) > 1e-9 ||
      std::abs(state.grading_highlights_hue) > 1e-9;
}

[[nodiscard]] bool has_color_replacement_adjustments(const ColorAdjustmentState& state) {
  return state.color_replacement_enabled && std::abs(state.color_replacement_strength) > 1e-9;
}

[[nodiscard]] double skin_tone_protection_weight(
    const ColorAdjustmentState& state,
    double normalized_hue,
    double chroma) {
  if (!state.skin_tone_protection_enabled || std::abs(state.skin_tone_protection_strength) <= 1e-9) {
    return 0.0;
  }
  const auto range = std::clamp(state.skin_tone_protection_range / 100.0, 0.01, 1.0) * 0.50;
  const auto legacy_semantics =
      state.color_pipeline.photographic_rendering_model !=
      PhotographicRenderingModel::Aces2OcioV1;
  const auto skin_center = legacy_semantics
      ? wrap_unit(state.skin_tone_line_hue)
      : wrap_unit(oklab_hue_for_rgb_hue(state.skin_tone_line_hue) / (2.0 * kPi));
  return circular_weight(normalized_hue, skin_center, range) *
         smoothstep(0.02, 0.12, chroma) *
         clamp_unit(state.skin_tone_protection_strength / 100.0);
}

[[nodiscard]] double skin_tone_protection_scale(
    const ColorAdjustmentState& state,
    double normalized_hue,
    double chroma) {
  return 1.0 - 0.95 * skin_tone_protection_weight(state, normalized_hue, chroma);
}

[[nodiscard]] double highlight_chroma_compression_scale(double lightness, double chroma) {
  const auto highlight_weight = smoothstep(0.78, 0.98, lightness);
  const auto chroma_weight = smoothstep(0.035, 0.22, chroma);
  return std::clamp(1.0 - 0.20 * highlight_weight * chroma_weight, 0.78, 1.0);
}

[[nodiscard]] bool highlight_chroma_compression_enabled(
    const ColorAdjustmentState& state,
    const ColorPixel& pixel,
    double output_headroom) {
  if (photographic_rendering_model_uses_aces2(
          state.color_pipeline.photographic_rendering_model) &&
      state.color_pipeline.render_intent == RenderIntent::Perceptual) {
    // The ACES DRT owns highlight colorfulness compression. Applying the
    // legacy pre-display compression here makes exposure hue-dependent in the
    // upper scene range and destroys information before output rendering.
    return false;
  }
  const auto peak = std::max({pixel.red, pixel.green, pixel.blue});
  const auto headroom =
      sanitized_scene_working_headroom(output_headroom);
  if (static_cast<double>(peak) > headroom + 1.0e-5) {
    return true;
  }
  if (peak < 0.98F) {
    return false;
  }
  return std::abs(effective_input_exposure(state)) > 1.0e-9 ||
         std::abs(state.filmic_shoulder) > 1.0e-9 ||
         std::abs(state.hdr_local_tone_mapping) > 1.0e-9;
}

[[nodiscard]] bool has_color_harmony_adjustments(const ColorAdjustmentState& state) {
  return state.color_harmony_enabled && std::abs(state.color_harmony_strength) > 1e-9;
}

[[nodiscard]] std::array<double, 4> color_harmony_anchors(const ColorAdjustmentState& state, int* count) {
  const auto base = wrap_unit(state.color_harmony_base_hue);
  if (state.color_harmony_mode == "Analogous") {
    *count = 3;
    return {wrap_unit(base - 0.08333333333333333), base, wrap_unit(base + 0.08333333333333333), base};
  }
  if (state.color_harmony_mode == "Triadic") {
    *count = 3;
    return {base, wrap_unit(base + 1.0 / 3.0), wrap_unit(base + 2.0 / 3.0), base};
  }
  if (state.color_harmony_mode == "Split Complementary") {
    *count = 3;
    return {base, wrap_unit(base + 5.0 / 12.0), wrap_unit(base + 7.0 / 12.0), base};
  }
  if (state.color_harmony_mode == "Tetradic") {
    *count = 4;
    return {base, wrap_unit(base + 0.25), wrap_unit(base + 0.50), wrap_unit(base + 0.75)};
  }
  *count = 2;
  return {base, wrap_unit(base + 0.50), base, base};
}

[[nodiscard]] double nearest_color_harmony_hue(const ColorAdjustmentState& state, double normalized_hue) {
  auto count = 0;
  const auto anchors = color_harmony_anchors(state, &count);
  auto best_hue = anchors[0];
  auto best_distance = 1.0;
  for (int i = 0; i < count; ++i) {
    auto distance = std::abs(normalized_hue - anchors[static_cast<std::size_t>(i)]);
    distance = std::min(distance, 1.0 - distance);
    if (distance < best_distance) {
      best_distance = distance;
      best_hue = anchors[static_cast<std::size_t>(i)];
    }
  }
  return best_hue;
}

[[nodiscard]] std::array<float, 4> color_harmony_oklab_anchors(
    const ColorAdjustmentState& state, int* count) {
  const auto display_anchors = color_harmony_anchors(state, count);
  std::array<float, 4> anchors{};
  for (auto index = 0; index < *count; ++index) {
    anchors[static_cast<std::size_t>(index)] = static_cast<float>(wrap_unit(
        oklab_hue_for_rgb_hue(
            display_anchors[static_cast<std::size_t>(index)]) /
        (2.0 * kPi)));
  }
  return anchors;
}

[[nodiscard]] bool has_legacy_color_mixer_adjustments(const ColorAdjustmentState& state) {
  return std::abs(state.hue_adjustment) > 1e-9 ||
         std::abs(state.saturation_adjustment) > 1e-9 ||
         std::abs(state.luminance_adjustment) > 1e-9;
}

[[nodiscard]] bool has_post_tone_color_adjustments(
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength) {
  return std::abs(saturation_strength) > 1e-9 ||
         std::abs(vibrance_strength) > 1e-9 ||
          has_per_channel_color_mixer_adjustments(state) ||
          has_legacy_color_mixer_adjustments(state) ||
          has_color_replacement_adjustments(state) ||
          has_color_harmony_adjustments(state) ||
          has_color_grading_adjustments(state);
}


ColorPixel apply_post_tone_color_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double output_headroom) {
  const auto legacy_semantics =
      state.color_pipeline.photographic_rendering_model !=
      PhotographicRenderingModel::Aces2OcioV1;
  const auto has_post_color = has_post_tone_color_adjustments(
                                  state, saturation_strength, vibrance_strength) ||
      std::max(0.0, dehaze_strength_for_saturation) > 1.0e-9;
  if (!has_post_color && aces_drt_owns_highlight_chroma(state)) {
    return input;
  }
  const auto channel_spread = std::max({
      std::abs(input.red - input.green),
      std::abs(input.green - input.blue),
      std::abs(input.red - input.blue),
  });
  const auto oklab_before_color = linear_rgb_to_oklab(input, context);
  const auto pre_color_lightness = clamp_oklab_lightness(oklab_before_color.lightness, context);
  const auto pre_color_chroma = std::hypot(oklab_before_color.a, oklab_before_color.b);
  const auto highlight_chroma_scale = highlight_chroma_compression_enabled(state, input, output_headroom)
                                          ? highlight_chroma_compression_scale(
                                                pre_color_lightness,
                                                pre_color_chroma)
                                          : 1.0;
  if (!has_post_color && highlight_chroma_scale >= 0.999999) {
    return input;
  }
  const auto neutral_channel_spread = legacy_semantics ? 1.0e-6 : 0.0;
  if (channel_spread <= neutral_channel_spread &&
      !has_color_grading_adjustments(state) &&
      highlight_chroma_scale >= 0.999999) {
    return input;
  }

  auto oklab = oklab_before_color;
  auto lightness = clamp_oklab_lightness(oklab.lightness, context);
  auto chroma = std::hypot(oklab.a, oklab.b);
  auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));
  const auto selection_rgb = pixel_to_linear_srgb_reference(input, context);
  const auto selection_confidence = color_selection_confidence(selection_rgb);

  auto explicit_skin_protection = 1.0;
  auto replacement_luminance_reference = -1.0;
  auto replacement_luminance_weight = 0.0;
  if (chroma > 0.0) {
    const auto mixer_model = normalized_color_mixer_model(state);
    const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
    const auto mixer_hue_for_selection = selection_confidence > 0.0
        ? rgb_hue_for_mixer_selection(selection_rgb) : -1.0;
    const auto low_chroma_weight = 1.0 - smoothstep(0.035, 0.24, chroma);
    explicit_skin_protection = skin_tone_protection_scale(state, normalized_hue, chroma);
    chroma *= std::max(0.0, 1.0 + vibrance_strength * low_chroma_weight *
                                       explicit_skin_protection * 0.82);
    chroma *= std::max(0.0, 1.0 + std::max(0.0, dehaze_strength_for_saturation) * 0.10);
    chroma *= highlight_chroma_scale;

    if (mixer_hue_for_selection >= 0.0) {
      // Match compiled/Metal execution: low chroma cannot trigger a full
      // absolute lightness lift. Capture this before channel edits so that
      // a saturation adjustment cannot change another channel's L response.
      const auto luminance_chroma_scale = legacy_semantics
          ? 1.0 : std::min(1.0, chroma / 0.12);
      const auto use_legacy_mixer = !has_per_channel_color_mixer_adjustments(state);
      const auto hue_scale = mixer_model == "HSV" ? 0.16 : 0.22;
      const auto saturation_scale = mixer_model == "LCH" ? 0.72 : (mixer_model == "HSV" ? 0.92 : 1.15);
      const auto luminance_scale = mixer_model == "HSV" ? 0.14 : (mixer_model == "LCH" ? 0.20 : 0.30);
      for (int ch = 0; ch < kColorMixerChannelCount; ++ch) {
        auto mixer = state.color_mixer_channels[static_cast<std::size_t>(ch)];
        if (use_legacy_mixer &&
            state.color_mixer_channel == kColorMixerChannelNames[static_cast<std::size_t>(ch)]) {
          mixer.hue = state.hue_adjustment;
          mixer.saturation = state.saturation_adjustment;
          mixer.luminance = state.luminance_adjustment;
        }
        const auto mixer_weight = channel_weight_for_selection(
            mixer_hue_for_selection, kColorMixerChannelNames[static_cast<std::size_t>(ch)]);
        if (mixer_weight > 0.0) {
          const auto protected_mixer_weight = mixer_weight * explicit_skin_protection * selection_confidence;
          hue = wrap_radians(hue + protected_mixer_weight * (clamp_signed_percent(mixer.hue) / 100.0) *
                                       hue_scale * 2.0 * kPi);
          chroma *= std::max(
              0.0,
              1.0 + protected_mixer_weight * (clamp_signed_percent(mixer.saturation) / 100.0) *
                        saturation_scale);
          lightness +=
              protected_mixer_weight *
              (clamp_signed_percent(mixer.luminance) / 100.0) *
              luminance_scale * luminance_chroma_scale;
          apply_selective_color_adjustment(mixer, protected_mixer_weight, &lightness, &chroma, &hue);
        }
      }
    }
    if (has_color_replacement_adjustments(state)) {
      const auto selection_hue = mixer_hue_for_selection;
      const auto range = std::clamp(state.color_replacement_range / 100.0, 0.01, 1.0) * 0.50;
      const auto softness = clamp_unit(state.color_replacement_softness / 100.0);
      const auto replacement_weight =
          (selection_hue >= 0.0
               ? circular_feathered_selection_weight(
                     selection_hue,
                     wrap_unit(state.color_replacement_source_hue),
                     range,
                     softness)
               : 0.0) *
          clamp_unit(state.color_replacement_strength / 100.0) * explicit_skin_protection * selection_confidence;
      if (replacement_weight > 1e-6) {
        if (state.color_replacement_preserve_luminance) {
          replacement_luminance_reference = luminance_for_pixel(
              oklab_to_linear_rgb(
                  OklabColor{
                      .lightness = lightness,
                      .a = chroma * std::cos(hue),
                      .b = chroma * std::sin(hue),
                  },
                  input.alpha,
                  context),
              context);
          replacement_luminance_weight = replacement_weight;
        }
        const auto target_hue = oklab_hue_for_rgb_hue(
            state.color_replacement_target_hue);
        hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * replacement_weight);
        if (!state.color_replacement_preserve_saturation) {
          chroma = std::max(chroma, std::lerp(chroma, 0.16, replacement_weight));
        }
      }
    }
    if (has_color_harmony_adjustments(state)) {
      const auto harmony_weight = clamp_unit(state.color_harmony_strength / 100.0) *
          0.65 * explicit_skin_protection * selection_confidence;
      if (legacy_semantics) {
        const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
        const auto target_hue = nearest_color_harmony_hue(state, normalized_hue) * 2.0 * kPi;
        hue = wrap_radians(
            hue + shortest_angular_delta(hue, target_hue) * harmony_weight);
      } else {
        auto anchor_count = 0;
        const auto anchors = color_harmony_oklab_anchors(state, &anchor_count);
        const auto force = photo_editor_color_harmony_circular_force(
            static_cast<float>(hue), anchors.data(),
            static_cast<unsigned int>(anchor_count));
        hue = wrap_radians(hue + static_cast<double>(force * harmony_weight));
      }
    }
  }

  {
    auto grade_a = chroma * std::cos(hue);
    auto grade_b = chroma * std::sin(hue);

    const auto grading_amount = clamp_unit(state.grading_amount / 100.0);
    const auto balance_shift = clamp_signed_percent(state.grading_balance) / 100.0 * 0.18;
    const auto grading_selection_lightness = clamp_unit(lightness);
    const auto shadow_weight =
        1.0 -
        smoothstep(
            0.15 - balance_shift,
            0.45 - balance_shift,
            grading_selection_lightness);
    const auto highlight_weight =
        smoothstep(
            0.55 - balance_shift,
            0.85 - balance_shift,
            grading_selection_lightness);
    const auto midtone_weight = 1.0 - shadow_weight - highlight_weight;

    const std::array<double, 3> weights{
        shadow_weight, std::max(0.0, midtone_weight), highlight_weight};
    const std::array<double, 3> display_hues{
        state.grading_shadows_hue, state.grading_midtones_hue,
        state.grading_highlights_hue};
    const std::array<double, 3> saturations{
        state.grading_shadows_saturation, state.grading_midtones_saturation,
        state.grading_highlights_saturation};
    const std::array<double, 3> luminances{
        state.grading_shadows_luminance, state.grading_midtones_luminance,
        state.grading_highlights_luminance};

    if (legacy_semantics) {
      // Preserve saved legacy recipes: saturation sign selects magnitude only,
      // and the stored hue remains a raw turn as it was in the frozen renderer.
      for (std::size_t band = 0; band < weights.size(); ++band) {
        if (weights[band] < 1.0e-9) continue;
        const auto sat_norm = clamp_signed_percent(saturations[band]) / 100.0;
        const auto hue_radians = wrap_unit(display_hues[band]) * 2.0 * kPi;
        const auto lum_norm = clamp_signed_percent(luminances[band]) / 100.0;
        const auto scaled_weight = weights[band] * grading_amount;
        const auto intensity = std::abs(sat_norm) * 0.18 * scaled_weight *
            explicit_skin_protection;
        if (intensity > 1.0e-9) {
          grade_a += intensity * std::cos(hue_radians);
          grade_b += intensity * std::sin(hue_radians);
        }
        lightness += lum_norm * 0.20 * scaled_weight * explicit_skin_protection;
      }
    } else {
      auto desaturation = 0.0F;
      auto tint_a = 0.0F;
      auto tint_b = 0.0F;
      for (std::size_t band = 0; band < weights.size(); ++band) {
        const auto hue_turns = wrap_unit(
            oklab_hue_for_rgb_hue(display_hues[band]) / (2.0 * kPi));
        const auto contribution = color_look_grading_band_contribution(
            static_cast<float>(hue_turns),
            static_cast<float>(clamp_signed_percent(saturations[band]) / 100.0),
            static_cast<float>(clamp_signed_percent(luminances[band]) / 100.0),
            static_cast<float>(weights[band] * grading_amount *
                               explicit_skin_protection));
        desaturation += contribution.desaturation;
        tint_a += contribution.tint_a;
        tint_b += contribution.tint_b;
        lightness += contribution.lightness_delta;
      }
      const auto chroma_retention = std::clamp(1.0F - desaturation, 0.0F, 1.0F);
      grade_a = static_cast<double>(static_cast<float>(grade_a) * chroma_retention + tint_a);
      grade_b = static_cast<double>(static_cast<float>(grade_b) * chroma_retention + tint_b);
    }

    chroma = std::hypot(grade_a, grade_b);
    hue = wrap_radians(std::atan2(grade_b, grade_a));
  }

  if (std::abs(saturation_strength) > 1e-9) {
    chroma *= saturation_strength >= 0.0
                  ? std::max(0.0, 1.0 + saturation_strength * 0.85)
                  : std::max(0.0, 1.0 + saturation_strength);
  }

  auto output = oklab_to_linear_rgb(
      OklabColor{
          .lightness = lightness,
          .a = chroma * std::cos(hue),
          .b = chroma * std::sin(hue),
      },
      input.alpha,
      context);
  if (replacement_luminance_reference >= 0.0 && replacement_luminance_weight > 1e-6) {
    const auto current_luminance = luminance_for_pixel(output, context);
    const auto target_luminance = std::lerp(
        current_luminance,
        std::clamp(replacement_luminance_reference, 0.0, output_headroom),
        clamp_unit(replacement_luminance_weight));
    output = scale_pixel_to_luminance(output, target_luminance, context);
  }
  const auto grading_adds_chroma = legacy_semantics
      ? std::abs(state.grading_shadows_saturation) > 1e-9 ||
            std::abs(state.grading_midtones_saturation) > 1e-9 ||
            std::abs(state.grading_highlights_saturation) > 1e-9
      : state.grading_shadows_saturation > 1e-9 ||
            state.grading_midtones_saturation > 1e-9 ||
            state.grading_highlights_saturation > 1e-9;
  if (saturation_strength <= -0.999999 && !grading_adds_chroma &&
      !has_color_replacement_adjustments(state)) {
    const auto neutral = static_cast<float>(
        luminance_for_pixel(output, context));
    output.red = neutral;
    output.green = neutral;
    output.blue = neutral;
  }
  return output;
}


}  // namespace photo_editor::pipeline
