#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#include <QColor>

#include "photo_editor/pipeline/color_adjustments.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::BrushPoint;
using photo_editor::pipeline::LocalMask;
using photo_editor::pipeline::LocalMaskSet;
using photo_editor::pipeline::PhotoLayer;

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

ColorPixel linear_pixel_from_srgb(const QColor& color) {
  return ColorPixel{
      .red = static_cast<float>(photo_editor::pipeline::srgb_to_linear(color.redF())),
      .green = static_cast<float>(photo_editor::pipeline::srgb_to_linear(color.greenF())),
      .blue = static_cast<float>(photo_editor::pipeline::srgb_to_linear(color.blueF())),
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

double stop_delta(double input_luminance, const ColorPixel& output_pixel) {
  return std::abs(std::log2(std::max(luminance(output_pixel), 1e-6) / std::max(input_luminance, 1e-6)));
}

ColorPixel apply_single_pixel(const ColorPixel& pixel, const ColorAdjustmentState& state) {
  std::vector<ColorPixel> pixels{pixel};
  photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
  return pixels.front();
}

std::vector<ColorPixel> apply_pixels(std::vector<ColorPixel> pixels, int width, int height, const ColorAdjustmentState& state) {
  photo_editor::pipeline::apply_color_adjustments(&pixels, width, height, state);
  return pixels;
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

  ColorAdjustmentState neutral_point_curve_state;
  neutral_point_curve_state.master_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 1.0, .y = 1.0},
  };
  neutral_point_curve_state.red_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 1.0, .y = 1.0},
  };
  neutral_point_curve_state.green_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 1.0, .y = 1.0},
  };
  neutral_point_curve_state.blue_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 1.0, .y = 1.0},
  };
  const auto neutral_curve_input = linear_pixel_from_srgb(QColor::fromRgbF(0.32F, 0.51F, 0.73F, 1.0F));
  const auto neutral_curve_output = apply_single_pixel(neutral_curve_input, neutral_point_curve_state);
  if (!almost_equal(neutral_curve_output.red, neutral_curve_input.red, 1e-5) ||
      !almost_equal(neutral_curve_output.green, neutral_curve_input.green, 1e-5) ||
      !almost_equal(neutral_curve_output.blue, neutral_curve_input.blue, 1e-5)) {
    return fail("identity point curves should preserve neutral rendered output");
  }

  ColorAdjustmentState endpoint_curve_state;
  endpoint_curve_state.master_curve_points = {
      {.x = 0.0, .y = 0.12},
      {.x = 1.0, .y = 0.88},
  };
  const auto endpoint_curve_low = photo_editor::pipeline::evaluate_tone_curve(0.05, endpoint_curve_state);
  const auto endpoint_curve_high = photo_editor::pipeline::evaluate_tone_curve(0.95, endpoint_curve_state);
  if (!(endpoint_curve_low > 0.05 && endpoint_curve_high < 0.95)) {
    return fail("master point curve endpoints should adjust black and white points");
  }

  const auto perceptual_gray_input = linear_pixel_from_srgb(QColor::fromRgbF(0.5F, 0.5F, 0.5F, 1.0F));
  const auto perceptual_curve_output = srgb_from_linear_pixel(apply_single_pixel(perceptual_gray_input, point_curve_state));
  if (!(perceptual_curve_output.redF() > 0.70F &&
        almost_equal(perceptual_curve_output.redF(), 0.75, 0.03) &&
        almost_equal(perceptual_curve_output.redF(), perceptual_curve_output.greenF(), 1e-5) &&
        almost_equal(perceptual_curve_output.greenF(), perceptual_curve_output.blueF(), 1e-5))) {
    return fail("master point curves should map visual mid-gray in perceptual space");
  }

  const auto low_gray_input = linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F));
  const auto low_gray_neutral = srgb_from_linear_pixel(apply_single_pixel(low_gray_input, ColorAdjustmentState{}));
  const auto endpoint_curve_output = srgb_from_linear_pixel(apply_single_pixel(low_gray_input, endpoint_curve_state));
  if (!(endpoint_curve_output.redF() > low_gray_neutral.redF() + 0.05F &&
        almost_equal(endpoint_curve_output.redF(), endpoint_curve_output.greenF(), 1e-5) &&
        almost_equal(endpoint_curve_output.greenF(), endpoint_curve_output.blueF(), 1e-5))) {
    return fail("endpoint-only master point curves should affect rendered output");
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

  ColorAdjustmentState positive_contrast_state;
  positive_contrast_state.contrast = 100.0;
  const auto positive_mid = apply_single_pixel(
      ColorPixel{.red = 0.35F, .green = 0.35F, .blue = 0.35F, .alpha = 1.0F},
      positive_contrast_state);
  const auto positive_high = apply_single_pixel(
      ColorPixel{.red = 0.95F, .green = 0.95F, .blue = 0.95F, .alpha = 1.0F},
      positive_contrast_state);
  if (!(stop_delta(0.35, positive_mid) > stop_delta(0.95, positive_high) + 0.08)) {
    return fail("positive contrast should focus more on midtones than highlight extremes");
  }

  ColorAdjustmentState negative_contrast_state;
  negative_contrast_state.contrast = -100.0;
  const auto negative_mid = apply_single_pixel(
      ColorPixel{.red = 0.35F, .green = 0.35F, .blue = 0.35F, .alpha = 1.0F},
      negative_contrast_state);
  const auto negative_high = apply_single_pixel(
      ColorPixel{.red = 0.95F, .green = 0.95F, .blue = 0.95F, .alpha = 1.0F},
      negative_contrast_state);
  if (!(stop_delta(0.35, negative_mid) > stop_delta(0.95, negative_high) + 0.08)) {
    return fail("negative contrast should flatten midtones more than bright highlights");
  }

  ColorAdjustmentState brightness_state;
  brightness_state.brightness = 70.0;
  const auto brightness_shadow = apply_single_pixel(
      ColorPixel{.red = 0.03F, .green = 0.03F, .blue = 0.03F, .alpha = 1.0F},
      brightness_state);
  const auto brightness_mid = apply_single_pixel(
      ColorPixel{.red = 0.35F, .green = 0.35F, .blue = 0.35F, .alpha = 1.0F},
      brightness_state);
  const auto brightness_high = apply_single_pixel(
      ColorPixel{.red = 1.20F, .green = 1.20F, .blue = 1.20F, .alpha = 1.0F},
      brightness_state);
  if (!(stop_delta(0.35, brightness_mid) > stop_delta(0.03, brightness_shadow) + 0.12 &&
        stop_delta(0.35, brightness_mid) > stop_delta(1.20, brightness_high) + 0.12)) {
    return fail("brightness should lift midtones more strongly than protected shadows and highlights");
  }

  const auto extended_highlights = apply_pixels(
      {
          ColorPixel{.red = 1.05F, .green = 0.52F, .blue = 0.24F, .alpha = 1.0F},
          ColorPixel{.red = 1.35F, .green = 0.52F, .blue = 0.24F, .alpha = 1.0F},
      },
      2,
      1,
      ColorAdjustmentState{});
  if (!(extended_highlights[1].red > extended_highlights[0].red + 0.10F &&
        luminance(extended_highlights[1]) > luminance(extended_highlights[0]) + 0.02)) {
    return fail("over-range chromatic highlights should not collapse at the LUT edge");
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
  hue_state.color_mixer_channels[0].hue = 35.0;  // Reds channel
  const auto red_adjusted = apply_single_pixel(red_input, hue_state);
  const QColor adjusted_red = srgb_from_linear_pixel(red_adjusted);
  if (almost_equal(adjusted_red.hslHueF(), 0.0, 0.01)) {
    return fail("hue adjustment should move matching channel hues");
  }

  const auto blue_input = linear_pixel_from_srgb(QColor::fromHslF(0.63F, 0.35F, 0.50F, 1.0F));
  const auto neutral_blue = srgb_from_linear_pixel(apply_single_pixel(blue_input, ColorAdjustmentState{}));
  ColorAdjustmentState multi_channel_mixer_state;
  multi_channel_mixer_state.color_mixer_channels[0].hue = 35.0;         // Reds channel
  multi_channel_mixer_state.color_mixer_channels[5].saturation = 50.0;  // Blues channel
  const auto multi_red_adjusted = srgb_from_linear_pixel(apply_single_pixel(red_input, multi_channel_mixer_state));
  const auto multi_blue_adjusted = srgb_from_linear_pixel(apply_single_pixel(blue_input, multi_channel_mixer_state));
  if (almost_equal(multi_red_adjusted.hslHueF(), 0.0, 0.01)) {
    return fail("color mixer should preserve red-channel edits while other channels are adjusted");
  }
  if (!(multi_blue_adjusted.hslSaturationF() > neutral_blue.hslSaturationF() + 0.02)) {
    return fail("color mixer should apply blue-channel saturation without discarding red-channel edits");
  }

  const auto dark_input = linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.12F, 0.12F, 1.0F));
  const auto neutral_dark = apply_single_pixel(dark_input, ColorAdjustmentState{});
  ColorAdjustmentState black_state;
  black_state.blacks = 70.0;
  const auto lifted_dark = apply_single_pixel(dark_input, black_state);
  if (!(luminance(lifted_dark) > luminance(neutral_dark))) {
    return fail("positive blacks should lift dark tones");
  }

  const ColorPixel near_black_color{
      .red = 8e-7F,
      .green = 1e-7F,
      .blue = 4e-7F,
      .alpha = 1.0F,
  };
  ColorAdjustmentState lifted_color_state;
  lifted_color_state.blacks = 100.0;
  const auto lifted_near_black_color = apply_single_pixel(near_black_color, lifted_color_state);
  if (!(lifted_near_black_color.red > lifted_near_black_color.blue &&
        lifted_near_black_color.blue > lifted_near_black_color.green)) {
    return fail("lifting near-black color should preserve chromatic ordering instead of turning gray");
  }

  const auto bright_input = linear_pixel_from_srgb(QColor::fromRgbF(0.82F, 0.82F, 0.82F, 1.0F));
  const auto neutral_bright = apply_single_pixel(bright_input, ColorAdjustmentState{});
  ColorAdjustmentState white_state;
  white_state.whites = 70.0;
  const auto brightened_highlight = apply_single_pixel(bright_input, white_state);
  if (!(luminance(brightened_highlight) > luminance(neutral_bright))) {
    return fail("positive whites should brighten bright tones");
  }

  const auto hdr_scene = std::vector<ColorPixel>{
      ColorPixel{.red = 0.22F, .green = 0.22F, .blue = 0.22F, .alpha = 1.0F},
      ColorPixel{.red = 2.10F, .green = 2.10F, .blue = 2.10F, .alpha = 1.0F},
      ColorPixel{.red = 3.20F, .green = 3.20F, .blue = 3.20F, .alpha = 1.0F},
      ColorPixel{.red = 2.10F, .green = 2.10F, .blue = 2.10F, .alpha = 1.0F},
      ColorPixel{.red = 0.22F, .green = 0.22F, .blue = 0.22F, .alpha = 1.0F},
  };
  const auto hdr_global_mapped = apply_pixels(hdr_scene, 5, 1, ColorAdjustmentState{});
  ColorAdjustmentState hdr_local_tone_state;
  hdr_local_tone_state.color_pipeline.output_headroom = 4.0;
  hdr_local_tone_state.hdr_local_tone_mapping = 100.0;
  ColorAdjustmentState hdr_headroom_state;
  hdr_headroom_state.color_pipeline.output_headroom = 4.0;
  const auto hdr_headroom_mapped = apply_pixels(hdr_scene, 5, 1, hdr_headroom_state);
  const auto hdr_locally_mapped = apply_pixels(hdr_scene, 5, 1, hdr_local_tone_state);
  if (!(luminance(hdr_global_mapped[2]) <= 1.0 &&
        luminance(hdr_locally_mapped[2]) + 0.01 < luminance(hdr_headroom_mapped[2]) &&
        luminance(hdr_locally_mapped[2]) > luminance(hdr_locally_mapped[1]))) {
    return fail("HDR local tone mapping should locally compress bright regions while preserving highlight detail order");
  }

  ColorAdjustmentState strong_shoulder_state;
  strong_shoulder_state.exposure = 0.75;
  strong_shoulder_state.filmic_shoulder = 80.0;
  const auto strong_shoulder_adjusted = apply_single_pixel(bright_input, strong_shoulder_state);
  ColorAdjustmentState soft_shoulder_state = strong_shoulder_state;
  soft_shoulder_state.filmic_shoulder = -80.0;
  const auto soft_shoulder_adjusted = apply_single_pixel(bright_input, soft_shoulder_state);
  if (!(luminance(strong_shoulder_adjusted) + 0.02 < luminance(soft_shoulder_adjusted))) {
    return fail("filmic shoulder should control highlight rolloff strength");
  }

  ColorAdjustmentState lifted_toe_state;
  lifted_toe_state.filmic_toe = 80.0;
  const auto toe_lifted_dark = apply_single_pixel(dark_input, lifted_toe_state);
  ColorAdjustmentState crushed_toe_state;
  crushed_toe_state.filmic_toe = -80.0;
  const auto toe_crushed_dark = apply_single_pixel(dark_input, crushed_toe_state);
  if (!(luminance(toe_lifted_dark) > luminance(neutral_dark) &&
        luminance(toe_crushed_dark) < luminance(neutral_dark))) {
    return fail("filmic toe should lift or deepen protected shadows");
  }

  const ColorPixel adaptive_shadow_input{.red = 0.10F, .green = 0.10F, .blue = 0.10F, .alpha = 1.0F};
  const ColorPixel adaptive_mid_input{.red = 0.38F, .green = 0.38F, .blue = 0.38F, .alpha = 1.0F};
  ColorAdjustmentState positive_adaptive_state;
  positive_adaptive_state.adaptive_contrast = 80.0;
  const auto adaptive_shadow_deepened = apply_single_pixel(adaptive_shadow_input, positive_adaptive_state);
  const auto adaptive_mid_brightened = apply_single_pixel(adaptive_mid_input, positive_adaptive_state);
  ColorAdjustmentState negative_adaptive_state;
  negative_adaptive_state.adaptive_contrast = -80.0;
  const auto adaptive_shadow_lifted = apply_single_pixel(adaptive_shadow_input, negative_adaptive_state);
  const auto adaptive_mid_compressed = apply_single_pixel(adaptive_mid_input, negative_adaptive_state);
  if (!(luminance(adaptive_shadow_deepened) < luminance(adaptive_shadow_input) &&
        luminance(adaptive_mid_brightened) > luminance(adaptive_mid_input) &&
        luminance(adaptive_shadow_lifted) > luminance(adaptive_shadow_input) &&
        luminance(adaptive_mid_compressed) < luminance(adaptive_mid_input))) {
    return fail("adaptive contrast should expand or compress protected midtone separation");
  }

  ColorAdjustmentState levels_state;
  levels_state.levels_black_point = 30.0;
  levels_state.levels_white_point = 30.0;
  const auto levels_shadow = apply_single_pixel(
      ColorPixel{.red = 0.08F, .green = 0.08F, .blue = 0.08F, .alpha = 1.0F},
      levels_state);
  const auto levels_highlight = apply_single_pixel(
      ColorPixel{.red = 0.86F, .green = 0.86F, .blue = 0.86F, .alpha = 1.0F},
      levels_state);
  if (!(luminance(levels_shadow) < 0.02 && luminance(levels_highlight) > 0.90)) {
    return fail("levels black and white points should remap tonal endpoints");
  }
  ColorAdjustmentState bright_gamma_state;
  bright_gamma_state.levels_gamma = 70.0;
  ColorAdjustmentState dark_gamma_state;
  dark_gamma_state.levels_gamma = -70.0;
  const auto gamma_brightened = apply_single_pixel(neutral_input, bright_gamma_state);
  const auto gamma_darkened = apply_single_pixel(neutral_input, dark_gamma_state);
  if (!(luminance(gamma_brightened) > luminance(neutral_input) &&
        luminance(gamma_darkened) < luminance(neutral_input))) {
    return fail("levels gamma should brighten or darken midtones");
  }

  const ColorPixel zone_deep_input{.red = 0.016F, .green = 0.016F, .blue = 0.016F, .alpha = 1.0F};
  const ColorPixel zone_shadow_input{.red = 0.055F, .green = 0.055F, .blue = 0.055F, .alpha = 1.0F};
  const ColorPixel zone_mid_input{.red = 0.18F, .green = 0.18F, .blue = 0.18F, .alpha = 1.0F};
  const ColorPixel zone_light_input{.red = 0.58F, .green = 0.58F, .blue = 0.58F, .alpha = 1.0F};
  const ColorPixel zone_high_input{.red = 1.75F, .green = 1.75F, .blue = 1.75F, .alpha = 1.0F};
  ColorAdjustmentState zone_mid_state;
  zone_mid_state.zone_midtones = 80.0;
  const auto zone_mid_lifted = apply_single_pixel(zone_mid_input, zone_mid_state);
  const auto zone_shadow_with_mid = apply_single_pixel(zone_shadow_input, zone_mid_state);
  if (!(stop_delta(0.18, zone_mid_lifted) > stop_delta(0.055, zone_shadow_with_mid) + 0.25)) {
    return fail("zone midtones should target mid-gray more strongly than shadows");
  }
  ColorAdjustmentState zone_deep_state;
  zone_deep_state.zone_deep_shadows = 80.0;
  const auto zone_deep_lifted = apply_single_pixel(zone_deep_input, zone_deep_state);
  const auto zone_mid_with_deep = apply_single_pixel(zone_mid_input, zone_deep_state);
  if (!(stop_delta(0.016, zone_deep_lifted) > stop_delta(0.18, zone_mid_with_deep) + 0.25)) {
    return fail("zone deep shadows should target near-black values more strongly than midtones");
  }
  ColorAdjustmentState zone_high_state;
  zone_high_state.color_pipeline.output_headroom = 4.0;
  zone_high_state.zone_highlights = 80.0;
  const auto zone_high_lifted = apply_single_pixel(zone_high_input, zone_high_state);
  const auto zone_light_with_high = apply_single_pixel(zone_light_input, zone_high_state);
  if (!(stop_delta(1.75, zone_high_lifted) > stop_delta(0.58, zone_light_with_high) + 0.20)) {
    return fail("zone highlights should target overrange highlights more strongly than lights");
  }
  ColorAdjustmentState zone_lights_state;
  zone_lights_state.zone_lights = -80.0;
  const auto zone_light_darkened = apply_single_pixel(zone_light_input, zone_lights_state);
  if (!(luminance(zone_light_darkened) < luminance(zone_light_input))) {
    return fail("zone lights should support signed darkening");
  }

  const auto neutral_gray_input = linear_pixel_from_srgb(QColor::fromRgbF(0.5F, 0.5F, 0.5F, 1.0F));
  ColorAdjustmentState warm_wb_state;
  warm_wb_state.temperature = 100.0;
  const auto warm_balanced = apply_single_pixel(neutral_gray_input, warm_wb_state);
  if (!(warm_balanced.red > warm_balanced.green && warm_balanced.green > warm_balanced.blue)) {
    return fail("positive temperature should warm neutrals without introducing a purple cast");
  }

  ColorAdjustmentState cool_wb_state;
  cool_wb_state.temperature = -100.0;
  const auto cool_balanced = apply_single_pixel(neutral_gray_input, cool_wb_state);
  if (!(cool_balanced.blue > cool_balanced.green && cool_balanced.green > cool_balanced.red)) {
    return fail("negative temperature should cool neutrals without introducing a green cast");
  }

  ColorAdjustmentState magenta_tint_state;
  magenta_tint_state.tint = 100.0;
  const auto magenta_tinted = apply_single_pixel(neutral_gray_input, magenta_tint_state);
  if (!(magenta_tinted.red > magenta_tinted.green &&
        magenta_tinted.blue > magenta_tinted.green)) {
    return fail("positive tint should move neutral tones toward magenta");
  }

  ColorAdjustmentState green_tint_state;
  green_tint_state.tint = -100.0;
  const auto green_tinted = apply_single_pixel(neutral_gray_input, green_tint_state);
  if (!(green_tinted.green > green_tinted.red &&
        green_tinted.green > green_tinted.blue)) {
    return fail("negative tint should move neutral tones toward green");
  }

  const ColorPixel tonal_shadow_input{.red = 0.06F, .green = 0.06F, .blue = 0.06F, .alpha = 1.0F};
  const ColorPixel tonal_highlight_input{.red = 0.82F, .green = 0.82F, .blue = 0.82F, .alpha = 1.0F};
  ColorAdjustmentState positive_shadows_state;
  positive_shadows_state.shadows = 100.0;
  ColorAdjustmentState negative_shadows_state;
  negative_shadows_state.shadows = -100.0;
  if (!(luminance(apply_single_pixel(tonal_shadow_input, positive_shadows_state)) >
            luminance(tonal_shadow_input) &&
        luminance(apply_single_pixel(tonal_shadow_input, negative_shadows_state)) <
            luminance(tonal_shadow_input))) {
    return fail("signed shadows should brighten positive values and darken negative values");
  }

  ColorAdjustmentState positive_highlights_state;
  positive_highlights_state.highlights = 100.0;
  ColorAdjustmentState negative_highlights_state;
  negative_highlights_state.highlights = -100.0;
  if (!(luminance(apply_single_pixel(tonal_highlight_input, positive_highlights_state)) >
            luminance(tonal_highlight_input) &&
        luminance(apply_single_pixel(tonal_highlight_input, negative_highlights_state)) <
            luminance(tonal_highlight_input))) {
    return fail("signed highlights should brighten positive values and darken negative values");
  }

  const auto vivid_red_input = linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.95F, 0.55F, 1.0F));
  ColorAdjustmentState full_desat_state;
  full_desat_state.saturation = -100.0;
  const auto desaturated_red = apply_single_pixel(vivid_red_input, full_desat_state);
  if (!almost_equal(desaturated_red.red, desaturated_red.green, 1e-5) ||
      !almost_equal(desaturated_red.green, desaturated_red.blue, 1e-5)) {
    return fail("saturation -100 should fully remove chroma");
  }

  ColorAdjustmentState gamut_state;
  gamut_state.saturation = 100.0;
  gamut_state.color_mixer_channels[0].saturation = 100.0;  // Reds channel
  const auto gamut_adjusted = apply_single_pixel(vivid_red_input, gamut_state);
  if (gamut_adjusted.red < -1e-6 || gamut_adjusted.red > 1.0 + 1e-6 ||
      gamut_adjusted.green < -1e-6 || gamut_adjusted.green > 1.0 + 1e-6 ||
      gamut_adjusted.blue < -1e-6 || gamut_adjusted.blue > 1.0 + 1e-6) {
    return fail("perceptual gamut fitting should keep output RGB in range");
  }

  const auto bright_yellow_input = linear_pixel_from_srgb(QColor::fromRgbF(1.0F, 0.96F, 0.42F, 1.0F));
  ColorAdjustmentState yellow_gamut_state;
  yellow_gamut_state.saturation = 100.0;
  const auto yellow_gamut_adjusted = srgb_from_linear_pixel(apply_single_pixel(bright_yellow_input, yellow_gamut_state));
  if (!(yellow_gamut_adjusted.redF() > 0.80F &&
        yellow_gamut_adjusted.greenF() > 0.75F &&
        yellow_gamut_adjusted.blueF() < 0.55F)) {
    return fail("gamut fitting should preserve vivid bright yellow instead of washing it toward white");
  }

  const auto shoulder_input = linear_pixel_from_srgb(QColor::fromRgbF(0.95F, 0.95F, 0.95F, 1.0F));
  ColorAdjustmentState fast_shoulder_state;
  fast_shoulder_state.exposure = 1.5;
  const auto fast_shoulder_adjusted = apply_single_pixel(shoulder_input, fast_shoulder_state);
  ColorAdjustmentState slow_shoulder_state = fast_shoulder_state;
  slow_shoulder_state.linear_mask_enabled = true;  // Forces the slow path with a no-op local adjustment.
  const auto slow_shoulder_adjusted = apply_single_pixel(shoulder_input, slow_shoulder_state);
  if (!almost_equal(luminance(fast_shoulder_adjusted), luminance(slow_shoulder_adjusted), 1e-5)) {
    return fail("fast and slow path highlight shoulder should match for the same exposure edit");
  }

  ColorAdjustmentState exposure_state;
  exposure_state.exposure = 1.0;
  const auto shoulder_adjusted = apply_single_pixel(shoulder_input, exposure_state);
  if (!(luminance(shoulder_adjusted) > luminance(shoulder_input))) {
    return fail("positive exposure should still brighten highlights");
  }
  if (!(luminance(shoulder_adjusted) < 0.999999)) {
    return fail("highlight shoulder should keep strong exposure edits below hard clipping");
  }

  const auto warm_highlight_input = linear_pixel_from_srgb(QColor::fromRgbF(0.95F, 0.88F, 0.76F, 1.0F));
  ColorAdjustmentState warm_highlight_state;
  warm_highlight_state.exposure = 1.25;
  warm_highlight_state.whites = 70.0;
  const auto warm_highlight_adjusted = apply_single_pixel(warm_highlight_input, warm_highlight_state);
  if (!(warm_highlight_adjusted.red > warm_highlight_adjusted.green &&
        warm_highlight_adjusted.green > warm_highlight_adjusted.blue)) {
    return fail("bright exposure and whites edits should preserve highlight chroma instead of collapsing toward white");
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

  ColorAdjustmentState luma_curve_state;
  luma_curve_state.luma_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.5, .y = 0.74},
      {.x = 1.0, .y = 1.0},
  };
  const auto warm_color_input = linear_pixel_from_srgb(QColor::fromRgbF(0.62F, 0.42F, 0.25F, 1.0F));
  const auto luma_curve_adjusted = apply_single_pixel(warm_color_input, luma_curve_state);
  const auto source_rg_ratio =
      static_cast<double>(warm_color_input.red) / std::max(1.0e-6, static_cast<double>(warm_color_input.green));
  const auto adjusted_rg_ratio =
      static_cast<double>(luma_curve_adjusted.red) / std::max(1.0e-6, static_cast<double>(luma_curve_adjusted.green));
  if (!(luminance(luma_curve_adjusted) > luminance(warm_color_input) &&
        almost_equal(source_rg_ratio, adjusted_rg_ratio, 0.02))) {
    return fail("luma point curve should adjust luminance while preserving chromatic ratios");
  }

  ColorAdjustmentState hsl_mixer_state;
  hsl_mixer_state.color_mixer_model = "HSL";
  hsl_mixer_state.color_mixer_channel = "Reds";
  hsl_mixer_state.hue_adjustment = 35.0;
  hsl_mixer_state.saturation_adjustment = 24.0;
  hsl_mixer_state.luminance_adjustment = 18.0;
  const auto red_model_input = linear_pixel_from_srgb(QColor::fromRgbF(0.72F, 0.18F, 0.12F, 1.0F));
  const auto hsl_mixer_adjusted = apply_single_pixel(red_model_input, hsl_mixer_state);
  ColorAdjustmentState hsv_mixer_state = hsl_mixer_state;
  hsv_mixer_state.color_mixer_model = "HSV";
  const auto hsv_mixer_adjusted = apply_single_pixel(red_model_input, hsv_mixer_state);
  if (!(luminance(hsl_mixer_adjusted) > luminance(red_model_input) &&
        std::abs(hsl_mixer_adjusted.green - hsv_mixer_adjusted.green) > 1e-5)) {
    return fail("HSL and HSV color mixer modes should apply distinct color model adjustments");
  }

  ColorAdjustmentState selective_color_state;
  selective_color_state.color_mixer_model = "OKLab";
  selective_color_state.color_mixer_channels[0].selective_cyan = 72.0;
  selective_color_state.color_mixer_channels[0].selective_black = 28.0;
  const auto selective_color_adjusted = apply_single_pixel(red_model_input, selective_color_state);
  if (!(selective_color_adjusted.green > red_model_input.green &&
        luminance(selective_color_adjusted) < luminance(red_model_input))) {
    return fail("selective color should tint and darken the selected color range with CMYK controls");
  }

  ColorAdjustmentState channel_mixer_state;
  channel_mixer_state.rgb_channel_mixer.red_from_red = 50.0;
  channel_mixer_state.rgb_channel_mixer.red_from_green = 50.0;
  channel_mixer_state.rgb_channel_mixer.green_from_red = 25.0;
  channel_mixer_state.rgb_channel_mixer.green_from_green = 100.0;
  channel_mixer_state.rgb_channel_mixer.blue_from_blue = 100.0;
  const auto channel_mixer_input = ColorPixel{.red = 0.60F, .green = 0.20F, .blue = 0.10F, .alpha = 1.0F};
  const auto channel_mixer_adjusted = apply_single_pixel(channel_mixer_input, channel_mixer_state);
  if (!(almost_equal(channel_mixer_adjusted.red, 0.40, 1e-5) &&
        almost_equal(channel_mixer_adjusted.green, 0.35, 1e-5) &&
        almost_equal(channel_mixer_adjusted.blue, 0.10, 1e-5))) {
    return fail("RGB channel mixer should remap output channels from input channel contributions");
  }

  ColorAdjustmentState black_and_white_state;
  black_and_white_state.black_and_white_enabled = true;
  const auto black_and_white_red = apply_single_pixel(red_model_input, black_and_white_state);
  if (!(almost_equal(black_and_white_red.red, black_and_white_red.green, 1e-5) &&
        almost_equal(black_and_white_red.green, black_and_white_red.blue, 1e-5))) {
    return fail("black-and-white mixer should convert color pixels to neutral output");
  }
  black_and_white_state.black_and_white_mixer[0] = 60.0;
  const auto brightened_black_and_white_red = apply_single_pixel(red_model_input, black_and_white_state);
  if (!(brightened_black_and_white_red.red > black_and_white_red.red)) {
    return fail("black-and-white mixer should brighten selected hue ranges");
  }

  ColorAdjustmentState color_replacement_state;
  color_replacement_state.color_replacement_enabled = true;
  color_replacement_state.color_replacement_source_hue = 0.0;
  color_replacement_state.color_replacement_target_hue = 0.33;
  color_replacement_state.color_replacement_range = 45.0;
  color_replacement_state.color_replacement_strength = 100.0;
  const auto replaced_red = apply_single_pixel(red_model_input, color_replacement_state);
  if (!(replaced_red.green > replaced_red.red && replaced_red.green > red_model_input.green)) {
    return fail("color replacement should shift matching source hues toward the target hue");
  }
  auto protected_replacement_state = color_replacement_state;
  protected_replacement_state.skin_tone_protection_enabled = true;
  protected_replacement_state.skin_tone_line_hue = 0.0;
  protected_replacement_state.skin_tone_protection_range = 100.0;
  protected_replacement_state.skin_tone_protection_strength = 100.0;
  const auto protected_replaced_red = apply_single_pixel(red_model_input, protected_replacement_state);
  if (!(protected_replaced_red.green < replaced_red.green &&
        protected_replaced_red.red > replaced_red.red)) {
    return fail("skin tone protection should reduce color replacement near the skin tone line");
  }

  ColorAdjustmentState color_harmony_state;
  color_harmony_state.color_harmony_enabled = true;
  color_harmony_state.color_harmony_mode = "Complementary";
  color_harmony_state.color_harmony_base_hue = 0.25;
  color_harmony_state.color_harmony_strength = 100.0;
  const auto harmonized_red = apply_single_pixel(red_model_input, color_harmony_state);
  if (!(harmonized_red.green > red_model_input.green)) {
    return fail("color harmony should pull hues toward the nearest harmony anchor");
  }
  color_harmony_state.skin_tone_protection_enabled = true;
  color_harmony_state.skin_tone_line_hue = 0.0;
  color_harmony_state.skin_tone_protection_range = 100.0;
  color_harmony_state.skin_tone_protection_strength = 100.0;
  const auto protected_harmonized_red = apply_single_pixel(red_model_input, color_harmony_state);
  if (!(protected_harmonized_red.green < harmonized_red.green)) {
    return fail("skin tone protection should reduce color harmony hue attraction near the skin tone line");
  }

  ColorAdjustmentState grading_state;
  grading_state.grading_midtones_hue = 0.0;
  grading_state.grading_midtones_saturation = 100.0;
  const auto graded_gray = apply_single_pixel(gray_input, grading_state);
  if (!(graded_gray.red > graded_gray.green && graded_gray.red > graded_gray.blue)) {
    return fail("midtone color grading should visibly tint neutral pixels");
  }

  const auto edge_dark = linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F));
  const auto edge_bright = linear_pixel_from_srgb(QColor::fromRgbF(0.70F, 0.70F, 0.70F, 1.0F));
  const std::vector<ColorPixel> detail_input = {
      edge_dark, edge_dark, edge_bright, edge_dark, edge_dark,
  };
  ColorAdjustmentState sharpen_state;
  sharpen_state.sharpening = 100.0;
  const auto sharpened_pixels = apply_pixels(detail_input, 5, 1, sharpen_state);
  if (!(luminance(sharpened_pixels[2]) > luminance(detail_input[2]) + 1e-4 ||
        luminance(sharpened_pixels[1]) < luminance(detail_input[1]) - 1e-4)) {
    return fail("sharpening should change local contrast on a simple edge pattern");
  }

  const std::vector<ColorPixel> smooth_texture_input = {
      ColorPixel{.red = 0.2000F, .green = 0.2000F, .blue = 0.2000F, .alpha = 1.0F},
      ColorPixel{.red = 0.2004F, .green = 0.2004F, .blue = 0.2004F, .alpha = 1.0F},
      ColorPixel{.red = 0.2000F, .green = 0.2000F, .blue = 0.2000F, .alpha = 1.0F},
      ColorPixel{.red = 0.2004F, .green = 0.2004F, .blue = 0.2004F, .alpha = 1.0F},
      ColorPixel{.red = 0.2000F, .green = 0.2000F, .blue = 0.2000F, .alpha = 1.0F},
  };
  ColorAdjustmentState masked_sharpen_state;
  masked_sharpen_state.sharpening = 100.0;
  masked_sharpen_state.sharpen_masking = 100.0;
  const auto unmasked_smooth_pixels = apply_pixels(smooth_texture_input, 5, 1, sharpen_state);
  const auto masked_smooth_pixels = apply_pixels(smooth_texture_input, 5, 1, masked_sharpen_state);
  const auto unmasked_smooth_change =
      std::abs(luminance(unmasked_smooth_pixels[1]) - luminance(smooth_texture_input[1]));
  const auto masked_smooth_change =
      std::abs(luminance(masked_smooth_pixels[1]) - luminance(smooth_texture_input[1]));
  if (!(masked_smooth_change < unmasked_smooth_change * 0.25)) {
    return fail("sharpen masking should protect low-contrast texture from sharpening");
  }
  const auto masked_edge_pixels = apply_pixels(detail_input, 5, 1, masked_sharpen_state);
  const auto masked_edge_change =
      std::abs(luminance(masked_edge_pixels[2]) - luminance(detail_input[2])) +
      std::abs(luminance(masked_edge_pixels[1]) - luminance(detail_input[1]));
  if (!(masked_edge_change > 1e-4)) {
    return fail("sharpen masking should still allow sharpening on strong edges");
  }

  const std::vector<ColorPixel> presence_input = {
      edge_dark, edge_dark, edge_dark, edge_bright, edge_bright, edge_bright, edge_dark, edge_dark, edge_dark,
  };
  const auto base_presence_contrast =
      luminance(presence_input[3]) - luminance(presence_input[2]);

  ColorAdjustmentState texture_state;
  texture_state.texture = 100.0;
  const auto textured_pixels = apply_pixels(presence_input, 9, 1, texture_state);
  if (!((luminance(textured_pixels[3]) - luminance(textured_pixels[2])) > base_presence_contrast + 1e-4)) {
    return fail("texture should increase fine local contrast on a small edge transition");
  }

  ColorAdjustmentState clarity_state;
  clarity_state.clarity = 100.0;
  const auto clarified_pixels = apply_pixels(presence_input, 9, 1, clarity_state);
  if (!((luminance(clarified_pixels[3]) - luminance(clarified_pixels[2])) > base_presence_contrast + 1e-4)) {
    return fail("clarity should increase midtone edge contrast on a broader transition");
  }

  ColorAdjustmentState dehaze_state;
  dehaze_state.dehaze = 100.0;
  const auto dehazed_pixels = apply_pixels(presence_input, 9, 1, dehaze_state);
  if (!((luminance(dehazed_pixels[4]) - luminance(dehazed_pixels[0])) >
        (luminance(presence_input[4]) - luminance(presence_input[0])) + 1e-4)) {
    return fail("dehaze should increase separation between brighter structure and a flatter surround");
  }

  ColorAdjustmentState luma_nr_state;
  luma_nr_state.luminance_nr = 100.0;
  std::vector<ColorPixel> luma_noise_input;
  luma_noise_input.reserve(25);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      const auto tone = ((x + y) % 2) == 0 ? 0.204F : 0.20F;
      luma_noise_input.push_back(ColorPixel{
          .red = tone,
          .green = tone,
          .blue = tone,
          .alpha = 1.0F,
      });
    }
  }
  const auto denoised_luma_pixels = apply_pixels(luma_noise_input, 5, 5, luma_nr_state);
  auto input_range = 0.0;
  auto denoised_range = 0.0;
  for (std::size_t i = 0; i < luma_noise_input.size(); ++i) {
    input_range = std::max(input_range, std::abs(luminance(luma_noise_input[i]) - luminance(luma_noise_input[0])));
    denoised_range =
        std::max(denoised_range, std::abs(luminance(denoised_luma_pixels[i]) - luminance(denoised_luma_pixels[0])));
  }
  if (!(denoised_range < input_range - 1e-6)) {
    return fail("luminance noise reduction should pull isolated brightness noise toward neighboring tones");
  }

  const auto neutral_gray = linear_pixel_from_srgb(QColor::fromRgbF(0.45F, 0.45F, 0.45F, 1.0F));
  std::vector<ColorPixel> color_noise_input(5, neutral_gray);
  color_noise_input[2] = linear_pixel_from_srgb(QColor::fromRgbF(0.72F, 0.40F, 0.40F, 1.0F));
  ColorAdjustmentState color_nr_state;
  color_nr_state.color_nr = 100.0;
  const auto denoised_color_pixels = apply_pixels(color_noise_input, 5, 1, color_nr_state);
  const auto base_color_gap = std::abs(color_noise_input[2].red - color_noise_input[2].green);
  const auto denoised_color_gap = std::abs(denoised_color_pixels[2].red - denoised_color_pixels[2].green);
  if (!(denoised_color_gap < base_color_gap - 1e-4)) {
    return fail("color noise reduction should reduce isolated chroma speckling");
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

  std::vector<ColorPixel> additive_stack_pixels(9, radial_input);
  ColorAdjustmentState additive_stack_state;
  additive_stack_state.local_masks = {
      LocalMask{
          .kind = "radial",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.18,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.32,
          .radius_y = 0.32,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&additive_stack_pixels, 3, 3, additive_stack_state);

  std::vector<ColorPixel> opacity_limited_pixels(9, radial_input);
  ColorAdjustmentState opacity_limited_state = additive_stack_state;
  opacity_limited_state.local_masks.front().opacity = 0.35;
  photo_editor::pipeline::apply_color_adjustments(&opacity_limited_pixels, 3, 3, opacity_limited_state);
  if (!(luminance(opacity_limited_pixels[4]) < luminance(additive_stack_pixels[4]))) {
    return fail("mask opacity should reduce the strength of local adjustments");
  }

  std::vector<ColorPixel> density_limited_pixels(9, radial_input);
  ColorAdjustmentState density_limited_state = additive_stack_state;
  density_limited_state.local_masks.front().density = 0.4;
  photo_editor::pipeline::apply_color_adjustments(&density_limited_pixels, 3, 3, density_limited_state);
  if (!(luminance(density_limited_pixels[4]) < luminance(additive_stack_pixels[4]))) {
    return fail("mask density should cap the maximum resolved mask weight");
  }

  std::vector<ColorPixel> stacked_mask_pixels(9, radial_input);
  ColorAdjustmentState stacked_mask_state = additive_stack_state;
  stacked_mask_state.local_masks.push_back(
      LocalMask{
          .kind = "radial",
          .operation = "subtract",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.12,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.14,
          .radius_y = 0.14,
      });
  photo_editor::pipeline::apply_color_adjustments(&stacked_mask_pixels, 3, 3, stacked_mask_state);
  if (!(luminance(stacked_mask_pixels[4]) < luminance(additive_stack_pixels[4]))) {
    return fail("subtract mask operations should reduce the center of an additive local selection");
  }

  std::vector<ColorPixel> exclude_mask_pixels(9, radial_input);
  ColorAdjustmentState exclude_mask_state = additive_stack_state;
  exclude_mask_state.local_masks.push_back(
      LocalMask{
          .kind = "radial",
          .operation = "exclude",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.18,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.32,
          .radius_y = 0.32,
      });
  photo_editor::pipeline::apply_color_adjustments(&exclude_mask_pixels, 3, 3, exclude_mask_state);
  if (!(luminance(exclude_mask_pixels[4]) < luminance(additive_stack_pixels[4]))) {
    return fail("exclude mask operations should remove overlapping mask influence");
  }

  std::vector<ColorPixel> replace_mask_pixels(9, radial_input);
  ColorAdjustmentState replace_mask_state = additive_stack_state;
  replace_mask_state.local_masks.push_back(
      LocalMask{
          .kind = "radial",
          .operation = "replace",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.12,
          .center_x = 0.0,
          .center_y = 0.0,
          .radius_x = 0.12,
          .radius_y = 0.12,
      });
  photo_editor::pipeline::apply_color_adjustments(&replace_mask_pixels, 3, 3, replace_mask_state);
  if (!(luminance(replace_mask_pixels[4]) < luminance(additive_stack_pixels[4]))) {
    return fail("replace mask operations should replace the current composite with the source mask");
  }

  std::vector<ColorPixel> union_mask_pixels(9, radial_input);
  ColorAdjustmentState union_mask_state = additive_stack_state;
  union_mask_state.local_masks.front().opacity = 0.5;
  union_mask_state.local_masks.push_back(
      LocalMask{
          .kind = "radial",
          .operation = "union",
          .enabled = true,
          .opacity = 0.5,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.18,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.32,
          .radius_y = 0.32,
      });
  photo_editor::pipeline::apply_color_adjustments(&union_mask_pixels, 3, 3, union_mask_state);
  if (!(luminance(union_mask_pixels[4]) > luminance(opacity_limited_pixels[4]))) {
    return fail("union mask operations should add source influence to the current composite");
  }

  std::vector<ColorPixel> nested_mask_pixels(9, radial_input);
  ColorAdjustmentState nested_mask_state;
  nested_mask_state.local_masks = {
      LocalMask{
          .id = "parent",
          .kind = "radial",
          .operation = "intersect",
          .enabled = true,
          .feather = 0.12,
          .center_x = 0.0,
          .center_y = 0.0,
          .radius_x = 0.14,
          .radius_y = 0.14,
      },
      LocalMask{
          .id = "child",
          .parent_id = "parent",
          .kind = "radial",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.18,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.32,
          .radius_y = 0.32,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&nested_mask_pixels, 3, 3, nested_mask_state);
  if (!(luminance(nested_mask_pixels[4]) < luminance(additive_stack_pixels[4]))) {
    return fail("child masks should be constrained by their parent mask weight");
  }

  std::vector<ColorPixel> inverted_mask_pixels(9, radial_input);
  ColorAdjustmentState inverted_mask_state = additive_stack_state;
  inverted_mask_state.local_masks.front().invert = true;
  photo_editor::pipeline::apply_color_adjustments(&inverted_mask_pixels, 3, 3, inverted_mask_state);
  if (!(luminance(inverted_mask_pixels[0]) > luminance(inverted_mask_pixels[4]))) {
    return fail("mask invert should move local adjustments outside the original mask");
  }

  std::vector<ColorPixel> grouped_stack_pixels(9, radial_input);
  ColorAdjustmentState grouped_stack_state;
  grouped_stack_state.local_mask_sets = {
      LocalMaskSet{
          .id = "subject",
          .name = "Subject",
          .enabled = true,
          .exposure = 1.0,
          .sources = {
              LocalMask{
                  .kind = "radial",
                  .operation = "add",
                  .enabled = true,
                  .feather = 0.18,
                  .center_x = 0.5,
                  .center_y = 0.5,
                  .radius_x = 0.32,
                  .radius_y = 0.32,
              },
          },
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&grouped_stack_pixels, 3, 3, grouped_stack_state);
  if (!(luminance(grouped_stack_pixels[4]) > luminance(grouped_stack_pixels[0]))) {
    return fail("grouped mask sets should apply set-level adjustments through their contributor sources");
  }

  std::vector<ColorPixel> grouped_subtract_pixels(9, radial_input);
  ColorAdjustmentState grouped_subtract_state = grouped_stack_state;
  grouped_subtract_state.local_mask_sets.front().sources.push_back(
      LocalMask{
          .kind = "radial",
          .operation = "subtract",
          .enabled = true,
          .feather = 0.12,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.14,
          .radius_y = 0.14,
      });
  photo_editor::pipeline::apply_color_adjustments(&grouped_subtract_pixels, 3, 3, grouped_subtract_state);
  if (!(luminance(grouped_subtract_pixels[4]) < luminance(grouped_stack_pixels[4]))) {
    return fail("grouped mask sets should compose add and subtract contributors inside one set");
  }

  std::vector<ColorPixel> grouped_intersect_pixels(9, radial_input);
  ColorAdjustmentState grouped_intersect_state = grouped_stack_state;
  grouped_intersect_state.local_mask_sets.front().sources.push_back(
      LocalMask{
          .kind = "radial",
          .operation = "intersect",
          .enabled = true,
          .feather = 0.12,
          .center_x = 0.15,
          .center_y = 0.15,
          .radius_x = 0.18,
          .radius_y = 0.18,
      });
  photo_editor::pipeline::apply_color_adjustments(&grouped_intersect_pixels, 3, 3, grouped_intersect_state);
  if (!(luminance(grouped_intersect_pixels[4]) < luminance(grouped_stack_pixels[4]))) {
    return fail("grouped mask sets should support boolean intersection contributors");
  }

  std::vector<ColorPixel> grouped_blend_pixels(9, radial_input);
  ColorAdjustmentState grouped_blend_state = grouped_stack_state;
  grouped_blend_state.local_mask_sets.front().sources.front().opacity = 0.5;
  grouped_blend_state.local_mask_sets.front().sources.push_back(
      LocalMask{
          .kind = "radial",
          .operation = "add",
          .blend_mode = "multiply",
          .enabled = true,
          .opacity = 0.5,
          .feather = 0.18,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.32,
          .radius_y = 0.32,
      });
  photo_editor::pipeline::apply_color_adjustments(&grouped_blend_pixels, 3, 3, grouped_blend_state);
  if (!(luminance(grouped_blend_pixels[4]) < luminance(grouped_stack_pixels[4]))) {
    return fail("mask blend mode should participate in contributor compositing");
  }

  std::vector<ColorPixel> subtract_first_pixels(9, radial_input);
  ColorAdjustmentState subtract_first_state = additive_stack_state;
  subtract_first_state.local_masks.insert(
      subtract_first_state.local_masks.begin(),
      LocalMask{
          .kind = "radial",
          .operation = "subtract",
          .enabled = true,
          .exposure = 0.2,
          .temperature = 0.0,
          .feather = 0.12,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.14,
          .radius_y = 0.14,
      });
  photo_editor::pipeline::apply_color_adjustments(&subtract_first_pixels, 3, 3, subtract_first_state);
  if (!almost_equal(luminance(subtract_first_pixels[4]), luminance(additive_stack_pixels[4]), 1e-5)) {
    return fail("leading subtract masks should not seed a composite selection or override the stack adjustment amount");
  }

  std::vector<ColorPixel> brush_pixels(9, radial_input);
  ColorAdjustmentState brush_state;
  brush_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.75,
          .brush_size = 0.18,
          .brush_flow = 1.0,
          .brush_points = {{.x = 0.5, .y = 0.5, .size = 0.18, .flow = 1.0}},
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&brush_pixels, 3, 3, brush_state);
  if (!(luminance(brush_pixels[4]) > luminance(brush_pixels[0]))) {
    return fail("brush masks should affect painted regions more strongly than untouched corners");
  }

  std::vector<ColorPixel> soft_brush_pixels(
      5,
      linear_pixel_from_srgb(QColor::fromRgbF(0.25F, 0.25F, 0.25F, 1.0F)));
  auto hard_brush_pixels = soft_brush_pixels;
  ColorAdjustmentState soft_brush_state;
  soft_brush_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .brush_size = 0.5,
          .brush_flow = 1.0,
          .brush_hardness = 0.1,
          .brush_points = {{.x = 0.5, .y = 0.5, .size = 0.5, .flow = 1.0}},
      },
  };
  auto hard_brush_state = soft_brush_state;
  hard_brush_state.local_masks.front().brush_hardness = 0.8;
  photo_editor::pipeline::apply_color_adjustments(&soft_brush_pixels, 5, 1, soft_brush_state);
  photo_editor::pipeline::apply_color_adjustments(&hard_brush_pixels, 5, 1, hard_brush_state);
  if (!(luminance(hard_brush_pixels[3]) > luminance(soft_brush_pixels[3]))) {
    return fail("brush hardness should preserve more opacity near the dab edge");
  }

  std::vector<ColorPixel> buildup_pixels(
      3,
      linear_pixel_from_srgb(QColor::fromRgbF(0.25F, 0.25F, 0.25F, 1.0F)));
  auto single_pass_pixels = buildup_pixels;
  ColorAdjustmentState buildup_state;
  buildup_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .brush_size = 0.5,
          .brush_flow = 0.5,
          .brush_hardness = 0.8,
          .brush_buildup = true,
          .brush_points = {
              {.x = 0.5, .y = 0.5, .size = 0.5, .flow = 0.5},
              {.x = 0.5, .y = 0.5, .size = 0.5, .flow = 0.5},
          },
      },
  };
  auto single_pass_state = buildup_state;
  single_pass_state.local_masks.front().brush_buildup = false;
  photo_editor::pipeline::apply_color_adjustments(&buildup_pixels, 3, 1, buildup_state);
  photo_editor::pipeline::apply_color_adjustments(&single_pass_pixels, 3, 1, single_pass_state);
  if (!(luminance(buildup_pixels[1]) > luminance(single_pass_pixels[1]))) {
    return fail("brush flow buildup should compound overlapping dab opacity when enabled");
  }

  std::vector<ColorPixel> paint_only_pixels(
      3,
      linear_pixel_from_srgb(QColor::fromRgbF(0.25F, 0.25F, 0.25F, 1.0F)));
  auto erase_mode_pixels = paint_only_pixels;
  ColorAdjustmentState paint_only_state;
  paint_only_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .brush_size = 0.5,
          .brush_flow = 1.0,
          .brush_hardness = 0.8,
          .brush_buildup = true,
          .brush_points = {{.x = 0.5, .y = 0.5, .size = 0.5, .flow = 1.0}},
      },
  };
  auto erase_mode_state = paint_only_state;
  erase_mode_state.local_masks.front().brush_points.push_back(
      BrushPoint{.x = 0.5, .y = 0.5, .size = 0.5, .flow = 1.0, .erase = true});
  photo_editor::pipeline::apply_color_adjustments(&paint_only_pixels, 3, 1, paint_only_state);
  photo_editor::pipeline::apply_color_adjustments(&erase_mode_pixels, 3, 1, erase_mode_state);
  if (!(luminance(paint_only_pixels[1]) > luminance(erase_mode_pixels[1]))) {
    return fail("brush erase mode should remove opacity from the current brush stroke");
  }

  std::vector<ColorPixel> lasso_pixels(9, radial_input);
  ColorAdjustmentState lasso_state;
  lasso_state.local_masks = {
      LocalMask{
          .kind = "lasso",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.04,
          .polygon_points = {{.x = 0.1, .y = 0.1}, {.x = 0.9, .y = 0.1}, {.x = 0.9, .y = 0.9}, {.x = 0.1, .y = 0.9}},
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&lasso_pixels, 3, 3, lasso_state);
  if (!(luminance(lasso_pixels[4]) > luminance(lasso_pixels[0]))) {
    return fail("lasso masks should use freehand polygon geometry for local adjustments");
  }

  std::vector<ColorPixel> raster_brush_pixels(9, radial_input);
  ColorAdjustmentState raster_brush_state;
  raster_brush_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .segmentation_mask = {
              0, 0, 0,
              0, 255, 0,
              0, 0, 0,
          },
          .segmentation_width = 3,
          .segmentation_height = 3,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&raster_brush_pixels, 3, 3, raster_brush_state);
  if (!(luminance(raster_brush_pixels[4]) > luminance(raster_brush_pixels[0]))) {
    return fail("brush masks should honor raster mask payloads when present");
  }

  ColorAdjustmentState luminance_mask_state;
  luminance_mask_state.local_masks = {
      LocalMask{
          .kind = "luminance",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .luminance_min = 0.6,
          .luminance_max = 1.0,
          .range_feather = 0.08,
      },
  };
  const auto dark_luma_adjusted =
      apply_single_pixel(linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)), luminance_mask_state);
  const auto bright_luma_adjusted =
      apply_single_pixel(linear_pixel_from_srgb(QColor::fromRgbF(0.82F, 0.82F, 0.82F, 1.0F)), luminance_mask_state);
  if (!(luminance(bright_luma_adjusted) > luminance(dark_luma_adjusted))) {
    return fail("luminance range masks should bias local adjustments toward the selected tonal band");
  }

  ColorAdjustmentState color_mask_state;
  color_mask_state.local_masks = {
      LocalMask{
          .kind = "color",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .range_feather = 0.06,
          .hue_center = 0.0,
          .hue_range = 0.12,
          .saturation_min = 0.15,
          .saturation_max = 1.0,
      },
  };
  const auto red_mask_adjusted =
      apply_single_pixel(linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.70F, 0.50F, 1.0F)), color_mask_state);
  const auto blue_mask_adjusted =
      apply_single_pixel(linear_pixel_from_srgb(QColor::fromHslF(0.63F, 0.70F, 0.50F, 1.0F)), color_mask_state);
  if (!(luminance(red_mask_adjusted) > luminance(blue_mask_adjusted))) {
    return fail("color range masks should target matching hues more strongly than distant hues");
  }

  std::vector<ColorPixel> depth_pixels(9, radial_input);
  ColorAdjustmentState depth_mask_state;
  depth_mask_state.local_masks = {
      LocalMask{
          .kind = "depth",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .range_feather = 0.02,
          .depth_min = 0.45,
          .depth_max = 1.0,
          .depth_map = {
              0, 0, 0,
              0, 255, 0,
              0, 0, 0,
          },
          .depth_width = 3,
          .depth_height = 3,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&depth_pixels, 3, 3, depth_mask_state);
  if (!(luminance(depth_pixels[4]) > luminance(depth_pixels[0]))) {
    return fail("depth range masks should target selected depth-map bands");
  }

  std::vector<ColorPixel> polygon_pixels(9, radial_input);
  ColorAdjustmentState polygon_mask_state;
  polygon_mask_state.local_masks = {
      LocalMask{
          .kind = "polygon",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.03,
          .polygon_points = {
              {.x = 0.2, .y = 0.2},
              {.x = 0.8, .y = 0.2},
              {.x = 0.8, .y = 0.8},
              {.x = 0.2, .y = 0.8},
          },
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&polygon_pixels, 3, 3, polygon_mask_state);
  if (!(luminance(polygon_pixels[4]) > luminance(polygon_pixels[0]) &&
        luminance(polygon_pixels[4]) > luminance(polygon_pixels[2]))) {
    return fail("polygon masks should brighten pixels inside the drawn selection more than outside pixels");
  }

  std::vector<ColorPixel> base_object_pixels(5, radial_input);
  ColorAdjustmentState base_object_state;
  base_object_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 0.05,
          .segmentation_mask = {255, 128, 0, 0, 0},
          .segmentation_width = 5,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&base_object_pixels, 5, 1, base_object_state);

  std::vector<ColorPixel> expanded_object_pixels(5, radial_input);
  ColorAdjustmentState expanded_object_state = base_object_state;
  expanded_object_state.local_masks.front().selection_expand = 100.0;
  photo_editor::pipeline::apply_color_adjustments(&expanded_object_pixels, 5, 1, expanded_object_state);
  if (!(luminance(expanded_object_pixels[2]) > luminance(base_object_pixels[2]) + 1e-4)) {
    return fail("selection expand should increase mask influence beyond the original selection edge");
  }

  std::vector<ColorPixel> hard_edge_object_pixels(5, radial_input);
  ColorAdjustmentState hard_edge_object_state = base_object_state;
  hard_edge_object_state.local_masks.front().feather = 1.0;
  hard_edge_object_state.local_masks.front().segmentation_mask = {255, 192, 0, 0, 0};
  photo_editor::pipeline::apply_color_adjustments(&hard_edge_object_pixels, 5, 1, hard_edge_object_state);
  std::vector<ColorPixel> smooth_edge_object_pixels(5, radial_input);
  ColorAdjustmentState smooth_edge_object_state = hard_edge_object_state;
  smooth_edge_object_state.local_masks.front().selection_smooth = 100.0;
  photo_editor::pipeline::apply_color_adjustments(&smooth_edge_object_pixels, 5, 1, smooth_edge_object_state);
  if (!(luminance(smooth_edge_object_pixels[1]) < luminance(hard_edge_object_pixels[1]) - 1e-4)) {
    return fail("selection smooth should soften partially selected mask edges");
  }

  std::vector<ColorPixel> hair_edge_pixels(5, radial_input);
  ColorAdjustmentState hair_edge_state = base_object_state;
  hair_edge_state.local_masks.front().kind = "hair";
  hair_edge_state.local_masks.front().feather = 1.0;
  hair_edge_state.local_masks.front().segmentation_mask = {255, 64, 0, 0, 0};
  photo_editor::pipeline::apply_color_adjustments(&hair_edge_pixels, 5, 1, hair_edge_state);
  std::vector<ColorPixel> refined_hair_edge_pixels(5, radial_input);
  ColorAdjustmentState refined_hair_edge_state = hair_edge_state;
  refined_hair_edge_state.local_masks.front().selection_fine_edge = 100.0;
  photo_editor::pipeline::apply_color_adjustments(&refined_hair_edge_pixels, 5, 1, refined_hair_edge_state);
  if (!(luminance(refined_hair_edge_pixels[1]) > luminance(hair_edge_pixels[1]) + 1e-4)) {
    return fail("fine-edge refinement should recover low-confidence hair and edge strands");
  }

  std::vector<ColorPixel> inverted_object_pixels(5, radial_input);
  ColorAdjustmentState inverted_object_state = base_object_state;
  inverted_object_state.local_masks.front().selection_invert = true;
  photo_editor::pipeline::apply_color_adjustments(&inverted_object_pixels, 5, 1, inverted_object_state);
  if (!(luminance(inverted_object_pixels[4]) > luminance(inverted_object_pixels[0]) + 1e-4)) {
    return fail("selection invert should move local adjustments outside the original object mask");
  }

  std::vector<ColorPixel> object_pixels(3, radial_input);
  ColorAdjustmentState object_mask_state;
  object_mask_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .colorize_hue = 0.0,
          .colorize_strength = 100.0,
          .segmentation_mask = {255, 128, 0},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&object_pixels, 3, 1, object_mask_state);
  if (!(luminance(object_pixels[0]) > luminance(object_pixels[1]) &&
        luminance(object_pixels[1]) > luminance(object_pixels[2]))) {
    return fail("object masks should scale local adjustments from the segmentation matte");
  }
  if (!(object_pixels[0].red > object_pixels[0].green &&
        object_pixels[0].red > object_pixels[0].blue)) {
    return fail("local object masks should support recoloring the selected region");
  }

  std::vector<ColorPixel> semantic_ai_pixels(3, radial_input);
  ColorAdjustmentState semantic_ai_state;
  semantic_ai_state.local_masks = {
      LocalMask{
          .kind = "sky",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .segmentation_mask = {0, 128, 255},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&semantic_ai_pixels, 3, 1, semantic_ai_state);
  if (!(luminance(semantic_ai_pixels[2]) > luminance(semantic_ai_pixels[1]) &&
        luminance(semantic_ai_pixels[1]) > luminance(semantic_ai_pixels[0]))) {
    return fail("semantic AI masks should render from their segmentation matte");
  }

  std::vector<ColorPixel> alpha_mask_pixels(3, radial_input);
  ColorAdjustmentState alpha_mask_state;
  alpha_mask_state.local_masks = {
      LocalMask{
          .kind = "alpha",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .segmentation_mask = {0, 128, 255},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&alpha_mask_pixels, 3, 1, alpha_mask_state);
  if (!(luminance(alpha_mask_pixels[2]) > luminance(alpha_mask_pixels[1]) &&
        luminance(alpha_mask_pixels[1]) > luminance(alpha_mask_pixels[0]))) {
    return fail("custom alpha image masks should render from their imported matte");
  }

  std::vector<ColorPixel> refined_luminance_pixels = {
      ColorPixel{.red = 0.10F, .green = 0.10F, .blue = 0.10F, .alpha = 1.0F},
      ColorPixel{.red = 0.50F, .green = 0.50F, .blue = 0.50F, .alpha = 1.0F},
      ColorPixel{.red = 0.90F, .green = 0.90F, .blue = 0.90F, .alpha = 1.0F},
  };
  ColorAdjustmentState refined_luminance_state;
  refined_luminance_state.local_masks = {
      LocalMask{
          .kind = "linear",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 1.0,
          .position = 1.0,
          .refine_luminance_min = 0.70,
          .refine_luminance_max = 1.0,
          .refine_range_feather = 0.02,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&refined_luminance_pixels, 3, 1, refined_luminance_state);
  if (!(luminance(refined_luminance_pixels[2]) > luminance(refined_luminance_pixels[0]) + 0.10)) {
    return fail("luminance range refinement should constrain local masks to selected brightness");
  }

  std::vector<ColorPixel> refined_color_pixels = {
      linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.80F, 0.45F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromHslF(0.62F, 0.80F, 0.45F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.12F, 0.45F, 1.0F)),
  };
  ColorAdjustmentState refined_color_state;
  refined_color_state.local_masks = {
      LocalMask{
          .kind = "linear",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 1.0,
          .position = 1.0,
          .refine_hue_center = 0.0,
          .refine_hue_range = 0.08,
          .refine_saturation_min = 0.50,
          .refine_saturation_max = 1.0,
          .refine_range_feather = 0.02,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&refined_color_pixels, 3, 1, refined_color_state);
  if (!(luminance(refined_color_pixels[0]) > luminance(refined_color_pixels[1]) + 0.05 &&
        luminance(refined_color_pixels[0]) > luminance(refined_color_pixels[2]) + 0.05)) {
    return fail("hue and saturation range refinement should constrain local masks to selected colors");
  }

  std::vector<ColorPixel> refined_depth_pixels(3, radial_input);
  ColorAdjustmentState refined_depth_state;
  refined_depth_state.local_masks = {
      LocalMask{
          .kind = "linear",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 1.0,
          .position = 1.0,
          .depth_map = {0, 128, 255},
          .depth_width = 3,
          .depth_height = 1,
          .refine_depth_min = 0.45,
          .refine_depth_max = 0.55,
          .refine_range_feather = 0.02,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&refined_depth_pixels, 3, 1, refined_depth_state);
  if (!(luminance(refined_depth_pixels[1]) > luminance(refined_depth_pixels[0]) + 0.05 &&
        luminance(refined_depth_pixels[1]) > luminance(refined_depth_pixels[2]) + 0.05)) {
    return fail("depth range refinement should constrain local masks by depth confidence bands");
  }

  std::vector<ColorPixel> refined_edge_pixels(3, radial_input);
  ColorAdjustmentState refined_edge_state;
  refined_edge_state.local_masks = {
      LocalMask{
          .kind = "alpha",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 0.05,
          .refine_edge_confidence_min = 0.80,
          .segmentation_mask = {255, 128, 0},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&refined_edge_pixels, 3, 1, refined_edge_state);
  if (!(luminance(refined_edge_pixels[0]) > luminance(refined_edge_pixels[1]) + 0.05)) {
    return fail("edge confidence refinement should suppress low-confidence matte edges");
  }

  std::vector<ColorPixel> malformed_object_pixels(3, radial_input);
  ColorAdjustmentState malformed_object_state;
  malformed_object_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .segmentation_mask = {255, 128},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&malformed_object_pixels, 3, 1, malformed_object_state);
  if (!almost_equal(luminance(malformed_object_pixels[0]), luminance(radial_input), 1e-6) ||
      !almost_equal(luminance(malformed_object_pixels[1]), luminance(radial_input), 1e-6) ||
      !almost_equal(luminance(malformed_object_pixels[2]), luminance(radial_input), 1e-6)) {
    return fail("malformed segmentation masks should be ignored instead of partially applying local adjustments");
  }

  std::vector<ColorPixel> removal_pixels(25, linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F)));
  removal_pixels[12] = linear_pixel_from_srgb(QColor::fromRgbF(0.90F, 0.12F, 0.12F, 1.0F));
  ColorAdjustmentState removal_state;
  removal_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {
              0, 0, 0, 0, 0,
              0, 0, 0, 0, 0,
              0, 0, 255, 0, 0,
              0, 0, 0, 0, 0,
              0, 0, 0, 0, 0,
          },
          .segmentation_width = 5,
          .segmentation_height = 5,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&removal_pixels, 5, 5, removal_state);
  if (!(removal_pixels[12].red < 0.30 &&
        removal_pixels[12].green > 0.05 &&
        removal_pixels[12].blue > 0.05)) {
    return fail("local removal should substantially suppress the removed subject color");
  }
  if (!(std::abs(removal_pixels[12].red - removal_pixels[0].red) < 0.18 &&
        std::abs(removal_pixels[12].green - removal_pixels[0].green) < 0.18 &&
        std::abs(removal_pixels[12].blue - removal_pixels[0].blue) < 0.18)) {
    return fail("local removal should pull the removed region toward surrounding colors");
  }

  std::vector<ColorPixel> grouped_removal_pixels(25, linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F)));
  grouped_removal_pixels[12] = linear_pixel_from_srgb(QColor::fromRgbF(0.90F, 0.12F, 0.12F, 1.0F));
  ColorAdjustmentState grouped_removal_state;
  grouped_removal_state.retouch_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {
              0, 0, 0, 0, 0,
              0, 0, 0, 0, 0,
              0, 0, 255, 0, 0,
              0, 0, 0, 0, 0,
              0, 0, 0, 0, 0,
          },
          .segmentation_width = 5,
          .segmentation_height = 5,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&grouped_removal_pixels, 5, 5, grouped_removal_state);
  if (!(grouped_removal_pixels[12].red < 0.30 &&
        grouped_removal_pixels[12].green > 0.05 &&
        grouped_removal_pixels[12].blue > 0.05)) {
    return fail("retouch masks should render from the dedicated retouch stack");
  }

  std::vector<ColorPixel> line_removal_pixels(
      25,
      linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F)));
  for (int y = 0; y < 5; ++y) {
    line_removal_pixels[static_cast<std::size_t>(y * 5 + 2)] =
        linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.10F, 0.10F, 1.0F));
  }
  line_removal_pixels[12] = linear_pixel_from_srgb(QColor::fromRgbF(0.90F, 0.12F, 0.12F, 1.0F));
  ColorAdjustmentState line_removal_state;
  line_removal_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {
              0, 0, 0, 0, 0,
              0, 0, 0, 0, 0,
              0, 0, 255, 0, 0,
              0, 0, 0, 0, 0,
              0, 0, 0, 0, 0,
          },
          .segmentation_width = 5,
          .segmentation_height = 5,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&line_removal_pixels, 5, 5, line_removal_state);
  const auto line_center = line_removal_pixels[12];
  const auto line_neighbor = line_removal_pixels[7];
  const auto background_neighbor = line_removal_pixels[11];
  if (!(std::abs(line_center.red - line_neighbor.red) <
            std::abs(line_center.red - background_neighbor.red) &&
        std::abs(line_center.green - line_neighbor.green) <
            std::abs(line_center.green - background_neighbor.green) &&
        std::abs(line_center.blue - line_neighbor.blue) <
            std::abs(line_center.blue - background_neighbor.blue))) {
    return fail("local removal should preserve simple line structure through the removed region");
  }

  std::vector<ColorPixel> checker_base_pixels;
  checker_base_pixels.reserve(49);
  for (int y = 0; y < 7; ++y) {
    for (int x = 0; x < 7; ++x) {
      const auto dark = ((x + y) % 2) == 0;
      checker_base_pixels.push_back(
          linear_pixel_from_srgb(
              QColor::fromRgbF(dark ? 0.10F : 0.80F, dark ? 0.10F : 0.80F, dark ? 0.10F : 0.80F, 1.0F)));
    }
  }
  for (int y = 2; y <= 4; ++y) {
    for (int x = 2; x <= 4; ++x) {
      checker_base_pixels[static_cast<std::size_t>(y * 7 + x)] =
          linear_pixel_from_srgb(QColor::fromRgbF(0.92F, 0.18F, 0.18F, 1.0F));
    }
  }
  ColorAdjustmentState checker_removal_state;
  checker_removal_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {
              0, 0, 0, 0, 0, 0, 0,
              0, 0, 0, 0, 0, 0, 0,
              0, 0, 255, 255, 255, 0, 0,
              0, 0, 255, 255, 255, 0, 0,
              0, 0, 255, 255, 255, 0, 0,
              0, 0, 0, 0, 0, 0, 0,
              0, 0, 0, 0, 0, 0, 0,
          },
          .segmentation_width = 7,
          .segmentation_height = 7,
      },
  };
  auto checker_interactive_pixels = checker_base_pixels;
  auto checker_final_pixels = checker_base_pixels;
  photo_editor::pipeline::apply_color_adjustments(
      &checker_interactive_pixels, 7, 7, checker_removal_state,
      photo_editor::pipeline::RemovalQuality::Interactive);
  photo_editor::pipeline::apply_color_adjustments(
      &checker_final_pixels, 7, 7, checker_removal_state,
      photo_editor::pipeline::RemovalQuality::Final);
  const auto checker_center = checker_final_pixels[24];
  const auto dark_checker =
      linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.10F, 0.10F, 1.0F));
  const auto light_checker =
      linear_pixel_from_srgb(QColor::fromRgbF(0.80F, 0.80F, 0.80F, 1.0F));
  const auto removed_red =
      linear_pixel_from_srgb(QColor::fromRgbF(0.92F, 0.18F, 0.18F, 1.0F));
  const auto distance_to_dark =
      std::abs(checker_center.red - dark_checker.red) +
      std::abs(checker_center.green - dark_checker.green) +
      std::abs(checker_center.blue - dark_checker.blue);
  const auto distance_to_light =
      std::abs(checker_center.red - light_checker.red) +
      std::abs(checker_center.green - light_checker.green) +
      std::abs(checker_center.blue - light_checker.blue);
  const auto distance_to_removed_red =
      std::abs(checker_center.red - removed_red.red) +
      std::abs(checker_center.green - removed_red.green) +
      std::abs(checker_center.blue - removed_red.blue);
  if (!(std::abs(checker_center.red - checker_center.green) < 0.08 &&
        std::abs(checker_center.green - checker_center.blue) < 0.08 &&
        std::min(distance_to_dark, distance_to_light) + 1e-6 < distance_to_removed_red)) {
    return fail("final removal quality should resolve repeated structure toward a neutral source patch instead of leaving removed-color contamination");
  }

  std::vector<ColorPixel> contrast_soft_pixels(
      5,
      linear_pixel_from_srgb(QColor::fromRgbF(0.30F, 0.30F, 0.30F, 1.0F)));
  ColorAdjustmentState contrast_soft_state;
  contrast_soft_state.local_masks = {
      LocalMask{
          .kind = "linear",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 1.0,
          .position = 0.5,
      },
  };
  auto contrast_hard_state = contrast_soft_state;
  contrast_hard_state.local_masks.front().selection_edge_contrast = 100.0;
  photo_editor::pipeline::apply_color_adjustments(&contrast_soft_pixels, 5, 1, contrast_soft_state);
  std::vector<ColorPixel> contrast_hard_pixels(
      5,
      linear_pixel_from_srgb(QColor::fromRgbF(0.30F, 0.30F, 0.30F, 1.0F)));
  photo_editor::pipeline::apply_color_adjustments(&contrast_hard_pixels, 5, 1, contrast_hard_state);
  if (!(luminance(contrast_hard_pixels[3]) + 1e-6 < luminance(contrast_soft_pixels[3]) &&
        luminance(contrast_hard_pixels[1]) > luminance(contrast_soft_pixels[1]))) {
    return fail("edge contrast should tighten soft selection falloff around the mask boundary");
  }

  std::vector<ColorPixel> decontaminate_pixels(
      5,
      linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F)));
  decontaminate_pixels[3] = linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.80F, 0.24F, 1.0F));
  const auto baseline_decontaminate_pixels = decontaminate_pixels;
  ColorAdjustmentState decontaminate_state;
  decontaminate_state.local_masks = {
      LocalMask{
          .kind = "linear",
          .operation = "add",
          .enabled = true,
          .selection_decontaminate = 100.0,
          .feather = 1.0,
          .position = 0.5,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&decontaminate_pixels, 5, 1, decontaminate_state);
  const auto baseline_green_gap =
      std::abs(static_cast<double>(baseline_decontaminate_pixels[3].green) -
               static_cast<double>(baseline_decontaminate_pixels[3].red));
  const auto cleaned_green_gap =
      std::abs(static_cast<double>(decontaminate_pixels[3].green) -
               static_cast<double>(decontaminate_pixels[3].red));
  if (!(cleaned_green_gap + 1e-6 < baseline_green_gap)) {
    return fail("selection decontamination should reduce colored fringe intensity on partial mask edges");
  }

  std::vector<ColorPixel> clone_pixels(5, linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F)));
  clone_pixels[0] = linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.24F, 0.92F, 1.0F));
  ColorAdjustmentState clone_state;
  clone_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .retouch_mode = "clone",
          .enabled = true,
          .brush_size = 0.20,
          .brush_flow = 1.0,
          .clone_source_set = true,
          .clone_offset_x = -1.0,
          .clone_offset_y = 0.0,
          .brush_points = {{.x = 1.0, .y = 0.5, .size = 0.20, .flow = 1.0}},
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&clone_pixels, 5, 1, clone_state);
  if (!(clone_pixels[4].blue > clone_pixels[4].red &&
        clone_pixels[4].blue > clone_pixels[4].green)) {
    return fail("clone stamp should copy source color into the painted destination region");
  }

  std::vector<ColorPixel> red_eye_pixels = {
      linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.10F, 0.10F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.95F, 0.05F, 0.04F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.10F, 0.10F, 1.0F)),
  };
  ColorAdjustmentState red_eye_state;
  red_eye_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .retouch_mode = "red-eye",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {0, 255, 0},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&red_eye_pixels, 3, 1, red_eye_state);
  if (!(red_eye_pixels[1].red < red_eye_pixels[1].green * 1.15F &&
        red_eye_pixels[1].red < red_eye_pixels[0].red)) {
    return fail("red-eye retouch should neutralize excess red inside the red-eye matte");
  }

  std::vector<ColorPixel> skin_cleanup_pixels(
      5,
      linear_pixel_from_srgb(QColor::fromRgbF(0.74F, 0.45F, 0.34F, 1.0F)));
  skin_cleanup_pixels[2] = linear_pixel_from_srgb(QColor::fromRgbF(0.28F, 0.15F, 0.12F, 1.0F));
  const auto blemish_before = luminance(skin_cleanup_pixels[2]);
  ColorAdjustmentState skin_cleanup_state;
  skin_cleanup_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .retouch_mode = "skin-cleanup",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {0, 0, 255, 0, 0},
          .segmentation_width = 5,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&skin_cleanup_pixels, 5, 1, skin_cleanup_state);
  if (!(luminance(skin_cleanup_pixels[2]) > blemish_before + 1e-4)) {
    return fail("skin cleanup retouch should smooth isolated blemish tone toward neighboring skin");
  }

  std::vector<ColorPixel> frequency_pixels(
      9,
      linear_pixel_from_srgb(QColor::fromRgbF(0.74F, 0.45F, 0.34F, 1.0F)));
  frequency_pixels[4] = linear_pixel_from_srgb(QColor::fromRgbF(0.24F, 0.13F, 0.11F, 1.0F));
  const auto frequency_before = luminance(frequency_pixels[4]);
  ColorAdjustmentState frequency_state;
  frequency_state.local_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .retouch_mode = "frequency-smoothing",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {0, 0, 0, 0, 255, 0, 0, 0, 0},
          .segmentation_width = 9,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&frequency_pixels, 9, 1, frequency_state);
  if (!(luminance(frequency_pixels[4]) > frequency_before + 1e-4)) {
    return fail("frequency smoothing should lift low-frequency blemish tone while preserving the retouch matte");
  }

  std::vector<ColorPixel> inpaint_patch_pixels(
      5,
      linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.18F, 0.18F, 1.0F)));
  ColorAdjustmentState inpaint_patch_state;
  inpaint_patch_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .retouch_mode = "remove",
          .enabled = true,
          .remove_strength = 100.0,
          .brush_size = 0.34,
          .brush_flow = 1.0,
          .brush_points = {{.x = 0.5, .y = 0.5, .size = 0.34, .flow = 1.0}},
          .inpaint_patch_pixels = {
              linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.18F, 0.18F, 1.0F)),
              linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.18F, 0.18F, 1.0F)),
              linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.82F, 0.10F, 1.0F)),
              linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.18F, 0.18F, 1.0F)),
              linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.18F, 0.18F, 1.0F)),
          },
          .inpaint_patch_x = 0,
          .inpaint_patch_y = 0,
          .inpaint_patch_width = 5,
          .inpaint_patch_height = 1,
          .inpaint_model_name = "test-fake-lama",
          .inpaint_blend_amount = 0.4,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &inpaint_patch_pixels, 5, 1, inpaint_patch_state);
  if (!(inpaint_patch_pixels[2].green > inpaint_patch_pixels[2].red &&
        inpaint_patch_pixels[2].green > inpaint_patch_pixels[2].blue)) {
    return fail("baked inpaint patch should drive remove retouch rendering when present");
  }
  if (!(inpaint_patch_pixels[2].green <
        inpaint_patch_state.local_masks[0].inpaint_patch_pixels[2].green)) {
    return fail("inpaint blend amount should fade the baked patch instead of replacing at full strength");
  }

  std::vector<ColorPixel> layer_pixels(
      4,
      linear_pixel_from_srgb(QColor::fromRgbF(0.22F, 0.22F, 0.22F, 1.0F)));
  const auto layer_luminance_before = luminance(layer_pixels[0]);
  ColorAdjustmentState layer_state;
  layer_state.photo_layers = {
      PhotoLayer{
          .id = "layer-1",
          .name = "Lift",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .exposure = 1.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&layer_pixels, 4, 1, layer_state);
  if (!(luminance(layer_pixels[0]) > layer_luminance_before * 1.45)) {
    return fail("photo-editing adjustment layers should participate in the image pipeline");
  }
  std::vector<ColorPixel> hidden_layer_pixels(
      1,
      linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)));
  const auto hidden_layer_luminance_before = luminance(hidden_layer_pixels[0]);
  ColorAdjustmentState hidden_layer_state;
  hidden_layer_state.photo_layers = {
      PhotoLayer{
          .id = "hidden-layer",
          .name = "Hidden",
          .kind = "adjustment",
          .enabled = false,
          .opacity = 1.0,
          .exposure = 1.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&hidden_layer_pixels, 1, 1, hidden_layer_state);
  if (!almost_equal(luminance(hidden_layer_pixels[0]), hidden_layer_luminance_before, 1e-5)) {
    return fail("hidden layers should not affect rendered pixels");
  }
  std::vector<ColorPixel> masked_layer_pixels(
      2,
      linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)));
  const auto masked_layer_luminance_before = luminance(masked_layer_pixels[0]);
  ColorAdjustmentState masked_layer_state;
  masked_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-layer-1",
          .name = "Masked Lift",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .exposure = 1.0,
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&masked_layer_pixels, 2, 1, masked_layer_state);
  if (!(almost_equal(luminance(masked_layer_pixels[0]), masked_layer_luminance_before, 1e-5) &&
        luminance(masked_layer_pixels[1]) > masked_layer_luminance_before * 1.45)) {
    return fail("layer masks should gate photo layer adjustments per pixel");
  }
  std::vector<ColorPixel> clipped_layer_pixels(
      2,
      linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)));
  const auto clipped_layer_luminance_before = luminance(clipped_layer_pixels[0]);
  ColorAdjustmentState clipped_layer_state;
  clipped_layer_state.photo_layers = {
      PhotoLayer{
          .id = "clip-base",
          .name = "Clip Base",
          .kind = "fill",
          .enabled = true,
          .opacity = 1.0,
          .fill_color = linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)),
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
      PhotoLayer{
          .id = "clipped-adjustment",
          .name = "Clipped Lift",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .clipping_mask = true,
          .exposure = 1.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&clipped_layer_pixels, 2, 1, clipped_layer_state);
  if (!(almost_equal(luminance(clipped_layer_pixels[0]), clipped_layer_luminance_before, 1e-5) &&
        luminance(clipped_layer_pixels[1]) > clipped_layer_luminance_before * 1.45)) {
    return fail("clipping masks should limit a layer to the coverage of the layer below it");
  }
  std::vector<ColorPixel> pixel_layer_base(
      4,
      linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.12F, 0.12F, 1.0F)));
  ColorAdjustmentState pixel_layer_state;
  pixel_layer_state.photo_layers = {
      PhotoLayer{
          .id = "pixel-layer-1",
          .name = "Pixel",
          .kind = "pixel",
          .enabled = true,
          .opacity = 1.0,
          .pixels = {
              linear_pixel_from_srgb(QColor::fromRgbF(0.88F, 0.10F, 0.10F, 1.0F)),
              linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.88F, 0.10F, 1.0F)),
          },
          .pixel_width = 2,
          .pixel_height = 1,
          .x = 1,
          .y = 0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&pixel_layer_base, 4, 1, pixel_layer_state);
  if (!(pixel_layer_base[1].red > pixel_layer_base[1].green &&
        pixel_layer_base[2].green > pixel_layer_base[2].red &&
        almost_equal(luminance(pixel_layer_base[0]), luminance(pixel_layer_base[3]), 1e-5))) {
    return fail("pixel layers should composite positioned pixel buffers without touching pixels outside their bounds");
  }
  std::vector<ColorPixel> fill_layer_base(
      3,
      linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.10F, 0.10F, 1.0F)));
  ColorAdjustmentState fill_layer_state;
  fill_layer_state.photo_layers = {
      PhotoLayer{
          .id = "fill-layer-1",
          .name = "Fill",
          .kind = "fill",
          .enabled = true,
          .opacity = 0.5,
          .fill_color = linear_pixel_from_srgb(QColor::fromRgbF(0.90F, 0.12F, 0.12F, 1.0F)),
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&fill_layer_base, 3, 1, fill_layer_state);
  if (!(fill_layer_base[0].red > fill_layer_base[0].green &&
        fill_layer_base[1].red > fill_layer_base[1].blue &&
        almost_equal(luminance(fill_layer_base[0]), luminance(fill_layer_base[2]), 1e-5))) {
    return fail("fill layers should composite a solid color across the full image at layer opacity");
  }
  std::vector<ColorPixel> multiply_blend_pixels = {ColorPixel{0.5F, 0.5F, 0.5F, 1.0F}};
  ColorAdjustmentState multiply_blend_state;
  multiply_blend_state.photo_layers = {
      PhotoLayer{
          .id = "multiply-layer",
          .name = "Multiply",
          .kind = "fill",
          .blend_mode = "multiply",
          .enabled = true,
          .opacity = 1.0,
          .fill_color = ColorPixel{0.5F, 0.5F, 0.5F, 1.0F},
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&multiply_blend_pixels, 1, 1, multiply_blend_state);
  if (!almost_equal(multiply_blend_pixels[0].red, 0.25, 1e-5)) {
    return fail("multiply blend mode should darken by multiplying source and destination channels");
  }
  std::vector<ColorPixel> screen_blend_pixels = {ColorPixel{0.5F, 0.5F, 0.5F, 1.0F}};
  ColorAdjustmentState screen_blend_state;
  screen_blend_state.photo_layers = {
      PhotoLayer{
          .id = "screen-layer",
          .name = "Screen",
          .kind = "fill",
          .blend_mode = "screen",
          .enabled = true,
          .opacity = 1.0,
          .fill_color = ColorPixel{0.5F, 0.5F, 0.5F, 1.0F},
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&screen_blend_pixels, 1, 1, screen_blend_state);
  if (!almost_equal(screen_blend_pixels[0].red, 0.75, 1e-5)) {
    return fail("screen blend mode should lighten by inverting multiplied inverse channels");
  }
  for (const auto& mode : {
           "overlay",
           "soft light",
           "hard light",
           "color",
           "luminosity",
           "hue",
           "saturation",
           "difference",
           "lighten",
           "darken",
           "linear-light",
       }) {
    std::vector<ColorPixel> blend_pixels = {ColorPixel{0.35F, 0.45F, 0.55F, 1.0F}};
    ColorAdjustmentState blend_state;
    blend_state.photo_layers = {
        PhotoLayer{
            .id = "blend-layer",
            .name = mode,
            .kind = "fill",
            .blend_mode = mode,
            .enabled = true,
            .opacity = 1.0,
            .fill_color = ColorPixel{0.75F, 0.20F, 0.35F, 1.0F},
        },
    };
    photo_editor::pipeline::apply_color_adjustments(&blend_pixels, 1, 1, blend_state);
    if (!(blend_pixels[0].red >= 0.0F && blend_pixels[0].red <= 1.0F &&
          blend_pixels[0].green >= 0.0F && blend_pixels[0].green <= 1.0F &&
          blend_pixels[0].blue >= 0.0F && blend_pixels[0].blue <= 1.0F)) {
      return fail("advanced blend modes should produce bounded color channels");
    }
  }
  std::vector<ColorPixel> text_layer_base(
      3,
      linear_pixel_from_srgb(QColor::fromRgbF(0.08F, 0.08F, 0.08F, 1.0F)));
  ColorAdjustmentState text_layer_state;
  text_layer_state.photo_layers = {
      PhotoLayer{
          .id = "text-layer-1",
          .name = "Text",
          .kind = "text",
          .enabled = true,
          .opacity = 1.0,
          .pixels = {
              linear_pixel_from_srgb(QColor::fromRgbF(0.95F, 0.95F, 0.95F, 1.0F)),
          },
          .pixel_width = 1,
          .pixel_height = 1,
          .x = 1,
          .y = 0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&text_layer_base, 3, 1, text_layer_state);
  if (!(luminance(text_layer_base[1]) > luminance(text_layer_base[0]) * 4.0 &&
        almost_equal(luminance(text_layer_base[0]), luminance(text_layer_base[2]), 1e-5))) {
    return fail("text layers should composite rasterized text pixels through the photo layer pipeline");
  }
  std::vector<ColorPixel> source_layer_base(
      3,
      linear_pixel_from_srgb(QColor::fromRgbF(0.08F, 0.08F, 0.08F, 1.0F)));
  ColorAdjustmentState source_layer_state;
  source_layer_state.photo_layers = {
      PhotoLayer{
          .id = "source-layer-1",
          .name = "Source",
          .kind = "source",
          .enabled = true,
          .opacity = 1.0,
          .pixels = {
              linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.92F, 0.12F, 1.0F)),
          },
          .pixel_width = 1,
          .pixel_height = 1,
          .x = 2,
          .y = 0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&source_layer_base, 3, 1, source_layer_state);
  if (!(source_layer_base[2].green > source_layer_base[2].red &&
        almost_equal(luminance(source_layer_base[0]), luminance(source_layer_base[1]), 1e-5))) {
    return fail("source layers should composite retained source pixels through the photo layer pipeline");
  }
  std::vector<ColorPixel> group_layer_base(
      2,
      linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.18F, 0.18F, 1.0F)));
  const auto group_layer_before = luminance(group_layer_base[0]);
  ColorAdjustmentState group_layer_state;
  group_layer_state.photo_layers = {
      PhotoLayer{
          .id = "group-1",
          .name = "Group",
          .kind = "group",
          .enabled = true,
          .opacity = 1.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&group_layer_base, 2, 1, group_layer_state);
  if (!almost_equal(luminance(group_layer_base[0]), group_layer_before, 1e-5)) {
    return fail("layer groups should organize the stack without directly altering pixels");
  }

  std::vector<ColorPixel> transformed_layer_base(
      4,
      linear_pixel_from_srgb(QColor::fromRgbF(0.08F, 0.08F, 0.08F, 1.0F)));
  ColorAdjustmentState transformed_layer_state;
  transformed_layer_state.photo_layers = {
      PhotoLayer{
          .id = "transformed-pixel-layer",
          .name = "Transformed Pixel",
          .kind = "pixel",
          .enabled = true,
          .opacity = 1.0,
          .pixels = {
              linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.92F, 0.10F, 1.0F)),
          },
          .pixel_width = 1,
          .pixel_height = 1,
          .x = 1,
          .y = 0,
          .transform_scale = 2.0,
          .transform_rotation = 0.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&transformed_layer_base, 4, 1, transformed_layer_state);
  if (!(transformed_layer_base[1].green > transformed_layer_base[1].red &&
        transformed_layer_base[2].green > transformed_layer_base[2].red &&
        almost_equal(luminance(transformed_layer_base[0]), luminance(transformed_layer_base[3]), 1e-5))) {
    return fail("non-destructive layer transforms should scale raster-backed layers during compositing");
  }

  std::vector<ColorPixel> rotation_base_pixels(5, linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F)));
  rotation_base_pixels[1] = linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.82F, 0.10F, 1.0F));
  rotation_base_pixels[3] = linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.10F, 0.86F, 1.0F));

  auto unrotated_clone_pixels = rotation_base_pixels;
  ColorAdjustmentState unrotated_clone_state;
  unrotated_clone_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .retouch_mode = "clone",
          .enabled = true,
          .brush_size = 0.36,
          .brush_flow = 1.0,
          .clone_source_set = true,
          .clone_source_x = 0.5,
          .clone_source_y = 0.5,
          .clone_offset_x = -0.25,
          .clone_offset_y = 0.0,
          .clone_scale = 1.0,
          .clone_rotation = 0.0,
          .brush_points = {{.x = 0.75, .y = 0.5, .size = 0.36, .flow = 1.0}},
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&unrotated_clone_pixels, 5, 1, unrotated_clone_state);

  auto rotated_clone_pixels = rotation_base_pixels;
  auto rotated_clone_state = unrotated_clone_state;
  rotated_clone_state.local_masks.front().clone_rotation = 180.0;
  photo_editor::pipeline::apply_color_adjustments(&rotated_clone_pixels, 5, 1, rotated_clone_state);
  if (!(unrotated_clone_pixels[4].blue > rotated_clone_pixels[4].blue &&
        rotated_clone_pixels[4].green > unrotated_clone_pixels[4].green)) {
    return fail("clone rotation should rotate the sampled source patch around the picked source point");
  }

  std::vector<ColorPixel> vignette_pixels(9, neutral_gray);
  ColorAdjustmentState vignette_state;
  vignette_state.lens_vignette = 55.0;
  photo_editor::pipeline::apply_color_adjustments(&vignette_pixels, 3, 3, vignette_state);
  if (!(luminance(vignette_pixels[0]) > luminance(vignette_pixels[4]))) {
    return fail("positive lens vignette correction should brighten frame edges more than the center");
  }

  std::vector<ColorPixel> distortion_pixels;
  distortion_pixels.reserve(15);
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 5; ++x) {
      const auto tone = 0.12F + static_cast<float>(x) * 0.18F;
      distortion_pixels.push_back(linear_pixel_from_srgb(QColor::fromRgbF(tone, tone, tone, 1.0F)));
    }
  }
  const auto neutral_distortion_pixels = distortion_pixels;
  ColorAdjustmentState distortion_state;
  distortion_state.lens_distortion = 70.0;
  photo_editor::pipeline::apply_color_adjustments(&distortion_pixels, 5, 3, distortion_state);
  if (almost_equal(
          luminance(distortion_pixels[6]),
          luminance(neutral_distortion_pixels[6]),
          1e-4)) {
    return fail("lens distortion correction should remap near-edge sampling");
  }

  std::vector<ColorPixel> perspective_pixels;
  perspective_pixels.reserve(25);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      const auto tone = 0.10F + static_cast<float>(x) * 0.16F + static_cast<float>(y) * 0.01F;
      perspective_pixels.push_back(linear_pixel_from_srgb(QColor::fromRgbF(tone, tone, tone, 1.0F)));
    }
  }
  const auto neutral_perspective_pixels = perspective_pixels;
  ColorAdjustmentState perspective_state;
  perspective_state.perspective_vertical = 80.0;
  perspective_state.perspective_horizontal = -60.0;
  photo_editor::pipeline::apply_color_adjustments(&perspective_pixels, 5, 5, perspective_state);
  if (almost_equal(
          luminance(perspective_pixels[1]),
          luminance(neutral_perspective_pixels[1]),
          1e-4) ||
      almost_equal(
          luminance(perspective_pixels[19]),
          luminance(neutral_perspective_pixels[19]),
          1e-4)) {
    return fail("perspective correction should remap vertical and horizontal keystone sampling");
  }

  auto transform_pixels = neutral_perspective_pixels;
  const auto neutral_transform_pixels = transform_pixels;
  ColorAdjustmentState transform_state;
  transform_state.transform_rotation = 8.0;
  transform_state.transform_scale = 1.35;
  transform_state.transform_skew_x = 35.0;
  transform_state.transform_skew_y = -20.0;
  photo_editor::pipeline::apply_color_adjustments(&transform_pixels, 5, 5, transform_state);
  if (almost_equal(
          luminance(transform_pixels[6]),
          luminance(neutral_transform_pixels[6]),
          1e-4) ||
      almost_equal(
          luminance(transform_pixels[18]),
          luminance(neutral_transform_pixels[18]),
          1e-4)) {
    return fail("transform correction should remap rotate, scale, and skew sampling");
  }

  auto crop_fill_clamped_pixels = neutral_transform_pixels;
  ColorAdjustmentState crop_fill_state;
  crop_fill_state.transform_scale = 0.45;
  photo_editor::pipeline::apply_color_adjustments(&crop_fill_clamped_pixels, 5, 5, crop_fill_state);

  auto crop_fill_pixels = neutral_transform_pixels;
  crop_fill_state.content_aware_crop_fill = true;
  photo_editor::pipeline::apply_color_adjustments(&crop_fill_pixels, 5, 5, crop_fill_state);
  if (almost_equal(luminance(crop_fill_pixels[0]), luminance(crop_fill_clamped_pixels[0]), 1e-4)) {
    return fail("content-aware crop fill should synthesize exposed transform edges from nearby image content");
  }

  auto liquify_pixels = neutral_transform_pixels;
  ColorAdjustmentState liquify_state;
  liquify_state.liquify_center_x = 0.50;
  liquify_state.liquify_center_y = 0.50;
  liquify_state.liquify_radius = 0.55;
  liquify_state.liquify_strength = 75.0;
  photo_editor::pipeline::apply_color_adjustments(&liquify_pixels, 5, 5, liquify_state);
  if (!almost_equal(luminance(liquify_pixels[12]), luminance(neutral_transform_pixels[12]), 1e-6) ||
      almost_equal(luminance(liquify_pixels[7]), luminance(neutral_transform_pixels[7]), 1e-4) ||
      almost_equal(luminance(liquify_pixels[17]), luminance(neutral_transform_pixels[17]), 1e-4)) {
    return fail("liquify should keep the brush center stable while remapping nearby samples");
  }

  std::vector<ColorPixel> panorama_pixels;
  panorama_pixels.reserve(123);
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 41; ++x) {
      const auto tone = 0.08F + static_cast<float>(x) * 0.018F;
      panorama_pixels.push_back(linear_pixel_from_srgb(QColor::fromRgbF(tone, tone, tone, 1.0F)));
    }
  }
  ColorAdjustmentState panorama_state;
  panorama_state.lens_distortion = 60.0;
  panorama_state.perspective_vertical = 35.0;
  panorama_state.transform_rotation = 3.0;
  photo_editor::pipeline::apply_color_adjustments(&panorama_pixels, 41, 3, panorama_state);
  if (panorama_pixels.size() != 123 || panorama_pixels.front().alpha <= 0.0F ||
      panorama_pixels.back().alpha <= 0.0F) {
    return fail("panorama geometry correction should preserve pixel storage and valid alpha");
  }

  std::vector<ColorPixel> huge_guard_pixels(1, neutral_gray);
  photo_editor::pipeline::apply_color_adjustments(
      &huge_guard_pixels,
      std::numeric_limits<int>::max(),
      std::numeric_limits<int>::max(),
      panorama_state);
  if (huge_guard_pixels.size() != 1 || !almost_equal(luminance(huge_guard_pixels.front()), luminance(neutral_gray))) {
    return fail("huge-image geometry guard should return before overflowing dimensions");
  }

  std::vector<ColorPixel> ca_pixels(21, linear_pixel_from_srgb(QColor::fromRgbF(0.0F, 0.0F, 0.0F, 1.0F)));
  for (int y = 0; y < 3; ++y) {
    ca_pixels[static_cast<std::size_t>(y * 7 + 6)] = linear_pixel_from_srgb(QColor::fromRgbF(1.0F, 0.0F, 0.0F, 1.0F));
  }
  ColorAdjustmentState ca_state;
  ca_state.chromatic_aberration = 100.0;
  photo_editor::pipeline::apply_color_adjustments(&ca_pixels, 7, 3, ca_state);
  if (!(ca_pixels[13].red < 0.95)) {
    return fail("chromatic aberration correction should pull red fringes inward from the frame edge");
  }

  return EXIT_SUCCESS;
}
