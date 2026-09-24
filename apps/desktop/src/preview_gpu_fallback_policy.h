#pragma once

#include "edit_render_policy.h"
#include "edit_recipe_state.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "photo_editor/pipeline/color_lut_program.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace photo_editor::desktop {

[[nodiscard]] inline QString preview_unsupported_stage_reason(
    const QString& stage, const QString& detail) {
  return QStringLiteral("unsupported_stage:%1:%2").arg(stage, detail);
}

[[nodiscard]] inline QString preview_cpu_required_stage_reason(
    const EditRecipeState& state) {
  if (!state.layers.isEmpty()) {
    return preview_unsupported_stage_reason(
        QStringLiteral("Layers"), QStringLiteral("photo_layers"));
  }
  return {};
}

[[nodiscard]] inline bool has_extended_range_pixels(
    const std::vector<photo_editor::pipeline::ColorPixel>& pixels) {
  constexpr auto kTolerance = 1.0e-6F;
  return std::any_of(pixels.cbegin(), pixels.cend(), [](const auto& pixel) {
    return pixel.red > 1.0F + kTolerance ||
           pixel.green > 1.0F + kTolerance ||
           pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
           pixel.green < -kTolerance || pixel.blue < -kTolerance;
  });
}

[[nodiscard]] inline QString preview_gpu_fallback_reason(
    const EditRecipeState& state,
    const photo_editor::pipeline::ColorAdjustmentState& pipeline_state,
    const std::vector<photo_editor::pipeline::ColorPixel>& pixels,
    bool metal_available,
    const QString& metal_unavailable_reason,
    bool input_is_characterized_working_scene = false) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_METAL_PREVIEW")) {
    return QStringLiteral("metal_disabled_by_env");
  }
  if (!metal_available) {
    return metal_unavailable_reason.isEmpty()
               ? QStringLiteral("metal_unavailable")
               : QStringLiteral("metal_unavailable:%1")
                     .arg(metal_unavailable_reason);
  }
  // Match export admission: a compatibility LUT sampled in camera RGB is not
  // equivalent to the exact camera prefix used for masked renders. Canonical
  // fusion starts after that prefix, so it must remain eligible for Metal.
  if (!input_is_characterized_working_scene &&
      pipeline_state.input_transform.raw_input_transform_enabled &&
      pipeline_state.color_pipeline.camera_profile.valid) {
    return preview_unsupported_stage_reason(QStringLiteral("CameraCharacterization"),
        QStringLiteral("camera_linear_lut_fidelity"));
  }
  if (!pipeline_state.local_mask_sets.empty()) {
    return preview_unsupported_stage_reason(
        QStringLiteral("Layers"), QStringLiteral("local_mask_sets"));
  }
  if (!pipeline_state.retouch_masks.empty()) {
    return preview_unsupported_stage_reason(
        QStringLiteral("Layers"), QStringLiteral("retouch_masks"));
  }
  if (!pipeline_state.local_masks.empty()) {
    return preview_unsupported_stage_reason(
        QStringLiteral("Layers"), QStringLiteral("legacy_local_masks"));
  }
  if ((pipeline_state.lut_enabled &&
       std::abs(pipeline_state.lut_strength) > 1.0e-9) ||
      !pipeline_state.lut_stack.empty()) {
    return preview_unsupported_stage_reason(
        QStringLiteral("Look"), QStringLiteral("user_lut"));
  }
  if (!input_is_characterized_working_scene && pixels.size() <=
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    const auto highlight_analysis =
        photo_editor::pipeline::analyze_highlight_reconstruction(
            pixels, static_cast<int>(pixels.size()), 1,
            pipeline_state.input_transform);
    if (highlight_analysis.has_clipping()) {
      return preview_unsupported_stage_reason(
          QStringLiteral("NormalizeRaw"),
          QStringLiteral("camera_linear_highlight_reconstruction"));
    }
  }
  if (!input_is_characterized_working_scene &&
      has_extended_range_pixels(pixels)) {
    return preview_unsupported_stage_reason(
        QStringLiteral("SceneAdjustments"),
        QStringLiteral("extended_range_pixels"));
  }
  if (render_state_requires_exact_global_tone(state)) {
    return preview_unsupported_stage_reason(
        QStringLiteral("Tone"), QStringLiteral("exact_global_tone"));
  }
  if (render_state_requires_exact_post_color(state)) {
    return preview_unsupported_stage_reason(
        QStringLiteral("Look"), QStringLiteral("exact_post_color"));
  }
  return preview_cpu_required_stage_reason(state);
#else
  Q_UNUSED(state);
  Q_UNUSED(pipeline_state);
  Q_UNUSED(pixels);
  Q_UNUSED(metal_available);
  Q_UNUSED(metal_unavailable_reason);
  Q_UNUSED(input_is_characterized_working_scene);
  return QStringLiteral("metal_not_compiled");
#endif
}

[[nodiscard]] inline QString preview_working_fusion_fallback_reason(
    const EditRecipeState& state,
    const photo_editor::pipeline::ColorAdjustmentState& pipeline_state,
    const std::vector<photo_editor::pipeline::ColorPixel>& pixels,
    bool metal_available,
    const QString& metal_unavailable_reason) {
  auto policy_state = state;
  auto policy_pipeline_state = pipeline_state;
  // Universal exposure and the canonical per-pixel tone-table controls have
  // dedicated retained kernels. Image-dependent shoulder and every later
  // stage retain normal fail-closed classification.
  policy_state.exposure = 0.0;
  policy_state.contrast = 0.0;
  policy_state.highlights = 0.0;
  policy_state.shadows = 0.0;
  policy_state.whites = 0.0;
  policy_state.blacks = 0.0;
  policy_state.brightness = 0.0;
  policy_state.filmic_toe = 0.0;
  policy_state.adaptive_contrast = 0.0;
  policy_state.levels_black_point = 0.0;
  policy_state.levels_gamma = 0.0;
  policy_state.levels_white_point = 0.0;
  policy_state.zone_deep_shadows = 0.0;
  policy_state.zone_shadows = 0.0;
  policy_state.zone_midtones = 0.0;
  policy_state.zone_lights = 0.0;
  policy_state.zone_highlights = 0.0;
  policy_state.hdr_local_tone_mapping = 0.0;
  policy_state.curve_shadows = 0.0;
  policy_state.curve_darks = 0.0;
  policy_state.curve_lights = 0.0;
  policy_state.curve_highlights = 0.0;
  policy_state.master_curve_points.clear();
  policy_state.luma_curve_points.clear();
  policy_state.red_curve_points.clear();
  policy_state.green_curve_points.clear();
  policy_state.blue_curve_points.clear();
  policy_state.saturation = 0.0;
  policy_state.vibrance = 0.0;
  policy_state.rgb_channel_mixer = {};
  policy_state.black_and_white_enabled = false;
  policy_state.black_and_white_mixer.fill(0.0);
  policy_state.hue_adjustment = 0.0;
  policy_state.saturation_adjustment = 0.0;
  policy_state.luminance_adjustment = 0.0;
  policy_state.mixer_channels = {};
  policy_state.color_harmony_enabled = false;
  policy_state.color_harmony_strength = 0.0;
  policy_state.color_replacement_enabled = false;
  policy_state.color_replacement_strength = 0.0;
  policy_state.skin_tone_protection_enabled = false;
  policy_state.skin_tone_protection_strength = 0.0;
  policy_state.grading_shadows_hue = 0.0;
  policy_state.grading_shadows_saturation = 0.0;
  policy_state.grading_shadows_luminance = 0.0;
  policy_state.grading_midtones_hue = 0.0;
  policy_state.grading_midtones_saturation = 0.0;
  policy_state.grading_midtones_luminance = 0.0;
  policy_state.grading_highlights_hue = 0.0;
  policy_state.grading_highlights_saturation = 0.0;
  policy_state.grading_highlights_luminance = 0.0;
  policy_state.grading_amount = 100.0;
  policy_state.grading_balance = 0.0;
  if (photo_editor::pipeline::retained_simple_layers_supported(
          pipeline_state)) {
    policy_state.layers.clear();
  }
  if (photo_editor::pipeline::user_lut_state_supports_compiled_execution(
          pipeline_state)) {
    policy_pipeline_state.lut_enabled = false;
    policy_pipeline_state.lut_strength = 0.0;
    policy_pipeline_state.lut_stack.clear();
  }
  return preview_gpu_fallback_reason(
      policy_state,
      policy_pipeline_state,
      pixels,
      metal_available,
      metal_unavailable_reason,
      true);
}

[[nodiscard]] inline bool preview_uses_compiled_user_luts(
    const photo_editor::pipeline::ColorAdjustmentState& state) {
  return (state.lut_enabled && state.lut_strength > 1.0e-6) ||
         std::any_of(
             state.lut_stack.cbegin(), state.lut_stack.cend(),
             [](const auto& lut) {
               return lut.enabled && lut.strength > 1.0e-6;
             });
}

[[nodiscard]] inline bool preview_uses_compiled_working_look(
    const EditRecipeState& state) {
  const auto& mixer = state.rgb_channel_mixer;
  const auto uses_channel_mixer =
      std::abs(state.hue_adjustment) > 1e-6 ||
      std::abs(state.saturation_adjustment) > 1e-6 ||
      std::abs(state.luminance_adjustment) > 1e-6 ||
      std::any_of(
          state.mixer_channels.cbegin(), state.mixer_channels.cend(),
          [](const auto& channel) {
            return std::abs(channel.hue) > 1e-6 ||
                   std::abs(channel.saturation) > 1e-6 ||
                   std::abs(channel.luminance) > 1e-6 ||
                   std::abs(channel.selective_cyan) > 1e-6 ||
                   std::abs(channel.selective_magenta) > 1e-6 ||
                   std::abs(channel.selective_yellow) > 1e-6 ||
                   std::abs(channel.selective_black) > 1e-6;
          });
  const auto uses_grading = state.grading_amount > 1e-6 &&
      (std::abs(state.grading_shadows_saturation) > 1e-6 ||
       std::abs(state.grading_shadows_luminance) > 1e-6 ||
       std::abs(state.grading_midtones_saturation) > 1e-6 ||
       std::abs(state.grading_midtones_luminance) > 1e-6 ||
       std::abs(state.grading_highlights_saturation) > 1e-6 ||
       std::abs(state.grading_highlights_luminance) > 1e-6);
  const auto uses_harmony = state.color_harmony_enabled &&
                            state.color_harmony_strength > 1e-6;
  const auto uses_replacement = state.color_replacement_enabled &&
                                state.color_replacement_strength > 1e-6;
  return std::abs(state.saturation) > 1e-6 ||
         std::abs(state.vibrance) > 1e-6 ||
         std::abs(mixer.red_from_red - 100.0) > 1e-6 ||
         std::abs(mixer.red_from_green) > 1e-6 ||
         std::abs(mixer.red_from_blue) > 1e-6 ||
         std::abs(mixer.green_from_red) > 1e-6 ||
         std::abs(mixer.green_from_green - 100.0) > 1e-6 ||
         std::abs(mixer.green_from_blue) > 1e-6 ||
         std::abs(mixer.blue_from_red) > 1e-6 ||
         std::abs(mixer.blue_from_green) > 1e-6 ||
         std::abs(mixer.blue_from_blue - 100.0) > 1e-6 ||
         state.black_and_white_enabled || uses_grading || uses_channel_mixer ||
         uses_harmony || uses_replacement;
}

[[nodiscard]] inline bool preview_uses_compiled_working_curves(
    const EditRecipeState& state) {
  return std::abs(state.curve_shadows) > 1e-6 ||
         std::abs(state.curve_darks) > 1e-6 ||
         std::abs(state.curve_lights) > 1e-6 ||
         std::abs(state.curve_highlights) > 1e-6 ||
         !state.master_curve_points.isEmpty() ||
         !state.luma_curve_points.isEmpty() ||
         !state.red_curve_points.isEmpty() ||
         !state.green_curve_points.isEmpty() ||
         !state.blue_curve_points.isEmpty();
}

[[nodiscard]] inline bool preview_uses_compiled_working_tone(
    const EditRecipeState& state) {
  return std::abs(state.contrast) > 1e-6 ||
         std::abs(state.highlights) > 1e-6 ||
         std::abs(state.shadows) > 1e-6 ||
         std::abs(state.whites) > 1e-6 ||
         std::abs(state.blacks) > 1e-6 ||
         std::abs(state.brightness) > 1e-6 ||
         std::abs(state.filmic_toe) > 1e-6 ||
         std::abs(state.adaptive_contrast) > 1e-6 ||
         std::abs(state.levels_black_point) > 1e-6 ||
         std::abs(state.levels_gamma) > 1e-6 ||
         std::abs(state.levels_white_point) > 1e-6 ||
         std::abs(state.zone_deep_shadows) > 1e-6 ||
         std::abs(state.zone_shadows) > 1e-6 ||
         std::abs(state.zone_midtones) > 1e-6 ||
         std::abs(state.zone_lights) > 1e-6 ||
         std::abs(state.zone_highlights) > 1e-6 ||
         std::abs(state.hdr_local_tone_mapping) > 1e-6;
}

}  // namespace photo_editor::desktop
