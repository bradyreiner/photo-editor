#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <QColor>

#include "photo_editor/pipeline/color_adjustments.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::CurvePoint;
using photo_editor::pipeline::BrushPoint;
using photo_editor::pipeline::LocalMask;
using photo_editor::pipeline::LocalMaskSet;
using photo_editor::pipeline::LutAdjustment;
using photo_editor::pipeline::LocalAdjustmentCoordinateMapping;
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

double hsl_saturation_metric(const ColorPixel& pixel) {
  return srgb_from_linear_pixel(pixel).hslSaturationF();
}

double channel_spread(const ColorPixel& pixel) {
  return std::max({
      std::abs(static_cast<double>(pixel.red) - static_cast<double>(pixel.green)),
      std::abs(static_cast<double>(pixel.green) - static_cast<double>(pixel.blue)),
      std::abs(static_cast<double>(pixel.red) - static_cast<double>(pixel.blue)),
  });
}

double circular_hue_distance(double left, double right) {
  const auto delta = std::abs(left - right);
  return std::min(delta, 1.0 - delta);
}

double red_blue_bias_metric(const ColorPixel& pixel) {
  return static_cast<double>(pixel.red) - static_cast<double>(pixel.blue);
}

double magenta_green_bias_metric(const ColorPixel& pixel) {
  return ((static_cast<double>(pixel.red) + static_cast<double>(pixel.blue)) * 0.5) -
         static_cast<double>(pixel.green);
}

double inverted_luminance_metric(const ColorPixel& pixel) {
  return -luminance(pixel);
}

void prepare_noop(ColorAdjustmentState&) {}

void prepare_extended_headroom(ColorAdjustmentState& state) {
  state.color_pipeline.output_headroom = 4.0;
}

void prepare_filmic_shoulder(ColorAdjustmentState& state) {
  state.exposure = 0.75;
}

using StateSetter = void (*)(ColorAdjustmentState&, double);
using StatePrepare = void (*)(ColorAdjustmentState&);
using PixelMetric = double (*)(const ColorPixel&);

struct SignedPixelSliderCase final {
  const char* name;
  ColorPixel input;
  StateSetter set_value;
  PixelMetric metric = luminance;
  StatePrepare prepare = prepare_noop;
  double positive_value = 80.0;
  double negative_value = -80.0;
};

struct MaskOpacityDensityCase final {
  const char* name;
  double opacity = 1.0;
  double density = 1.0;
  double expected_weight = 1.0;
};

ColorPixel apply_single_pixel(const ColorPixel& pixel, const ColorAdjustmentState& state) {
  std::vector<ColorPixel> pixels{pixel};
  photo_editor::pipeline::apply_color_adjustments(&pixels, 1, 1, state);
  return pixels.front();
}

std::vector<ColorPixel> apply_pixels(std::vector<ColorPixel> pixels, int width, int height, const ColorAdjustmentState& state) {
  photo_editor::pipeline::apply_color_adjustments(&pixels, width, height, state);
  return pixels;
}

std::vector<ColorPixel> apply_local_adjusted_pixels(
    std::vector<ColorPixel> pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    bool* ok) {
  *ok = photo_editor::pipeline::apply_local_adjustments_to_adjusted_pixels(
      &pixels,
      width,
      height,
       state);
  return pixels;
}

std::vector<ColorPixel> apply_local_adjusted_pixels(
    std::vector<ColorPixel> pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const LocalAdjustmentCoordinateMapping& mapping,
    bool* ok) {
  *ok = photo_editor::pipeline::apply_local_adjustments_to_adjusted_pixels(
      &pixels,
      width,
      height,
      state,
      mapping);
  return pixels;
}

LocalMask synthetic_segmentation_source(
    const std::string& id,
    const std::string& operation,
    const std::vector<uint8_t>& samples) {
  LocalMask mask;
  mask.id = id;
  mask.kind = "subject";
  mask.operation = operation;
  mask.enabled = true;
  mask.feather = 0.01;
  mask.segmentation_mask = samples;
  mask.segmentation_width = static_cast<int>(samples.size());
  mask.segmentation_height = samples.empty() ? 0 : 1;
  return mask;
}

ColorAdjustmentState synthetic_mask_set_state(const std::vector<LocalMask>& sources) {
  LocalMaskSet mask_set;
  mask_set.id = "synthetic-set";
  mask_set.name = "Synthetic set";
  mask_set.enabled = true;
  mask_set.exposure = 1.0;
  mask_set.sources = sources;

  ColorAdjustmentState state;
  state.local_mask_sets = {mask_set};
  return state;
}

bool pixel_close(const ColorPixel& left, const ColorPixel& right, double tolerance = 1e-5) {
  return almost_equal(left.red, right.red, tolerance) &&
         almost_equal(left.green, right.green, tolerance) &&
         almost_equal(left.blue, right.blue, tolerance) &&
         almost_equal(left.alpha, right.alpha, tolerance);
}

bool pixel_is_finite(const ColorPixel& pixel) {
  return std::isfinite(pixel.red) &&
         std::isfinite(pixel.green) &&
         std::isfinite(pixel.blue) &&
         std::isfinite(pixel.alpha);
}

double expected_soft_light_channel(double base, double source) {
  if (source <= 0.5) {
    return base - (1.0 - 2.0 * source) * base * (1.0 - base);
  }
  const auto d = base <= 0.25 ? ((16.0 * base - 12.0) * base + 4.0) * base
                              : std::sqrt(base);
  return base + (2.0 * source - 1.0) * (d - base);
}

double expected_overlay_channel(double base, double source) {
  return base <= 0.5 ? 2.0 * base * source
                     : 1.0 - 2.0 * (1.0 - base) * (1.0 - source);
}

double expected_layer_blend_channel(double base, double source, const std::string& mode) {
  if (mode == "multiply") {
    return base * source;
  }
  if (mode == "screen") {
    return 1.0 - (1.0 - base) * (1.0 - source);
  }
  if (mode == "overlay") {
    return expected_overlay_channel(base, source);
  }
  if (mode == "soft-light") {
    return expected_soft_light_channel(base, source);
  }
  if (mode == "hard-light") {
    return expected_overlay_channel(source, base);
  }
  if (mode == "difference") {
    return std::abs(base - source);
  }
  if (mode == "lighten") {
    return std::max(base, source);
  }
  if (mode == "darken") {
    return std::min(base, source);
  }
  if (mode == "linear-light") {
    return photo_editor::pipeline::clamp_unit(base + 2.0 * source - 1.0);
  }
  return source;
}

ColorPixel expected_composited_layer_pixel(
    const ColorPixel& base,
    const ColorPixel& source,
    double effective_alpha,
    const std::string& mode) {
  const auto blended_red = expected_layer_blend_channel(base.red, source.red, mode);
  const auto blended_green = expected_layer_blend_channel(base.green, source.green, mode);
  const auto blended_blue = expected_layer_blend_channel(base.blue, source.blue, mode);
  return ColorPixel{
      .red = static_cast<float>(base.red + (blended_red - base.red) * effective_alpha),
      .green = static_cast<float>(base.green + (blended_green - base.green) * effective_alpha),
      .blue = static_cast<float>(base.blue + (blended_blue - base.blue) * effective_alpha),
      .alpha = static_cast<float>(effective_alpha + base.alpha * (1.0 - effective_alpha)),
  };
}

bool matches_binary_selection_pattern(
    const std::vector<ColorPixel>& pixels,
    const ColorPixel& neutral_pixel,
    const ColorPixel& selected_pixel,
    const std::vector<int>& expected_selected) {
  if (pixels.size() != expected_selected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < pixels.size(); ++index) {
    const auto& expected_pixel = expected_selected[index] != 0 ? selected_pixel : neutral_pixel;
    if (!pixel_close(pixels[index], expected_pixel)) {
      return false;
    }
  }
  return true;
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

  ColorAdjustmentState extended_highlight_state;
  extended_highlight_state.color_pipeline.output_headroom = 4.0;
  const auto extended_highlights = apply_pixels(
      {
          ColorPixel{.red = 1.05F, .green = 0.52F, .blue = 0.24F, .alpha = 1.0F},
          ColorPixel{.red = 1.35F, .green = 0.52F, .blue = 0.24F, .alpha = 1.0F},
      },
      2,
      1,
      extended_highlight_state);
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

  const ColorPixel signed_mid_input{.red = 0.35F, .green = 0.35F, .blue = 0.35F, .alpha = 1.0F};
  const ColorPixel signed_shadow_input{.red = 0.06F, .green = 0.06F, .blue = 0.06F, .alpha = 1.0F};
  const ColorPixel signed_deep_shadow_input{.red = 0.016F, .green = 0.016F, .blue = 0.016F, .alpha = 1.0F};
  const ColorPixel signed_light_input{.red = 0.58F, .green = 0.58F, .blue = 0.58F, .alpha = 1.0F};
  const ColorPixel signed_highlight_input{.red = 0.82F, .green = 0.82F, .blue = 0.82F, .alpha = 1.0F};
  const ColorPixel curve_shadow_input{.red = 0.20F, .green = 0.20F, .blue = 0.20F, .alpha = 1.0F};
  const ColorPixel curve_dark_input{.red = 0.40F, .green = 0.40F, .blue = 0.40F, .alpha = 1.0F};
  const ColorPixel curve_light_input{.red = 0.65F, .green = 0.65F, .blue = 0.65F, .alpha = 1.0F};
  const ColorPixel curve_highlight_input{.red = 0.85F, .green = 0.85F, .blue = 0.85F, .alpha = 1.0F};
  const auto signed_extended_highlight_input =
      ColorPixel{.red = 1.70F, .green = 1.70F, .blue = 1.70F, .alpha = 1.0F};
  const auto signed_muted_red_input = linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.28F, 0.50F, 1.0F));

  const std::vector<SignedPixelSliderCase> signed_global_slider_cases = {
      {"temperature", signed_mid_input,
       [](ColorAdjustmentState& state, double value) { state.temperature = value; },
       red_blue_bias_metric},
      {"tint", signed_mid_input,
       [](ColorAdjustmentState& state, double value) { state.tint = value; },
       magenta_green_bias_metric},
      {"exposure", signed_mid_input,
       [](ColorAdjustmentState& state, double value) { state.exposure = value / 100.0; }},
      {"brightness", signed_mid_input,
       [](ColorAdjustmentState& state, double value) { state.brightness = value; }},
      {"contrast", signed_light_input,
       [](ColorAdjustmentState& state, double value) { state.contrast = value; }},
      {"highlights", signed_highlight_input,
       [](ColorAdjustmentState& state, double value) { state.highlights = value; }},
      {"shadows", signed_shadow_input,
       [](ColorAdjustmentState& state, double value) { state.shadows = value; }},
      {"whites", signed_highlight_input,
       [](ColorAdjustmentState& state, double value) { state.whites = value; }},
      {"blacks", signed_shadow_input,
       [](ColorAdjustmentState& state, double value) { state.blacks = value; }},
      {"filmic_shoulder", signed_highlight_input,
       [](ColorAdjustmentState& state, double value) { state.filmic_shoulder = value; },
       inverted_luminance_metric, prepare_filmic_shoulder},
      {"filmic_toe", signed_shadow_input,
       [](ColorAdjustmentState& state, double value) { state.filmic_toe = value; }},
      {"adaptive_contrast", signed_mid_input,
       [](ColorAdjustmentState& state, double value) { state.adaptive_contrast = value; }},
      {"levels_gamma", signed_mid_input,
       [](ColorAdjustmentState& state, double value) { state.levels_gamma = value; }},
      {"zone_deep_shadows", signed_deep_shadow_input,
       [](ColorAdjustmentState& state, double value) { state.zone_deep_shadows = value; }},
      {"zone_shadows", signed_shadow_input,
       [](ColorAdjustmentState& state, double value) { state.zone_shadows = value; }},
      {"zone_midtones", signed_mid_input,
       [](ColorAdjustmentState& state, double value) { state.zone_midtones = value; }},
      {"zone_lights", signed_light_input,
       [](ColorAdjustmentState& state, double value) { state.zone_lights = value; }},
      {"zone_highlights", signed_extended_highlight_input,
       [](ColorAdjustmentState& state, double value) { state.zone_highlights = value; },
       luminance, prepare_extended_headroom},
      {"curve_shadows", curve_shadow_input,
       [](ColorAdjustmentState& state, double value) { state.curve_shadows = value; }},
      {"curve_darks", curve_dark_input,
       [](ColorAdjustmentState& state, double value) { state.curve_darks = value; }},
      {"curve_lights", curve_light_input,
       [](ColorAdjustmentState& state, double value) { state.curve_lights = value; }},
      {"curve_highlights", curve_highlight_input,
       [](ColorAdjustmentState& state, double value) { state.curve_highlights = value; }},
      {"vibrance", signed_muted_red_input,
       [](ColorAdjustmentState& state, double value) { state.vibrance = value; },
       hsl_saturation_metric},
      {"saturation", signed_muted_red_input,
       [](ColorAdjustmentState& state, double value) { state.saturation = value; },
       hsl_saturation_metric},
  };
  if (signed_global_slider_cases.size() != 24) {
    return fail("signed global slider direction registry should cover all non-spatial global signed sliders");
  }
  for (std::size_t i = 0; i < signed_global_slider_cases.size(); ++i) {
    for (std::size_t j = i + 1; j < signed_global_slider_cases.size(); ++j) {
      if (std::string(signed_global_slider_cases[i].name) == signed_global_slider_cases[j].name) {
        return fail("signed global slider direction registry should not contain duplicate controls");
      }
    }
  }
  for (const auto& slider_case : signed_global_slider_cases) {
    ColorAdjustmentState neutral_state_for_case;
    ColorAdjustmentState positive_state;
    ColorAdjustmentState negative_state;
    slider_case.prepare(neutral_state_for_case);
    slider_case.prepare(positive_state);
    slider_case.prepare(negative_state);
    slider_case.set_value(positive_state, slider_case.positive_value);
    slider_case.set_value(negative_state, slider_case.negative_value);
    const auto neutral_metric =
        slider_case.metric(apply_single_pixel(slider_case.input, neutral_state_for_case));
    const auto positive_metric =
        slider_case.metric(apply_single_pixel(slider_case.input, positive_state));
    const auto negative_metric =
        slider_case.metric(apply_single_pixel(slider_case.input, negative_state));
    if (!(positive_metric > neutral_metric + 1e-5 && negative_metric < neutral_metric - 1e-5)) {
      const std::string message =
          std::string("signed global slider render direction failed for ") + slider_case.name;
      return fail(message.c_str());
    }
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

  auto local_stage_state = synthetic_mask_set_state(
      {synthetic_segmentation_source("local-stage-mask", "add", {255, 0, 255, 0})});
  local_stage_state.local_mask_sets.front().exposure = 0.35;
  const std::vector<ColorPixel> local_stage_input = {
      ColorPixel{.red = 0.18F, .green = 0.12F, .blue = 0.08F, .alpha = 1.0F},
      ColorPixel{.red = 0.22F, .green = 0.18F, .blue = 0.13F, .alpha = 1.0F},
      ColorPixel{.red = 0.15F, .green = 0.20F, .blue = 0.24F, .alpha = 1.0F},
      ColorPixel{.red = 0.08F, .green = 0.10F, .blue = 0.12F, .alpha = 1.0F},
  };
  const auto local_stage_reference = apply_pixels(local_stage_input, 4, 1, local_stage_state);
  auto local_stage_ok = false;
  const auto local_stage_adjusted =
      apply_local_adjusted_pixels(local_stage_input, 4, 1, local_stage_state, &local_stage_ok);
  if (!local_stage_ok) {
    return fail("local adjusted-pixel stage should accept native local mask sets");
  }
  for (std::size_t index = 0; index < local_stage_reference.size(); ++index) {
    if (!pixel_close(local_stage_adjusted[index], local_stage_reference[index], 1e-5)) {
      return fail("local adjusted-pixel stage should match the full pipeline for isolated local masks");
    }
  }
  LocalMask mapped_brush_source;
  mapped_brush_source.id = "mapped-brush";
  mapped_brush_source.kind = "brush";
  mapped_brush_source.operation = "add";
  mapped_brush_source.brush_hardness = 0.25;
  mapped_brush_source.brush_buildup = true;
  mapped_brush_source.brush_points = {
      BrushPoint{.x = 0.5, .y = 0.75, .size = 0.28, .flow = 1.0}};
  mapped_brush_source.segmentation_mask = {
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 128, 0, 0,
      0, 128, 255, 128, 0,
      0, 0, 128, 0, 0};
  mapped_brush_source.segmentation_width = 5;
  mapped_brush_source.segmentation_height = 5;
  LocalMaskSet mapped_brush_set;
  mapped_brush_set.id = "mapped-brush-set";
  mapped_brush_set.enabled = true;
  mapped_brush_set.exposure = 0.6;
  mapped_brush_set.sources = {mapped_brush_source};
  ColorAdjustmentState mapped_brush_state;
  mapped_brush_state.local_mask_sets = {mapped_brush_set};
  std::vector<ColorPixel> mapped_full_input;
  mapped_full_input.reserve(25);
  for (int index = 0; index < 25; ++index) {
    mapped_full_input.push_back(ColorPixel{
        .red = static_cast<float>(0.08 + 0.004 * static_cast<double>(index)),
        .green = static_cast<float>(0.10 + 0.003 * static_cast<double>(index)),
        .blue = static_cast<float>(0.12 + 0.002 * static_cast<double>(index)),
        .alpha = 1.0F,
    });
  }
  const auto mapped_full_reference = apply_pixels(mapped_full_input, 5, 5, mapped_brush_state);
  std::vector<ColorPixel> mapped_patch_input;
  mapped_patch_input.reserve(6);
  for (int y = 2; y < 4; ++y) {
    for (int x = 1; x < 4; ++x) {
      mapped_patch_input.push_back(mapped_full_input[static_cast<std::size_t>(y * 5 + x)]);
    }
  }
  const auto mapped_patch_adjusted = apply_local_adjusted_pixels(
      mapped_patch_input,
      3,
      2,
      mapped_brush_state,
      LocalAdjustmentCoordinateMapping{
          .source_x = 1,
          .source_y = 2,
          .source_width = 5,
          .source_height = 5,
      },
      &local_stage_ok);
  if (!local_stage_ok) {
    return fail("local adjusted-pixel stage should accept source-coordinate crop mapping");
  }
  auto mapped_patch_index = std::size_t{0};
  for (int y = 2; y < 4; ++y) {
    for (int x = 1; x < 4; ++x) {
      if (!pixel_close(
              mapped_patch_adjusted[mapped_patch_index],
              mapped_full_reference[static_cast<std::size_t>(y * 5 + x)],
              1e-5)) {
        return fail("source-coordinate local adjusted-pixel crops should match full-frame local masks");
      }
      ++mapped_patch_index;
    }
  }
  auto clipped_local_stage_state = synthetic_mask_set_state(
      {synthetic_segmentation_source("local-stage-clipped-mask", "add", {255})});
  clipped_local_stage_state.local_mask_sets.front().exposure = 1.2;
  const std::vector<ColorPixel> clipped_local_stage_input = {
      ColorPixel{.red = 0.86F, .green = 0.74F, .blue = 0.42F, .alpha = 1.0F},
  };
  const auto clipped_local_stage_reference =
      apply_pixels(clipped_local_stage_input, 1, 1, clipped_local_stage_state);
  local_stage_ok = false;
  const auto clipped_local_stage_adjusted = apply_local_adjusted_pixels(
      clipped_local_stage_input,
      1,
      1,
      clipped_local_stage_state,
      &local_stage_ok);
  if (!local_stage_ok ||
      !pixel_close(clipped_local_stage_adjusted.front(), clipped_local_stage_reference.front(), 1e-5)) {
    return fail("local adjusted-pixel stage should keep full-pipeline headroom behavior when local edits exceed output headroom");
  }
  auto unsupported_local_stage_state = local_stage_state;
  unsupported_local_stage_state.linear_mask_enabled = true;
  local_stage_ok = true;
  (void)apply_local_adjusted_pixels(local_stage_input, 4, 1, unsupported_local_stage_state, &local_stage_ok);
  if (local_stage_ok) {
    return fail("local adjusted-pixel stage should reject mixed spatial mask modes");
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
  const auto preserved_luma_delta =
      std::abs(luminance(replaced_red) - luminance(red_model_input));
  ColorAdjustmentState legacy_replacement_state = color_replacement_state;
  legacy_replacement_state.color_replacement_preserve_luminance = false;
  legacy_replacement_state.color_replacement_preserve_saturation = false;
  const auto legacy_replaced_red = apply_single_pixel(red_model_input, legacy_replacement_state);
  if (!(std::abs(luminance(legacy_replaced_red) - luminance(red_model_input)) >
            preserved_luma_delta + 0.025 &&
        channel_spread(legacy_replaced_red) > channel_spread(replaced_red) + 0.025)) {
    return fail("color replacement preserve toggles should avoid the legacy luminance and saturation jump");
  }
  const auto replacement_edge_input = linear_pixel_from_srgb(QColor::fromHslF(0.06F, 0.75F, 0.50F, 1.0F));
  ColorAdjustmentState soft_replacement_state = color_replacement_state;
  soft_replacement_state.color_replacement_range = 20.0;
  soft_replacement_state.color_replacement_softness = 100.0;
  ColorAdjustmentState hard_replacement_state = soft_replacement_state;
  hard_replacement_state.color_replacement_softness = 0.0;
  const auto soft_replaced_edge = apply_single_pixel(replacement_edge_input, soft_replacement_state);
  const auto hard_replaced_edge = apply_single_pixel(replacement_edge_input, hard_replacement_state);
  const auto soft_edge_hue = srgb_from_linear_pixel(soft_replaced_edge).hslHueF();
  const auto hard_edge_hue = srgb_from_linear_pixel(hard_replaced_edge).hslHueF();
  if (!(circular_hue_distance(hard_edge_hue, color_replacement_state.color_replacement_target_hue) <
        circular_hue_distance(soft_edge_hue, color_replacement_state.color_replacement_target_hue))) {
    return fail("color replacement softness should control the selected hue falloff");
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
  grading_state.grading_amount = 0.0;
  const auto amount_zero_gray = apply_single_pixel(gray_input, grading_state);
  const auto neutral_gray_output = apply_single_pixel(gray_input, ColorAdjustmentState{});
  if (std::abs(luminance(amount_zero_gray) - luminance(neutral_gray_output)) > 1e-5 ||
      std::abs(amount_zero_gray.red - neutral_gray_output.red) > 1e-5 ||
      std::abs(amount_zero_gray.green - neutral_gray_output.green) > 1e-5 ||
      std::abs(amount_zero_gray.blue - neutral_gray_output.blue) > 1e-5) {
    return fail("color grading amount at zero should fade grading out completely");
  }
  ColorAdjustmentState graded_desaturate_state = grading_state;
  graded_desaturate_state.grading_amount = 100.0;
  graded_desaturate_state.saturation = -100.0;
  const auto desaturated_graded_gray = apply_single_pixel(gray_input, graded_desaturate_state);
  if (!(channel_spread(desaturated_graded_gray) < channel_spread(graded_gray) * 0.05)) {
    return fail("global saturation should control final chroma introduced by color grading");
  }
  ColorAdjustmentState graded_saturate_state = grading_state;
  graded_saturate_state.grading_amount = 100.0;
  graded_saturate_state.saturation = 100.0;
  const auto saturated_graded_gray = apply_single_pixel(gray_input, graded_saturate_state);
  if (!(channel_spread(saturated_graded_gray) > channel_spread(graded_gray) * 1.25)) {
    return fail("positive global saturation should intensify final color grading chroma");
  }
  const auto muted_source_color = linear_pixel_from_srgb(QColor::fromHslF(0.52F, 0.18F, 0.50F, 1.0F));
  ColorAdjustmentState vibrance_state;
  vibrance_state.vibrance = 100.0;
  const auto vibrant_source_color = apply_single_pixel(muted_source_color, vibrance_state);
  if (!(hsl_saturation_metric(vibrant_source_color) > hsl_saturation_metric(muted_source_color) + 0.03)) {
    return fail("vibrance should visibly lift natural low-saturation source color");
  }
  ColorAdjustmentState grading_balance_state;
  grading_balance_state.grading_shadows_hue = 240.0 / 360.0;
  grading_balance_state.grading_shadows_saturation = 100.0;
  grading_balance_state.grading_highlights_hue = 0.0;
  grading_balance_state.grading_highlights_saturation = 100.0;
  grading_balance_state.grading_balance = -100.0;
  const auto shadow_balanced_gray = apply_single_pixel(gray_input, grading_balance_state);
  grading_balance_state.grading_balance = 100.0;
  const auto highlight_balanced_gray = apply_single_pixel(gray_input, grading_balance_state);
  if (!(shadow_balanced_gray.blue > highlight_balanced_gray.blue &&
        highlight_balanced_gray.red > shadow_balanced_gray.red)) {
    return fail("color grading balance should shift tint weight between shadows and highlights");
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
  ColorAdjustmentState detailed_sharpen_state = sharpen_state;
  detailed_sharpen_state.sharpening = 45.0;
  detailed_sharpen_state.sharpen_detail = 100.0;
  ColorAdjustmentState base_detail_sharpen_state = sharpen_state;
  base_detail_sharpen_state.sharpening = detailed_sharpen_state.sharpening;
  const auto base_detail_sharpen_pixels = apply_pixels(detail_input, 5, 1, base_detail_sharpen_state);
  const auto detailed_sharpen_pixels = apply_pixels(detail_input, 5, 1, detailed_sharpen_state);
  const auto base_sharpen_change =
      std::abs(luminance(base_detail_sharpen_pixels[1]) - luminance(detail_input[1])) +
      std::abs(luminance(base_detail_sharpen_pixels[2]) - luminance(detail_input[2]));
  const auto detailed_sharpen_change =
      std::abs(luminance(detailed_sharpen_pixels[1]) - luminance(detail_input[1])) +
      std::abs(luminance(detailed_sharpen_pixels[2]) - luminance(detail_input[2]));
  if (!(detailed_sharpen_change > base_sharpen_change + 1e-4)) {
    return fail("sharpen detail should increase fine-edge gain");
  }
  ColorAdjustmentState broad_sharpen_state = sharpen_state;
  broad_sharpen_state.sharpen_radius = 3.0;
  const std::vector<ColorPixel> broad_detail_input = {
      edge_dark, edge_dark, edge_dark, edge_dark, edge_bright, edge_dark, edge_dark, edge_dark, edge_dark,
  };
  const auto base_radius_pixels = apply_pixels(broad_detail_input, 9, 1, sharpen_state);
  const auto broad_sharpen_pixels = apply_pixels(broad_detail_input, 9, 1, broad_sharpen_state);
  auto radius_delta = 0.0;
  for (std::size_t index = 0; index < broad_detail_input.size(); ++index) {
    radius_delta += std::abs(luminance(base_radius_pixels[index]) - luminance(broad_sharpen_pixels[index]));
  }
  if (!(radius_delta > 1e-5)) {
    return fail("sharpen radius should change the unsharp mask scale");
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

  const std::vector<ColorPixel> fine_texture_input = {
      linear_pixel_from_srgb(QColor::fromRgbF(0.42F, 0.42F, 0.42F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.47F, 0.47F, 0.47F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.42F, 0.42F, 0.42F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.47F, 0.47F, 0.47F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.42F, 0.42F, 0.42F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.47F, 0.47F, 0.47F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.42F, 0.42F, 0.42F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.47F, 0.47F, 0.47F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.42F, 0.42F, 0.42F, 1.0F)),
  };
  const auto base_fine_texture_contrast =
      luminance(fine_texture_input[1]) - luminance(fine_texture_input[0]);

  ColorAdjustmentState texture_state;
  texture_state.texture = 100.0;
  const auto textured_pixels = apply_pixels(presence_input, 9, 1, texture_state);
  if (!((luminance(textured_pixels[3]) - luminance(textured_pixels[2])) > base_presence_contrast + 1e-4)) {
    return fail("texture should increase fine local contrast on a small edge transition");
  }
  const auto fine_textured_pixels = apply_pixels(fine_texture_input, 9, 1, texture_state);
  const auto fine_texture_contrast =
      luminance(fine_textured_pixels[1]) - luminance(fine_textured_pixels[0]);
  if (!(fine_texture_contrast > base_fine_texture_contrast + 0.006)) {
    return fail("texture should produce a visible fine-detail contrast lift");
  }

  ColorAdjustmentState clarity_state;
  clarity_state.clarity = 100.0;
  const auto clarified_pixels = apply_pixels(presence_input, 9, 1, clarity_state);
  if (!((luminance(clarified_pixels[3]) - luminance(clarified_pixels[2])) > base_presence_contrast + 1e-4)) {
    return fail("clarity should increase midtone edge contrast on a broader transition");
  }
  const auto texture_broad_gain =
      (luminance(textured_pixels[3]) - luminance(textured_pixels[2])) - base_presence_contrast;
  const auto clarity_broad_gain =
      (luminance(clarified_pixels[3]) - luminance(clarified_pixels[2])) - base_presence_contrast;
  if (!(clarity_broad_gain > texture_broad_gain * 1.15)) {
    return fail("clarity should own broad-edge contrast more than texture");
  }

  const std::vector<ColorPixel> hard_presence_edge_input = {
      edge_dark, edge_dark, edge_dark, edge_dark, edge_dark, edge_dark, edge_dark,
      edge_bright, edge_bright, edge_bright, edge_bright, edge_bright, edge_bright, edge_bright, edge_bright,
  };
  const auto hard_edge_base_contrast =
      luminance(hard_presence_edge_input[7]) - luminance(hard_presence_edge_input[6]);
  const auto hard_edge_clarity_pixels = apply_pixels(hard_presence_edge_input, 15, 1, clarity_state);
  if (!((luminance(hard_edge_clarity_pixels[7]) - luminance(hard_edge_clarity_pixels[6])) >
        hard_edge_base_contrast + 1e-4)) {
    return fail("clarity should still add controlled contrast at a hard edge");
  }
  const auto hard_edge_left_flat_shift =
      std::abs(luminance(hard_edge_clarity_pixels[2]) - luminance(hard_presence_edge_input[2]));
  const auto hard_edge_right_flat_shift =
      std::abs(luminance(hard_edge_clarity_pixels[12]) - luminance(hard_presence_edge_input[12]));
  if (!(hard_edge_left_flat_shift < 0.004 && hard_edge_right_flat_shift < 0.004)) {
    const std::string message =
        "guided presence should avoid halo shifts across flat areas beside hard edges: left_shift=" +
        std::to_string(hard_edge_left_flat_shift) +
        " right_shift=" + std::to_string(hard_edge_right_flat_shift);
    return fail(message.c_str());
  }

  ColorAdjustmentState dehaze_state;
  dehaze_state.dehaze = 100.0;
  ColorAdjustmentState negative_dehaze_state;
  negative_dehaze_state.dehaze = -100.0;
  const std::vector<ColorPixel> hazy_presence_input = {
      linear_pixel_from_srgb(QColor::fromRgbF(0.30F, 0.30F, 0.30F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.31F, 0.31F, 0.31F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.33F, 0.33F, 0.33F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.36F, 0.36F, 0.36F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.40F, 0.40F, 0.40F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.44F, 0.44F, 0.44F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.47F, 0.47F, 0.47F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.44F, 0.44F, 0.44F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.40F, 0.40F, 0.40F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.36F, 0.36F, 0.36F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.33F, 0.33F, 0.33F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.31F, 0.31F, 0.31F, 1.0F)),
      linear_pixel_from_srgb(QColor::fromRgbF(0.30F, 0.30F, 0.30F, 1.0F)),
  };
  const auto dehazed_pixels = apply_pixels(hazy_presence_input, 13, 1, dehaze_state);
  const auto negative_dehazed_pixels = apply_pixels(hazy_presence_input, 13, 1, negative_dehaze_state);
  const auto dehaze_base_separation =
      luminance(hazy_presence_input[6]) - luminance(hazy_presence_input[0]);
  const auto dehaze_rendered_separation =
      luminance(dehazed_pixels[6]) - luminance(dehazed_pixels[0]);
  const auto negative_dehaze_rendered_separation =
      luminance(negative_dehazed_pixels[6]) - luminance(negative_dehazed_pixels[0]);
  if (!(dehaze_rendered_separation > dehaze_base_separation + 1e-4)) {
    const std::string message =
        "dehaze should increase separation in low-contrast hazy structure: base=" +
        std::to_string(dehaze_base_separation) +
        " rendered=" + std::to_string(dehaze_rendered_separation);
    return fail(message.c_str());
  }
  if (!(negative_dehaze_rendered_separation < dehaze_base_separation - 1e-5)) {
    const std::string message =
        "negative dehaze should soften low-contrast hazy structure: base=" +
        std::to_string(dehaze_base_separation) +
        " rendered=" + std::to_string(negative_dehaze_rendered_separation);
    return fail(message.c_str());
  }
  const auto dehaze_shadow_depth =
      luminance(hazy_presence_input[0]) - luminance(dehazed_pixels[0]);
  if (!(dehaze_shadow_depth > 0.001)) {
    const std::string message =
        "positive dehaze should add controlled black depth in hazy shadows: depth=" +
        std::to_string(dehaze_shadow_depth);
    return fail(message.c_str());
  }
  const auto hard_edge_dehaze_pixels = apply_pixels(hard_presence_edge_input, 15, 1, dehaze_state);
  const auto hard_edge_dehaze_left_shift =
      std::abs(luminance(hard_edge_dehaze_pixels[2]) - luminance(hard_presence_edge_input[2]));
  const auto hard_edge_dehaze_right_shift =
      std::abs(luminance(hard_edge_dehaze_pixels[12]) - luminance(hard_presence_edge_input[12]));
  if (!(hard_edge_dehaze_left_shift < 0.004 && hard_edge_dehaze_right_shift < 0.004)) {
    const std::string message =
        "dehaze should not create flat-area halos around hard edges: left_shift=" +
        std::to_string(hard_edge_dehaze_left_shift) +
        " right_shift=" + std::to_string(hard_edge_dehaze_right_shift);
    return fail(message.c_str());
  }

  struct SignedSpatialSliderCase final {
    const char* name;
    StateSetter set_value;
    int low_index;
    int high_index;
  };
  const std::vector<SignedSpatialSliderCase> signed_spatial_slider_cases = {
      {"texture", [](ColorAdjustmentState& state, double value) { state.texture = value; }, 2, 3},
      {"clarity", [](ColorAdjustmentState& state, double value) { state.clarity = value; }, 2, 3},
  };
  if (signed_spatial_slider_cases.size() != 2) {
    return fail("signed spatial slider direction registry should cover texture and clarity");
  }
  for (const auto& slider_case : signed_spatial_slider_cases) {
    ColorAdjustmentState positive_state;
    ColorAdjustmentState negative_state;
    slider_case.set_value(positive_state, 100.0);
    slider_case.set_value(negative_state, -100.0);
    const auto positive_pixels = apply_pixels(presence_input, 9, 1, positive_state);
    const auto negative_pixels = apply_pixels(presence_input, 9, 1, negative_state);
    const auto base_metric =
        luminance(presence_input[static_cast<std::size_t>(slider_case.high_index)]) -
        luminance(presence_input[static_cast<std::size_t>(slider_case.low_index)]);
    const auto positive_metric =
        luminance(positive_pixels[static_cast<std::size_t>(slider_case.high_index)]) -
        luminance(positive_pixels[static_cast<std::size_t>(slider_case.low_index)]);
    const auto negative_metric =
        luminance(negative_pixels[static_cast<std::size_t>(slider_case.high_index)]) -
        luminance(negative_pixels[static_cast<std::size_t>(slider_case.low_index)]);
    if (!(positive_metric > base_metric + 1e-4 && negative_metric < base_metric - 1e-6)) {
      const std::string message =
          std::string("signed spatial slider render direction failed for ") + slider_case.name +
          ": base=" + std::to_string(base_metric) +
          " positive=" + std::to_string(positive_metric) +
          " negative=" + std::to_string(negative_metric);
      return fail(message.c_str());
    }
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

  const auto synthetic_mask_pixel = linear_pixel_from_srgb(QColor::fromRgbF(0.34F, 0.34F, 0.34F, 1.0F));
  const std::vector<ColorPixel> synthetic_mask_input(4, synthetic_mask_pixel);
  const auto synthetic_neutral_pixels =
      apply_pixels(synthetic_mask_input, 4, 1, ColorAdjustmentState{});
  const auto synthetic_selected_pixels = apply_pixels(
      synthetic_mask_input,
      4,
      1,
      synthetic_mask_set_state({
          synthetic_segmentation_source("all", "add", {255, 255, 255, 255}),
      }));
  if (!(luminance(synthetic_selected_pixels.front()) > luminance(synthetic_neutral_pixels.front()) + 0.03)) {
    return fail("synthetic mask algebra fixture should distinguish selected and unselected pixels");
  }

  const std::vector<MaskOpacityDensityCase> opacity_density_cases = {
      {.name = "opacity", .opacity = 0.25, .density = 1.0, .expected_weight = 0.25},
      {.name = "density", .opacity = 1.0, .density = 0.40, .expected_weight = 0.40},
      {.name = "opacity times density", .opacity = 0.50, .density = 0.40, .expected_weight = 0.20},
      {.name = "density cap before opacity", .opacity = 0.50, .density = 0.80, .expected_weight = 0.40},
  };
  for (const auto& test_case : opacity_density_cases) {
    auto source = synthetic_segmentation_source(test_case.name, "add", {255});
    source.opacity = test_case.opacity;
    source.density = test_case.density;
    const auto output_pixel = apply_single_pixel(
        synthetic_mask_pixel,
        synthetic_mask_set_state({source}));
    const auto measured_weight = stop_delta(luminance(synthetic_mask_pixel), output_pixel);
    if (!almost_equal(measured_weight, test_case.expected_weight, 1e-4)) {
      return fail("mask opacity and density should resolve to min(base, density) multiplied by opacity");
    }
  }

  auto inverted_empty_source = synthetic_segmentation_source("invert-density-opacity-order", "add", {0});
  inverted_empty_source.invert = true;
  inverted_empty_source.density = 0.40;
  inverted_empty_source.opacity = 0.50;
  const auto source_invert_order_pixel = apply_single_pixel(
      synthetic_mask_pixel,
      synthetic_mask_set_state({inverted_empty_source}));
  const auto source_invert_order_weight = stop_delta(luminance(synthetic_mask_pixel), source_invert_order_pixel);
  if (!almost_equal(source_invert_order_weight, 0.20, 1e-4)) {
    return fail("source invert should run after matte refinement but before density and opacity");
  }

  const auto synthetic_mask_a = synthetic_segmentation_source("a", "add", {255, 255, 0, 0});
  const auto synthetic_mask_b_add = synthetic_segmentation_source("b", "add", {255, 0, 255, 0});
  const auto synthetic_mask_b_subtract =
      synthetic_segmentation_source("b", "subtract", {255, 0, 255, 0});
  const auto synthetic_mask_b_intersect =
      synthetic_segmentation_source("b", "intersect", {255, 0, 255, 0});

  const auto synthetic_union_pixels = apply_pixels(
      synthetic_mask_input,
      4,
      1,
      synthetic_mask_set_state({synthetic_mask_a, synthetic_mask_b_add}));
  if (!matches_binary_selection_pattern(
          synthetic_union_pixels,
          synthetic_neutral_pixels.front(),
          synthetic_selected_pixels.front(),
          {1, 1, 1, 0})) {
    return fail("synthetic add masks should equal set union A or B");
  }

  const auto synthetic_difference_pixels = apply_pixels(
      synthetic_mask_input,
      4,
      1,
      synthetic_mask_set_state({synthetic_mask_a, synthetic_mask_b_subtract}));
  if (!matches_binary_selection_pattern(
          synthetic_difference_pixels,
          synthetic_neutral_pixels.front(),
          synthetic_selected_pixels.front(),
          {0, 1, 0, 0})) {
    return fail("synthetic subtract masks should equal set difference A minus B");
  }

  const auto synthetic_intersection_pixels = apply_pixels(
      synthetic_mask_input,
      4,
      1,
      synthetic_mask_set_state({synthetic_mask_a, synthetic_mask_b_intersect}));
  if (!matches_binary_selection_pattern(
          synthetic_intersection_pixels,
          synthetic_neutral_pixels.front(),
          synthetic_selected_pixels.front(),
          {1, 0, 0, 0})) {
    return fail("synthetic intersect masks should equal set intersection A and B");
  }

  auto synthetic_soft_subtract = synthetic_mask_b_subtract;
  synthetic_soft_subtract.density = 0.5;
  const auto source_refined_before_composition_pixels = apply_pixels(
      synthetic_mask_input,
      4,
      1,
      synthetic_mask_set_state({synthetic_mask_a, synthetic_soft_subtract}));
  const auto half_subtract_weight = stop_delta(
      luminance(synthetic_mask_pixel),
      source_refined_before_composition_pixels.front());
  if (!(half_subtract_weight > 0.45 && half_subtract_weight < 0.55) ||
      !pixel_close(source_refined_before_composition_pixels.at(1), synthetic_selected_pixels.front()) ||
      !pixel_close(source_refined_before_composition_pixels.at(2), synthetic_neutral_pixels.front())) {
    return fail("source refinement should resolve before add/subtract set composition");
  }

  auto set_refined_after_composition_state =
      synthetic_mask_set_state({synthetic_mask_a, synthetic_mask_b_subtract});
  set_refined_after_composition_state.local_mask_sets.front().selection_invert = true;
  const auto set_refined_after_composition_pixels = apply_pixels(
      synthetic_mask_input,
      4,
      1,
      set_refined_after_composition_state);
  if (!matches_binary_selection_pattern(
          set_refined_after_composition_pixels,
          synthetic_neutral_pixels.front(),
          synthetic_selected_pixels.front(),
          {1, 0, 1, 1})) {
    return fail("mask-set refinement should apply after source composition");
  }

  auto set_invert_density_order_state =
      synthetic_mask_set_state({synthetic_mask_a, synthetic_mask_b_subtract});
  set_invert_density_order_state.local_mask_sets.front().selection_invert = true;
  set_invert_density_order_state.local_mask_sets.front().density = 0.40;
  set_invert_density_order_state.local_mask_sets.front().opacity = 0.50;
  const auto set_invert_density_order_pixels = apply_pixels(
      synthetic_mask_input,
      4,
      1,
      set_invert_density_order_state);
  const auto set_invert_selected_weight = stop_delta(
      luminance(synthetic_mask_pixel),
      set_invert_density_order_pixels.front());
  const auto set_invert_unselected_weight = stop_delta(
      luminance(synthetic_mask_pixel),
      set_invert_density_order_pixels.at(1));
  if (!almost_equal(set_invert_selected_weight, 0.20, 1e-4) ||
      !almost_equal(set_invert_unselected_weight, 0.0, 1e-4)) {
    return fail("mask-set invert should run after composition but before set density and opacity");
  }

  const auto source_hash_a =
      photo_editor::pipeline::refined_local_mask_hash(synthetic_mask_a, 4, 1, synthetic_mask_input);
  const auto source_hash_b =
      photo_editor::pipeline::refined_local_mask_hash(synthetic_mask_a, 4, 1, synthetic_mask_input);
  auto exposure_only_source = synthetic_mask_a;
  exposure_only_source.exposure = 2.0;
  auto refined_source = synthetic_mask_a;
  refined_source.selection_invert = true;
  const auto source_hash_exposure_only =
      photo_editor::pipeline::refined_local_mask_hash(exposure_only_source, 4, 1, synthetic_mask_input);
  const auto source_hash_refined =
      photo_editor::pipeline::refined_local_mask_hash(refined_source, 4, 1, synthetic_mask_input);
  if (source_hash_a == 0 || source_hash_a != source_hash_b ||
      source_hash_a != source_hash_exposure_only ||
      source_hash_a == source_hash_refined) {
    return fail("refined source matte hashes should be deterministic and sensitive only to matte changes");
  }

  const auto base_hash_set = synthetic_mask_set_state({synthetic_mask_a, synthetic_mask_b_subtract}).local_mask_sets.front();
  const auto set_hash_a =
      photo_editor::pipeline::refined_local_mask_set_hash(base_hash_set, 4, 1, synthetic_mask_input);
  const auto set_hash_b =
      photo_editor::pipeline::refined_local_mask_set_hash(base_hash_set, 4, 1, synthetic_mask_input);
  auto exposure_only_set = base_hash_set;
  exposure_only_set.exposure = 3.0;
  auto refined_set = base_hash_set;
  refined_set.selection_invert = true;
  const auto set_hash_exposure_only =
      photo_editor::pipeline::refined_local_mask_set_hash(exposure_only_set, 4, 1, synthetic_mask_input);
  const auto set_hash_refined =
      photo_editor::pipeline::refined_local_mask_set_hash(refined_set, 4, 1, synthetic_mask_input);
  if (set_hash_a == 0 || set_hash_a != set_hash_b ||
      set_hash_a != set_hash_exposure_only ||
      set_hash_a == set_hash_refined) {
    return fail("refined mask-set hashes should be deterministic and sensitive only to composed matte changes");
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

  std::vector<ColorPixel> brush_history_only_pixels(9, radial_input);
  const auto brush_history_only_input = brush_history_only_pixels;
  ColorAdjustmentState brush_history_only_state;
  brush_history_only_state.local_masks = {
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
  photo_editor::pipeline::apply_color_adjustments(
      &brush_history_only_pixels, 3, 3, brush_history_only_state);
  for (std::size_t index = 0; index < brush_history_only_pixels.size(); ++index) {
    if (!pixel_close(brush_history_only_pixels[index], brush_history_only_input[index], 1e-5)) {
      return fail("brush point history alone should not affect normal render paths without a raster matte");
    }
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
          .brush_points = {{.x = 0.0, .y = 0.0, .size = 0.18, .flow = 1.0}},
          .segmentation_mask = {
              0, 0, 0,
              0, 255, 0,
              0, 0, 0},
          .segmentation_width = 3,
          .segmentation_height = 3,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&brush_pixels, 3, 3, brush_state);
  if (!(luminance(brush_pixels[4]) > luminance(brush_pixels[0]))) {
    return fail("brush raster mattes should affect painted regions more strongly than untouched corners");
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

  ColorAdjustmentState hard_polygon_state;
  hard_polygon_state.local_masks = {
      LocalMask{
          .kind = "polygon",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.0,
          .polygon_points = {
              {.x = 0.30, .y = 0.20},
              {.x = 0.70, .y = 0.20},
              {.x = 0.70, .y = 0.80},
              {.x = 0.30, .y = 0.80},
          },
      },
  };
  auto feathered_polygon_state = hard_polygon_state;
  feathered_polygon_state.local_masks.front().feather = 0.20;
  const auto hard_polygon_pixels =
      apply_pixels(std::vector<ColorPixel>(25, radial_input), 5, 5, hard_polygon_state);
  const auto feathered_polygon_pixels =
      apply_pixels(std::vector<ColorPixel>(25, radial_input), 5, 5, feathered_polygon_state);
  if (!pixel_close(feathered_polygon_pixels[12], hard_polygon_pixels[12], 1e-5)) {
    return fail("polygon feather should not reduce fully interior selection strength");
  }
  if (!(luminance(feathered_polygon_pixels[11]) > luminance(hard_polygon_pixels[11]) &&
        luminance(feathered_polygon_pixels[11]) < luminance(feathered_polygon_pixels[12]))) {
    return fail("polygon feather should ramp just-outside pixels without weakening the interior");
  }
  if (!(luminance(hard_polygon_pixels[12]) > luminance(hard_polygon_pixels[0]))) {
    return fail("manual polygon add should use point-in-polygon geometry for selected pixels");
  }
  auto inverted_polygon_state = hard_polygon_state;
  inverted_polygon_state.local_masks.front().invert = true;
  const auto inverted_polygon_pixels =
      apply_pixels(std::vector<ColorPixel>(25, radial_input), 5, 5, inverted_polygon_state);
  if (!(luminance(inverted_polygon_pixels[0]) > luminance(inverted_polygon_pixels[12]))) {
    return fail("manual polygon invert should move local adjustments outside the polygon");
  }
  auto subtract_polygon_state = hard_polygon_state;
  auto subtract_polygon_source = hard_polygon_state.local_masks.front();
  subtract_polygon_source.operation = "subtract";
  subtract_polygon_state.local_masks.push_back(subtract_polygon_source);
  const auto subtract_polygon_pixels =
      apply_pixels(std::vector<ColorPixel>(25, radial_input), 5, 5, subtract_polygon_state);
  if (!(luminance(subtract_polygon_pixels[12]) < luminance(hard_polygon_pixels[12]))) {
    return fail("manual polygon subtract should remove overlapping polygon selection influence");
  }
  auto intersect_polygon_state = hard_polygon_state;
  auto intersect_polygon_source = hard_polygon_state.local_masks.front();
  intersect_polygon_source.operation = "intersect";
  intersect_polygon_source.polygon_points = {
      {.x = 0.30, .y = 0.20},
      {.x = 0.60, .y = 0.20},
      {.x = 0.60, .y = 0.80},
      {.x = 0.30, .y = 0.80},
  };
  intersect_polygon_state.local_masks.push_back(intersect_polygon_source);
  const auto intersect_polygon_pixels =
      apply_pixels(std::vector<ColorPixel>(25, radial_input), 5, 5, intersect_polygon_state);
  if (!(luminance(intersect_polygon_pixels[12]) > luminance(intersect_polygon_pixels[13]) &&
        luminance(intersect_polygon_pixels[12]) > luminance(intersect_polygon_pixels[0]))) {
    return fail("manual polygon intersect should keep only the overlapping polygon region");
  }

  ColorAdjustmentState rectangle_mask_state;
  rectangle_mask_state.local_masks = {
      LocalMask{
          .kind = "rectangle",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.0,
          .rotation = 0.0,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.26,
          .radius_y = 0.18,
      },
  };
  const auto rectangle_pixels =
      apply_pixels(std::vector<ColorPixel>(25, radial_input), 5, 5, rectangle_mask_state);
  if (!(luminance(rectangle_pixels[12]) > luminance(rectangle_pixels[0]) &&
        luminance(rectangle_pixels[12]) > luminance(rectangle_pixels[22]))) {
    return fail("rectangle masks should brighten pixels inside the drawn rectangle more than outside pixels");
  }

  ColorAdjustmentState ellipse_mask_state;
  ellipse_mask_state.local_masks = {
      LocalMask{
          .kind = "ellipse",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .temperature = 0.0,
          .feather = 0.08,
          .rotation = 0.0,
          .center_x = 0.5,
          .center_y = 0.5,
          .radius_x = 0.26,
          .radius_y = 0.18,
      },
  };
  const auto ellipse_pixels =
      apply_pixels(std::vector<ColorPixel>(25, radial_input), 5, 5, ellipse_mask_state);
  if (!(luminance(ellipse_pixels[12]) > luminance(ellipse_pixels[0]) &&
        luminance(ellipse_pixels[12]) > luminance(ellipse_pixels[22]))) {
    return fail("ellipse masks should use center/radius geometry for local adjustments");
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

  std::vector<ColorPixel> localized_expand_pixels(5, radial_input);
  ColorAdjustmentState localized_expand_state = base_object_state;
  localized_expand_state.local_masks.front().feather = 1.0;
  localized_expand_state.local_masks.front().selection_expand = 20.0;
  photo_editor::pipeline::apply_color_adjustments(&localized_expand_pixels, 5, 1, localized_expand_state);
  if (!(stop_delta(luminance(radial_input), localized_expand_pixels[2]) >
        stop_delta(luminance(radial_input), localized_expand_pixels[4]) + 1e-3)) {
    return fail("selection expand should use local raster dilation instead of a global scalar alpha lift");
  }

  auto raster_refine_state = [&](const std::vector<uint8_t>& matte) {
    ColorAdjustmentState state = base_object_state;
    state.local_masks.front().feather = 1.0;
    state.local_masks.front().selection_expand = 0.0;
    state.local_masks.front().segmentation_mask = matte;
    state.local_masks.front().segmentation_width = static_cast<int>(matte.size());
    state.local_masks.front().segmentation_height = matte.empty() ? 0 : 1;
    return state;
  };
  const auto render_refine_delta = [&](ColorAdjustmentState state, int index) {
    auto pixels = std::vector<ColorPixel>(static_cast<std::size_t>(state.local_masks.front().segmentation_width), radial_input);
    photo_editor::pipeline::apply_color_adjustments(
        &pixels,
        state.local_masks.front().segmentation_width,
        1,
        state);
    return stop_delta(luminance(radial_input), pixels[static_cast<std::size_t>(index)]);
  };

  auto explicit_grow_state = raster_refine_state({255, 128, 0, 0, 0});
  explicit_grow_state.local_masks.front().selection_grow = 20.0;
  if (!(render_refine_delta(explicit_grow_state, 2) >
        render_refine_delta(explicit_grow_state, 4) + 1e-3)) {
    return fail("selection grow should explicitly dilate raster mattes near selected pixels");
  }

  std::vector<uint8_t> high_resolution_grow_matte(21, 0);
  std::fill(high_resolution_grow_matte.begin(), high_resolution_grow_matte.begin() + 11, 255);
  auto high_resolution_grow_state = raster_refine_state(high_resolution_grow_matte);
  high_resolution_grow_state.local_masks.front().selection_grow = 20.0;
  if (!(render_refine_delta(high_resolution_grow_state, 15) > 1e-3 &&
        render_refine_delta(high_resolution_grow_state, 20) < 1e-3)) {
    return fail("selection grow radius should scale with raster resolution instead of using a fixed pixel radius");
  }

  auto explicit_shrink_base_state = raster_refine_state({255, 255, 255, 0, 0});
  auto explicit_shrink_state = explicit_shrink_base_state;
  explicit_shrink_state.local_masks.front().selection_shrink = 100.0;
  if (!(render_refine_delta(explicit_shrink_state, 1) <
        render_refine_delta(explicit_shrink_base_state, 1) - 1e-3)) {
    return fail("selection shrink should explicitly erode raster mattes near unselected pixels");
  }

  auto explicit_feather_state = raster_refine_state({255, 0, 0, 0, 0});
  const auto explicit_feather_base_state = explicit_feather_state;
  explicit_feather_state.local_masks.front().selection_feather = 100.0;
  if (!(render_refine_delta(explicit_feather_state, 1) >
        render_refine_delta(explicit_feather_base_state, 1) + 1e-3)) {
    return fail("selection feather should soften raster matte edges into neighboring pixels");
  }

  const auto feather_foreground_pixel = linear_pixel_from_srgb(QColor::fromRgbF(0.80F, 0.18F, 0.12F));
  const auto feather_background_pixel = linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.24F, 0.82F));
  auto edge_aware_feather_state = raster_refine_state({255, 0, 0, 0, 0});
  edge_aware_feather_state.local_masks.front().selection_feather = 100.0;
  const auto same_source_feather_input = std::vector<ColorPixel>(5, feather_foreground_pixel);
  const auto same_source_feather_output = apply_pixels(same_source_feather_input, 5, 1, edge_aware_feather_state);
  const std::vector<ColorPixel> hard_source_feather_input = {
      feather_foreground_pixel,
      feather_background_pixel,
      feather_background_pixel,
      feather_background_pixel,
      feather_background_pixel,
  };
  const auto hard_source_feather_output = apply_pixels(hard_source_feather_input, 5, 1, edge_aware_feather_state);
  const auto same_source_feather_delta =
      stop_delta(luminance(same_source_feather_input[1]), same_source_feather_output[1]);
  const auto hard_source_feather_delta =
      stop_delta(luminance(hard_source_feather_input[1]), hard_source_feather_output[1]);
  if (!(same_source_feather_delta > hard_source_feather_delta + 0.04)) {
    return fail("selection feather should respect source-image edges instead of bleeding equally across high-contrast boundaries");
  }

  auto explicit_blur_state = raster_refine_state({255, 0, 0, 0, 0});
  const auto explicit_blur_base_state = explicit_blur_state;
  explicit_blur_state.local_masks.front().selection_blur = 100.0;
  if (!(render_refine_delta(explicit_blur_state, 2) >
        render_refine_delta(explicit_blur_base_state, 2) + 1e-3)) {
    return fail("selection blur should spread raster matte influence across a wider radius");
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

  std::vector<ColorPixel> smooth_island_pixels(5, radial_input);
  ColorAdjustmentState smooth_island_state = base_object_state;
  smooth_island_state.local_masks.front().feather = 1.0;
  smooth_island_state.local_masks.front().segmentation_mask = {0, 0, 255, 0, 0};
  smooth_island_state.local_masks.front().selection_smooth = 100.0;
  photo_editor::pipeline::apply_color_adjustments(&smooth_island_pixels, 5, 1, smooth_island_state);
  if (!(stop_delta(luminance(radial_input), smooth_island_pixels[1]) > 1e-3 &&
        stop_delta(luminance(radial_input), smooth_island_pixels[3]) > 1e-3)) {
    return fail("selection smooth should use neighboring raster matte samples, not only the current alpha");
  }

  auto explicit_harden_base_state = raster_refine_state({255, 192, 64, 0, 0});
  auto explicit_harden_state = explicit_harden_base_state;
  explicit_harden_state.local_masks.front().selection_harden = 100.0;
  if (!(render_refine_delta(explicit_harden_state, 1) >
            render_refine_delta(explicit_harden_base_state, 1) + 1e-3 &&
        render_refine_delta(explicit_harden_state, 2) <
            render_refine_delta(explicit_harden_base_state, 2) - 1e-3)) {
    return fail("selection harden should increase high matte values and suppress low matte values");
  }

  auto explicit_despeckle_base_state = raster_refine_state({0, 0, 255, 0, 0});
  auto explicit_despeckle_state = explicit_despeckle_base_state;
  explicit_despeckle_state.local_masks.front().selection_despeckle = 100.0;
  if (!(render_refine_delta(explicit_despeckle_state, 2) <
        render_refine_delta(explicit_despeckle_base_state, 2) - 1e-3)) {
    return fail("selection despeckle should suppress isolated raster matte islands");
  }

  auto explicit_fill_holes_base_state = raster_refine_state({255, 255, 0, 255, 255});
  auto explicit_fill_holes_state = explicit_fill_holes_base_state;
  explicit_fill_holes_state.local_masks.front().selection_fill_holes = 100.0;
  if (!(render_refine_delta(explicit_fill_holes_state, 2) >
        render_refine_delta(explicit_fill_holes_base_state, 2) + 1e-3)) {
    return fail("selection fill holes should raise enclosed raster matte gaps");
  }

  auto explicit_remove_islands_base_state = raster_refine_state({0, 0, 255, 0, 0});
  auto explicit_remove_islands_state = explicit_remove_islands_base_state;
  explicit_remove_islands_state.local_masks.front().selection_remove_islands = 100.0;
  if (!(render_refine_delta(explicit_remove_islands_state, 2) <
        render_refine_delta(explicit_remove_islands_base_state, 2) - 1e-3)) {
    return fail("selection remove islands should lower isolated raster matte components");
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

  const auto hair_foreground_pixel = linear_pixel_from_srgb(QColor::fromRgbF(0.78F, 0.16F, 0.12F));
  const auto hair_background_pixel = linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.20F, 0.78F));
  std::vector<ColorPixel> guided_hair_pixels{
      hair_foreground_pixel,
      hair_foreground_pixel,
      hair_background_pixel,
      hair_background_pixel,
      hair_background_pixel,
  };
  ColorAdjustmentState guided_hair_state;
  guided_hair_state.local_masks = {
      LocalMask{
          .kind = "hair",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .feather = 1.0,
          .selection_fine_edge = 100.0,
          .segmentation_mask = {255, 128, 128, 0, 0},
          .segmentation_width = 5,
          .segmentation_height = 1,
      },
  };
  const auto guided_hair_output = apply_pixels(guided_hair_pixels, 5, 1, guided_hair_state);
  const auto foreground_like_boundary_delta =
      stop_delta(luminance(guided_hair_pixels[1]), guided_hair_output[1]);
  const auto background_like_boundary_delta =
      stop_delta(luminance(guided_hair_pixels[2]), guided_hair_output[2]);
  if (!(foreground_like_boundary_delta > background_like_boundary_delta + 0.10)) {
    return fail("fine-edge hair matting should use source color affinity at ambiguous boundaries");
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
      linear_pixel_from_srgb(QColor::fromRgbF(0.45F, 0.45F, 0.45F, 1.0F)),
  };
  const auto refined_color_base_pixels = refined_color_pixels;
  ColorAdjustmentState refined_color_state;
  refined_color_state.local_masks = {
      LocalMask{
          .kind = "alpha",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .refine_hue_center = 0.0,
          .refine_hue_range = 0.08,
          .refine_saturation_min = 0.50,
          .refine_saturation_max = 1.0,
          .refine_range_feather = 0.02,
          .segmentation_mask = {255, 255, 255},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&refined_color_pixels, 3, 1, refined_color_state);
  const auto selected_red_delta =
      luminance(refined_color_pixels[0]) - luminance(refined_color_base_pixels[0]);
  const auto rejected_blue_delta =
      luminance(refined_color_pixels[1]) - luminance(refined_color_base_pixels[1]);
  const auto rejected_low_sat_delta =
      luminance(refined_color_pixels[2]) - luminance(refined_color_base_pixels[2]);
  if (!(selected_red_delta > rejected_blue_delta + 0.05 &&
        selected_red_delta > rejected_low_sat_delta + 0.05)) {
    const std::string message =
        "hue and saturation range refinement should constrain local masks to selected colors: red_delta=" +
        std::to_string(selected_red_delta) +
        " blue_delta=" + std::to_string(rejected_blue_delta) +
        " low_sat_red_delta=" + std::to_string(rejected_low_sat_delta);
    return fail(message.c_str());
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

  const auto refine_brush_foreground = linear_pixel_from_srgb(QColor::fromRgbF(0.86F, 0.18F, 0.12F, 1.0F));
  const auto refine_brush_background = linear_pixel_from_srgb(QColor::fromRgbF(0.10F, 0.16F, 0.78F, 1.0F));
  std::vector<ColorPixel> refine_brush_pixels = {
      refine_brush_foreground,
      refine_brush_foreground,
      refine_brush_foreground,
      refine_brush_background,
      refine_brush_background,
  };
  const auto refine_brush_base_pixels = refine_brush_pixels;
  ColorAdjustmentState refine_brush_state;
  refine_brush_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .enabled = true,
          .exposure = 1.0,
          .selection_fine_edge = 62.0,
          .selection_edge_contrast = 18.0,
          .segmentation_mask = {255, 255, 128, 128, 0},
          .segmentation_width = 5,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&refine_brush_pixels, 5, 1, refine_brush_state);
  const auto foreground_edge_delta =
      stop_delta(luminance(refine_brush_base_pixels[2]), refine_brush_pixels[2]);
  const auto background_edge_delta =
      stop_delta(luminance(refine_brush_base_pixels[3]), refine_brush_pixels[3]);
  if (!(foreground_edge_delta > background_edge_delta + 0.08)) {
    return fail("Brush Refine edge recalculation should recover the foreground-colored side of a painted edge guide");
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

  const auto fringe_pixel = linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.80F, 0.24F, 1.0F));
  const auto neutral_edge_pixel = linear_pixel_from_srgb(QColor::fromRgbF(0.35F, 0.35F, 0.35F, 1.0F));
  const std::vector<ColorPixel> scoped_decontaminate_input = {
      neutral_edge_pixel,
      fringe_pixel,
      neutral_edge_pixel,
      fringe_pixel,
      neutral_edge_pixel,
  };
  ColorAdjustmentState scoped_decontaminate_state;
  scoped_decontaminate_state.local_mask_sets = {
      LocalMaskSet{
          .id = "scoped-cleanup",
          .name = "Scoped cleanup",
          .enabled = true,
          .sources = {
              LocalMask{
                  .id = "cleanup-source",
                  .kind = "subject",
                  .operation = "add",
                  .enabled = true,
                  .selection_decontaminate = 100.0,
                  .feather = 1.0,
                  .segmentation_mask = {0, 128, 255, 0, 0},
                  .segmentation_width = 5,
                  .segmentation_height = 1,
              },
              LocalMask{
                  .id = "plain-source",
                  .kind = "subject",
                  .operation = "add",
                  .enabled = true,
                  .feather = 1.0,
                  .segmentation_mask = {0, 0, 0, 128, 255},
                  .segmentation_width = 5,
                  .segmentation_height = 1,
              },
          },
      },
  };
  const auto scoped_decontaminate_output =
      apply_pixels(scoped_decontaminate_input, 5, 1, scoped_decontaminate_state);
  const auto scoped_cleaned_gap =
      std::abs(static_cast<double>(scoped_decontaminate_output[1].green) -
               static_cast<double>(scoped_decontaminate_output[1].red));
  const auto scoped_untouched_gap =
      std::abs(static_cast<double>(scoped_decontaminate_output[3].green) -
               static_cast<double>(scoped_decontaminate_output[3].red));
  const auto scoped_original_gap =
      std::abs(static_cast<double>(fringe_pixel.green) - static_cast<double>(fringe_pixel.red));
  if (!(scoped_cleaned_gap + 1e-6 < scoped_original_gap &&
        almost_equal(scoped_untouched_gap, scoped_original_gap, 1e-6))) {
    return fail("selection decontamination should be an explicit per-source edge cleanup, not a mask-set-wide side effect");
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
          .segmentation_mask = {0, 0, 0, 0, 255},
          .segmentation_width = 5,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(&clone_pixels, 5, 1, clone_state);
  if (!(clone_pixels[4].blue > clone_pixels[4].red &&
        clone_pixels[4].blue > clone_pixels[4].green)) {
    return fail("clone stamp should copy source color into the painted destination region");
  }

  const std::vector<std::string> finite_retouch_modes = {"remove", "heal", "patch"};
  for (const auto& mode : finite_retouch_modes) {
    std::vector<ColorPixel> finite_retouch_pixels = {
        ColorPixel{.red = 0.18F, .green = 0.24F, .blue = 0.31F, .alpha = 0.73F},
        ColorPixel{.red = 0.92F, .green = 0.08F, .blue = 0.07F, .alpha = 0.73F},
        ColorPixel{.red = 0.21F, .green = 0.27F, .blue = 0.35F, .alpha = 0.73F},
    };
    ColorAdjustmentState finite_retouch_state;
    finite_retouch_state.retouch_masks = {
        LocalMask{
            .kind = "object",
            .operation = "add",
            .retouch_mode = mode,
            .enabled = true,
            .remove_strength = 92.0,
            .segmentation_mask = {0, 255, 0},
            .segmentation_width = 3,
            .segmentation_height = 1,
        },
    };
    photo_editor::pipeline::apply_color_adjustments(
        &finite_retouch_pixels, 3, 1, finite_retouch_state);
    for (const auto& pixel : finite_retouch_pixels) {
      if (!pixel_is_finite(pixel) || !almost_equal(pixel.alpha, 0.73, 1e-6)) {
        return fail("remove, heal, and patch retouch should preserve finite output and alpha");
      }
    }
  }

  const std::vector<ColorPixel> heal_blend_input = {
      ColorPixel{.red = 0.08F, .green = 0.13F, .blue = 0.20F, .alpha = 1.0F},
      ColorPixel{.red = 0.82F, .green = 0.75F, .blue = 0.18F, .alpha = 1.0F},
      ColorPixel{.red = 0.95F, .green = 0.05F, .blue = 0.06F, .alpha = 1.0F},
      ColorPixel{.red = 0.12F, .green = 0.68F, .blue = 0.82F, .alpha = 1.0F},
      ColorPixel{.red = 0.11F, .green = 0.16F, .blue = 0.24F, .alpha = 1.0F},
  };
  auto smooth_heal_pixels = heal_blend_input;
  ColorAdjustmentState smooth_heal_state;
  smooth_heal_state.retouch_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .retouch_mode = "heal",
          .enabled = true,
          .remove_strength = 100.0,
          .heal_texture_blend = 0.0,
          .heal_color_blend = 1.0,
          .segmentation_mask = {0, 0, 255, 0, 0},
          .segmentation_width = 5,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &smooth_heal_pixels, 5, 1, smooth_heal_state);
  auto texture_heal_pixels = heal_blend_input;
  ColorAdjustmentState texture_heal_state = smooth_heal_state;
  texture_heal_state.retouch_masks.front().heal_texture_blend = 1.0;
  texture_heal_state.retouch_masks.front().heal_color_blend = 0.0;
  photo_editor::pipeline::apply_color_adjustments(
      &texture_heal_pixels, 5, 1, texture_heal_state);
  if (std::abs(luminance(texture_heal_pixels[2]) - luminance(smooth_heal_pixels[2])) < 0.01) {
    return fail("heal texture and color blends should produce separate rendered results");
  }

  const std::vector<ColorPixel> inactive_retouch_input = {
      ColorPixel{.red = 0.18F, .green = 0.24F, .blue = 0.31F, .alpha = 0.81F},
      ColorPixel{.red = 0.92F, .green = 0.08F, .blue = 0.07F, .alpha = 0.81F},
      ColorPixel{.red = 0.21F, .green = 0.27F, .blue = 0.35F, .alpha = 0.81F},
  };
  auto inactive_retouch_pixels = inactive_retouch_input;
  ColorAdjustmentState inactive_retouch_state;
  inactive_retouch_state.retouch_masks = {
      LocalMask{
          .kind = "object",
          .operation = "add",
          .retouch_mode = "remove",
          .enabled = false,
          .remove_strength = 100.0,
          .segmentation_mask = {0, 255, 0},
          .segmentation_width = 3,
          .segmentation_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &inactive_retouch_pixels, 3, 1, inactive_retouch_state);
  for (std::size_t index = 0; index < inactive_retouch_pixels.size(); ++index) {
    if (!pixel_close(inactive_retouch_pixels[index], inactive_retouch_input[index], 1e-6)) {
      return fail("inactive retouch operations should not affect rendered output");
    }
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
          .segmentation_mask = {0, 0, 255, 0, 0},
          .segmentation_width = 5,
          .segmentation_height = 1,
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

  std::vector<ColorPixel> hard_seam_pixels(
      7,
      linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)));
  ColorAdjustmentState hard_seam_state;
  hard_seam_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .retouch_mode = "patch",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {0, 255, 255, 255, 255, 255, 0},
          .segmentation_width = 7,
          .segmentation_height = 1,
          .inpaint_patch_pixels = std::vector<ColorPixel>(
              7,
              linear_pixel_from_srgb(QColor::fromRgbF(0.92F, 0.08F, 0.08F, 1.0F))),
          .inpaint_patch_x = 0,
          .inpaint_patch_y = 0,
          .inpaint_patch_width = 7,
          .inpaint_patch_height = 1,
          .inpaint_model_name = "source-region-patch-v1",
          .inpaint_blend_amount = 1.0,
          .inpaint_seam_feather = 2.0,
          .inpaint_color_match = 0.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &hard_seam_pixels, 7, 1, hard_seam_state);
  const auto seam_edge_delta =
      luminance(hard_seam_pixels[1]) -
      luminance(linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)));
  const auto seam_center_delta =
      luminance(hard_seam_pixels[3]) -
      luminance(linear_pixel_from_srgb(QColor::fromRgbF(0.20F, 0.20F, 0.20F, 1.0F)));
  if (!(std::abs(seam_edge_delta) < std::abs(seam_center_delta) * 0.85)) {
    return fail("inpaint seam feather should reduce patch influence near hard matte edges");
  }

  std::vector<ColorPixel> unmatched_patch_pixels(
      3,
      linear_pixel_from_srgb(QColor::fromRgbF(0.55F, 0.55F, 0.55F, 1.0F)));
  std::vector<ColorPixel> matched_patch_pixels = unmatched_patch_pixels;
  ColorAdjustmentState unmatched_patch_state;
  unmatched_patch_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .retouch_mode = "patch",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {255, 255, 255},
          .segmentation_width = 3,
          .segmentation_height = 1,
          .inpaint_patch_pixels = std::vector<ColorPixel>(
              3,
              linear_pixel_from_srgb(QColor::fromRgbF(0.12F, 0.12F, 0.12F, 1.0F))),
          .inpaint_patch_x = 0,
          .inpaint_patch_y = 0,
          .inpaint_patch_width = 3,
          .inpaint_patch_height = 1,
          .inpaint_model_name = "source-region-patch-v1",
          .inpaint_blend_amount = 1.0,
          .inpaint_seam_feather = 0.0,
          .inpaint_color_match = 0.0,
      },
  };
  auto matched_patch_state = unmatched_patch_state;
  matched_patch_state.local_masks.front().inpaint_color_match = 1.0;
  photo_editor::pipeline::apply_color_adjustments(
      &unmatched_patch_pixels, 3, 1, unmatched_patch_state);
  photo_editor::pipeline::apply_color_adjustments(
      &matched_patch_pixels, 3, 1, matched_patch_state);
  const auto source_luminance =
      luminance(linear_pixel_from_srgb(QColor::fromRgbF(0.55F, 0.55F, 0.55F, 1.0F)));
  if (!(std::abs(luminance(matched_patch_pixels[1]) - source_luminance) <
        std::abs(luminance(unmatched_patch_pixels[1]) - source_luminance))) {
    return fail("inpaint color matching should pull patch tone toward the seam source color");
  }

  const auto clipped_base_pixel =
      linear_pixel_from_srgb(QColor::fromRgbF(0.18F, 0.18F, 0.18F, 0.37F));
  std::vector<ColorPixel> clipped_patch_pixels(4, clipped_base_pixel);
  ColorAdjustmentState clipped_patch_state;
  clipped_patch_state.local_masks = {
      LocalMask{
          .kind = "brush",
          .operation = "add",
          .retouch_mode = "remove",
          .enabled = true,
          .remove_strength = 100.0,
          .segmentation_mask = {255, 255, 255, 255},
          .segmentation_width = 4,
          .segmentation_height = 1,
          .inpaint_patch_pixels = {
              linear_pixel_from_srgb(QColor::fromRgbF(0.90F, 0.05F, 0.05F, 1.0F)),
              linear_pixel_from_srgb(QColor::fromRgbF(0.05F, 0.90F, 0.05F, 1.0F)),
              linear_pixel_from_srgb(QColor::fromRgbF(0.05F, 0.05F, 0.90F, 1.0F)),
          },
          .inpaint_patch_x = -1,
          .inpaint_patch_y = 0,
          .inpaint_patch_width = 3,
          .inpaint_patch_height = 1,
          .inpaint_model_name = "bounds-test",
          .inpaint_blend_amount = 1.0,
          .inpaint_seam_feather = 0.0,
          .inpaint_color_match = 0.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &clipped_patch_pixels, 4, 1, clipped_patch_state);
  if (!(clipped_patch_pixels[0].green > clipped_patch_pixels[0].red &&
        clipped_patch_pixels[1].blue > clipped_patch_pixels[1].green &&
        pixel_close(clipped_patch_pixels[2], clipped_base_pixel) &&
        pixel_close(clipped_patch_pixels[3], clipped_base_pixel))) {
    return fail("inpaint patch bounds should clip outside-image patch pixels without shifting the visible patch");
  }
  if (!almost_equal(clipped_patch_pixels[0].alpha, clipped_base_pixel.alpha, 1e-6) ||
      !almost_equal(clipped_patch_pixels[1].alpha, clipped_base_pixel.alpha, 1e-6)) {
    return fail("inpaint patch compositing should preserve target alpha while replacing RGB");
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
  std::vector<ColorPixel> masked_advanced_tone_layer_pixels(
      2,
      linear_pixel_from_srgb(QColor::fromRgbF(0.32F, 0.32F, 0.32F, 1.0F)));
  const auto masked_advanced_tone_luminance_before =
      luminance(masked_advanced_tone_layer_pixels[0]);
  ColorAdjustmentState masked_advanced_tone_layer_state;
  masked_advanced_tone_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-advanced-tone-layer",
          .name = "Masked Advanced Tone",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .brightness = 70.0,
          .filmic_toe = 35.0,
          .adaptive_contrast = 45.0,
          .levels_gamma = 45.0,
          .zone_midtones = 55.0,
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &masked_advanced_tone_layer_pixels,
      2,
      1,
      masked_advanced_tone_layer_state);
  if (!(almost_equal(
            luminance(masked_advanced_tone_layer_pixels[0]),
            masked_advanced_tone_luminance_before,
            1e-5) &&
        luminance(masked_advanced_tone_layer_pixels[1]) >
            masked_advanced_tone_luminance_before * 1.20)) {
    return fail("layer-local advanced tone controls should be gated by the layer mask alpha");
  }
  std::vector<ColorPixel> masked_curve_layer_pixels(
      2,
      linear_pixel_from_srgb(QColor::fromRgbF(0.62F, 0.62F, 0.62F, 1.0F)));
  const auto masked_curve_luminance_before = luminance(masked_curve_layer_pixels[0]);
  ColorAdjustmentState masked_curve_layer_state;
  masked_curve_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-curve-layer",
          .name = "Masked Curve",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .master_curve_points = {
              CurvePoint{.x = 0.0, .y = 0.0},
              CurvePoint{.x = 1.0, .y = 0.25},
          },
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &masked_curve_layer_pixels,
      2,
      1,
      masked_curve_layer_state);
  if (!(almost_equal(luminance(masked_curve_layer_pixels[0]), masked_curve_luminance_before, 1e-5) &&
        luminance(masked_curve_layer_pixels[1]) < masked_curve_luminance_before * 0.45)) {
    return fail("layer-local tone curves should be gated by the layer mask alpha");
  }
  std::vector<ColorPixel> channel_curve_layer_pixels = {
      ColorPixel{0.48F, 0.48F, 0.48F, 1.0F},
  };
  ColorAdjustmentState channel_curve_layer_state;
  channel_curve_layer_state.photo_layers = {
      PhotoLayer{
          .id = "channel-curve-layer",
          .name = "Red Channel Curve",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .red_curve_points = {
              CurvePoint{.x = 0.0, .y = 0.0},
              CurvePoint{.x = 1.0, .y = 0.0},
          },
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &channel_curve_layer_pixels,
      1,
      1,
      channel_curve_layer_state);
  if (!(channel_curve_layer_pixels[0].red < 0.01F &&
        almost_equal(channel_curve_layer_pixels[0].green, 0.48, 1e-5) &&
        almost_equal(channel_curve_layer_pixels[0].blue, 0.48, 1e-5))) {
    return fail("layer-local channel curves should affect the selected channel without touching other channels");
  }
  const auto layer_color_red = linear_pixel_from_srgb(QColor::fromHslF(0.0F, 0.70F, 0.50F, 1.0F));
  std::vector<ColorPixel> masked_color_mixer_layer_pixels = {layer_color_red, layer_color_red};
  ColorAdjustmentState masked_color_mixer_layer_state;
  masked_color_mixer_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-color-mixer-layer",
          .name = "Masked Color Mixer",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .color_mixer_channel = "Reds",
          .hue_adjustment = 80.0,
          .saturation_adjustment = -45.0,
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &masked_color_mixer_layer_pixels,
      2,
      1,
      masked_color_mixer_layer_state);
  if (!pixel_close(masked_color_mixer_layer_pixels[0], layer_color_red, 1e-5) ||
      pixel_close(masked_color_mixer_layer_pixels[1], layer_color_red, 1e-3)) {
    return fail("layer-local HSL/color mixer edits should be gated by the layer mask alpha");
  }
  const auto rgb_mixer_source = ColorPixel{0.20F, 0.35F, 0.65F, 1.0F};
  std::vector<ColorPixel> masked_rgb_mixer_layer_pixels = {rgb_mixer_source, rgb_mixer_source};
  ColorAdjustmentState masked_rgb_mixer_layer_state;
  masked_rgb_mixer_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-rgb-mixer-layer",
          .name = "Masked RGB Mixer",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .rgb_channel_mixer = {
              .red_from_red = 0.0,
              .red_from_green = 0.0,
              .red_from_blue = 100.0,
              .green_from_red = 0.0,
              .green_from_green = 100.0,
              .green_from_blue = 0.0,
              .blue_from_red = 0.0,
              .blue_from_green = 0.0,
              .blue_from_blue = 100.0,
          },
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &masked_rgb_mixer_layer_pixels,
      2,
      1,
      masked_rgb_mixer_layer_state);
  if (!pixel_close(masked_rgb_mixer_layer_pixels[0], rgb_mixer_source, 1e-5) ||
      !(masked_rgb_mixer_layer_pixels[1].red > rgb_mixer_source.red * 2.5F &&
        almost_equal(masked_rgb_mixer_layer_pixels[1].green, rgb_mixer_source.green, 1e-5))) {
    return fail("layer-local RGB channel mixer edits should be gated by the layer mask alpha");
  }
  const auto bw_mixer_source = linear_pixel_from_srgb(QColor::fromRgbF(0.78F, 0.18F, 0.10F, 1.0F));
  std::vector<ColorPixel> masked_bw_layer_pixels = {bw_mixer_source, bw_mixer_source};
  ColorAdjustmentState masked_bw_layer_state;
  masked_bw_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-bw-layer",
          .name = "Masked B&W Mixer",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .black_and_white_enabled = true,
          .black_and_white_mixer = {20.0, 0.0, 0.0, 0.0, 0.0, -20.0, 0.0, 0.0},
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &masked_bw_layer_pixels,
      2,
      1,
      masked_bw_layer_state);
  if (!pixel_close(masked_bw_layer_pixels[0], bw_mixer_source, 1e-5) ||
      !(almost_equal(masked_bw_layer_pixels[1].red, masked_bw_layer_pixels[1].green, 1e-5) &&
        almost_equal(masked_bw_layer_pixels[1].green, masked_bw_layer_pixels[1].blue, 1e-5))) {
    return fail("layer-local black and white mixer edits should be gated by the layer mask alpha");
  }
  const auto grading_source = linear_pixel_from_srgb(QColor::fromRgbF(0.45F, 0.45F, 0.45F, 1.0F));
  std::vector<ColorPixel> masked_grading_layer_pixels = {grading_source, grading_source};
  ColorAdjustmentState masked_grading_layer_state;
  masked_grading_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-grading-layer",
          .name = "Masked Color Grade",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .grading_midtones_hue = 0.0,
          .grading_midtones_saturation = 100.0,
          .grading_amount = 100.0,
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &masked_grading_layer_pixels,
      2,
      1,
      masked_grading_layer_state);
  if (!pixel_close(masked_grading_layer_pixels[0], grading_source, 1e-5) ||
      !(masked_grading_layer_pixels[1].red > masked_grading_layer_pixels[1].blue &&
        masked_grading_layer_pixels[1].red > masked_grading_layer_pixels[1].green)) {
    return fail("layer-local color grading edits should be gated by the layer mask alpha");
  }
  const auto lut_source = linear_pixel_from_srgb(QColor::fromRgbF(0.32F, 0.28F, 0.24F, 1.0F));
  LutAdjustment red_lut;
  red_lut.enabled = true;
  red_lut.strength = 100.0;
  red_lut.name = "Layer Red LUT";
  red_lut.size = 2;
  red_lut.table.assign(
      8,
      ColorPixel{
          .red = 1.0F,
          .green = 0.0F,
          .blue = 0.0F,
          .alpha = 1.0F,
      });
  std::vector<ColorPixel> masked_lut_layer_pixels = {lut_source, lut_source};
  ColorAdjustmentState masked_lut_layer_state;
  masked_lut_layer_state.photo_layers = {
      PhotoLayer{
          .id = "masked-lut-layer",
          .name = "Masked LUT",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .mask_data = {0, 255},
          .mask_width = 2,
          .mask_height = 1,
          .lut_stack = {red_lut},
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &masked_lut_layer_pixels,
      2,
      1,
      masked_lut_layer_state);
  if (!pixel_close(masked_lut_layer_pixels[0], lut_source, 1e-5) ||
      !(masked_lut_layer_pixels[1].red > 0.95F &&
        masked_lut_layer_pixels[1].green < 0.05F &&
        masked_lut_layer_pixels[1].blue < 0.05F)) {
    return fail("layer-local LUT stacks should be gated by the layer mask alpha");
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
  const auto opacity_formula_base = ColorPixel{0.20F, 0.40F, 0.60F, 1.0F};
  const auto opacity_formula_source = ColorPixel{0.80F, 0.10F, 0.30F, 0.50F};
  std::vector<ColorPixel> opacity_formula_pixels = {opacity_formula_base};
  ColorAdjustmentState opacity_formula_state;
  opacity_formula_state.photo_layers = {
      PhotoLayer{
          .id = "opacity-formula-layer",
          .name = "Opacity Formula",
          .kind = "fill",
          .enabled = true,
          .opacity = 0.40,
          .fill_color = opacity_formula_source,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &opacity_formula_pixels, 1, 1, opacity_formula_state);
  const auto expected_opacity_formula_pixel = expected_composited_layer_pixel(
      opacity_formula_base,
      opacity_formula_source,
      opacity_formula_source.alpha * opacity_formula_state.photo_layers[0].opacity,
      "normal");
  if (!pixel_close(opacity_formula_pixels[0], expected_opacity_formula_pixel, 1e-6)) {
    return fail("photo layer opacity should equal source alpha times layer opacity in the compositing formula");
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
  std::vector<ColorPixel> normal_adjustment_blend_pixels = {
      ColorPixel{0.25F, 0.25F, 0.25F, 1.0F}};
  ColorAdjustmentState normal_adjustment_blend_state;
  normal_adjustment_blend_state.photo_layers = {
      PhotoLayer{
          .id = "normal-adjustment-blend-layer",
          .name = "Normal Adjustment Blend",
          .kind = "adjustment",
          .blend_mode = "normal",
          .enabled = true,
          .opacity = 1.0,
          .exposure = 1.0,
      },
  };
  photo_editor::pipeline::apply_color_adjustments(
      &normal_adjustment_blend_pixels,
      1,
      1,
      normal_adjustment_blend_state);
  std::vector<ColorPixel> screen_adjustment_blend_pixels = {
      ColorPixel{0.25F, 0.25F, 0.25F, 1.0F}};
  ColorAdjustmentState screen_adjustment_blend_state = normal_adjustment_blend_state;
  screen_adjustment_blend_state.photo_layers.front().blend_mode = "screen";
  photo_editor::pipeline::apply_color_adjustments(
      &screen_adjustment_blend_pixels,
      1,
      1,
      screen_adjustment_blend_state);
  if (!(screen_adjustment_blend_pixels[0].red > normal_adjustment_blend_pixels[0].red)) {
    return fail("screen blend mode should affect adjustment photo layers, not only fill layers");
  }
  const auto blend_formula_base = ColorPixel{0.35F, 0.45F, 0.65F, 1.0F};
  const auto blend_formula_source = ColorPixel{0.75F, 0.20F, 0.40F, 1.0F};
  for (const auto& mode : {
           "normal",
           "multiply",
           "screen",
           "overlay",
           "soft-light",
           "hard-light",
           "difference",
           "lighten",
           "darken",
           "linear-light",
       }) {
    std::vector<ColorPixel> blend_formula_pixels = {blend_formula_base};
    ColorAdjustmentState blend_formula_state;
    blend_formula_state.photo_layers = {
        PhotoLayer{
            .id = "blend-formula-layer",
            .name = mode,
            .kind = "fill",
            .blend_mode = mode,
            .enabled = true,
            .opacity = 1.0,
            .fill_color = blend_formula_source,
        },
    };
    photo_editor::pipeline::apply_color_adjustments(
        &blend_formula_pixels, 1, 1, blend_formula_state);
    const auto expected_blend_formula_pixel =
        expected_composited_layer_pixel(blend_formula_base, blend_formula_source, 1.0, mode);
    if (!pixel_close(blend_formula_pixels[0], expected_blend_formula_pixel, 1e-6)) {
      return fail("photo layer blend modes should match their documented channel formulas");
    }
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
          .segmentation_mask = {0, 0, 0, 0, 255},
          .segmentation_width = 5,
          .segmentation_height = 1,
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
