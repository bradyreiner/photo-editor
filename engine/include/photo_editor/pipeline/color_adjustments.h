#pragma once

#include "photo_editor/pipeline/color_pipeline.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct ColorPixel final {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;
};

struct CurvePoint final {
  double x = 0.0;
  double y = 0.0;
};

struct BrushPoint final {
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

struct LocalMask final {
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
};

constexpr int kColorMixerChannelCount = 8;
constexpr std::array<const char*, kColorMixerChannelCount> kColorMixerChannelNames = {
    "Reds", "Oranges", "Yellows", "Greens", "Aquas", "Blues", "Purples", "Magentas",
};

struct PhotoLayer final {
  std::string id;
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
  int x = 0;
  int y = 0;
  double transform_scale = 1.0;
  double transform_rotation = 0.0;
};

struct InputTransformSettings final {
  std::string input_profile_name = "Embedded";
  double exposure = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  bool raw_input_transform_enabled = true;
  bool highlight_recovery_enabled = true;
  std::string highlight_reconstruction_mode = "Enabled";
  std::string chromatic_adaptation_method = "Bradford";
  bool gamut_mapping_enabled = true;
  bool lens_correction_enabled = true;
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
};

struct ColorAdjustmentState final {
  InputTransformSettings input_transform{};
  ColorPipelineConfig color_pipeline = default_color_pipeline_config();
  double exposure = 0.0;
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
  std::vector<PhotoLayer> photo_layers;
};

[[nodiscard]] inline double effective_input_exposure(const ColorAdjustmentState& state) {
  return std::abs(state.input_transform.exposure) > 1e-9 ? state.input_transform.exposure : state.exposure;
}

[[nodiscard]] inline double effective_input_temperature(const ColorAdjustmentState& state) {
  return std::abs(state.input_transform.temperature) > 1e-9 ? state.input_transform.temperature : state.temperature;
}

[[nodiscard]] inline double effective_input_tint(const ColorAdjustmentState& state) {
  return std::abs(state.input_transform.tint) > 1e-9 ? state.input_transform.tint : state.tint;
}

constexpr int kLutSize = 33;  // 33x33x33 3D LUT

struct ColorLut3D final {
  std::vector<ColorPixel> table;  // kLutSize^3 entries, indexed [r][g][b]
  int size = kLutSize;
  std::array<double, 3> domain_min = {0.0, 0.0, 0.0};
  std::array<double, 3> domain_max = {1.0, 1.0, 1.0};
  std::string input_color_space = "sRGB";
  std::string output_color_space = "sRGB";
  double shoulder_threshold = 1.0;
  double shoulder_headroom = 1.0;
  bool valid = false;
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

struct GamutWarningAnalysis final {
  std::size_t tested_pixels = 0;
  std::size_t out_of_gamut_pixels = 0;
  double out_of_gamut_fraction = 0.0;
  double max_channel_excursion = 0.0;

  [[nodiscard]] bool has_warnings() const {
    return out_of_gamut_pixels > 0;
  }
};

struct CancellationToken final {
  const std::atomic<int>* latest_generation = nullptr;
  int generation = 0;

  [[nodiscard]] bool cancelled() const noexcept {
    return latest_generation != nullptr &&
           generation < latest_generation->load(std::memory_order_relaxed);
  }
};

[[nodiscard]] double clamp_signed_percent(double value);
[[nodiscard]] double clamp_unit(double value);
[[nodiscard]] double srgb_to_linear(double value);
[[nodiscard]] double linear_to_srgb(double value);
[[nodiscard]] double perceptual_delta(
    const ColorPixel& left,
    const ColorPixel& right,
    const RgbColorSpace& color_space = make_linear_srgb_color_space(),
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] std::vector<float> build_perceptual_curve_lut(std::span<const CurvePoint> points, int sample_count = 256);
[[nodiscard]] double evaluate_tone_curve(double value, const ColorAdjustmentState& state);
[[nodiscard]] double channel_weight_for_selection(double hue, std::string_view channel);
[[nodiscard]] GamutWarningAnalysis analyze_gamut_warnings(
    std::span<const ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target = GamutWarningTarget::ExportOutput,
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] ColorLut3D build_color_lut(const ColorAdjustmentState& state);
[[nodiscard]] ColorPixel apply_lut(const ColorLut3D& lut, const ColorPixel& pixel);
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
  std::int64_t fast_global_ms = 0;
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
  std::int64_t final_output_ms = 0;
  std::int64_t final_output_black_point_ms = 0;
  std::int64_t final_output_headroom_ms = 0;
  std::int64_t gpu_command_buffer_count = 0;
  std::int64_t gpu_kernel_dispatch_count = 0;
  std::int64_t gpu_blit_count = 0;
  std::int64_t gpu_cpu_to_gpu_bytes = 0;
  std::int64_t gpu_gpu_to_cpu_bytes = 0;
  std::int64_t gpu_spatial_texture_blur_dispatch_count = 0;
  std::int64_t gpu_spatial_clarity_blur_dispatch_count = 0;
  std::int64_t gpu_spatial_haze_blur_dispatch_count = 0;
};
void apply_color_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality = RemovalQuality::Interactive,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);

struct LocalAdjustmentCoordinateMapping final {
  int source_x = 0;
  int source_y = 0;
  int source_width = 0;
  int source_height = 0;
};

[[nodiscard]] bool apply_local_adjustments_to_adjusted_pixels(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation = {});
[[nodiscard]] bool apply_local_adjustments_to_adjusted_pixels(
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

}  // namespace photo_editor::pipeline
