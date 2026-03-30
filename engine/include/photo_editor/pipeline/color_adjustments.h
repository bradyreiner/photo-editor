#pragma once

#include <array>
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
};

struct LocalMask final {
  std::string id;
  std::string name;
  std::string kind = "linear";
  std::string operation = "add";
  std::string retouch_mode;
  bool enabled = true;
  double exposure = 0.0;
  double temperature = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  double remove_strength = 0.0;
  bool selection_invert = false;
  double selection_expand = 0.0;
  double feather = 0.22;
  double position = 0.5;
  double rotation = 0.0;
  double center_x = 0.5;
  double center_y = 0.5;
  double radius_x = 0.25;
  double radius_y = 0.25;
  double brush_size = 0.08;
  double brush_flow = 1.0;
  bool clone_source_set = false;
  double clone_source_x = 0.5;
  double clone_source_y = 0.5;
  double clone_offset_x = 0.0;
  double clone_offset_y = 0.0;
  double clone_scale = 1.0;
  double clone_rotation = 0.0;
  std::vector<BrushPoint> brush_points;
  std::vector<CurvePoint> polygon_points;
  double luminance_min = 0.2;
  double luminance_max = 0.8;
  double range_feather = 0.1;
  double hue_center = 0.08;
  double hue_range = 0.12;
  double saturation_min = 0.1;
  double saturation_max = 1.0;
  std::vector<uint8_t> segmentation_mask;
  int segmentation_width = 0;
  int segmentation_height = 0;
};

struct ColorMixerChannelAdjustment final {
  double hue = 0.0;
  double saturation = 0.0;
  double luminance = 0.0;
};

constexpr int kColorMixerChannelCount = 8;
constexpr std::array<const char*, kColorMixerChannelCount> kColorMixerChannelNames = {
    "Reds", "Oranges", "Yellows", "Greens", "Aquas", "Blues", "Purples", "Magentas",
};

struct ColorAdjustmentState final {
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
  double sharpen_masking = 0.0;
  double luminance_nr = 0.0;
  double color_nr = 0.0;
  double lens_distortion = 0.0;
  double lens_vignette = 0.0;
  double chromatic_aberration = 0.0;
  double vibrance = 0.0;
  double saturation = 0.0;
  double curve_shadows = 0.0;
  double curve_darks = 0.0;
  double curve_lights = 0.0;
  double curve_highlights = 0.0;
  std::vector<CurvePoint> master_curve_points;
  std::vector<CurvePoint> red_curve_points;
  std::vector<CurvePoint> green_curve_points;
  std::vector<CurvePoint> blue_curve_points;
  std::string color_mixer_channel = "Reds";
  double hue_adjustment = 0.0;
  double saturation_adjustment = 0.0;
  double luminance_adjustment = 0.0;
  std::array<ColorMixerChannelAdjustment, kColorMixerChannelCount> color_mixer_channels{};
  double grading_shadows_hue = 0.0;
  double grading_shadows_saturation = 0.0;
  double grading_shadows_luminance = 0.0;
  double grading_midtones_hue = 0.0;
  double grading_midtones_saturation = 0.0;
  double grading_midtones_luminance = 0.0;
  double grading_highlights_hue = 0.0;
  double grading_highlights_saturation = 0.0;
  double grading_highlights_luminance = 0.0;
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
  std::vector<LocalMask> local_masks;
};

constexpr int kLutSize = 33;  // 33x33x33 3D LUT

struct ColorLut3D final {
  std::vector<ColorPixel> table;  // kLutSize^3 entries, indexed [r][g][b]
  double shoulder_threshold = 1.0;
  bool valid = false;
};

[[nodiscard]] double clamp_signed_percent(double value);
[[nodiscard]] double clamp_unit(double value);
[[nodiscard]] double srgb_to_linear(double value);
[[nodiscard]] double linear_to_srgb(double value);
[[nodiscard]] std::vector<float> build_perceptual_curve_lut(std::span<const CurvePoint> points, int sample_count = 256);
[[nodiscard]] double evaluate_tone_curve(double value, const ColorAdjustmentState& state);
[[nodiscard]] double channel_weight_for_selection(double hue, std::string_view channel);
[[nodiscard]] ColorLut3D build_color_lut(const ColorAdjustmentState& state);
[[nodiscard]] ColorPixel apply_lut(const ColorLut3D& lut, const ColorPixel& pixel);
void apply_color_adjustments(std::vector<ColorPixel>* pixels, int width, int height, const ColorAdjustmentState& state);

}  // namespace photo_editor::pipeline
