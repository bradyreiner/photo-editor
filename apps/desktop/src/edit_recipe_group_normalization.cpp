#include "edit_recipe_state.h"

namespace photo_editor::desktop {

QStringList all_adjustment_groups() {
  return {
      QStringLiteral("raw"),
      QStringLiteral("tone"),
      QStringLiteral("color"),
      QStringLiteral("crop"),
      QStringLiteral("masks"),
      QStringLiteral("presets"),
      QStringLiteral("layers"),
      QStringLiteral("history"),
      QStringLiteral("detail"),
  };
}

QString normalize_adjustment_group(const QString& group) {
  const auto normalized = group.trimmed().toLower();
  if (normalized == QStringLiteral("all")) {
    return normalized;
  }

  for (const auto& candidate : all_adjustment_groups()) {
    if (candidate == normalized) {
      return candidate;
    }
  }
  return {};
}

QStringList normalize_adjustment_groups(const QStringList& groups) {
  QStringList normalized_groups;
  for (const auto& group : groups) {
    const auto normalized = normalize_adjustment_group(group);
    if (normalized.isEmpty()) {
      continue;
    }
    if (normalized == QStringLiteral("all")) {
      return all_adjustment_groups();
    }
    if (!normalized_groups.contains(normalized)) {
      normalized_groups.push_back(normalized);
    }
  }

  return normalized_groups.isEmpty()
             ? all_adjustment_groups()
             : normalized_groups;
}


EditRecipeState merge_edit_recipe_state_by_groups(
    const EditRecipeState& base,
    const EditRecipeState& source,
    const QStringList& groups) {
  const auto normalized_groups = normalize_adjustment_groups(groups);
  EditRecipeState merged = base;

  if (normalized_groups.contains(QStringLiteral("raw"))) {
    merged.input_color_profile = source.input_color_profile;
    merged.working_color_profile = source.working_color_profile;
    merged.camera_dcp_profile_asset = source.camera_dcp_profile_asset;
    merged.camera_profile_rendering_model = source.camera_profile_rendering_model;
    merged.photographic_rendering_model = source.photographic_rendering_model;
    merged.renderer_transition_look = source.renderer_transition_look;
    merged.input_icc_profile_asset = source.input_icc_profile_asset;
    merged.preview_icc_profile_asset = source.preview_icc_profile_asset;
    merged.proof_icc_profile_asset = source.proof_icc_profile_asset;
    merged.export_icc_profile_asset = source.export_icc_profile_asset;
    merged.render_intent = source.render_intent;
    merged.proof_render_intent = source.proof_render_intent;
    merged.proof_simulate_paper_white = source.proof_simulate_paper_white;
    merged.proof_simulate_ink_black = source.proof_simulate_ink_black;
    merged.output_gamut_mapping_enabled = source.output_gamut_mapping_enabled;
    merged.output_gamut_mapping_policy = source.output_gamut_mapping_policy;
    merged.black_point_compensation_enabled = source.black_point_compensation_enabled;
    merged.source_black_point = source.source_black_point;
    merged.output_black_point = source.output_black_point;
    merged.raw_input_transform_enabled = source.raw_input_transform_enabled;
    merged.highlight_reconstruction_mode = source.highlight_reconstruction_mode;
    merged.highlight_reconstruction_model = source.highlight_reconstruction_model;
    merged.chromatic_adaptation_method = source.chromatic_adaptation_method;
    merged.raw_demosaic_quality = source.raw_demosaic_quality;
    merged.raw_exposure = source.raw_exposure;
    merged.raw_boost = source.raw_boost;
    merged.raw_shadow_boost = source.raw_shadow_boost;
    merged.raw_detail = source.raw_detail;
    merged.raw_chroma_noise_reduction = source.raw_chroma_noise_reduction;
    merged.raw_moire_reduction = source.raw_moire_reduction;
    merged.raw_hot_dead_pixel_cleanup = source.raw_hot_dead_pixel_cleanup;
    merged.raw_false_color_suppression = source.raw_false_color_suppression;
    merged.raw_highlight_recovery = source.raw_highlight_recovery;
    merged.raw_gamut_mapping = source.raw_gamut_mapping;
    merged.raw_lens_correction = source.raw_lens_correction;
    merged.raw_white_balance_override = source.raw_white_balance_override;
    merged.raw_camera_matching_look = source.raw_camera_matching_look;
    merged.raw_white_balance_temperature = source.raw_white_balance_temperature;
    merged.raw_white_balance_tint = source.raw_white_balance_tint;
    merged.raw_black_level = source.raw_black_level;
    merged.raw_white_level = source.raw_white_level;
    merged.raw_calibration_hue = source.raw_calibration_hue;
    merged.raw_calibration_saturation = source.raw_calibration_saturation;
    merged.raw_red_primary_hue = source.raw_red_primary_hue;
    merged.raw_red_primary_saturation = source.raw_red_primary_saturation;
    merged.raw_green_primary_hue = source.raw_green_primary_hue;
    merged.raw_green_primary_saturation = source.raw_green_primary_saturation;
    merged.raw_blue_primary_hue = source.raw_blue_primary_hue;
    merged.raw_blue_primary_saturation = source.raw_blue_primary_saturation;
  }

  if (normalized_groups.contains(QStringLiteral("tone"))) {
    merged.exposure = source.exposure;
    merged.contrast = source.contrast;
    merged.highlights = source.highlights;
    merged.shadows = source.shadows;
    merged.whites = source.whites;
    merged.blacks = source.blacks;
    merged.brightness = source.brightness;
    merged.hdr_local_tone_mapping = source.hdr_local_tone_mapping;
    merged.filmic_shoulder = source.filmic_shoulder;
    merged.filmic_toe = source.filmic_toe;
    merged.adaptive_contrast = source.adaptive_contrast;
    merged.levels_black_point = source.levels_black_point;
    merged.levels_gamma = source.levels_gamma;
    merged.levels_white_point = source.levels_white_point;
    merged.zone_deep_shadows = source.zone_deep_shadows;
    merged.zone_shadows = source.zone_shadows;
    merged.zone_midtones = source.zone_midtones;
    merged.zone_lights = source.zone_lights;
    merged.zone_highlights = source.zone_highlights;
    merged.curve_shadows = source.curve_shadows;
    merged.curve_darks = source.curve_darks;
    merged.curve_lights = source.curve_lights;
    merged.curve_highlights = source.curve_highlights;
    merged.master_curve_points = source.master_curve_points;
    merged.luma_curve_points = source.luma_curve_points;
    merged.red_curve_points = source.red_curve_points;
    merged.green_curve_points = source.green_curve_points;
    merged.blue_curve_points = source.blue_curve_points;
  }

  if (normalized_groups.contains(QStringLiteral("color"))) {
    merged.preview_color_space = source.preview_color_space;
    merged.preview_output_mode = source.preview_output_mode;
    merged.monitor_color_profile = source.monitor_color_profile;
    merged.soft_proof_enabled = source.soft_proof_enabled;
    merged.soft_proof_profile = source.soft_proof_profile;
    merged.proof_render_intent = source.proof_render_intent;
    merged.proof_simulate_paper_white = source.proof_simulate_paper_white;
    merged.proof_simulate_ink_black = source.proof_simulate_ink_black;
    merged.temperature = source.temperature;
    merged.tint = source.tint;
    merged.output_color_space = source.output_color_space;
    merged.output_headroom = source.output_headroom;
    merged.vibrance = source.vibrance;
    merged.saturation = source.saturation;
    merged.color_mixer_model = source.color_mixer_model;
    merged.hsl_channel = source.hsl_channel;
    merged.hue_adjustment = source.hue_adjustment;
    merged.saturation_adjustment = source.saturation_adjustment;
    merged.luminance_adjustment = source.luminance_adjustment;
    merged.mixer_channels = source.mixer_channels;
    merged.rgb_channel_mixer = source.rgb_channel_mixer;
    merged.black_and_white_enabled = source.black_and_white_enabled;
    merged.black_and_white_mixer = source.black_and_white_mixer;
    merged.color_replacement_enabled = source.color_replacement_enabled;
    merged.color_replacement_source_hue = source.color_replacement_source_hue;
    merged.color_replacement_target_hue = source.color_replacement_target_hue;
    merged.color_replacement_range = source.color_replacement_range;
    merged.color_replacement_softness = source.color_replacement_softness;
    merged.color_replacement_strength = source.color_replacement_strength;
    merged.color_replacement_preserve_luminance = source.color_replacement_preserve_luminance;
    merged.color_replacement_preserve_saturation = source.color_replacement_preserve_saturation;
    merged.skin_tone_protection_enabled = source.skin_tone_protection_enabled;
    merged.skin_tone_line_hue = source.skin_tone_line_hue;
    merged.skin_tone_protection_range = source.skin_tone_protection_range;
    merged.skin_tone_protection_strength = source.skin_tone_protection_strength;
    merged.color_harmony_enabled = source.color_harmony_enabled;
    merged.color_harmony_mode = source.color_harmony_mode;
    merged.color_harmony_base_hue = source.color_harmony_base_hue;
    merged.color_harmony_strength = source.color_harmony_strength;
    merged.grading_shadows_hue = source.grading_shadows_hue;
    merged.grading_shadows_saturation = source.grading_shadows_saturation;
    merged.grading_shadows_luminance = source.grading_shadows_luminance;
    merged.grading_midtones_hue = source.grading_midtones_hue;
    merged.grading_midtones_saturation = source.grading_midtones_saturation;
    merged.grading_midtones_luminance = source.grading_midtones_luminance;
    merged.grading_highlights_hue = source.grading_highlights_hue;
    merged.grading_highlights_saturation = source.grading_highlights_saturation;
    merged.grading_highlights_luminance = source.grading_highlights_luminance;
    merged.grading_amount = source.grading_amount;
    merged.grading_balance = source.grading_balance;
  }

  if (normalized_groups.contains(QStringLiteral("crop"))) {
    merged.crop_rect_x = source.crop_rect_x;
    merged.crop_rect_y = source.crop_rect_y;
    merged.crop_rect_width = source.crop_rect_width;
    merged.crop_rect_height = source.crop_rect_height;
    merged.crop_zoom = source.crop_zoom;
    merged.crop_preset = source.crop_preset;
    merged.upright_mode = source.upright_mode;
    merged.upright_vertical_guide_angle = source.upright_vertical_guide_angle;
    merged.upright_horizontal_guide_angle = source.upright_horizontal_guide_angle;
    merged.perspective_vertical = source.perspective_vertical;
    merged.perspective_horizontal = source.perspective_horizontal;
    merged.transform_rotation = source.transform_rotation;
    merged.transform_scale = source.transform_scale;
    merged.transform_skew_x = source.transform_skew_x;
    merged.transform_skew_y = source.transform_skew_y;
    merged.liquify_center_x = source.liquify_center_x;
    merged.liquify_center_y = source.liquify_center_y;
    merged.liquify_radius = source.liquify_radius;
    merged.liquify_strength = source.liquify_strength;
    merged.content_aware_crop_fill = source.content_aware_crop_fill;
  }

  if (normalized_groups.contains(QStringLiteral("masks"))) {
    merged.linear_mask_enabled = source.linear_mask_enabled;
    merged.linear_mask_exposure = source.linear_mask_exposure;
    merged.linear_mask_temperature = source.linear_mask_temperature;
    merged.linear_mask_feather = source.linear_mask_feather;
    merged.linear_mask_position = source.linear_mask_position;
    merged.linear_mask_rotation = source.linear_mask_rotation;
    merged.radial_mask_enabled = source.radial_mask_enabled;
    merged.radial_mask_exposure = source.radial_mask_exposure;
    merged.radial_mask_temperature = source.radial_mask_temperature;
    merged.radial_mask_feather = source.radial_mask_feather;
    merged.radial_mask_center_x = source.radial_mask_center_x;
    merged.radial_mask_center_y = source.radial_mask_center_y;
    merged.radial_mask_radius_x = source.radial_mask_radius_x;
    merged.radial_mask_radius_y = source.radial_mask_radius_y;
    merged.radial_mask_rotation = source.radial_mask_rotation;
    merged.mask_stack = source.mask_stack;
  }

  if (normalized_groups.contains(QStringLiteral("presets"))) {
    merged.preset_applications = source.preset_applications;
  }

  if (normalized_groups.contains(QStringLiteral("layers"))) {
    merged.layers = source.layers;
  }

  if (normalized_groups.contains(QStringLiteral("history"))) {
    merged.active_history_branch = source.active_history_branch;
    merged.recipe_snapshots = source.recipe_snapshots;
  }

  if (normalized_groups.contains(QStringLiteral("detail"))) {
    merged.texture = source.texture;
    merged.clarity = source.clarity;
    merged.dehaze = source.dehaze;
    merged.sharpening = source.sharpening;
    merged.sharpen_radius = source.sharpen_radius;
    merged.sharpen_detail = source.sharpen_detail;
    merged.sharpen_masking = source.sharpen_masking;
    merged.luminance_nr = source.luminance_nr;
    merged.color_nr = source.color_nr;
    merged.lens_distortion = source.lens_distortion;
    merged.lens_vignette = source.lens_vignette;
    merged.chromatic_aberration = source.chromatic_aberration;
  }

  return merged;
}

// Preview sections follow visible inspector boundaries, independently of the
// broader copy/paste groups. Only a render-request copy is passed here.
QStringList adjustment_preview_sections() {
  return {QStringLiteral("white-balance"), QStringLiteral("light"), QStringLiteral("curves"), QStringLiteral("color"), QStringLiteral("grading"), QStringLiteral("presence"), QStringLiteral("detail"), QStringLiteral("lens"), QStringLiteral("raw"), QStringLiteral("color-mix"), QStringLiteral("channel-mixer"), QStringLiteral("black-and-white"), QStringLiteral("color-replace"), QStringLiteral("skin"), QStringLiteral("harmony"), QStringLiteral("advanced-color"), QStringLiteral("all-adjustments")};
}

EditRecipeState edit_recipe_without_preview_section(const EditRecipeState& state, const QString& section) {
  EditRecipeState result = state;
  const EditRecipeState neutral;
  const bool all = section == QStringLiteral("all-adjustments");
  if (all) {
    result.preset_applications = neutral.preset_applications;
    result.zone_deep_shadows = neutral.zone_deep_shadows;
    result.zone_shadows = neutral.zone_shadows;
    result.zone_midtones = neutral.zone_midtones;
    result.zone_lights = neutral.zone_lights;
    result.zone_highlights = neutral.zone_highlights;
  }
  if (all || section == QStringLiteral("white-balance")) {
    result.temperature = neutral.temperature;
    result.tint = neutral.tint;
  }
  if (all || section == QStringLiteral("light")) {
    result.exposure = neutral.exposure;
    result.contrast = neutral.contrast;
    result.highlights = neutral.highlights;
    result.shadows = neutral.shadows;
    result.whites = neutral.whites;
    result.blacks = neutral.blacks;
    result.brightness = neutral.brightness;
    result.hdr_local_tone_mapping = neutral.hdr_local_tone_mapping;
    result.filmic_shoulder = neutral.filmic_shoulder;
    result.filmic_toe = neutral.filmic_toe;
    result.adaptive_contrast = neutral.adaptive_contrast;
  }
  if (all || section == QStringLiteral("curves")) {
    result.curve_shadows = neutral.curve_shadows;
    result.curve_darks = neutral.curve_darks;
    result.curve_lights = neutral.curve_lights;
    result.curve_highlights = neutral.curve_highlights;
    result.master_curve_points = neutral.master_curve_points;
    result.luma_curve_points = neutral.luma_curve_points;
    result.red_curve_points = neutral.red_curve_points;
    result.green_curve_points = neutral.green_curve_points;
    result.blue_curve_points = neutral.blue_curve_points;
    result.levels_black_point = neutral.levels_black_point;
    result.levels_gamma = neutral.levels_gamma;
    result.levels_white_point = neutral.levels_white_point;
  }
  if (all || section == QStringLiteral("color")) {
    result.vibrance = neutral.vibrance;
    result.saturation = neutral.saturation;
  }
  if (all || section == QStringLiteral("grading")) {
    result.grading_shadows_hue = neutral.grading_shadows_hue;
    result.grading_shadows_saturation = neutral.grading_shadows_saturation;
    result.grading_shadows_luminance = neutral.grading_shadows_luminance;
    result.grading_midtones_hue = neutral.grading_midtones_hue;
    result.grading_midtones_saturation = neutral.grading_midtones_saturation;
    result.grading_midtones_luminance = neutral.grading_midtones_luminance;
    result.grading_highlights_hue = neutral.grading_highlights_hue;
    result.grading_highlights_saturation = neutral.grading_highlights_saturation;
    result.grading_highlights_luminance = neutral.grading_highlights_luminance;
    result.grading_amount = neutral.grading_amount;
    result.grading_balance = neutral.grading_balance;
  }
  if (all || section == QStringLiteral("presence")) {
    result.texture = neutral.texture;
    result.clarity = neutral.clarity;
    result.dehaze = neutral.dehaze;
  }
  if (all || section == QStringLiteral("detail")) {
    result.sharpening = neutral.sharpening;
    result.sharpen_radius = neutral.sharpen_radius;
    result.sharpen_detail = neutral.sharpen_detail;
    result.sharpen_masking = neutral.sharpen_masking;
    result.luminance_nr = neutral.luminance_nr;
    result.color_nr = neutral.color_nr;
  }
  if (all || section == QStringLiteral("lens")) {
    result.lens_distortion = neutral.lens_distortion;
    result.lens_vignette = neutral.lens_vignette;
    result.chromatic_aberration = neutral.chromatic_aberration;
  }
  if (all || section == QStringLiteral("raw")) {
    result.raw_exposure = neutral.raw_exposure;
    result.raw_highlight_recovery = neutral.raw_highlight_recovery;
    result.raw_lens_correction = neutral.raw_lens_correction;
  }
  if (all || section == QStringLiteral("color-mix") || section == QStringLiteral("advanced-color")) {
    result.hue_adjustment = neutral.hue_adjustment;
    result.saturation_adjustment = neutral.saturation_adjustment;
    result.luminance_adjustment = neutral.luminance_adjustment;
    result.mixer_channels = neutral.mixer_channels;
  }
  if (all || section == QStringLiteral("channel-mixer") || section == QStringLiteral("advanced-color")) {
    result.rgb_channel_mixer = neutral.rgb_channel_mixer;
  }
  if (all || section == QStringLiteral("black-and-white") || section == QStringLiteral("advanced-color")) {
    result.black_and_white_enabled = neutral.black_and_white_enabled;
    result.black_and_white_mixer = neutral.black_and_white_mixer;
  }
  if (all || section == QStringLiteral("color-replace") || section == QStringLiteral("advanced-color")) {
    result.color_replacement_enabled = neutral.color_replacement_enabled;
    result.color_replacement_source_hue = neutral.color_replacement_source_hue;
    result.color_replacement_target_hue = neutral.color_replacement_target_hue;
    result.color_replacement_range = neutral.color_replacement_range;
    result.color_replacement_softness = neutral.color_replacement_softness;
    result.color_replacement_strength = neutral.color_replacement_strength;
    result.color_replacement_preserve_luminance = neutral.color_replacement_preserve_luminance;
    result.color_replacement_preserve_saturation = neutral.color_replacement_preserve_saturation;
  }
  if (all || section == QStringLiteral("skin") || section == QStringLiteral("advanced-color")) {
    result.skin_tone_protection_enabled = neutral.skin_tone_protection_enabled;
    result.skin_tone_line_hue = neutral.skin_tone_line_hue;
    result.skin_tone_protection_range = neutral.skin_tone_protection_range;
    result.skin_tone_protection_strength = neutral.skin_tone_protection_strength;
  }
  if (all || section == QStringLiteral("harmony") || section == QStringLiteral("advanced-color")) {
    result.color_harmony_enabled = neutral.color_harmony_enabled;
    result.color_harmony_mode = neutral.color_harmony_mode;
    result.color_harmony_base_hue = neutral.color_harmony_base_hue;
    result.color_harmony_strength = neutral.color_harmony_strength;
  }
  return result;
}

}  // namespace photo_editor::desktop
