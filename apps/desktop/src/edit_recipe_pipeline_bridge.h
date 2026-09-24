#pragma once

#include "color_profile_asset_bridge.h"
#include "edit_recipe_state.h"

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QVariantList>

#include <array>
#include <cstddef>
#include <vector>

namespace photo_editor::desktop {

inline std::vector<photo_editor::pipeline::CurvePoint>
compile_pipeline_curve_points(const QVariantList& points) {
  std::vector<photo_editor::pipeline::CurvePoint> curve_points;
  curve_points.reserve(static_cast<std::size_t>(points.size()));
  for (const auto& value : points) {
    const auto point = value.toMap();
    curve_points.push_back(photo_editor::pipeline::CurvePoint{
        .x = point.value(QStringLiteral("x"), 0.0).toDouble(),
        .y = point.value(QStringLiteral("y"), 0.0).toDouble(),
    });
  }
  return curve_points;
}

inline QString preview_output_color_space_name_for_recipe(
    const EditRecipeState& state) {
  if (!state.soft_proof_enabled &&
      !state.preview_icc_profile_asset.configured() &&
      !state.monitor_color_profile.trimmed().isEmpty()) {
    return normalized_monitor_color_profile_name(
        state.monitor_color_profile);
  }
  return state.preview_color_space.trimmed().isEmpty()
             ? state.output_color_space
             : state.preview_color_space;
}

inline photo_editor::pipeline::ColorPipelineConfig
compile_color_pipeline_config(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr) {
  auto config = photo_editor::pipeline::ColorPipelineConfig{
      .input_profile =
          photo_editor::pipeline::input_color_profile_from_name(
              state.input_color_profile.toStdString()),
      .working_profile =
          photo_editor::pipeline::working_color_profile_from_name(
              state.working_color_profile.toStdString()),
      .preview_output_profile =
          photo_editor::pipeline::output_color_profile_from_name(
              preview_output_color_space_name_for_recipe(state)
                  .toStdString()),
      .export_output_profile =
          photo_editor::pipeline::output_color_profile_from_name(
              state.output_color_space.toStdString()),
      .proof_output_profile =
          photo_editor::pipeline::output_color_profile_from_name(
              state.soft_proof_profile.toStdString()),
      .photographic_rendering_model =
          photo_editor::pipeline::photographic_rendering_model_from_name(
              state.photographic_rendering_model.toStdString()),
      .renderer_transition_look =
          photo_editor::pipeline::renderer_transition_look_from_name(
              state.renderer_transition_look.toStdString()),
      .camera_profile_rendering_model =
          photo_editor::pipeline::camera_profile_rendering_model_from_name(
              state.camera_profile_rendering_model.toStdString()),
      .camera_profile = {},
      .input_transform_recipe = {},
      .render_intent =
          photo_editor::pipeline::render_intent_from_name(
              state.render_intent.toStdString()),
      .proof_render_intent =
          photo_editor::pipeline::render_intent_from_name(
              state.proof_render_intent.toStdString()),
      .soft_proof_enabled = state.soft_proof_enabled,
      .proof_gamut_check_enabled = false,
      .proof_simulate_paper_white = state.proof_simulate_paper_white,
      .proof_simulate_ink_black = state.proof_simulate_ink_black,
      .output_gamut_mapping_enabled =
          state.output_gamut_mapping_enabled,
      .output_gamut_mapping_policy =
          photo_editor::pipeline::gamut_mapping_policy_from_name(
              state.output_gamut_mapping_policy.toStdString()),
      .output_headroom = state.output_headroom,
      .black_point_compensation_enabled =
          state.black_point_compensation_enabled,
      .source_black_point = state.source_black_point,
      .output_black_point = state.output_black_point,
  };
  apply_icc_profile_assets(&config, state);
  config.preview_display_target =
      photo_editor::pipeline::make_sdr_display_target(
          config.preview_output_profile);
  config.export_display_target =
      photo_editor::pipeline::make_sdr_display_target(
          config.export_output_profile);
  if (metadata != nullptr && metadata->valid) {
    config.camera_profile =
        photo_editor::pipeline::resolve_camera_profile(
            *metadata,
            config.input_profile,
            config.camera_dcp_profile_asset.ready()
                ? config.camera_dcp_profile_asset.profile.get()
                : nullptr,
            config.input_icc_profile_asset.ready()
                ? &config.input_icc_profile_asset
                : nullptr);
    config.input_transform_recipe =
        photo_editor::pipeline::resolve_input_transform_recipe(
            config.input_profile,
            config.camera_profile,
            *metadata);
  }
  return config;
}

inline photo_editor::pipeline::ColorAdjustmentState
compile_base_color_adjustment_state(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    const photo_editor::pipeline::ColorPipelineConfig* config_override =
        nullptr) {
  const auto raw_input_transform_enabled =
      metadata != nullptr && state.raw_input_transform_enabled &&
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(
          *metadata);
  const auto color_pipeline_config =
      config_override == nullptr
          ? compile_color_pipeline_config(state, metadata)
          : *config_override;
  return photo_editor::pipeline::ColorAdjustmentState{
      .input_transform =
          photo_editor::pipeline::InputTransformSettings{
              .input_profile_name =
                  normalized_input_color_profile_name(
                      state.input_color_profile)
                      .toStdString(),
              .exposure = state.exposure,
              .temperature = state.temperature,
              .tint = state.tint,
              .raw_input_transform_enabled = raw_input_transform_enabled,
              .highlight_recovery_enabled =
                  raw_input_transform_enabled &&
                  state.raw_highlight_recovery,
              .highlight_reconstruction_mode =
                  normalized_highlight_reconstruction_mode_name(
                      state.highlight_reconstruction_mode)
                      .toStdString(),
              .highlight_reconstruction_model =
                  normalized_highlight_reconstruction_model_name(
                      state.highlight_reconstruction_model)
                      .toStdString(),
              .chromatic_adaptation_method =
                  normalized_chromatic_adaptation_method_name(
                      state.chromatic_adaptation_method)
                      .toStdString(),
              .raw_white_balance_override_enabled =
                  raw_input_transform_enabled &&
                  state.raw_white_balance_override,
              .camera_matching_look =
                  raw_input_transform_enabled
                      ? normalized_camera_matching_look_name(
                            state.raw_camera_matching_look)
                            .toStdString()
                      : std::string("Standard"),
              .raw_white_balance_temperature =
                  state.raw_white_balance_temperature,
              .raw_white_balance_tint = state.raw_white_balance_tint,
              .raw_black_level =
                  raw_input_transform_enabled ? state.raw_black_level : 0.0,
              .raw_white_level =
                  raw_input_transform_enabled ? state.raw_white_level : 1.0,
              .calibration_hue =
                  raw_input_transform_enabled ? state.raw_calibration_hue
                                              : 0.0,
              .calibration_saturation =
                  raw_input_transform_enabled
                      ? state.raw_calibration_saturation
                      : 0.0,
              .red_primary_hue =
                  raw_input_transform_enabled ? state.raw_red_primary_hue
                                              : 0.0,
              .red_primary_saturation =
                  raw_input_transform_enabled
                      ? state.raw_red_primary_saturation
                      : 0.0,
              .green_primary_hue =
                  raw_input_transform_enabled ? state.raw_green_primary_hue
                                              : 0.0,
              .green_primary_saturation =
                  raw_input_transform_enabled
                      ? state.raw_green_primary_saturation
                      : 0.0,
              .blue_primary_hue =
                  raw_input_transform_enabled ? state.raw_blue_primary_hue
                                              : 0.0,
              .blue_primary_saturation =
                  raw_input_transform_enabled
                      ? state.raw_blue_primary_saturation
                      : 0.0,
      },
      .color_pipeline = color_pipeline_config,
      .contrast = state.contrast,
      .highlights = state.highlights,
      .shadows = state.shadows,
      .whites = state.whites,
      .blacks = state.blacks,
      .brightness = state.brightness,
      .hdr_local_tone_mapping = state.hdr_local_tone_mapping,
      .filmic_shoulder = state.filmic_shoulder,
      .filmic_toe = state.filmic_toe,
      .adaptive_contrast = state.adaptive_contrast,
      .levels_black_point = state.levels_black_point,
      .levels_gamma = state.levels_gamma,
      .levels_white_point = state.levels_white_point,
      .zone_deep_shadows = state.zone_deep_shadows,
      .zone_shadows = state.zone_shadows,
      .zone_midtones = state.zone_midtones,
      .zone_lights = state.zone_lights,
      .zone_highlights = state.zone_highlights,
      .texture = state.texture,
      .clarity = state.clarity,
      .dehaze = state.dehaze,
      .sharpening = state.sharpening,
      .sharpen_radius = state.sharpen_radius,
      .sharpen_detail = state.sharpen_detail,
      .sharpen_masking = state.sharpen_masking,
      .luminance_nr = state.luminance_nr,
      .color_nr = state.color_nr,
      .lens_distortion = state.lens_distortion,
      .lens_vignette = state.lens_vignette,
      .chromatic_aberration = state.chromatic_aberration,
      .perspective_vertical = state.perspective_vertical,
      .perspective_horizontal = state.perspective_horizontal,
      .transform_rotation = state.transform_rotation,
      .transform_scale = state.transform_scale,
      .transform_skew_x = state.transform_skew_x,
      .transform_skew_y = state.transform_skew_y,
      .liquify_center_x = state.liquify_center_x,
      .liquify_center_y = state.liquify_center_y,
      .liquify_radius = state.liquify_radius,
      .liquify_strength = state.liquify_strength,
      .content_aware_crop_fill = state.content_aware_crop_fill,
      .vibrance = state.vibrance,
      .saturation = state.saturation,
      .curve_shadows = state.curve_shadows,
      .curve_darks = state.curve_darks,
      .curve_lights = state.curve_lights,
      .curve_highlights = state.curve_highlights,
      .master_curve_points =
          compile_pipeline_curve_points(state.master_curve_points),
      .luma_curve_points =
          compile_pipeline_curve_points(state.luma_curve_points),
      .red_curve_points =
          compile_pipeline_curve_points(state.red_curve_points),
      .green_curve_points =
          compile_pipeline_curve_points(state.green_curve_points),
      .blue_curve_points =
          compile_pipeline_curve_points(state.blue_curve_points),
      .color_mixer_model = state.color_mixer_model.toStdString(),
      .color_mixer_channel = state.hsl_channel.toStdString(),
      .hue_adjustment = state.hue_adjustment,
      .saturation_adjustment = state.saturation_adjustment,
      .luminance_adjustment = state.luminance_adjustment,
      .color_mixer_channels = [&]() {
        std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
                   photo_editor::pipeline::kColorMixerChannelCount>
            channels{};
        for (std::size_t i = 0; i < channels.size(); ++i) {
          channels[i].hue = state.mixer_channels[i].hue;
          channels[i].saturation = state.mixer_channels[i].saturation;
          channels[i].luminance = state.mixer_channels[i].luminance;
          channels[i].selective_cyan =
              state.mixer_channels[i].selective_cyan;
          channels[i].selective_magenta =
              state.mixer_channels[i].selective_magenta;
          channels[i].selective_yellow =
              state.mixer_channels[i].selective_yellow;
          channels[i].selective_black =
              state.mixer_channels[i].selective_black;
        }
        return channels;
      }(),
      .rgb_channel_mixer = {
          .red_from_red = state.rgb_channel_mixer.red_from_red,
          .red_from_green = state.rgb_channel_mixer.red_from_green,
          .red_from_blue = state.rgb_channel_mixer.red_from_blue,
          .green_from_red = state.rgb_channel_mixer.green_from_red,
          .green_from_green = state.rgb_channel_mixer.green_from_green,
          .green_from_blue = state.rgb_channel_mixer.green_from_blue,
          .blue_from_red = state.rgb_channel_mixer.blue_from_red,
          .blue_from_green = state.rgb_channel_mixer.blue_from_green,
          .blue_from_blue = state.rgb_channel_mixer.blue_from_blue,
      },
      .black_and_white_enabled = state.black_and_white_enabled,
      .black_and_white_mixer = state.black_and_white_mixer,
      .color_replacement_enabled = state.color_replacement_enabled,
      .color_replacement_source_hue =
          state.color_replacement_source_hue,
      .color_replacement_target_hue =
          state.color_replacement_target_hue,
      .color_replacement_range = state.color_replacement_range,
      .color_replacement_softness = state.color_replacement_softness,
      .color_replacement_strength = state.color_replacement_strength,
      .color_replacement_preserve_luminance =
          state.color_replacement_preserve_luminance,
      .color_replacement_preserve_saturation =
          state.color_replacement_preserve_saturation,
      .skin_tone_protection_enabled =
          state.skin_tone_protection_enabled,
      .skin_tone_line_hue = state.skin_tone_line_hue,
      .skin_tone_protection_range =
          state.skin_tone_protection_range,
      .skin_tone_protection_strength =
          state.skin_tone_protection_strength,
      .color_harmony_enabled = state.color_harmony_enabled,
      .color_harmony_mode = state.color_harmony_mode.toStdString(),
      .color_harmony_base_hue = state.color_harmony_base_hue,
      .color_harmony_strength = state.color_harmony_strength,
      .lut_enabled = false,
      .lut_strength = 100.0,
      .lut_name = {},
      .lut_table = {},
      .lut_size = 0,
      .lut_domain_min = {0.0, 0.0, 0.0},
      .lut_domain_max = {1.0, 1.0, 1.0},
      .lut_input_color_space = "sRGB",
      .lut_output_color_space = "sRGB",
      .lut_stack = {},
      .grading_shadows_hue = state.grading_shadows_hue,
      .grading_shadows_saturation =
          state.grading_shadows_saturation,
      .grading_shadows_luminance =
          state.grading_shadows_luminance,
      .grading_midtones_hue = state.grading_midtones_hue,
      .grading_midtones_saturation =
          state.grading_midtones_saturation,
      .grading_midtones_luminance =
          state.grading_midtones_luminance,
      .grading_highlights_hue = state.grading_highlights_hue,
      .grading_highlights_saturation =
          state.grading_highlights_saturation,
      .grading_highlights_luminance =
          state.grading_highlights_luminance,
      .grading_amount = state.grading_amount,
      .grading_balance = state.grading_balance,
      .linear_mask_enabled = state.linear_mask_enabled,
      .linear_mask_exposure = state.linear_mask_exposure,
      .linear_mask_temperature = state.linear_mask_temperature,
      .linear_mask_feather = state.linear_mask_feather,
      .linear_mask_position = state.linear_mask_position,
      .linear_mask_rotation = state.linear_mask_rotation,
      .radial_mask_enabled = state.radial_mask_enabled,
      .radial_mask_exposure = state.radial_mask_exposure,
      .radial_mask_temperature = state.radial_mask_temperature,
      .radial_mask_feather = state.radial_mask_feather,
      .radial_mask_center_x = state.radial_mask_center_x,
      .radial_mask_center_y = state.radial_mask_center_y,
      .radial_mask_radius_x = state.radial_mask_radius_x,
      .radial_mask_radius_y = state.radial_mask_radius_y,
      .radial_mask_rotation = state.radial_mask_rotation,
  };
}

}  // namespace photo_editor::desktop
