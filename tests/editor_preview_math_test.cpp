#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <QColor>

#include "photo_editor/pipeline/color_adjustments.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

ColorPixel linear_pixel_from_srgb(const QColor& color) {
  return ColorPixel{
      .red = photo_editor::pipeline::srgb_to_linear(color.redF()),
      .green = photo_editor::pipeline::srgb_to_linear(color.greenF()),
      .blue = photo_editor::pipeline::srgb_to_linear(color.blueF()),
      .alpha = color.alphaF(),
  };
}

QColor srgb_from_linear_pixel(const ColorPixel& pixel) {
  return QColor::fromRgbF(
      static_cast<float>(photo_editor::pipeline::linear_to_srgb(pixel.red)),
      static_cast<float>(photo_editor::pipeline::linear_to_srgb(pixel.green)),
      static_cast<float>(photo_editor::pipeline::linear_to_srgb(pixel.blue)),
      static_cast<float>(photo_editor::pipeline::clamp_unit(pixel.alpha)));
}

double luminance(const ColorPixel& pixel) {
  return (pixel.red * 0.2126) + (pixel.green * 0.7152) + (pixel.blue * 0.0722);
}

ColorPixel apply_single_pixel(const ColorPixel& pixel, const ColorAdjustmentState& state) {
  std::vector<ColorPixel> pixels{pixel};
  photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
  return pixels.front();
}

}  // namespace

int main() {
  ColorAdjustmentState neutral_state;
  ColorAdjustmentState curve_state;
  curve_state.curve_highlights = 60.0;
  curve_state.curve_shadows = -40.0;

  const auto neutral_high = photo_editor::pipeline::evaluate_tone_curve(0.85, neutral_state);
  const auto curved_high = photo_editor::pipeline::evaluate_tone_curve(0.85, curve_state);
  if (!(curved_high > neutral_high)) {
    return fail("raising the highlight curve point should brighten high values");
  }

  ColorAdjustmentState point_curve_state;
  point_curve_state.master_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.5, .y = 0.75},
      {.x = 1.0, .y = 1.0},
  };
  const auto point_curve_mid = photo_editor::pipeline::evaluate_tone_curve(0.5, point_curve_state);
  if (!(point_curve_mid > 0.5)) {
    return fail("master point curve should brighten a lifted midpoint");
  }

  const auto neutral_shadow = photo_editor::pipeline::evaluate_tone_curve(0.20, neutral_state);
  const auto curved_shadow = photo_editor::pipeline::evaluate_tone_curve(0.20, curve_state);
  if (!(curved_shadow < neutral_shadow)) {
    return fail("lowering the shadow curve point should darken low values");
  }

  if (!(photo_editor::pipeline::channel_weight_for_selection(0.0, "Reds") >
        photo_editor::pipeline::channel_weight_for_selection(0.0, "Blues"))) {
    return fail("red hue should match the red channel more strongly than blue");
  }

  const auto neutral_input = linear_pixel_from_srgb(QColor::fromRgbF(0.5F, 0.5F, 0.5F, 1.0F));
  const auto neutral_passthrough = apply_single_pixel(neutral_input, ColorAdjustmentState{});
  if (!almost_equal(luminance(neutral_passthrough), luminance(neutral_input), 1e-6)) {
    return fail("neutral pipeline should preserve in-range luminance when no tone controls are active");
  }

  ColorAdjustmentState chroma_state;
  chroma_state.vibrance = 85.0;
  chroma_state.saturation = 85.0;
  const auto neutral_adjusted = apply_single_pixel(neutral_input, chroma_state);
  if (!almost_equal(neutral_adjusted.red, neutral_adjusted.green, 1e-6) ||
      !almost_equal(neutral_adjusted.green, neutral_adjusted.blue, 1e-6)) {
    return fail("neutral tones should remain neutral under vibrance and saturation");
  }
  if (!almost_equal(luminance(neutral_adjusted), luminance(neutral_input), 1e-6)) {
    return fail("chroma-only edits should not dim neutral highlights");
  }

  const auto red_input = linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.35F, 0.50F, 1.0F));
  ColorAdjustmentState hue_state;
  hue_state.color_mixer_channel = "Reds";
  hue_state.hue_adjustment = 35.0;
  const auto red_adjusted = apply_single_pixel(red_input, hue_state);
  const QColor adjusted_red = srgb_from_linear_pixel(red_adjusted);
  if (almost_equal(adjusted_red.hslHueF(), 0.0, 0.01)) {
    return fail("hue adjustment should move matching channel hues");
  }

  const auto dark_input = linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.12F, 0.12F, 1.0F));
  const auto neutral_dark = apply_single_pixel(dark_input, ColorAdjustmentState{});
  ColorAdjustmentState black_state;
  black_state.blacks = 70.0;
  const auto lifted_dark = apply_single_pixel(dark_input, black_state);
  if (!(luminance(lifted_dark) > luminance(neutral_dark))) {
    return fail("positive blacks should lift dark tones");
  }

  const auto bright_input = linear_pixel_from_srgb(QColor::fromRgbF(0.82F, 0.82F, 0.82F, 1.0F));
  const auto neutral_bright = apply_single_pixel(bright_input, ColorAdjustmentState{});
  ColorAdjustmentState white_state;
  white_state.whites = 70.0;
  const auto brightened_highlight = apply_single_pixel(bright_input, white_state);
  if (!(luminance(brightened_highlight) > luminance(neutral_bright))) {
    return fail("positive whites should brighten bright tones");
  }

  const auto vivid_red_input = linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.95F, 0.55F, 1.0F));
  ColorAdjustmentState gamut_state;
  gamut_state.saturation = 100.0;
  gamut_state.color_mixer_channel = "Reds";
  gamut_state.saturation_adjustment = 100.0;
  const auto gamut_adjusted = apply_single_pixel(vivid_red_input, gamut_state);
  if (gamut_adjusted.red < -1e-6 || gamut_adjusted.red > 1.0 + 1e-6 ||
      gamut_adjusted.green < -1e-6 || gamut_adjusted.green > 1.0 + 1e-6 ||
      gamut_adjusted.blue < -1e-6 || gamut_adjusted.blue > 1.0 + 1e-6) {
    return fail("perceptual gamut fitting should keep output RGB in range");
  }

  const auto shoulder_input = linear_pixel_from_srgb(QColor::fromRgbF(0.95F, 0.95F, 0.95F, 1.0F));
  ColorAdjustmentState exposure_state;
  exposure_state.exposure = 1.0;
  const auto shoulder_adjusted = apply_single_pixel(shoulder_input, exposure_state);
  if (!(luminance(shoulder_adjusted) > luminance(shoulder_input))) {
    return fail("positive exposure should still brighten highlights");
  }
  if (!(luminance(shoulder_adjusted) < 0.999999)) {
    return fail("highlight shoulder should keep strong exposure edits below hard clipping");
  }

  ColorAdjustmentState red_curve_state;
  red_curve_state.red_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.5, .y = 0.72},
      {.x = 1.0, .y = 1.0},
  };
  const auto gray_input = linear_pixel_from_srgb(QColor::fromRgbF(0.5F, 0.5F, 0.5F, 1.0F));
  const auto red_curve_adjusted = apply_single_pixel(gray_input, red_curve_state);
  if (!(red_curve_adjusted.red > red_curve_adjusted.green &&
        almost_equal(red_curve_adjusted.green, red_curve_adjusted.blue, 1e-5))) {
    return fail("red point curve should affect red without shifting green and blue equally");
  }

  const auto radial_input = linear_pixel_from_srgb(QColor::fromRgbF(0.45F, 0.45F, 0.45F, 1.0F));
  std::vector<ColorPixel> radial_pixels(9, radial_input);
  ColorAdjustmentState radial_state;
  radial_state.radial_mask_enabled = true;
  radial_state.radial_mask_exposure = 1.0;
  radial_state.radial_mask_feather = 0.2;
  radial_state.radial_mask_center_x = 0.5;
  radial_state.radial_mask_center_y = 0.5;
  radial_state.radial_mask_radius_x = 0.22;
  radial_state.radial_mask_radius_y = 0.22;
  photo_editor::pipeline::apply_color_adjustments(&radial_pixels, 3, 3, radial_state);
  if (!(luminance(radial_pixels[4]) > luminance(radial_pixels[0]))) {
    return fail("radial mask should affect the center more strongly than the corners");
  }

  return EXIT_SUCCESS;
}
