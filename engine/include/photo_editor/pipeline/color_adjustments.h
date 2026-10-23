#pragma once

#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/output_rendering.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/photo_effect_types.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <span>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct ColorPixel final {
  bool operator==(const ColorPixel&) const = default;
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;
};

struct CurvePoint final {
  bool operator==(const CurvePoint&) const = default;
  double x = 0.0;
  double y = 0.0;
};

struct BrushPoint final {
  bool operator==(const BrushPoint&) const = default;
  double x = 0.5;
  double y = 0.5;
  double size = 0.08;
  double flow = 1.0;
  double opacity = 1.0;
  double auto_mask_anchor_x = 0.5;
  double auto_mask_anchor_y = 0.5;
  double pressure = 1.0;
  double tilt_x = 0.0;
  double tilt_y = 0.0;
  bool erase = false;
};

struct CloneStroke final {
  bool operator==(const CloneStroke&) const = default;
  std::vector<BrushPoint> points;
  double source_x = 0.5;
  double source_y = 0.5;
  double offset_x = 0.0;
  double offset_y = 0.0;
  double scale = 1.0;
  double rotation = 0.0;
};

struct LocalMask final {
  bool operator==(const LocalMask&) const = default;
  std::string id;
  std::string parent_id;
  std::string output_id;
  std::string name;
  std::string kind = "linear";
  std::string operation = "add";
  std::string blend_mode = "normal";
  std::string retouch_mode;
  int retouch_order = 0;
  bool enabled = true;
  bool reusable_output = false;
  bool invert = false;
  double opacity = 1.0;
  double density = 1.0;
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double sharpening = 0.0;
  double sharpen_radius = 1.0;
  double sharpen_detail = 0.0;
  double sharpen_masking = 0.0;
  double luminance_nr = 0.0;
  double color_nr = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double perspective_vertical = 0.0;
  double perspective_horizontal = 0.0;
  double global_transform_rotation = 0.0;
  double global_transform_scale = 1.0;
  double transform_skew_x = 0.0;
  double transform_skew_y = 0.0;
  double liquify_center_x = 0.5;
  double liquify_center_y = 0.5;
  double liquify_radius = 0.25;
  double liquify_strength = 0.0;
  bool content_aware_crop_fill = false;
  double vibrance = 0.0;
  double saturation = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  double remove_strength = 0.0;
  double heal_texture_blend = 0.68;
  double heal_color_blend = 0.46;
  bool selection_invert = false;
  double selection_expand = 0.0;
  double selection_smooth = 0.0;
  double selection_fine_edge = 0.0;
  double selection_edge_contrast = 0.0;
  double selection_decontaminate = 0.0;
  double selection_grow = 0.0;
  double selection_shrink = 0.0;
  double selection_feather = 0.0;
  double selection_blur = 0.0;
  double selection_harden = 0.0;
  double selection_despeckle = 0.0;
  double selection_fill_holes = 0.0;
  double selection_remove_islands = 0.0;
  double feather = 0.22;
  double position = 0.5;
  double rotation = 0.0;
  double center_x = 0.5;
  double center_y = 0.5;
  double radius_x = 0.25;
  double radius_y = 0.25;
  double brush_size = 0.08;
  double brush_flow = 1.0;
  double brush_hardness = 0.25;
  bool brush_buildup = true;
  bool clone_source_set = false;
  double clone_source_x = 0.5;
  double clone_source_y = 0.5;
  double clone_offset_x = 0.0;
  double clone_offset_y = 0.0;
  double clone_scale = 1.0;
  double clone_rotation = 0.0;
  bool clone_aligned = true;
  std::vector<BrushPoint> brush_points;
  std::vector<CloneStroke> clone_strokes;
  std::vector<CurvePoint> polygon_points;
  double luminance_min = 0.2;
  double luminance_max = 0.8;
  double range_feather = 0.1;
  double hue_center = 0.08;
  double hue_range = 0.12;
  double saturation_min = 0.1;
  double saturation_max = 1.0;
  double depth_min = 0.2;
  double depth_max = 0.8;
  std::vector<uint8_t> depth_map;
  int depth_width = 0;
  int depth_height = 0;
  double refine_luminance_min = 0.0;
  double refine_luminance_max = 1.0;
  double refine_hue_center = 0.0;
  double refine_hue_range = 0.5;
  double refine_saturation_min = 0.0;
  double refine_saturation_max = 1.0;
  double refine_depth_min = 0.0;
  double refine_depth_max = 1.0;
  double refine_edge_confidence_min = 0.0;
  double refine_range_feather = 0.05;
  std::vector<uint8_t> segmentation_mask;
  int segmentation_width = 0;
  int segmentation_height = 0;
  std::vector<ColorPixel> inpaint_patch_pixels;
  int inpaint_patch_x = 0;
  int inpaint_patch_y = 0;
  int inpaint_patch_width = 0;
  int inpaint_patch_height = 0;
  std::string inpaint_model_name;
  double inpaint_blend_amount = 1.0;
  double inpaint_seam_feather = 4.0;
  double inpaint_color_match = 0.75;
};

struct LocalMaskSet final {
  bool operator==(const LocalMaskSet&) const = default;
  std::string id;
  std::string name;
  bool enabled = true;
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double sharpening = 0.0;
  double sharpen_radius = 1.0;
  double sharpen_detail = 0.0;
  double sharpen_masking = 0.0;
  double luminance_nr = 0.0;
  double color_nr = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double perspective_vertical = 0.0;
  double perspective_horizontal = 0.0;
  double global_transform_rotation = 0.0;
  double global_transform_scale = 1.0;
  double transform_skew_x = 0.0;
  double transform_skew_y = 0.0;
  double liquify_center_x = 0.5;
  double liquify_center_y = 0.5;
  double liquify_radius = 0.25;
  double liquify_strength = 0.0;
  bool content_aware_crop_fill = false;
  double vibrance = 0.0;
  double saturation = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  bool selection_invert = false;
  double selection_expand = 0.0;
  double selection_smooth = 0.0;
  double selection_fine_edge = 0.0;
  double selection_edge_contrast = 0.0;
  double opacity = 1.0;
  double density = 1.0;
  std::vector<LocalMask> sources;
};

struct ColorMixerChannelAdjustment final {
  double hue = 0.0;
  double saturation = 0.0;
  double luminance = 0.0;
  double selective_cyan = 0.0;
  double selective_magenta = 0.0;
  double selective_yellow = 0.0;
  double selective_black = 0.0;
};

struct RgbChannelMixerAdjustment final {
  double red_from_red = 100.0;
  double red_from_green = 0.0;
  double red_from_blue = 0.0;
  double green_from_red = 0.0;
  double green_from_green = 100.0;
  double green_from_blue = 0.0;
  double blue_from_red = 0.0;
  double blue_from_green = 0.0;
  double blue_from_blue = 100.0;
};

struct LutAdjustment final {
  bool enabled = true;
  double strength = 100.0;
  std::string name;
  std::vector<ColorPixel> table;
  int size = 0;
  std::array<double, 3> domain_min = {0.0, 0.0, 0.0};
  std::array<double, 3> domain_max = {1.0, 1.0, 1.0};
  std::string input_color_space = "sRGB";
  std::string output_color_space = "sRGB";
  std::vector<ColorPixel> shaper_table;
  int shaper_size = 0;
  std::array<double, 2> shaper_input_range = {0.0, 1.0};
};

constexpr int kColorMixerChannelCount = 8;
constexpr std::array<const char*, kColorMixerChannelCount> kColorMixerChannelNames = {
    "Reds", "Oranges", "Yellows", "Greens", "Aquas", "Blues", "Purples", "Magentas",
};

struct PhotoLayer final {
  // Resolve against the base at the layer stage, before any sibling content.
  bool copy_base_image = false;
  bool erase_base_image = false;
  std::string id;
  std::string parent_id;
  std::string name;
  std::string kind = "adjustment";
  std::string blend_mode = "normal";
  bool enabled = true;
  double opacity = 1.0;
  bool clipping_mask = false;
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double sharpening = 0.0;
  double sharpen_radius = 1.0;
  double sharpen_detail = 0.0;
  double sharpen_masking = 0.0;
  double luminance_nr = 0.0;
  double color_nr = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double perspective_vertical = 0.0;
  double perspective_horizontal = 0.0;
  double global_transform_rotation = 0.0;
  double global_transform_scale = 1.0;
  double transform_skew_x = 0.0;
  double transform_skew_y = 0.0;
  double liquify_center_x = 0.5;
  double liquify_center_y = 0.5;
  double liquify_radius = 0.25;
  double liquify_strength = 0.0;
  bool content_aware_crop_fill = false;
  double vibrance = 0.0;
  double saturation = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  double brightness = 0.0;
  double filmic_shoulder = 0.0;
  double filmic_toe = 0.0;
  double adaptive_contrast = 0.0;
  double levels_black_point = 0.0;
  double levels_gamma = 0.0;
  double levels_white_point = 0.0;
  double zone_deep_shadows = 0.0;
  double zone_shadows = 0.0;
  double zone_midtones = 0.0;
  double zone_lights = 0.0;
  double zone_highlights = 0.0;
  double curve_shadows = 0.0;
  double curve_darks = 0.0;
  double curve_lights = 0.0;
  double curve_highlights = 0.0;
  std::vector<CurvePoint> master_curve_points;
  std::vector<CurvePoint> luma_curve_points;
  std::vector<CurvePoint> red_curve_points;
  std::vector<CurvePoint> green_curve_points;
  std::vector<CurvePoint> blue_curve_points;
  std::string color_mixer_model = "OKLab";
  std::string color_mixer_channel = "Reds";
  double hue_adjustment = 0.0;
  double saturation_adjustment = 0.0;
  double luminance_adjustment = 0.0;
  std::array<ColorMixerChannelAdjustment, kColorMixerChannelCount> color_mixer_channels{};
  RgbChannelMixerAdjustment rgb_channel_mixer;
  bool black_and_white_enabled = false;
  std::array<double, kColorMixerChannelCount> black_and_white_mixer{};
  bool color_replacement_enabled = false;
  double color_replacement_source_hue = 0.0;
  double color_replacement_target_hue = 0.33;
  double color_replacement_range = 35.0;
  double color_replacement_softness = 100.0;
  double color_replacement_strength = 0.0;
  bool color_replacement_preserve_luminance = true;
  bool color_replacement_preserve_saturation = true;
  bool skin_tone_protection_enabled = false;
  double skin_tone_line_hue = 0.08;
  double skin_tone_protection_range = 30.0;
  double skin_tone_protection_strength = 0.0;
  bool color_harmony_enabled = false;
  std::string color_harmony_mode = "Complementary";
  double color_harmony_base_hue = 0.0;
  double color_harmony_strength = 0.0;
  double grading_shadows_hue = 0.0;
  double grading_shadows_saturation = 0.0;
  double grading_shadows_luminance = 0.0;
  double grading_midtones_hue = 0.0;
  double grading_midtones_saturation = 0.0;
  double grading_midtones_luminance = 0.0;
  double grading_highlights_hue = 0.0;
  double grading_highlights_saturation = 0.0;
  double grading_highlights_luminance = 0.0;
  double grading_amount = 100.0;
  double grading_balance = 0.0;
  bool lut_enabled = false;
  double lut_strength = 100.0;
  std::string lut_name;
  std::vector<ColorPixel> lut_table;
  int lut_size = 0;
  std::array<double, 3> lut_domain_min = {0.0, 0.0, 0.0};
  std::array<double, 3> lut_domain_max = {1.0, 1.0, 1.0};
  std::string lut_input_color_space = "sRGB";
  std::string lut_output_color_space = "sRGB";
  std::vector<LutAdjustment> lut_stack;
  std::vector<ColorPixel> lut_shaper_table;
  int lut_shaper_size = 0;
  std::array<double, 2> lut_shaper_input_range = {0.0, 1.0};
  ColorPixel fill_color = {0.5F, 0.5F, 0.5F, 1.0F};
  std::vector<uint8_t> mask_data;
  int mask_width = 0;
  int mask_height = 0;
  bool mask_inverted = false;
  double mask_opacity = 1.0;
  double mask_density = 1.0;
  std::vector<ColorPixel> pixels;
  int pixel_width = 0;
  int pixel_height = 0;
  // Material typography is resolved during source-over against only the
  // backdrop accumulated below this text layer.
  std::string text_material;
  ColorPixel text_material_tint = {0.34F, 0.48F, 0.58F, 1.0F};
  double text_material_blur = 6.0;
  double text_material_transmission = 0.68;
  double text_material_edge_softness = 1.0;
  double text_material_depth = 4.0;
  double text_material_light_angle = -45.0;
  int x = 0;
  int y = 0;
  double transform_scale = 1.0;
  double transform_rotation = 0.0;
  std::vector<PhotoEffectInstance> effects;
  std::vector<PhotoEffectMaskRaster> effect_masks;
  double effect_scale_x = 1.0;
  double effect_scale_y = 1.0;
  std::array<double, 3> effect_luminance_weights{0.2126, 0.7152, 0.0722};
  std::string effect_error;
  int effect_padding_x = 0;
  int effect_padding_y = 0;
  std::string content_error;
};

inline bool photo_layer_has_active_effects(const PhotoLayer& layer) {
  if (!layer.effect_error.empty()) return true;
  for (const auto& effect : layer.effects) {
    if (!effect.enabled || effect.amount == 0.0) continue;
    if (effect.type_id == "gaussian_blur" && effect.algorithm_version == 1 &&
        effect.gaussian.sigma == 0.0) continue;
    if (effect.type_id == "box_blur" && effect.algorithm_version == 1 && effect.box.radius == 0) continue;
    return true;
  }
  return false;
}

struct InputTransformSettings final {
  std::string input_profile_name = "Embedded";
  double exposure = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  bool raw_input_transform_enabled = false;
  bool highlight_recovery_enabled = true;
  std::string highlight_reconstruction_mode = "Enabled";
  std::string highlight_reconstruction_model = "camera-linear-opposed-v2";
  std::string chromatic_adaptation_method = "Bradford";
  bool raw_white_balance_override_enabled = false;
  std::string camera_matching_look = "Standard";
  double raw_white_balance_temperature = 5200.0;
  double raw_white_balance_tint = 0.0;
  double raw_black_level = 0.0;
  double raw_white_level = 1.0;
  double calibration_hue = 0.0;
  double calibration_saturation = 0.0;
  double red_primary_hue = 0.0;
  double red_primary_saturation = 0.0;
  double green_primary_hue = 0.0;
  double green_primary_saturation = 0.0;
  double blue_primary_hue = 0.0;
  double blue_primary_saturation = 0.0;
  // Normalized area of the original sensor image represented by this raster.
  // Camera-profile spatial maps use this mapping; edit masks remain raster-local.
  double source_area_x = 0.0;
  double source_area_y = 0.0;
  double source_area_width = 1.0;
  double source_area_height = 1.0;
};

struct HighlightReconstructionAnalysis final {
  bool active = false;
  std::string model = "disabled";
  std::array<std::size_t, 3> clipped_channel_samples = {0U, 0U, 0U};
  std::size_t clipped_pixels = 0U;
  std::size_t multi_channel_clipped_pixels = 0U;
  std::size_t fully_clipped_pixels = 0U;
  std::array<double, 3> chrominance_correction = {0.0, 0.0, 0.0};
  std::array<std::size_t, 3> correction_reference_samples = {0U, 0U, 0U};
  std::size_t reconstructed_channel_samples = 0U;

  [[nodiscard]] bool has_clipping() const {
    return clipped_pixels > 0U;
  }
};

struct ColorAdjustmentState final {
  InputTransformSettings input_transform{};
  ColorPipelineConfig color_pipeline = default_color_pipeline_config();
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double brightness = 0.0;
  double hdr_local_tone_mapping = 0.0;
  double filmic_shoulder = 0.0;
  double filmic_toe = 0.0;
  double adaptive_contrast = 0.0;
  double levels_black_point = 0.0;
  double levels_gamma = 0.0;
  double levels_white_point = 0.0;
  double zone_deep_shadows = 0.0;
  double zone_shadows = 0.0;
  double zone_midtones = 0.0;
  double zone_lights = 0.0;
  double zone_highlights = 0.0;
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double sharpening = 0.0;
  double sharpen_radius = 1.0;
  double sharpen_detail = 0.0;
  double sharpen_masking = 0.0;
  double luminance_nr = 0.0;
  double color_nr = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double perspective_vertical = 0.0;
  double perspective_horizontal = 0.0;
  double transform_rotation = 0.0;
  double transform_scale = 1.0;
  double transform_skew_x = 0.0;
  double transform_skew_y = 0.0;
  double liquify_center_x = 0.5;
  double liquify_center_y = 0.5;
  double liquify_radius = 0.25;
  double liquify_strength = 0.0;
  bool content_aware_crop_fill = false;
  double vibrance = 0.0;
  double saturation = 0.0;
  double curve_shadows = 0.0;
  double curve_darks = 0.0;
  double curve_lights = 0.0;
  double curve_highlights = 0.0;
  std::vector<CurvePoint> master_curve_points;
  std::vector<CurvePoint> luma_curve_points;
  std::vector<CurvePoint> red_curve_points;
  std::vector<CurvePoint> green_curve_points;
  std::vector<CurvePoint> blue_curve_points;
  std::string color_mixer_model = "OKLab";
  std::string color_mixer_channel = "Reds";
  double hue_adjustment = 0.0;
  double saturation_adjustment = 0.0;
  double luminance_adjustment = 0.0;
  std::array<ColorMixerChannelAdjustment, kColorMixerChannelCount> color_mixer_channels{};
  RgbChannelMixerAdjustment rgb_channel_mixer;
  bool black_and_white_enabled = false;
  std::array<double, kColorMixerChannelCount> black_and_white_mixer{};
  bool color_replacement_enabled = false;
  double color_replacement_source_hue = 0.0;
  double color_replacement_target_hue = 0.33;
  double color_replacement_range = 35.0;
  double color_replacement_softness = 100.0;
  double color_replacement_strength = 0.0;
  bool color_replacement_preserve_luminance = true;
  bool color_replacement_preserve_saturation = true;
  bool skin_tone_protection_enabled = false;
  double skin_tone_line_hue = 0.08;
  double skin_tone_protection_range = 30.0;
  double skin_tone_protection_strength = 0.0;
  bool color_harmony_enabled = false;
  std::string color_harmony_mode = "Complementary";
  double color_harmony_base_hue = 0.0;
  double color_harmony_strength = 0.0;
  bool lut_enabled = false;
  double lut_strength = 100.0;
  std::string lut_name;
  std::vector<ColorPixel> lut_table;
  int lut_size = 0;
  std::array<double, 3> lut_domain_min = {0.0, 0.0, 0.0};
  std::array<double, 3> lut_domain_max = {1.0, 1.0, 1.0};
  std::string lut_input_color_space = "sRGB";
  std::string lut_output_color_space = "sRGB";
  std::vector<LutAdjustment> lut_stack;
  std::vector<ColorPixel> lut_shaper_table;
  int lut_shaper_size = 0;
  std::array<double, 2> lut_shaper_input_range = {0.0, 1.0};
  double grading_shadows_hue = 0.0;
  double grading_shadows_saturation = 0.0;
  double grading_shadows_luminance = 0.0;
  double grading_midtones_hue = 0.0;
  double grading_midtones_saturation = 0.0;
  double grading_midtones_luminance = 0.0;
  double grading_highlights_hue = 0.0;
  double grading_highlights_saturation = 0.0;
  double grading_highlights_luminance = 0.0;
  double grading_amount = 100.0;
  double grading_balance = 0.0;
  bool linear_mask_enabled = false;
  double linear_mask_exposure = 0.0;
  double linear_mask_temperature = 0.0;
  double linear_mask_feather = 0.22;
  double linear_mask_position = 0.5;
  double linear_mask_rotation = 0.0;
  bool radial_mask_enabled = false;
  double radial_mask_exposure = 0.0;
  double radial_mask_temperature = 0.0;
  double radial_mask_feather = 0.25;
  double radial_mask_center_x = 0.5;
  double radial_mask_center_y = 0.5;
  double radial_mask_radius_x = 0.25;
  double radial_mask_radius_y = 0.25;
  double radial_mask_rotation = 0.0;
  std::vector<LocalMaskSet> local_mask_sets;
  std::vector<LocalMask> retouch_masks;
  std::vector<LocalMask> local_masks;
  // Plain root text layers that the desktop compiler has proven safe to
  // composite in display-linear space after photographic output. The normal
  // layer stage applies these in working-scene space when the output plan
  // cannot honor that policy.
  std::vector<PhotoLayer> display_referred_text_layers;
  std::string display_referred_text_color_domain = "working-scene-linear";
  std::string display_referred_text_eligibility = "legacy-working-scene";
  std::string display_referred_text_eligibility_reason =
      "no-topmost-plain-text-suffix";
  std::string display_referred_text_policy_version =
      "display-referred-text-suffix-v1";
  std::vector<PhotoLayer> photo_layers;
};

enum class ColorOperationDomain {
  SceneReferred,
  DisplayReferred,
  LookSpace,
};

enum class ColorPipelineStage {
  InputSceneLinear = 100,
  SceneTone = 200,
  DisplayCurve = 300,
  ChannelLook = 400,
  PerceptualLook = 500,
  CreativeLut = 600,
  OutputRendering = 700,
  ProofSimulation = 800,
};

struct ColorControlSemantics final {
  std::string_view key;
  std::string_view label;
  ColorOperationDomain domain = ColorOperationDomain::SceneReferred;
  ColorPipelineStage stage = ColorPipelineStage::InputSceneLinear;
  int order = 0;
  bool mathematically_reversible = false;
  bool image_dependent = false;
  std::string_view description;
};

[[nodiscard]] const char* color_operation_domain_name(ColorOperationDomain domain);
[[nodiscard]] const char* color_pipeline_stage_name(ColorPipelineStage stage);
[[nodiscard]] std::span<const ColorControlSemantics> core_color_control_semantics();
[[nodiscard]] const ColorControlSemantics* color_control_semantics_for_key(
    std::string_view key);

[[nodiscard]] inline double effective_input_exposure(const ColorAdjustmentState& state) {
  return state.input_transform.exposure;
}

[[nodiscard]] inline double effective_input_temperature(const ColorAdjustmentState& state) {
  return state.input_transform.temperature;
}

[[nodiscard]] inline double effective_input_tint(const ColorAdjustmentState& state) {
  return state.input_transform.tint;
}

constexpr int kLutSize = 33;  // 33x33x33 3D LUT

struct ColorLut3D final {
  std::vector<ColorPixel> table;  // kLutSize^3 entries, indexed [r][g][b]
  std::uint64_t cache_identity = 0U;
  int size = kLutSize;
  std::array<double, 3> domain_min = {0.0, 0.0, 0.0};
  std::array<double, 3> domain_max = {1.0, 1.0, 1.0};
  std::string input_color_space = "sRGB";
  std::string output_color_space = "sRGB";
  double shoulder_threshold = 1.0;
  double shoulder_headroom = 1.0;
  bool valid = false;
  // Optional per-channel CUBE shaper, applied before the 3D table. A pure
  // 1D CUBE has size == 0 and an empty table while retaining this payload.
  std::vector<ColorPixel> shaper_table;
  int shaper_size = 0;
  std::array<double, 2> shaper_input_range = {0.0, 1.0};
};

// Immutable, scene-linear global-tone program shared by the exact CPU stage
// and retained GPU implementations. The samples are stored as float because
// that is the value actually consumed by GPU backends; parity is measured
// against the double-precision canonical tone table before a backend may use
// it for preview publication.
struct CompiledToneStageLut final {
  static constexpr int kSampleCount = 16385;
  static constexpr float kMinimumStops = -20.0F;
  static constexpr float kMaximumStops = 20.0F;
  static constexpr float kMiddleGray = 0.18F;

  std::vector<float> samples;
  std::array<float, 3> luminance_coefficients{};
  float stop_step =
      (kMaximumStops - kMinimumStops) /
      static_cast<float>(kSampleCount - 1);
  float output_headroom = 1.0F;
  std::uint64_t cache_identity = 0U;

  [[nodiscard]] bool ready() const {
    return samples.size() == static_cast<std::size_t>(kSampleCount) &&
           cache_identity != 0U;
  }
};

struct CompiledCurveStageLuts final {
  static constexpr int kSampleCount = 4097;

  std::vector<float> parametric;
  std::vector<float> master;
  std::vector<float> luma;
  std::vector<float> red;
  std::vector<float> green;
  std::vector<float> blue;
  std::array<float, 3> luminance_coefficients{};
  float output_headroom = 1.0F;
  std::uint64_t cache_identity = 0U;

  [[nodiscard]] bool active() const {
    return !parametric.empty() || !master.empty() || !luma.empty() ||
           !red.empty() || !green.empty() || !blue.empty();
  }

  [[nodiscard]] bool ready() const {
    const auto valid_lut = [](const std::vector<float>& lut) {
      return lut.empty() ||
             lut.size() == static_cast<std::size_t>(kSampleCount);
    };
    return active() && cache_identity != 0U && valid_lut(parametric) &&
           valid_lut(master) && valid_lut(luma) && valid_lut(red) &&
           valid_lut(green) && valid_lut(blue);
  }
};

struct CompiledLookStageProgram final {
  static constexpr std::uint32_t kRgbMixer = 1U << 0U;
  static constexpr std::uint32_t kBlackAndWhite = 1U << 1U;
  static constexpr std::uint32_t kSaturationVibrance = 1U << 2U;
  static constexpr std::uint32_t kColorGrading = 1U << 3U;
  static constexpr std::uint32_t kColorGradingAddsChroma = 1U << 4U;
  static constexpr std::uint32_t kPerChannelMixer = 1U << 5U;
  static constexpr std::uint32_t kColorHarmony = 1U << 6U;
  static constexpr std::uint32_t kColorReplacement = 1U << 7U;
  static constexpr std::uint32_t kSkinProtection = 1U << 8U;
  // Historical renderer versions retain their saved creative-control semantics.
  static constexpr std::uint32_t kLegacyCreativeSemantics = 1U << 9U;
  static constexpr std::uint32_t kHighlightChromaCompressionOverHeadroom = 1U << 10U;
  static constexpr std::uint32_t kHighlightChromaCompressionFromTone = 1U << 11U;

  ColorMatrix3 rgb_to_xyz_d65;
  ColorMatrix3 xyz_d65_to_rgb;
  // Named color selections use a fixed reference, independent of working gamut.
  ColorMatrix3 rgb_to_selection_rgb;
  std::array<float, 3> luminance_coefficients{};
  std::array<float, 9> rgb_mixer{};
  std::array<float, kColorMixerChannelCount> black_and_white_mixer{};
  std::array<float, 3> grading_hue{};
  std::array<float, 3> grading_saturation{};
  std::array<float, 3> grading_luminance{};
  std::array<float, kColorMixerChannelCount> mixer_hue{};
  std::array<float, kColorMixerChannelCount> mixer_saturation{};
  std::array<float, kColorMixerChannelCount> mixer_luminance{};
  std::array<float, kColorMixerChannelCount> selective_cyan{};
  std::array<float, kColorMixerChannelCount> selective_magenta{};
  std::array<float, kColorMixerChannelCount> selective_yellow{};
  std::array<float, kColorMixerChannelCount> selective_black{};
  std::array<float, 4> harmony_anchors{};
  float saturation_strength = 0.0F;
  float vibrance_strength = 0.0F;
  float grading_amount = 0.0F;
  float grading_balance = 0.0F;
  float harmony_strength = 0.0F;
  float replacement_source_hue = 0.0F;
  float replacement_target_oklab_hue = 0.0F;
  float replacement_range = 0.0F;
  float replacement_softness = 0.0F;
  float replacement_strength = 0.0F;
  float skin_hue = 0.0F;
  float skin_range = 0.0F;
  float skin_strength = 0.0F;
  float output_headroom = 1.0F;
  std::uint32_t mixer_model = 0U;
  std::uint32_t harmony_anchor_count = 0U;
  std::uint32_t replacement_preserve_luminance = 0U;
  std::uint32_t replacement_preserve_saturation = 0U;
  std::uint32_t active_mask = 0U;
  std::uint64_t cache_identity = 0U;

  [[nodiscard]] bool ready() const {
    return active_mask != 0U && cache_identity != 0U;
  }
};

struct CubeLutParseResult final {
  ColorLut3D lut;
  std::string title;
  std::string error_message;

  [[nodiscard]] bool ok() const {
    return lut.valid && error_message.empty();
  }
};

struct LutLibraryEntry final {
  std::string name;
  std::string file_path;
  std::string format;
};

enum class RemovalQuality {
  Interactive,
  Final,
};

enum class GamutWarningTarget {
  PreviewDisplay,
  ExportOutput,
};

struct CompiledAces2OutputLutApplication final {
  std::shared_ptr<const Aces2OutputLut3D> lut;
  ColorMatrix3 target_to_working;
  bool black_point_compensation_enabled = false;
  double source_black_point = 0.0;
  double output_black_point = 0.0;
  double black_point_headroom = 1.0;
  double display_headroom = 1.0;

  [[nodiscard]] bool ready() const {
    return lut != nullptr && lut->valid();
  }
};

struct GamutWarningAnalysis final {
  std::size_t tested_pixels = 0;
  std::size_t out_of_gamut_pixels = 0;
  double out_of_gamut_fraction = 0.0;
  double max_channel_excursion = 0.0;
  bool transform_ready = true;
  std::string processor;
  std::string profile_asset_id;
  std::string profile_payload_sha256;
  std::string transform_cache_key;
  std::string failure_reason;

  [[nodiscard]] bool has_warnings() const {
    return out_of_gamut_pixels > 0;
  }
};

struct OutputTransformResult final {
  ColorPixel pixel;
  bool source_out_of_gamut = false;
  double max_channel_excursion = 0.0;
  std::string output_profile_name;
};

enum class OutputSampleEncoding {
  LinearDisplay,
  DisplayEncoded,
};

enum class OutputInverseStatus {
  Ready,
  InvalidDisplayTarget,
  NonInvertiblePhotographicRendering,
  NonInvertibleSoftProof,
  NonInvertibleCustomIcc,
  NonInvertibleBlackPointCompensation,
  NonInvertibleClippedOrMappedSample,
  NonInvertibleAlphaClamp,
  OutputProfileMismatch,
  EncodedSignalOutsideInvertibleDomain,
  NonFiniteSample,
};

struct OutputSampleProvenance final {
  bool rgb_was_clipped_or_mapped = false;
  bool alpha_was_clamped = false;
  std::string output_profile_name;
};

struct CompiledOutputInverseTransform final {
  OutputInverseStatus status = OutputInverseStatus::InvalidDisplayTarget;
  ColorMatrix3 output_to_working;
  DisplayTarget display_target;
  std::string output_profile_name;
  std::string provenance;
  std::string failure_reason;
  bool requires_unmapped_sample = true;
  bool supports_display_encoded_samples = false;

  [[nodiscard]] bool ready() const {
    return status == OutputInverseStatus::Ready;
  }
};

struct OutputInverseResult final {
  ColorPixel pixel;
  OutputInverseStatus status = OutputInverseStatus::InvalidDisplayTarget;
  bool exact = false;
  std::string failure_reason;
};

struct CancellationToken final {
  const std::atomic<int>* latest_generation = nullptr;
  int generation = 0;
  const std::atomic_bool* cancellation_requested = nullptr;

  [[nodiscard]] bool cancelled() const noexcept {
    return (latest_generation != nullptr &&
            generation < latest_generation->load(std::memory_order_relaxed)) ||
           (cancellation_requested != nullptr &&
            cancellation_requested->load(std::memory_order_relaxed));
  }
};

[[nodiscard]] double clamp_signed_percent(double value);
[[nodiscard]] double clamp_unit(double value);
[[nodiscard]] double perceptual_delta(
    const ColorPixel& left,
    const ColorPixel& right,
    const RgbColorSpace& color_space = make_linear_srgb_color_space(),
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] std::vector<float> build_perceptual_curve_lut(std::span<const CurvePoint> points, int sample_count = 256);
[[nodiscard]] double evaluate_tone_curve(double value, const ColorAdjustmentState& state);
[[nodiscard]] double channel_weight_for_selection(double hue, std::string_view channel);
[[nodiscard]] OutputTransformResult apply_output_transform(
    const ColorPixel& pixel,
    const ColorPipelineConfig& config,
    GamutWarningTarget target = GamutWarningTarget::ExportOutput,
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] const char* output_inverse_status_name(OutputInverseStatus status);
[[nodiscard]] OutputSampleProvenance output_sample_provenance(
    const ColorPixel& source_pixel,
    const OutputTransformResult& output);
[[nodiscard]] CompiledOutputInverseTransform compile_output_inverse_transform(
    const ColorPipelineConfig& config,
    GamutWarningTarget target = GamutWarningTarget::ExportOutput,
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] OutputInverseResult apply_output_inverse_transform(
    const CompiledOutputInverseTransform& transform,
    const ColorPixel& output_pixel,
    OutputSampleEncoding encoding,
    const OutputSampleProvenance& provenance);
void apply_output_transform(
    std::span<ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target = GamutWarningTarget::ExportOutput,
    std::string_view chromatic_adaptation_method = "Bradford");
void apply_output_gamut_mapping_in_working_space(
    std::span<ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target = GamutWarningTarget::ExportOutput,
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] CompiledAces2OutputLutApplication
compile_aces2_output_lut_application(
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    Aces2OutputLutQuality quality,
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] ColorPixel apply_aces2_output_lut_application(
    const CompiledAces2OutputLutApplication& application,
    const ColorPixel& pixel);
[[nodiscard]] ColorPixel
apply_aces2_output_lut_target_application(
    const CompiledAces2OutputLutApplication& application,
    const ColorPixel& pixel);
void apply_aces2_output_lut_application(
    std::span<ColorPixel> pixels,
    const CompiledAces2OutputLutApplication& application);
void apply_aces2_output_lut_target_application(
    std::span<ColorPixel> pixels,
    const CompiledAces2OutputLutApplication& application);
[[nodiscard]] GamutWarningAnalysis analyze_gamut_warnings(
    std::span<const ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target = GamutWarningTarget::ExportOutput,
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] ColorLut3D build_color_lut(const ColorAdjustmentState& state);
[[nodiscard]] ColorPixel apply_lut(const ColorLut3D& lut, const ColorPixel& pixel);
[[nodiscard]] ColorPixel apply_lut_tetrahedral(
    const ColorLut3D& lut,
    const ColorPixel& pixel);
[[nodiscard]] CubeLutParseResult parse_cube_lut(std::string_view text);
[[nodiscard]] CubeLutParseResult load_cube_lut_file(std::string_view file_path);
[[nodiscard]] std::string serialize_cube_lut(const ColorLut3D& lut, std::string_view title = {});
[[nodiscard]] bool save_cube_lut_file(
    std::string_view file_path,
    const ColorLut3D& lut,
    std::string_view title = {},
    std::string* error_message = nullptr);
[[nodiscard]] CubeLutParseResult parse_hald_lut(
    std::span<const ColorPixel> pixels,
    int width,
    int height);
[[nodiscard]] bool serialize_hald_lut(
    const ColorLut3D& lut,
    std::vector<ColorPixel>* pixels,
    int* width,
    int* height,
    std::string* error_message = nullptr);
[[nodiscard]] std::vector<ColorPixel> render_lut_preview_thumbnail(
    const ColorLut3D& lut,
    int width,
    int height);
[[nodiscard]] std::vector<LutLibraryEntry> scan_lut_folder(
    std::string_view folder_path,
    bool recursive = true);
[[nodiscard]] std::vector<LutLibraryEntry> search_lut_entries(
    std::span<const LutLibraryEntry> entries,
    std::string_view query);
struct ColorAdjustmentTelemetry final {
  std::int64_t input_vignette_ms = 0;
  std::int64_t normalize_raw_ms = 0;
  std::int64_t camera_characterization_ms = 0;
  std::int64_t scene_adjustments_ms = 0;
  std::int64_t tone_ms = 0;
  std::int64_t curves_ms = 0;
  std::int64_t look_ms = 0;
  std::int64_t fast_global_ms = 0;
  std::int64_t tone_table_cache_hit_count = 0;
  std::int64_t tone_table_cache_miss_count = 0;
  std::int64_t tone_table_build_ms = 0;
  std::int64_t tone_mask_ms = 0;
  std::int64_t spatial_ms = 0;
  std::int64_t spatial_luminance_ms = 0;
  std::int64_t spatial_texture_blur_ms = 0;
  std::int64_t spatial_clarity_blur_ms = 0;
  std::int64_t spatial_haze_blur_ms = 0;
  std::int64_t spatial_apply_ms = 0;
  std::int64_t post_color_ms = 0;
  std::int64_t retouch_ms = 0;
  std::int64_t user_lut_ms = 0;
  std::int64_t detail_ms = 0;
  std::int64_t detail_oklab_ms = 0;
  std::int64_t detail_sharpen_ms = 0;
  std::int64_t detail_luminance_nr_ms = 0;
  std::int64_t detail_color_nr_ms = 0;
  std::int64_t detail_output_ms = 0;
  std::int64_t geometry_ms = 0;
  std::int64_t photographic_output_ms = 0;
  std::string display_referred_text_color_domain = "working-scene-linear";
  std::string display_referred_text_eligibility = "legacy-working-scene";
  std::string display_referred_text_eligibility_reason;
  std::string display_referred_text_policy_version =
      "display-referred-text-suffix-v1";
  std::int64_t display_referred_text_layer_count = 0;
  std::int64_t display_referred_text_applied_layer_count = 0;
  std::int64_t proof_ms = 0;
  std::int64_t device_transform_ms = 0;
  std::int64_t publish_ms = 0;
  std::int64_t final_output_ms = 0;
  std::int64_t final_output_gamut_fit_pass_count = 0;
  std::int64_t final_output_black_point_ms = 0;
  std::int64_t final_output_headroom_ms = 0;
  HighlightReconstructionAnalysis highlight_reconstruction;
  std::int64_t gpu_command_buffer_count = 0;
  std::int64_t gpu_kernel_dispatch_count = 0;
  std::int64_t gpu_blit_count = 0;
  std::int64_t gpu_cpu_to_gpu_bytes = 0;
  std::int64_t gpu_gpu_to_cpu_bytes = 0;
  std::int64_t gpu_spatial_texture_blur_dispatch_count = 0;
  std::int64_t gpu_spatial_clarity_blur_dispatch_count = 0;
  std::int64_t gpu_spatial_haze_blur_dispatch_count = 0;
  std::string gpu_fallback_reason;
  std::string failure_reason;
};

[[nodiscard]] CompiledToneStageLut compile_tone_stage_lut(
    const ColorAdjustmentState& state,
    ColorAdjustmentTelemetry* telemetry = nullptr);
[[nodiscard]] ColorPixel apply_compiled_tone_stage_lut(
    const CompiledToneStageLut& compiled,
    const ColorPixel& pixel);
[[nodiscard]] CompiledCurveStageLuts compile_curve_stage_luts(
    const ColorAdjustmentState& state);
[[nodiscard]] ColorPixel apply_compiled_curve_stage_luts(
    const CompiledCurveStageLuts& compiled,
    const ColorPixel& pixel);
[[nodiscard]] CompiledLookStageProgram compile_look_stage_program(
    const ColorAdjustmentState& state);
[[nodiscard]] ColorPixel apply_compiled_look_stage_program(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& pixel);

struct ColorPrefixStageContext final {
  HighlightReconstructionAnalysis highlight_reconstruction;
  bool prepared = false;
};

[[nodiscard]] HighlightReconstructionAnalysis analyze_highlight_reconstruction(
    std::span<const ColorPixel> pixels,
    int width,
    int height,
    const InputTransformSettings& settings);
[[nodiscard]] bool metal_color_backend_available();
[[nodiscard]] std::string metal_color_backend_unavailable_reason();
[[nodiscard]] bool metal_color_pipeline_compatible(const ColorAdjustmentState& state);
[[nodiscard]] std::string metal_color_pipeline_incompatibility_reason(
    const ColorAdjustmentState& state);
[[nodiscard]] bool apply_color_geometry_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
[[nodiscard]] bool apply_color_photographic_output_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    Aces2OutputLutQuality quality,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    std::span<const PhotoLayer> display_referred_text_layers = {},
    bool display_referred_text_overlay_enabled = false);
[[nodiscard]] bool apply_color_publish_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const DisplayTarget& display_target,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);
[[nodiscard]] bool apply_color_layers_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality = RemovalQuality::Interactive,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false,
    bool include_display_referred_text_layers = true);
[[nodiscard]] ColorPrefixStageContext prepare_color_prefix_stage_context(
    std::span<const ColorPixel> decoded_pixels,
    int width,
    int height,
    const ColorAdjustmentState& state);
[[nodiscard]] bool apply_color_normalize_raw_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    ColorPrefixStageContext* context,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
[[nodiscard]] bool apply_color_camera_characterization_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const ColorPrefixStageContext& context,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
[[nodiscard]] bool apply_color_scene_adjustments_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
[[nodiscard]] bool apply_color_tone_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
[[nodiscard]] bool apply_color_curves_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
[[nodiscard]] bool apply_color_look_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
[[nodiscard]] bool apply_color_compatibility_output_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);
void apply_color_adjustments_through_look_base(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality = RemovalQuality::Interactive,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);
bool apply_color_adjustments_through_layers(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality = RemovalQuality::Interactive,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);
[[nodiscard]] bool apply_color_detail_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality = RemovalQuality::Interactive,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr,
    bool input_finite_validated = false);
bool apply_color_adjustments_through_detail(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality = RemovalQuality::Interactive,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);
bool apply_color_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality = RemovalQuality::Interactive,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);

struct LocalAdjustmentCoordinateMapping final {
  bool operator==(const LocalAdjustmentCoordinateMapping&) const = default;
  int source_x = 0;
  int source_y = 0;
  int source_width = 0;
  int source_height = 0;
};

[[nodiscard]] bool apply_local_adjustments_to_look_pixels(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {});
[[nodiscard]] bool apply_local_adjustments_to_look_pixels(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const LocalAdjustmentCoordinateMapping& coordinate_mapping,
    CancellationToken cancellation = {});
[[nodiscard]] std::uint64_t refined_local_mask_hash(
    const LocalMask& mask,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels = {});
[[nodiscard]] std::uint64_t refined_local_mask_set_hash(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels = {});
[[nodiscard]] std::vector<std::uint8_t> rasterize_local_mask_set_alpha(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels = {});
[[nodiscard]] std::vector<std::uint8_t> rasterize_local_mask_set_alpha(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels,
    const ColorPipelineConfig& color_pipeline);

}  // namespace photo_editor::pipeline
