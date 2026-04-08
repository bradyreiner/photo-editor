#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <QColor>

#include "photo_editor/pipeline/color_adjustments.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::LocalMask;
using photo_editor::pipeline::LocalMaskSet;

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

  const auto perceptual_gray_input = linear_pixel_from_srgb(QColor::fromRgbF(0.5F, 0.5F, 0.5F, 1.0F));
  const auto perceptual_curve_output = srgb_from_linear_pixel(apply_single_pixel(perceptual_gray_input, point_curve_state));
  if (!(perceptual_curve_output.redF() > 0.70F &&
        almost_equal(perceptual_curve_output.redF(), 0.75, 0.03) &&
        almost_equal(perceptual_curve_output.redF(), perceptual_curve_output.greenF(), 1e-5) &&
        almost_equal(perceptual_curve_output.greenF(), perceptual_curve_output.blueF(), 1e-5))) {
    return fail("master point curves should map visual mid-gray in perceptual space");
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
          .hue_center = 0.0,
          .hue_range = 0.12,
          .saturation_min = 0.15,
          .saturation_max = 1.0,
          .range_feather = 0.06,
      },
  };
  const auto red_mask_adjusted =
      apply_single_pixel(linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.70F, 0.50F, 1.0F)), color_mask_state);
  const auto blue_mask_adjusted =
      apply_single_pixel(linear_pixel_from_srgb(QColor::fromHslF(0.63F, 0.70F, 0.50F, 1.0F)), color_mask_state);
  if (!(luminance(red_mask_adjusted) > luminance(blue_mask_adjusted))) {
    return fail("color range masks should target matching hues more strongly than distant hues");
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
