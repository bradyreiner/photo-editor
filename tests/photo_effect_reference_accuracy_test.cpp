#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_optical_effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string_view>
#include <vector>

using namespace photo_editor::pipeline;

namespace {
#define CHECK(condition, message) do { \
  if (!(condition)) { std::cerr << message << '\n'; return 1; } \
} while (false)

bool near(double actual, double expected, double tolerance = 2.0e-5) {
  return std::abs(actual - expected) <= tolerance;
}

double luma(ColorPixel pixel, const std::array<double, 3>& weights) {
  return weights[0] * pixel.red + weights[1] * pixel.green + weights[2] * pixel.blue;
}

ColorPixel direct_gaussian(const std::vector<ColorPixel>& source, int width,
    int height, int x, int y, double sigma) {
  const auto radius = static_cast<int>(std::ceil(3.0 * sigma));
  std::array<double, 4> sum{};
  double weight_sum = 0.0;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const auto weight = std::exp(-0.5 * (dx * dx + dy * dy) / (sigma * sigma));
      const auto sx = std::clamp(x + dx, 0, width - 1);
      const auto sy = std::clamp(y + dy, 0, height - 1);
      const auto pixel = source[static_cast<std::size_t>(sy * width + sx)];
      weight_sum += weight;
      sum[0] += weight * pixel.red * pixel.alpha;
      sum[1] += weight * pixel.green * pixel.alpha;
      sum[2] += weight * pixel.blue * pixel.alpha;
      sum[3] += weight * pixel.alpha;
    }
  }
  for (auto& channel : sum) channel /= weight_sum;
  if (sum[3] <= 1.0e-12) return {0, 0, 0, 0};
  return {static_cast<float>(sum[0] / sum[3]),
      static_cast<float>(sum[1] / sum[3]),
      static_cast<float>(sum[2] / sum[3]), static_cast<float>(sum[3])};
}

double premultiplied_red(ColorPixel pixel) {
  return static_cast<double>(pixel.red) * pixel.alpha;
}

int legacy_reference_cases() {
  PhotoEffectRenderOptions options;

  // The print model reduces to a closed-form density curve when texture,
  // spread, and registration are zero. Values remain in linear RGB and alpha
  // is the source coverage.
  PhotoEffectInstance print;
  print.id = "print-reference";
  print.type_id = "imperfect_print";
  print.imperfect_print = {20.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0, 7U};
  std::vector<ColorPixel> print_pixel{{0.2F, 0.4F, 0.8F, 0.35F}};
  CHECK(apply_photo_effect(&print_pixel, 1, 1, print, options).ok(),
      "Imperfect print reference render failed");
  CHECK(near(print_pixel[0].red, 0.6) && near(print_pixel[0].green, 0.7) &&
      near(print_pixel[0].blue, 0.9) && near(print_pixel[0].alpha, 0.35),
      "Ink density must map c to 1 - density * (1 - c), preserving coverage");

  PhotoEffectInstance invert;
  invert.id = "invert-reference";
  invert.type_id = "invert_linear_rgb";
  std::vector<ColorPixel> inverted{{2.0F, -0.25F, 0.4F, 0.6F}};
  CHECK(apply_photo_effect(&inverted, 1, 1, invert, options).ok(),
      "Linear invert reference render failed");
  CHECK(near(inverted[0].red, -1.0) && near(inverted[0].green, 1.25) &&
      near(inverted[0].blue, 0.6) && near(inverted[0].alpha, 0.6),
      "Invert must apply 1-c in linear RGB without clipping HDR or negative values");

  PhotoEffectInstance fade;
  fade.id = "fade-reference";
  fade.type_id = "soft_fade";
  fade.soft_fade = {0.025, 0.8, 0.6};
  const ColorPixel fade_source{0.1F, 0.3F, 0.5F, 0.4F};
  auto fade_pixel = std::vector<ColorPixel>{fade_source};
  const auto y = luma(fade_source, options.luminance_weights);
  const auto lift = fade.soft_fade.lift * std::exp(-2.5 * std::max(0.0, y));
  const auto faded_y = 0.18 + (y - 0.18) * fade.soft_fade.contrast + lift;
  const std::array<double, 3> source_channels{fade_source.red, fade_source.green,
      fade_source.blue};
  CHECK(apply_photo_effect(&fade_pixel, 1, 1, fade, options).ok(),
      "Soft fade reference render failed");
  for (std::size_t channel = 0; channel < source_channels.size(); ++channel) {
    const auto contrasted = source_channels[channel] * fade.soft_fade.contrast +
        0.18 * (1.0 - fade.soft_fade.contrast) + lift;
    const auto expected = faded_y + (contrasted - faded_y) * fade.soft_fade.saturation;
    const auto actual = channel == 0 ? fade_pixel[0].red :
        channel == 1 ? fade_pixel[0].green : fade_pixel[0].blue;
    CHECK(near(actual, expected), "Soft fade must match its stated linear-light equation");
  }
  CHECK(near(fade_pixel[0].alpha, fade_source.alpha),
      "Soft fade must preserve source alpha");

  PhotoEffectInstance vignette;
  vignette.id = "vignette-reference";
  vignette.type_id = "vignette";
  vignette.vignette = {0.5, 0.0, 0.5, 2.0};
  std::vector<ColorPixel> field(9, {1.0F, 0.5F, 0.25F, 0.7F});
  CHECK(apply_photo_effect(&field, 3, 3, vignette, options).ok(),
      "Vignette reference render failed");
  const auto corner_distance = std::hypot(2.0 / 3.0, 2.0 / 3.0);
  const auto t = std::clamp((corner_distance - 0.5) / 0.5, 0.0, 1.0);
  const auto smooth = t * t * (3.0 - 2.0 * t);
  const auto expected_exposure = std::exp2(-2.0 * smooth);
  CHECK(near(field[4].red, 1.0) && near(field[0].red, expected_exposure) &&
      near(field[0].alpha, 0.7),
      "Vignette must be centered, use smooth radial exposure falloff, and preserve alpha");

  // These are ACES AP0 XYZ-Y coefficients, including the negative blue term.
  // Hue normalization must preserve the actual working-gamut luminance.
  options.luminance_weights = {0.34396645, 0.72816610, -0.07213255};
  PhotoEffectInstance split;
  split.id = "split-tone-ap0-reference";
  split.type_id = "split_tone";
  split.split_tone = {28.0, 0.55, 215.0, 0.65, -0.2};
  std::vector<ColorPixel> gray{{0.4F, 0.4F, 0.4F, 1.0F}};
  const auto source_y = luma(gray[0], options.luminance_weights);
  CHECK(apply_photo_effect(&gray, 1, 1, split, options).ok(),
      "AP0 split-tone reference render failed");
  CHECK(near(luma(gray[0], options.luminance_weights), source_y),
      "Split-tone hue weights must use the actual signed AP0 luminance row");
  return 0;
}

int optical_reference_cases() {
  PhotoEffectRenderOptions options;

  // Above the soft-knee shoulder, emitted energy must be exactly Y - T;
  // knee width controls the transition and must not add a permanent bias.
  OpticalPhotoEffectParameters optical;
  optical.halation = {1.0, 1.0, 0.25, 0.5, 0.0};
  std::vector<ColorPixel> highlight{{2.0F, 2.0F, 2.0F, 1.0F}};
  CHECK(apply_optical_photo_effect(&highlight, 1, 1, "halation", optical,
      options).ok(), "Halation knee reference render failed");
  CHECK(near(highlight[0].red, 2.5),
      "Halation above the knee must add strength times (Y - threshold)");

  constexpr int flare_size = 61;
  const auto flare_impulse = static_cast<std::size_t>(30 * flare_size + 30);
  const auto flare_energy = [&](std::string_view type,
      const OpticalPhotoEffectParameters& parameters) {
    std::vector<ColorPixel> source(flare_size * flare_size, {0, 0, 0, 0});
    source[flare_impulse] = {2, 2, 2, 1};
    const auto input = source;
    const auto result = apply_optical_photo_effect(&source, flare_size,
        flare_size, type, parameters, options);
    if (!result.ok()) return -1.0;
    double energy = 0.0;
    for (std::size_t i = 0; i < source.size(); ++i)
      energy += premultiplied_red(source[i]) - premultiplied_red(input[i]);
    return energy;
  };

  optical = {};
  optical.anamorphic_streaks = {8.0, 0.8, 27.0, 0.0, 0.0, 0.4, 0.0};
  CHECK(near(flare_energy("anamorphic_streaks", optical), 0.8, 2.0e-4),
      "Normalized anamorphic line and cross kernels must conserve impulse energy");
  optical = {};
  optical.starburst = {7.0, 0.8, 13.0, 6, 0.0, 0.0, 0.4, 0.0};
  CHECK(near(flare_energy("starburst", optical), 0.8, 3.0e-4),
      "Averaging normalized starburst ray kernels must conserve impulse energy");

  // A unit point source is integrated over a normalized aperture kernel. With
  // the source away from the border, summing the premultiplied bloom must
  // conserve its extracted energy independent of polygon blade count.
  optical = {};
  optical.lens_bokeh = {4.0, 6, 0.0, 64, 0.0, 0.0, 0.4};
  constexpr int bokeh_size = 25;
  std::vector<ColorPixel> bokeh_source(bokeh_size * bokeh_size, {0, 0, 0, 0});
  const auto impulse = static_cast<std::size_t>(12 * bokeh_size + 12);
  bokeh_source[impulse] = {2, 2, 2, 1};
  const auto bokeh_input = bokeh_source;
  CHECK(apply_optical_photo_effect(&bokeh_source, bokeh_size, bokeh_size,
      "lens_bokeh", optical, options).ok(), "Bokeh aperture reference render failed");
  double bokeh_added_energy = 0.0;
  for (std::size_t i = 0; i < bokeh_source.size(); ++i)
    bokeh_added_energy += premultiplied_red(bokeh_source[i]) -
        premultiplied_red(bokeh_input[i]);
  CHECK(near(bokeh_added_energy, 0.8, 2.0e-4),
      "Polygon-aperture integration must conserve the extracted impulse energy");

  // Surface blur is edge-preserving when the range scale is far smaller than
  // the step contrast. It must also be independent of undefined RGB at alpha 0.
  optical = {};
  optical.surface_blur = {1.0, 0.05};
  std::vector<ColorPixel> step{{0, 0, 0, 1}, {0, 0, 0, 1}, {1, 1, 1, 1},
      {1, 1, 1, 1}, {1, 1, 1, 1}};
  CHECK(apply_optical_photo_effect(&step, 5, 1, "surface_blur", optical,
      options).ok(), "Surface step reference render failed");
  CHECK(step[1].red < 1.0e-6F && step[2].red > 1.0F - 1.0e-6F,
      "Bilateral range weighting must not average across a high-contrast edge");

  std::vector<ColorPixel> hidden_a{{0.25F, 0.25F, 0.25F, 1.0F},
      {0.91F, -0.4F, 0.7F, 0.0F}, {0.75F, 0.75F, 0.75F, 1.0F}};
  auto hidden_b = hidden_a;
  hidden_b[1] = {-100.0F, 60.0F, 22.0F, 0.0F};
  CHECK(apply_optical_photo_effect(&hidden_a, 3, 1, "surface_blur", optical,
      options).ok() && apply_optical_photo_effect(&hidden_b, 3, 1, "surface_blur",
      optical, options).ok(), "Transparent surface reference render failed");
  for (std::size_t i = 0; i < hidden_a.size(); ++i) {
    CHECK(near(premultiplied_red(hidden_a[i]), premultiplied_red(hidden_b[i])) &&
        near(hidden_a[i].green * hidden_a[i].alpha,
            hidden_b[i].green * hidden_b[i].alpha) &&
        near(hidden_a[i].blue * hidden_a[i].alpha,
            hidden_b[i].blue * hidden_b[i].alpha) &&
        near(hidden_a[i].alpha, hidden_b[i].alpha),
        "Surface blur output must not depend on hidden zero-alpha RGB");
  }

  // The high-pass output is deliberately a gray-offset residual. Compare it
  // with an independent direct 2-D Gaussian reference, rather than testing only
  // that a constant image returns gray.
  optical = {};
  optical.high_pass = {0.7, 1.3};
  constexpr int width = 7, height = 5;
  std::vector<ColorPixel> texture;
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const auto value = static_cast<float>((x * 7 + y * 3) % 11) / 10.0F;
    texture.push_back({value, value * 0.7F, value * 1.4F, 1.0F});
  }
  const auto texture_input = texture;
  CHECK(apply_optical_photo_effect(&texture, width, height, "high_pass", optical,
      options).ok(), "High-pass numerical reference render failed");
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const auto index = static_cast<std::size_t>(y * width + x);
    const auto low = direct_gaussian(texture_input, width, height, x, y,
        optical.high_pass.radius);
    const auto& source = texture_input[index];
    CHECK(near(texture[index].red, 0.5 + optical.high_pass.strength * (source.red - low.red)) &&
        near(texture[index].green, 0.5 + optical.high_pass.strength * (source.green - low.green)) &&
        near(texture[index].blue, 0.5 + optical.high_pass.strength * (source.blue - low.blue)) &&
        near(texture[index].alpha, source.alpha),
        "High-pass must equal centered source minus alpha-normalized Gaussian low pass");
  }

  // Selective focus has an exact unblurred band. Radial and angular samples
  // are checked at coordinates whose linear-ramp averages have closed forms.
  optical = {};
  optical.tilt_shift = {0.5, 0.5, 0.0, 0.1, 0.1, 1.0};
  options.frame_width = 7;
  options.frame_height = 7;
  std::vector<ColorPixel> ramp;
  for (int y = 0; y < 7; ++y) for (int x = 0; x < 7; ++x)
    ramp.push_back({static_cast<float>(x), static_cast<float>(x),
        static_cast<float>(x), 1.0F});
  const auto ramp_input = ramp;
  CHECK(apply_optical_photo_effect(&ramp, 7, 7, "tilt_shift", optical,
      options).ok(), "Tilt-shift focus-band reference render failed");
  for (int x = 0; x < 7; ++x)
    CHECK(ramp[static_cast<std::size_t>(3 * 7 + x)] ==
        ramp_input[static_cast<std::size_t>(3 * 7 + x)],
        "The geometric focus band must retain exact source pixels");

  optical = {};
  optical.zoom_blur = {0.5, 0.5, 2.0, 4};
  ramp = ramp_input;
  CHECK(apply_optical_photo_effect(&ramp, 7, 7, "zoom_blur", optical,
      options).ok() && near(ramp[3 * 7 + 5].red, 4.0),
      "Zoom blur must integrate four midpoint samples along its radial source path");

  optical = {};
  optical.spin_blur = {0.5, 0.5, 90.0, 4};
  ramp = ramp_input;
  CHECK(apply_optical_photo_effect(&ramp, 7, 7, "spin_blur", optical,
      options).ok() && near(ramp[3 * 7 + 5].red,
          3.0 + std::cos(std::numbers::pi / 16.0) +
              std::cos(3.0 * std::numbers::pi / 16.0)),
      "Spin blur must integrate four inverse-rotated midpoint samples over the requested arc");

  return 0;
}
}  // namespace

int main() {
  CHECK(legacy_reference_cases() == 0, "Legacy effect reference cases");
  CHECK(optical_reference_cases() == 0, "Optical effect reference cases");
  std::cout << "Independent analytic/reference accuracy cases passed\n";
  return 0;
}
