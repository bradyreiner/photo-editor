#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct ColorPixel final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  double alpha = 1.0;
};

struct CurvePoint final {
  double x = 0.0;
  double y = 0.0;
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
};

[[nodiscard]] double clamp_signed_percent(double value);
[[nodiscard]] double clamp_unit(double value);
[[nodiscard]] double srgb_to_linear(double value);
[[nodiscard]] double linear_to_srgb(double value);
[[nodiscard]] double evaluate_tone_curve(double value, const ColorAdjustmentState& state);
[[nodiscard]] double channel_weight_for_selection(double hue, std::string_view channel);
void apply_color_adjustments(std::vector<ColorPixel>* pixels, int width, int height, const ColorAdjustmentState& state);

}  // namespace photo_editor::pipeline
