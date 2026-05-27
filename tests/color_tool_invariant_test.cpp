#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::CurvePoint;
using photo_editor::pipeline::LutAdjustment;

constexpr double kEpsilon = 1.0e-6;

struct NamedPixel final {
  std::string_view name;
  ColorPixel pixel;
};

bool check(bool condition, std::string_view message, int* failures) {
  if (condition) {
    return true;
  }
  std::cerr << message << '\n';
  ++(*failures);
  return false;
}

double luminance(const ColorPixel& pixel) {
  return 0.2126 * static_cast<double>(pixel.red) +
         0.7152 * static_cast<double>(pixel.green) +
         0.0722 * static_cast<double>(pixel.blue);
}

double max_channel(const ColorPixel& pixel) {
  return std::max({
      static_cast<double>(pixel.red),
      static_cast<double>(pixel.green),
      static_cast<double>(pixel.blue),
  });
}

double channel_spread(const ColorPixel& pixel) {
  return std::max({
      std::abs(static_cast<double>(pixel.red) - static_cast<double>(pixel.green)),
      std::abs(static_cast<double>(pixel.green) - static_cast<double>(pixel.blue)),
      std::abs(static_cast<double>(pixel.red) - static_cast<double>(pixel.blue)),
  });
}

bool finite_pixel(const ColorPixel& pixel) {
  return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
         std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
}

double rgb_hue(const ColorPixel& pixel) {
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto delta = maximum - minimum;
  if (delta <= kEpsilon) {
    return -1.0;
  }
  auto hue = 0.0;
  if (maximum == red) {
    hue = (green - blue) / delta;
    if (hue < 0.0) {
      hue += 6.0;
    }
  } else if (maximum == green) {
    hue = ((blue - red) / delta) + 2.0;
  } else {
    hue = ((red - green) / delta) + 4.0;
  }
  return hue / 6.0;
}

double circular_hue_distance(double left, double right) {
  auto distance = std::abs(left - right);
  return std::min(distance, 1.0 - distance);
}

ColorAdjustmentState base_state() {
  ColorAdjustmentState state;
  state.color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  state.color_pipeline.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  state.color_pipeline.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  state.color_pipeline.output_headroom = 1.0;
  return state;
}

std::vector<ColorPixel> apply_pixels(
    std::vector<ColorPixel> pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  photo_editor::pipeline::apply_color_adjustments(&pixels, width, height, state);
  return pixels;
}

ColorPixel apply_pixel(const ColorPixel& pixel, const ColorAdjustmentState& state) {
  return apply_pixels({pixel}, 1, 1, state).front();
}

ColorPixel gray(double value) {
  const auto safe = static_cast<float>(value);
  return ColorPixel{.red = safe, .green = safe, .blue = safe, .alpha = 1.0F};
}

std::vector<NamedPixel> corpus() {
  return {
      {"black", gray(0.0)},
      {"shadow neutral", gray(0.018)},
      {"middle neutral", gray(0.18)},
      {"highlight neutral", gray(0.82)},
      {"overrange neutral", gray(1.45)},
      {"skin warm", ColorPixel{0.52F, 0.31F, 0.22F, 1.0F}},
      {"skin red", ColorPixel{0.66F, 0.34F, 0.28F, 1.0F}},
      {"red", ColorPixel{0.92F, 0.04F, 0.03F, 1.0F}},
      {"green", ColorPixel{0.04F, 0.82F, 0.08F, 1.0F}},
      {"blue", ColorPixel{0.05F, 0.14F, 0.94F, 1.0F}},
      {"cyan", ColorPixel{0.02F, 0.76F, 0.86F, 1.0F}},
      {"magenta", ColorPixel{0.88F, 0.05F, 0.74F, 1.0F}},
      {"yellow", ColorPixel{0.94F, 0.82F, 0.04F, 1.0F}},
      {"hot red", ColorPixel{1.65F, 0.10F, 0.05F, 1.0F}},
      {"hot warm", ColorPixel{1.55F, 1.12F, 0.58F, 1.0F}},
  };
}

void require_finite_and_alpha(
    std::string_view label,
    const std::vector<ColorPixel>& input,
    const std::vector<ColorPixel>& output,
    int* failures) {
  check(input.size() == output.size(), "output should preserve pixel count", failures);
  const auto count = std::min(input.size(), output.size());
  for (std::size_t index = 0; index < count; ++index) {
    check(finite_pixel(output[index]), std::string(label) + " should produce finite pixels", failures);
    check(
        std::abs(static_cast<double>(input[index].alpha) - static_cast<double>(output[index].alpha)) <=
            1.0e-6,
        std::string(label) + " should preserve alpha",
        failures);
  }
}

void require_sdr_headroom(
    std::string_view label,
    const std::vector<ColorPixel>& output,
    int* failures) {
  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto& pixel = output[index];
    std::ostringstream over_message;
    over_message << label << " sample " << index
                 << " should not exceed SDR output headroom; max_channel="
                 << max_channel(pixel);
    std::ostringstream under_message;
    under_message << label << " sample " << index
                  << " should not create negative SDR channels; rgb=("
                  << pixel.red << ", " << pixel.green << ", " << pixel.blue << ")";
    check(
        max_channel(pixel) <= 1.0 + 1.0e-5,
        over_message.str(),
        failures);
    check(
        pixel.red >= -1.0e-5F && pixel.green >= -1.0e-5F && pixel.blue >= -1.0e-5F,
        under_message.str(),
        failures);
  }
}

std::vector<ColorPixel> corpus_pixels() {
  std::vector<ColorPixel> pixels;
  for (const auto& sample : corpus()) {
    pixels.push_back(sample.pixel);
  }
  return pixels;
}

std::vector<ColorPixel> neutral_ramp() {
  return {
      gray(0.0), gray(0.006), gray(0.018), gray(0.055), gray(0.18),
      gray(0.36), gray(0.62), gray(0.86), gray(1.18), gray(1.80),
  };
}

ColorPixel average_pixel(const std::vector<ColorPixel>& pixels) {
  auto red = 0.0;
  auto green = 0.0;
  auto blue = 0.0;
  for (const auto& pixel : pixels) {
    red += pixel.red;
    green += pixel.green;
    blue += pixel.blue;
  }
  const auto scale = pixels.empty() ? 0.0 : 1.0 / static_cast<double>(pixels.size());
  return ColorPixel{
      .red = static_cast<float>(red * scale),
      .green = static_cast<float>(green * scale),
      .blue = static_cast<float>(blue * scale),
      .alpha = 1.0F,
  };
}

double luminance_variance(const std::vector<ColorPixel>& pixels) {
  if (pixels.empty()) {
    return 0.0;
  }
  auto mean = 0.0;
  for (const auto& pixel : pixels) {
    mean += luminance(pixel);
  }
  mean /= static_cast<double>(pixels.size());
  auto variance = 0.0;
  for (const auto& pixel : pixels) {
    const auto delta = luminance(pixel) - mean;
    variance += delta * delta;
  }
  return variance / static_cast<double>(pixels.size());
}

double chroma_variance(const std::vector<ColorPixel>& pixels) {
  if (pixels.empty()) {
    return 0.0;
  }
  const auto average = average_pixel(pixels);
  auto variance = 0.0;
  for (const auto& pixel : pixels) {
    const auto red_delta = static_cast<double>(pixel.red) - average.red;
    const auto green_delta = static_cast<double>(pixel.green) - average.green;
    const auto blue_delta = static_cast<double>(pixel.blue) - average.blue;
    const auto luma_delta = luminance(pixel) - luminance(average);
    variance += red_delta * red_delta + green_delta * green_delta + blue_delta * blue_delta -
                luma_delta * luma_delta;
  }
  return variance / static_cast<double>(pixels.size());
}

LutAdjustment make_identity_lut_adjustment() {
  LutAdjustment lut;
  lut.enabled = true;
  lut.strength = 100.0;
  lut.size = 2;
  lut.table = {
      ColorPixel{0.0F, 0.0F, 0.0F, 1.0F},
      ColorPixel{0.0F, 0.0F, 1.0F, 1.0F},
      ColorPixel{0.0F, 1.0F, 0.0F, 1.0F},
      ColorPixel{0.0F, 1.0F, 1.0F, 1.0F},
      ColorPixel{1.0F, 0.0F, 0.0F, 1.0F},
      ColorPixel{1.0F, 0.0F, 1.0F, 1.0F},
      ColorPixel{1.0F, 1.0F, 0.0F, 1.0F},
      ColorPixel{1.0F, 1.0F, 1.0F, 1.0F},
  };
  return lut;
}

}  // namespace

int main() {
  auto failures = 0;

  const auto base = base_state();
  const auto source_corpus = corpus_pixels();

  {
    const auto output = apply_pixels(source_corpus, static_cast<int>(source_corpus.size()), 1, base);
    require_finite_and_alpha("neutral", source_corpus, output, &failures);
    for (std::size_t index = 0; index < source_corpus.size(); ++index) {
      if (max_channel(source_corpus[index]) > 1.0 + 1.0e-6 ||
          source_corpus[index].red < -1.0e-6F ||
          source_corpus[index].green < -1.0e-6F ||
          source_corpus[index].blue < -1.0e-6F) {
        continue;
      }
      check(
          std::abs(static_cast<double>(source_corpus[index].red) - output[index].red) <= 1.0e-5 &&
              std::abs(static_cast<double>(source_corpus[index].green) - output[index].green) <= 1.0e-5 &&
              std::abs(static_cast<double>(source_corpus[index].blue) - output[index].blue) <= 1.0e-5,
          "neutral recipe should preserve working-space pixels",
          &failures);
    }
  }

  {
    auto state = base;
    state.exposure = 1.0;
    state.contrast = 24.0;
    state.whites = 32.0;
    state.highlights = -18.0;
    const auto output = apply_pixels(source_corpus, static_cast<int>(source_corpus.size()), 1, state);
    require_finite_and_alpha("tone", source_corpus, output, &failures);
    require_sdr_headroom("tone", output, &failures);
  }

  {
    auto state = base;
    state.contrast = 36.0;
    state.shadows = 28.0;
    state.blacks = -18.0;
    state.filmic_toe = 20.0;
    state.levels_black_point = 6.0;
    state.zone_deep_shadows = 22.0;
    const auto input = neutral_ramp();
    const auto output = apply_pixels(input, static_cast<int>(input.size()), 1, state);
    require_finite_and_alpha("shadow controls", input, output, &failures);
    for (std::size_t index = 1; index < output.size(); ++index) {
      check(
          luminance(output[index]) + 1.0e-6 >= luminance(output[index - 1]),
          "shadow and contrast controls should preserve neutral-ramp monotonicity",
          &failures);
    }
  }

  {
    auto state = base;
    state.temperature = 42.0;
    state.tint = -24.0;
    const auto input = std::vector<ColorPixel>{
        gray(0.0), gray(0.006), gray(0.018), gray(0.055), gray(0.18),
        gray(0.36), gray(0.62), gray(0.86),
    };
    const auto output = apply_pixels(input, static_cast<int>(input.size()), 1, state);
    require_finite_and_alpha("white balance", input, output, &failures);
    for (std::size_t index = 1; index < output.size(); ++index) {
      check(
          luminance(output[index]) + 1.0e-6 >= luminance(output[index - 1]),
          "white balance should preserve neutral-ramp luminance order",
          &failures);
    }
    require_sdr_headroom("white balance", output, &failures);
  }

  {
    auto state = base;
    state.curve_shadows = -24.0;
    state.curve_darks = -8.0;
    state.curve_lights = 14.0;
    state.curve_highlights = 18.0;
    state.master_curve_points = {
        CurvePoint{0.0, 0.0},
        CurvePoint{0.30, 0.24},
        CurvePoint{0.72, 0.82},
        CurvePoint{1.0, 1.0},
    };
    state.luma_curve_points = {
        CurvePoint{0.0, 0.0},
        CurvePoint{0.50, 0.55},
        CurvePoint{1.0, 1.0},
    };
    const auto input = neutral_ramp();
    const auto output = apply_pixels(input, static_cast<int>(input.size()), 1, state);
    require_finite_and_alpha("curves", input, output, &failures);
    for (std::size_t index = 1; index < output.size(); ++index) {
      check(
          luminance(output[index]) + 1.0e-6 >= luminance(output[index - 1]),
          "tone curves should preserve neutral-ramp monotonicity",
          &failures);
      check(channel_spread(output[index]) <= 1.0e-5, "luma/master curves should preserve neutral axis", &failures);
    }
  }

  {
    auto state = base;
    state.vibrance = 54.0;
    state.saturation = 28.0;
    state.color_mixer_model = "OKLab";
    state.color_mixer_channels[0].hue = 18.0;
    state.color_mixer_channels[0].saturation = 22.0;
    state.color_mixer_channels[0].luminance = -12.0;
    state.color_mixer_channels[5].hue = -12.0;
    state.color_mixer_channels[5].saturation = 30.0;
    state.color_replacement_enabled = true;
    state.color_replacement_source_hue = 0.0;
    state.color_replacement_target_hue = 0.62;
    state.color_replacement_range = 40.0;
    state.color_replacement_strength = 35.0;
    state.skin_tone_protection_enabled = true;
    state.skin_tone_protection_strength = 60.0;
    state.color_harmony_enabled = true;
    state.color_harmony_mode = "Triadic";
    state.color_harmony_base_hue = 0.06;
    state.color_harmony_strength = 30.0;
    const auto output = apply_pixels(source_corpus, static_cast<int>(source_corpus.size()), 1, state);
    require_finite_and_alpha("color tools", source_corpus, output, &failures);
    require_sdr_headroom("color tools", output, &failures);
    const auto red_before = rgb_hue(ColorPixel{0.92F, 0.04F, 0.03F, 1.0F});
    const auto red_after = rgb_hue(apply_pixel(ColorPixel{0.92F, 0.04F, 0.03F, 1.0F}, state));
    check(
        red_before >= 0.0 && red_after >= 0.0 &&
            circular_hue_distance(red_after, 0.62) < circular_hue_distance(red_before, 0.62),
        "color replacement should move selected red hue toward target hue",
        &failures);
  }

  {
    auto state = base;
    state.grading_shadows_hue = 220.0;
    state.grading_shadows_saturation = 28.0;
    state.grading_midtones_hue = 36.0;
    state.grading_midtones_saturation = 22.0;
    state.grading_highlights_hue = 54.0;
    state.grading_highlights_saturation = 18.0;
    state.rgb_channel_mixer.red_from_red = 92.0;
    state.rgb_channel_mixer.red_from_green = 8.0;
    state.rgb_channel_mixer.green_from_red = -4.0;
    state.rgb_channel_mixer.green_from_green = 104.0;
    const auto output = apply_pixels(source_corpus, static_cast<int>(source_corpus.size()), 1, state);
    require_finite_and_alpha("grading/channel mixers", source_corpus, output, &failures);
    require_sdr_headroom("grading/channel mixers", output, &failures);
  }

  {
    auto state = base;
    state.black_and_white_enabled = true;
    state.black_and_white_mixer[0] = 12.0;
    state.black_and_white_mixer[5] = -10.0;
    const auto output = apply_pixels(source_corpus, static_cast<int>(source_corpus.size()), 1, state);
    require_finite_and_alpha("black-and-white mixer", source_corpus, output, &failures);
    require_sdr_headroom("black-and-white mixer", output, &failures);
    for (const auto& pixel : output) {
      check(channel_spread(pixel) <= 1.0e-5, "black-and-white output should be achromatic", &failures);
    }
  }

  {
    auto state = base;
    state.lut_stack.push_back(make_identity_lut_adjustment());
    std::vector<ColorPixel> lut_domain_corpus;
    for (const auto& sample : source_corpus) {
      if (max_channel(sample) <= 1.0 + 1.0e-6 &&
          sample.red >= -1.0e-6F &&
          sample.green >= -1.0e-6F &&
          sample.blue >= -1.0e-6F) {
        lut_domain_corpus.push_back(sample);
      }
    }
    const auto output = apply_pixels(lut_domain_corpus, static_cast<int>(lut_domain_corpus.size()), 1, state);
    require_finite_and_alpha("identity LUT", lut_domain_corpus, output, &failures);
    for (std::size_t index = 0; index < lut_domain_corpus.size(); ++index) {
      check(
          std::abs(static_cast<double>(lut_domain_corpus[index].red) - output[index].red) <= 1.0e-5 &&
              std::abs(static_cast<double>(lut_domain_corpus[index].green) - output[index].green) <= 1.0e-5 &&
              std::abs(static_cast<double>(lut_domain_corpus[index].blue) - output[index].blue) <= 1.0e-5,
          "identity user LUT should preserve pixels",
          &failures);
    }
  }

  {
    std::vector<ColorPixel> flat(16, ColorPixel{0.35F, 0.35F, 0.35F, 1.0F});
    auto state = base;
    state.texture = 60.0;
    state.clarity = 40.0;
    state.dehaze = 30.0;
    state.sharpening = 80.0;
    state.sharpen_masking = 40.0;
    const auto output = apply_pixels(flat, 4, 4, state);
    require_finite_and_alpha("presence/detail flat field", flat, output, &failures);
    for (const auto& pixel : output) {
      check(
          std::abs(static_cast<double>(pixel.red) - 0.35) <= 2.0e-4 &&
              std::abs(static_cast<double>(pixel.green) - 0.35) <= 2.0e-4 &&
              std::abs(static_cast<double>(pixel.blue) - 0.35) <= 2.0e-4,
          "presence/detail tools should not alter a perfectly flat field",
          &failures);
    }
  }

  {
    std::vector<ColorPixel> noisy = {
        ColorPixel{0.30F, 0.36F, 0.28F, 1.0F}, ColorPixel{0.43F, 0.30F, 0.45F, 1.0F},
        ColorPixel{0.32F, 0.39F, 0.31F, 1.0F}, ColorPixel{0.46F, 0.33F, 0.43F, 1.0F},
        ColorPixel{0.31F, 0.37F, 0.29F, 1.0F}, ColorPixel{0.44F, 0.31F, 0.46F, 1.0F},
        ColorPixel{0.33F, 0.40F, 0.30F, 1.0F}, ColorPixel{0.45F, 0.32F, 0.44F, 1.0F},
        ColorPixel{0.30F, 0.38F, 0.29F, 1.0F}, ColorPixel{0.43F, 0.30F, 0.45F, 1.0F},
        ColorPixel{0.32F, 0.39F, 0.31F, 1.0F}, ColorPixel{0.46F, 0.33F, 0.43F, 1.0F},
        ColorPixel{0.31F, 0.37F, 0.29F, 1.0F}, ColorPixel{0.44F, 0.31F, 0.46F, 1.0F},
        ColorPixel{0.33F, 0.40F, 0.30F, 1.0F}, ColorPixel{0.45F, 0.32F, 0.44F, 1.0F},
    };
    auto state = base;
    state.luminance_nr = 80.0;
    state.color_nr = 80.0;
    const auto output = apply_pixels(noisy, 4, 4, state);
    require_finite_and_alpha("noise reduction", noisy, output, &failures);
    check(
        luminance_variance(output) < luminance_variance(noisy),
        "luminance noise reduction should reduce luminance variance",
        &failures);
    check(
        chroma_variance(output) < chroma_variance(noisy),
        "color noise reduction should reduce chroma variance",
        &failures);
  }

  if (failures > 0) {
    std::cerr << "color tool invariant failures: " << failures << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
