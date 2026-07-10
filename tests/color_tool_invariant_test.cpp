#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::CurvePoint;
using photo_editor::pipeline::LutAdjustment;
using photo_editor::pipeline::PhotoLayer;

constexpr double kEpsilon = 1.0e-6;
constexpr double kToneChromaRelationshipTolerance = 5.0e-4;

struct NamedPixel final {
  std::string_view name;
  ColorPixel pixel;
};

struct NeutralRampCase final {
  std::string name;
  ColorAdjustmentState state;
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

std::array<double, 3> channel_relationships(const ColorPixel& pixel) {
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
  const auto sum = std::max(kEpsilon, red + green + blue);
  return {red / sum, green / sum, blue / sum};
}

double channel_relationship_error(const ColorPixel& before, const ColorPixel& after) {
  const auto before_relationships = channel_relationships(before);
  const auto after_relationships = channel_relationships(after);
  auto error = 0.0;
  for (std::size_t index = 0; index < before_relationships.size(); ++index) {
    error = std::max(error, std::abs(before_relationships[index] - after_relationships[index]));
  }
  return error;
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

void require_neutral_ramp_preserved(
    std::string_view label,
    const std::vector<ColorPixel>& input,
    const std::vector<ColorPixel>& output,
    int* failures) {
  require_finite_and_alpha(label, input, output, failures);
  const auto count = std::min(input.size(), output.size());
  for (std::size_t index = 0; index < count; ++index) {
    std::ostringstream neutral_message;
    neutral_message << label << " sample " << index
                    << " should preserve the neutral axis; rgb=("
                    << output[index].red << ", " << output[index].green << ", "
                    << output[index].blue << ")";
    std::ostringstream luminance_message;
    luminance_message << label << " sample " << index
                      << " should preserve neutral luminance; before="
                      << luminance(input[index]) << " after=" << luminance(output[index]);
    check(channel_spread(output[index]) <= 1.0e-5, neutral_message.str(), failures);
    check(
        std::abs(luminance(output[index]) - luminance(input[index])) <= 2.0e-5,
        luminance_message.str(),
        failures);
  }
}

void require_chroma_relationships_preserved(
    std::string_view label,
    const std::vector<ColorPixel>& input,
    const std::vector<ColorPixel>& output,
    int* failures) {
  require_finite_and_alpha(label, input, output, failures);
  const auto count = std::min(input.size(), output.size());
  for (std::size_t index = 0; index < count; ++index) {
    std::ostringstream relationship_message;
    relationship_message << label << " sample " << index
                         << " should preserve normalized channel relationships; error="
                         << channel_relationship_error(input[index], output[index]);
    check(
        channel_relationship_error(input[index], output[index]) <= kToneChromaRelationshipTolerance,
        relationship_message.str(),
        failures);

    const auto before_hue = rgb_hue(input[index]);
    const auto after_hue = rgb_hue(output[index]);
    std::ostringstream hue_message;
    hue_message << label << " sample " << index
                << " should preserve hue; before=" << before_hue << " after=" << after_hue;
    check(
        before_hue >= 0.0 && after_hue >= 0.0 &&
            circular_hue_distance(before_hue, after_hue) <= kToneChromaRelationshipTolerance,
        hue_message.str(),
        failures);
  }
}

void require_pixels_match(
    std::string_view label,
    const std::vector<ColorPixel>& expected,
    const std::vector<ColorPixel>& actual,
    int* failures) {
  check(expected.size() == actual.size(), std::string(label) + " should preserve pixel count", failures);
  const auto count = std::min(expected.size(), actual.size());
  for (std::size_t index = 0; index < count; ++index) {
    std::ostringstream message;
    message << label << " sample " << index << " should match the default render; expected=("
            << expected[index].red << ", " << expected[index].green << ", " << expected[index].blue
            << ", " << expected[index].alpha << ") actual=(" << actual[index].red << ", "
            << actual[index].green << ", " << actual[index].blue << ", " << actual[index].alpha
            << ")";
    check(
        std::abs(static_cast<double>(expected[index].red) - static_cast<double>(actual[index].red)) <= 1.0e-5 &&
            std::abs(static_cast<double>(expected[index].green) - static_cast<double>(actual[index].green)) <=
                1.0e-5 &&
            std::abs(static_cast<double>(expected[index].blue) - static_cast<double>(actual[index].blue)) <=
                1.0e-5 &&
            std::abs(static_cast<double>(expected[index].alpha) - static_cast<double>(actual[index].alpha)) <=
                1.0e-6,
        message.str(),
        failures);
  }
}

void require_rgb_channels_monotonic(std::string_view label, const std::vector<ColorPixel>& output, int* failures) {
  for (std::size_t index = 1; index < output.size(); ++index) {
    std::ostringstream message;
    message << label << " sample " << index
            << " should stay monotonic on neutral ramps; previous=("
            << output[index - 1].red << ", " << output[index - 1].green << ", "
            << output[index - 1].blue << ") current=(" << output[index].red << ", "
            << output[index].green << ", " << output[index].blue << ")";
    check(
        output[index].red + 1.0e-5F >= output[index - 1].red &&
            output[index].green + 1.0e-5F >= output[index - 1].green &&
            output[index].blue + 1.0e-5F >= output[index - 1].blue,
        message.str(),
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
    const auto expected = apply_pixels(source_corpus, static_cast<int>(source_corpus.size()), 1, base);
    std::vector<NeutralRampCase> cases;

    auto state = base;
    cases.push_back(NeutralRampCase{"raw/input transform defaults", state});

    state = base;
    state.exposure = 0.0;
    state.contrast = 0.0;
    state.highlights = 0.0;
    state.shadows = 0.0;
    state.whites = 0.0;
    state.blacks = 0.0;
    state.brightness = 0.0;
    cases.push_back(NeutralRampCase{"light defaults", state});

    state = base;
    state.hdr_local_tone_mapping = 0.0;
    state.filmic_shoulder = 0.0;
    state.filmic_toe = 0.0;
    state.adaptive_contrast = 0.0;
    state.levels_black_point = 0.0;
    state.levels_gamma = 0.0;
    state.levels_white_point = 0.0;
    cases.push_back(NeutralRampCase{"levels and advanced tone defaults", state});

    state = base;
    state.zone_deep_shadows = 0.0;
    state.zone_shadows = 0.0;
    state.zone_midtones = 0.0;
    state.zone_lights = 0.0;
    state.zone_highlights = 0.0;
    cases.push_back(NeutralRampCase{"zone mapping defaults", state});

    state = base;
    state.temperature = 0.0;
    state.tint = 0.0;
    state.vibrance = 0.0;
    state.saturation = 0.0;
    cases.push_back(NeutralRampCase{"white balance and color defaults", state});

    state = base;
    state.texture = 0.0;
    state.clarity = 0.0;
    state.dehaze = 0.0;
    state.sharpening = 0.0;
    state.sharpen_masking = 0.0;
    state.luminance_nr = 0.0;
    state.color_nr = 0.0;
    cases.push_back(NeutralRampCase{"presence and detail defaults", state});

    state = base;
    state.lens_distortion = 0.0;
    state.lens_vignette = 0.0;
    state.chromatic_aberration = 0.0;
    state.perspective_vertical = 0.0;
    state.perspective_horizontal = 0.0;
    state.transform_rotation = 0.0;
    state.transform_scale = 1.0;
    state.transform_skew_x = 0.0;
    state.transform_skew_y = 0.0;
    state.liquify_strength = 0.0;
    cases.push_back(NeutralRampCase{"optics and transform defaults", state});

    state = base;
    state.curve_shadows = 0.0;
    state.curve_darks = 0.0;
    state.curve_lights = 0.0;
    state.curve_highlights = 0.0;
    cases.push_back(NeutralRampCase{"parametric curve defaults", state});

    state = base;
    state.master_curve_points = {CurvePoint{0.0, 0.0}, CurvePoint{1.0, 1.0}};
    state.luma_curve_points = {CurvePoint{0.0, 0.0}, CurvePoint{1.0, 1.0}};
    state.red_curve_points = {CurvePoint{0.0, 0.0}, CurvePoint{1.0, 1.0}};
    state.green_curve_points = {CurvePoint{0.0, 0.0}, CurvePoint{1.0, 1.0}};
    state.blue_curve_points = {CurvePoint{0.0, 0.0}, CurvePoint{1.0, 1.0}};
    cases.push_back(NeutralRampCase{"identity point curves", state});

    state = base;
    state.color_mixer_model = "OKLab";
    state.color_mixer_channel = "Reds";
    state.hue_adjustment = 0.0;
    state.saturation_adjustment = 0.0;
    state.luminance_adjustment = 0.0;
    cases.push_back(NeutralRampCase{"color mixer defaults", state});

    state = base;
    state.rgb_channel_mixer.red_from_red = 100.0;
    state.rgb_channel_mixer.green_from_green = 100.0;
    state.rgb_channel_mixer.blue_from_blue = 100.0;
    cases.push_back(NeutralRampCase{"rgb channel mixer identity", state});

    state = base;
    state.black_and_white_enabled = false;
    state.black_and_white_mixer[0] = 45.0;
    state.black_and_white_mixer[5] = -45.0;
    cases.push_back(NeutralRampCase{"disabled black-and-white group", state});

    state = base;
    state.color_replacement_enabled = false;
    state.color_replacement_strength = 100.0;
    state.skin_tone_protection_enabled = false;
    state.skin_tone_protection_strength = 100.0;
    state.color_harmony_enabled = false;
    state.color_harmony_strength = 100.0;
    cases.push_back(NeutralRampCase{"disabled advanced color groups", state});

    state = base;
    cases.push_back(NeutralRampCase{"color grading defaults", state});

    state = base;
    state.lut_enabled = false;
    state.lut_table = make_identity_lut_adjustment().table;
    state.lut_size = 2;
    auto disabled_lut = make_identity_lut_adjustment();
    disabled_lut.enabled = false;
    state.lut_stack.push_back(disabled_lut);
    cases.push_back(NeutralRampCase{"disabled LUT defaults", state});

    check(
        cases.size() == 15,
        "identity/default registry should cover every global adjustment group",
        &failures);
    for (const auto& identity_case : cases) {
      const auto output = apply_pixels(source_corpus, static_cast<int>(source_corpus.size()), 1, identity_case.state);
      require_pixels_match(identity_case.name, expected, output, &failures);
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
    const auto input = std::vector<ColorPixel>{
        gray(0.0), gray(0.012), gray(0.035), gray(0.08), gray(0.18),
        gray(0.34), gray(0.55), gray(0.74), gray(0.90), gray(1.0),
    };
    std::vector<NeutralRampCase> cases;

    auto state = base;
    state.curve_shadows = -48.0;
    state.curve_darks = -18.0;
    state.curve_lights = 24.0;
    state.curve_highlights = 38.0;
    cases.push_back(NeutralRampCase{"parametric curve monotonicity", state});

    state = base;
    state.master_curve_points = {
        CurvePoint{0.0, 0.04},
        CurvePoint{0.22, 0.18},
        CurvePoint{0.58, 0.62},
        CurvePoint{1.0, 0.96},
    };
    cases.push_back(NeutralRampCase{"master point curve monotonicity", state});

    state = base;
    state.luma_curve_points = {
        CurvePoint{0.0, 0.02},
        CurvePoint{0.30, 0.24},
        CurvePoint{0.72, 0.78},
        CurvePoint{1.0, 0.98},
    };
    cases.push_back(NeutralRampCase{"luma point curve monotonicity", state});

    state = base;
    state.red_curve_points = {
        CurvePoint{0.0, 0.00},
        CurvePoint{0.35, 0.30},
        CurvePoint{0.70, 0.76},
        CurvePoint{1.0, 1.0},
    };
    cases.push_back(NeutralRampCase{"red point curve monotonicity", state});

    state = base;
    state.green_curve_points = {
        CurvePoint{0.0, 0.00},
        CurvePoint{0.28, 0.22},
        CurvePoint{0.68, 0.72},
        CurvePoint{1.0, 1.0},
    };
    cases.push_back(NeutralRampCase{"green point curve monotonicity", state});

    state = base;
    state.blue_curve_points = {
        CurvePoint{0.0, 0.00},
        CurvePoint{0.32, 0.26},
        CurvePoint{0.78, 0.84},
        CurvePoint{1.0, 1.0},
    };
    cases.push_back(NeutralRampCase{"blue point curve monotonicity", state});

    state = base;
    state.master_curve_points = {
        CurvePoint{0.0, 0.05},
        CurvePoint{0.45, 0.48},
        CurvePoint{1.0, 0.95},
    };
    state.luma_curve_points = {
        CurvePoint{0.0, 0.0},
        CurvePoint{0.50, 0.55},
        CurvePoint{1.0, 1.0},
    };
    cases.push_back(NeutralRampCase{"stacked master and luma curve monotonicity", state});

    check(
        cases.size() == 7,
        "tone curve monotonicity registry should cover parametric, master, luma, and RGB channels",
        &failures);
    for (const auto& curve_case : cases) {
      const auto output = apply_pixels(input, static_cast<int>(input.size()), 1, curve_case.state);
      require_finite_and_alpha(curve_case.name, input, output, &failures);
      require_sdr_headroom(curve_case.name, output, &failures);
      require_rgb_channels_monotonic(curve_case.name, output, &failures);
    }
  }

  {
    const auto input = std::vector<ColorPixel>{
        ColorPixel{0.02F, 0.04F, 0.08F, 1.0F}, ColorPixel{0.18F, 0.12F, 0.05F, 1.0F},
        ColorPixel{0.45F, 0.30F, 0.62F, 1.0F}, ColorPixel{0.72F, 0.90F, 0.48F, 1.0F},
        ColorPixel{1.10F, 0.74F, 0.32F, 1.0F},
    };
    std::vector<NeutralRampCase> cases;

    auto state = base;
    state.curve_shadows = -100.0;
    state.curve_darks = 100.0;
    state.curve_lights = -100.0;
    state.curve_highlights = 100.0;
    cases.push_back(NeutralRampCase{"extreme parametric curve finite output", state});

    state = base;
    state.master_curve_points = {
        CurvePoint{-0.40, 1.30},
        CurvePoint{0.30, -0.20},
        CurvePoint{0.30, 0.70},
        CurvePoint{1.40, -0.10},
    };
    cases.push_back(NeutralRampCase{"clamped master point curve finite output", state});

    state = base;
    state.luma_curve_points = {
        CurvePoint{0.80, 0.10},
        CurvePoint{0.20, 0.90},
        CurvePoint{0.50, 0.50},
    };
    cases.push_back(NeutralRampCase{"unsorted luma point curve finite output", state});

    state = base;
    state.red_curve_points = {
        CurvePoint{0.0, 1.0},
        CurvePoint{0.5, 0.0},
        CurvePoint{1.0, 1.0},
    };
    state.green_curve_points = {
        CurvePoint{0.0, 0.0},
        CurvePoint{0.5, 1.0},
        CurvePoint{1.0, 0.0},
    };
    state.blue_curve_points = {
        CurvePoint{0.0, 0.5},
        CurvePoint{0.5, 0.5},
        CurvePoint{1.0, 0.5},
    };
    cases.push_back(NeutralRampCase{"nonmonotonic RGB point curves finite output", state});

    check(
        cases.size() == 4,
        "tone curve finite-output registry should cover extreme, clamped, unsorted, and RGB point curves",
        &failures);
    for (const auto& curve_case : cases) {
      const auto output = apply_pixels(input, static_cast<int>(input.size()), 1, curve_case.state);
      require_finite_and_alpha(curve_case.name, input, output, &failures);
      require_sdr_headroom(curve_case.name, output, &failures);
    }
  }

  {
    const auto input = std::vector<ColorPixel>{
        ColorPixel{0.42F, 0.18F, 0.08F, 1.0F},
        ColorPixel{0.10F, 0.40F, 0.16F, 1.0F},
        ColorPixel{0.08F, 0.18F, 0.48F, 1.0F},
        ColorPixel{0.44F, 0.13F, 0.38F, 1.0F},
        ColorPixel{0.52F, 0.46F, 0.12F, 1.0F},
        ColorPixel{0.14F, 0.50F, 0.52F, 1.0F},
    };
    std::vector<NeutralRampCase> cases;

    auto state = base;
    state.exposure = 0.55;
    state.brightness = 18.0;
    cases.push_back(NeutralRampCase{"exposure and brightness", state});

    state = base;
    state.contrast = 32.0;
    state.highlights = -24.0;
    state.shadows = 22.0;
    cases.push_back(NeutralRampCase{"contrast highlight shadow tone", state});

    state = base;
    state.whites = 18.0;
    state.blacks = -12.0;
    state.levels_black_point = 3.0;
    state.levels_white_point = 97.0;
    cases.push_back(NeutralRampCase{"levels whites and blacks", state});

    state = base;
    state.filmic_shoulder = 26.0;
    state.filmic_toe = 18.0;
    state.adaptive_contrast = 30.0;
    cases.push_back(NeutralRampCase{"filmic and adaptive tone", state});

    state = base;
    state.levels_gamma = -28.0;
    cases.push_back(NeutralRampCase{"levels gamma", state});

    state = base;
    state.zone_deep_shadows = 18.0;
    state.zone_shadows = -14.0;
    state.zone_midtones = 20.0;
    state.zone_lights = -16.0;
    state.zone_highlights = 14.0;
    cases.push_back(NeutralRampCase{"zone mapping tone", state});

    state = base;
    state.curve_shadows = 22.0;
    state.curve_darks = -16.0;
    state.curve_lights = 18.0;
    state.curve_highlights = -12.0;
    cases.push_back(NeutralRampCase{"parametric tone curve", state});

    state = base;
    state.luma_curve_points = {
        CurvePoint{0.0, 0.0},
        CurvePoint{0.24, 0.20},
        CurvePoint{0.66, 0.74},
        CurvePoint{1.0, 1.0},
    };
    cases.push_back(NeutralRampCase{"luma point curve", state});

    check(
        cases.size() == 8,
        "tone-only chroma registry should cover global light, levels, zone, and luma curve tools",
        &failures);
    for (const auto& tone_case : cases) {
      const auto output = apply_pixels(input, static_cast<int>(input.size()), 1, tone_case.state);
      require_chroma_relationships_preserved(tone_case.name, input, output, &failures);
    }
  }

  {
    const auto curve_points = std::vector<CurvePoint>{
        CurvePoint{0.0, 0.0},
        CurvePoint{0.25, 0.18},
        CurvePoint{0.50, 0.68},
        CurvePoint{0.75, 0.84},
        CurvePoint{1.0, 1.0},
    };
    const auto input = ColorPixel{
        static_cast<float>(photo_editor::pipeline::srgb_to_linear(0.25)),
        static_cast<float>(photo_editor::pipeline::srgb_to_linear(0.50)),
        static_cast<float>(photo_editor::pipeline::srgb_to_linear(0.75)),
        1.0F,
    };

    auto master_state = base;
    master_state.master_curve_points = curve_points;
    const auto master_output = apply_pixel(input, master_state);
    const auto expected_master = ColorPixel{
        static_cast<float>(photo_editor::pipeline::srgb_to_linear(0.18)),
        static_cast<float>(photo_editor::pipeline::srgb_to_linear(0.68)),
        static_cast<float>(photo_editor::pipeline::srgb_to_linear(0.84)),
        1.0F,
    };
    check(
        std::abs(master_output.red - expected_master.red) <= 2.0e-5F &&
            std::abs(master_output.green - expected_master.green) <= 2.0e-5F &&
            std::abs(master_output.blue - expected_master.blue) <= 2.0e-5F,
        "RGB point curve should apply the same display-referred transfer independently to each channel",
        &failures);

    auto luma_state = base;
    luma_state.luma_curve_points = curve_points;
    const auto luma_output = apply_pixel(input, luma_state);
    check(
        channel_relationship_error(input, luma_output) <= kToneChromaRelationshipTolerance,
        "Luma point curve should preserve chromatic ratios",
        &failures);
    check(
        channel_relationship_error(master_output, luma_output) > 0.01,
        "RGB and Luma point curves should have distinct color behavior",
        &failures);

    auto layer_state = base;
    PhotoLayer layer;
    layer.kind = "adjustment";
    layer.opacity = 1.0;
    layer.master_curve_points = curve_points;
    layer_state.photo_layers.push_back(layer);
    const auto layer_output = apply_pixel(input, layer_state);
    check(
        std::abs(layer_output.red - expected_master.red) <= 2.0e-5F &&
            std::abs(layer_output.green - expected_master.green) <= 2.0e-5F &&
            std::abs(layer_output.blue - expected_master.blue) <= 2.0e-5F,
        "adjustment-layer RGB point curve should match the global composite RGB curve",
        &failures);
  }

  {
    const auto source = std::vector<ColorPixel>{
        gray(0.025),
        gray(0.08),
        gray(0.18),
        gray(0.55),
        gray(0.82),
        gray(0.96),
    };
    const auto delta_at = [&](const ColorAdjustmentState& state, std::size_t index) {
      const auto output = apply_pixels(source, static_cast<int>(source.size()), 1, state);
      return luminance(output[index]) - luminance(source[index]);
    };

    auto state = base;
    state.whites = 70.0;
    check(
        delta_at(state, 5) > delta_at(state, 3) + 0.015 &&
            delta_at(state, 5) > delta_at(state, 2) + 0.035,
        "whites should target the upper endpoint more than midtones",
        &failures);

    state = base;
    state.highlights = -70.0;
    check(
        std::abs(delta_at(state, 4)) > std::abs(delta_at(state, 2)) + 0.020 &&
            std::abs(delta_at(state, 4)) > std::abs(delta_at(state, 1)) + 0.020,
        "highlights should target bright tones more than shadows and midtones",
        &failures);

    state = base;
    state.shadows = 70.0;
    check(
        delta_at(state, 1) > delta_at(state, 3) + 0.015 &&
            delta_at(state, 1) > delta_at(state, 4) + 0.020,
        "shadows should target lower tones more than highlights",
        &failures);

    state = base;
    state.blacks = 70.0;
    check(
        delta_at(state, 0) > delta_at(state, 2) + 0.004 &&
            delta_at(state, 0) > delta_at(state, 4) + 0.004,
        "blacks should target the lower endpoint more than midtones and highlights",
        &failures);
  }

  {
    const auto input = std::vector<ColorPixel>{
        gray(0.0), gray(0.006), gray(0.018), gray(0.055), gray(0.18),
        gray(0.36), gray(0.62), gray(0.86), gray(0.98),
    };
    std::vector<NeutralRampCase> cases;

    auto state = base;
    state.vibrance = 74.0;
    state.saturation = -48.0;
    cases.push_back(NeutralRampCase{"vibrance and saturation", state});

    for (const auto model : std::array<std::string_view, 4>{"HSL", "HSV", "LCH", "OKLab"}) {
      state = base;
      state.color_mixer_model = std::string(model);
      state.color_mixer_channel = "Reds";
      state.hue_adjustment = 70.0;
      state.saturation_adjustment = -45.0;
      state.luminance_adjustment = 24.0;
      cases.push_back(NeutralRampCase{std::string("legacy ") + std::string(model) + " mixer", state});
    }

    for (const auto model : std::array<std::string_view, 4>{"HSL", "HSV", "LCH", "OKLab"}) {
      state = base;
      state.color_mixer_model = std::string(model);
      state.color_mixer_channels[0].hue = 34.0;
      state.color_mixer_channels[2].saturation = 62.0;
      state.color_mixer_channels[5].luminance = -28.0;
      state.color_mixer_channels[7].hue = -48.0;
      cases.push_back(NeutralRampCase{std::string("per-channel ") + std::string(model) + " mixer", state});
    }

    state = base;
    state.color_mixer_model = "OKLab";
    state.color_mixer_channels[0].selective_cyan = 40.0;
    state.color_mixer_channels[2].selective_magenta = -32.0;
    state.color_mixer_channels[4].selective_yellow = 58.0;
    state.color_mixer_channels[6].selective_black = -45.0;
    cases.push_back(NeutralRampCase{"selective color mixer", state});

    state = base;
    state.black_and_white_enabled = true;
    state.black_and_white_mixer[0] = 72.0;
    state.black_and_white_mixer[2] = -52.0;
    state.black_and_white_mixer[5] = 44.0;
    cases.push_back(NeutralRampCase{"black-and-white color mixer", state});

    state = base;
    state.color_replacement_enabled = true;
    state.color_replacement_source_hue = 0.0;
    state.color_replacement_target_hue = 0.61;
    state.color_replacement_range = 80.0;
    state.color_replacement_strength = 100.0;
    cases.push_back(NeutralRampCase{"color replacement", state});

    state = base;
    state.skin_tone_protection_enabled = true;
    state.skin_tone_protection_range = 45.0;
    state.skin_tone_protection_strength = 100.0;
    cases.push_back(NeutralRampCase{"skin tone protection", state});

    state = base;
    state.color_harmony_enabled = true;
    state.color_harmony_mode = "Tetradic";
    state.color_harmony_base_hue = 0.08;
    state.color_harmony_strength = 100.0;
    cases.push_back(NeutralRampCase{"color harmony", state});

    state = base;
    state.lut_stack.push_back(make_identity_lut_adjustment());
    cases.push_back(NeutralRampCase{"identity LUT stack", state});

    check(
        cases.size() == 15,
        "neutral-ramp color-only registry should cover every hue/chroma-selective color capability",
        &failures);
    for (const auto& color_case : cases) {
      const auto output = apply_pixels(input, static_cast<int>(input.size()), 1, color_case.state);
      require_neutral_ramp_preserved(color_case.name, input, output, &failures);
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
    state.grading_shadows_hue = 220.0 / 360.0;
    state.grading_shadows_saturation = 28.0;
    state.grading_midtones_hue = 36.0 / 360.0;
    state.grading_midtones_saturation = 22.0;
    state.grading_highlights_hue = 54.0 / 360.0;
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
