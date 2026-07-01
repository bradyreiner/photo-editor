#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;

int fail(std::string_view message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
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

double luminance(const ColorPixel& pixel) {
  return 0.2126 * static_cast<double>(pixel.red) +
         0.7152 * static_cast<double>(pixel.green) +
         0.0722 * static_cast<double>(pixel.blue);
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
  const auto sum = std::max(1.0e-9, red + green + blue);
  return {red / sum, green / sum, blue / sum};
}

double relationship_error(const ColorPixel& before, const ColorPixel& after) {
  const auto before_channels = channel_relationships(before);
  const auto after_channels = channel_relationships(after);
  auto error = 0.0;
  for (std::size_t index = 0; index < before_channels.size(); ++index) {
    error = std::max(error, std::abs(before_channels[index] - after_channels[index]));
  }
  return error;
}

std::vector<ColorPixel> apply_pixels(std::vector<ColorPixel> pixels, const ColorAdjustmentState& state) {
  photo_editor::pipeline::apply_color_adjustments(
      &pixels,
      static_cast<int>(pixels.size()),
      1,
      state);
  return pixels;
}

bool finite_pixel(const ColorPixel& pixel) {
  return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
         std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
}

}  // namespace

int main() {
  constexpr std::array<double, 7> kSliderValues = {-100.0, -50.0, -25.0, 0.0, 25.0, 50.0, 100.0};
  const std::vector<ColorPixel> neutral_ramp = {
      ColorPixel{0.0F, 0.0F, 0.0F, 1.0F},
      ColorPixel{0.006F, 0.006F, 0.006F, 1.0F},
      ColorPixel{0.018F, 0.018F, 0.018F, 1.0F},
      ColorPixel{0.055F, 0.055F, 0.055F, 1.0F},
      ColorPixel{0.18F, 0.18F, 0.18F, 1.0F},
      ColorPixel{0.36F, 0.36F, 0.36F, 1.0F},
      ColorPixel{0.62F, 0.62F, 0.62F, 1.0F},
      ColorPixel{0.86F, 0.86F, 0.86F, 1.0F},
      ColorPixel{0.98F, 0.98F, 0.98F, 1.0F},
  };

  for (const auto value : kSliderValues) {
    std::array<ColorAdjustmentState, 8> states = {
        base_state(), base_state(), base_state(), base_state(),
        base_state(), base_state(), base_state(), base_state()};
    states[0].exposure = value / 50.0;
    states[1].contrast = value;
    states[2].highlights = value;
    states[3].shadows = value;
    states[4].whites = value;
    states[5].blacks = value;
    states[6].brightness = value;
    states[7].vibrance = value;

    for (const auto& state : states) {
      const auto output = apply_pixels(neutral_ramp, state);
      auto previous_luminance = -1.0;
      for (const auto& pixel : output) {
        if (!finite_pixel(pixel)) {
          return fail("slider sweep should produce finite neutral-ramp pixels");
        }
        if (channel_spread(pixel) > 1.0e-5) {
          return fail("slider sweep should keep neutral-ramp pixels neutral");
        }
        const auto current_luminance = luminance(pixel);
        if (current_luminance + 1.0e-6 < previous_luminance) {
          return fail("slider sweep should keep neutral-ramp luminance monotonic");
        }
        previous_luminance = current_luminance;
      }
    }
  }

  const std::vector<ColorPixel> tone_colors = {
      ColorPixel{0.20F, 0.08F, 0.03F, 1.0F},
      ColorPixel{0.06F, 0.24F, 0.10F, 1.0F},
      ColorPixel{0.04F, 0.09F, 0.28F, 1.0F},
      ColorPixel{0.24F, 0.07F, 0.21F, 1.0F},
  };
  auto tone_state = base_state();
  tone_state.exposure = 0.75;
  tone_state.contrast = 35.0;
  tone_state.highlights = -35.0;
  tone_state.shadows = 28.0;
  const auto tone_output = apply_pixels(tone_colors, tone_state);
  for (std::size_t index = 0; index < tone_colors.size(); ++index) {
    if (relationship_error(tone_colors[index], tone_output[index]) > 1.0e-3) {
      return fail("tone controls should preserve hue for non-clipped colors");
    }
  }

  auto dark_recovery_state = base_state();
  dark_recovery_state.exposure = 2.0;
  dark_recovery_state.shadows = 100.0;
  dark_recovery_state.brightness = 100.0;
  const auto dark_recovery_output = apply_pixels(
      {
          ColorPixel{0.01F, 0.01F, 0.01F, 1.0F},
          ColorPixel{0.02F, 0.02F, 0.02F, 1.0F},
          ColorPixel{0.05F, 0.05F, 0.05F, 1.0F},
      },
      dark_recovery_state);
  if (!(luminance(dark_recovery_output[0]) > 0.075 &&
        luminance(dark_recovery_output[1]) > 0.12 &&
        luminance(dark_recovery_output[2]) > 0.24)) {
    return fail("exposure, shadows, and brightness should recover visibly dark linear detail");
  }

  auto zone_state = base_state();
  zone_state.zone_deep_shadows = 100.0;
  zone_state.zone_shadows = 100.0;
  const auto zone_base = apply_pixels({ColorPixel{0.018F, 0.018F, 0.018F, 1.0F}}, base_state()).front();
  const auto zone_lifted = apply_pixels({ColorPixel{0.018F, 0.018F, 0.018F, 1.0F}}, zone_state).front();
  if (!(luminance(zone_lifted) > luminance(zone_base) * 1.7)) {
    return fail("zone shadow controls should produce a meaningful deep-shadow lift");
  }

  auto levels_state = base_state();
  levels_state.levels_black_point = 12.0;
  levels_state.levels_white_point = 18.0;
  const auto levels_output = apply_pixels(
      {
          ColorPixel{0.04F, 0.04F, 0.04F, 1.0F},
          ColorPixel{0.18F, 0.18F, 0.18F, 1.0F},
          ColorPixel{0.72F, 0.72F, 0.72F, 1.0F},
      },
      levels_state);
  if (!(luminance(levels_output[0]) < 0.02 &&
        luminance(levels_output[1]) < 0.12 &&
        luminance(levels_output[2]) > 0.84)) {
    return fail("levels should move black and white input endpoints visibly");
  }

  auto blue_grade_state = base_state();
  blue_grade_state.grading_midtones_hue = 0.66;
  blue_grade_state.grading_midtones_saturation = 100.0;
  const auto blue_grade =
      apply_pixels({ColorPixel{0.18F, 0.18F, 0.18F, 1.0F}}, blue_grade_state).front();
  if (!(blue_grade.blue > blue_grade.red + 0.015 && blue_grade.blue > blue_grade.green + 0.010)) {
    return fail("split toning hue should use the full normalized hue wheel");
  }

  auto chroma_state = base_state();
  chroma_state.saturation = 60.0;
  chroma_state.vibrance = 60.0;
  const auto neutral_output =
      apply_pixels({ColorPixel{0.32F, 0.32F, 0.32F, 1.0F}}, chroma_state).front();
  if (channel_spread(neutral_output) > 1.0e-6 ||
      std::abs(luminance(neutral_output) - 0.32) > 1.0e-5) {
    return fail("saturation and vibrance should preserve neutral pixels");
  }

  auto vibrance_state = base_state();
  vibrance_state.vibrance = 70.0;
  const auto muted = ColorPixel{0.16F, 0.24F, 0.38F, 1.0F};
  const auto saturated = ColorPixel{0.03F, 0.10F, 0.88F, 1.0F};
  const auto vibrance_output = apply_pixels({muted, saturated}, vibrance_state);
  const auto muted_gain = channel_spread(vibrance_output[0]) - channel_spread(muted);
  const auto saturated_gain = channel_spread(vibrance_output[1]) - channel_spread(saturated);
  if (!(muted_gain > saturated_gain + 0.005)) {
    return fail("vibrance should favor muted colors over already saturated colors");
  }

  auto color_replace_state = base_state();
  color_replace_state.color_replacement_enabled = true;
  color_replace_state.color_replacement_source_hue = 0.0;
  color_replace_state.color_replacement_target_hue = 1.0 / 3.0;
  color_replace_state.color_replacement_range = 35.0;
  color_replace_state.color_replacement_strength = 100.0;
  const auto replaced_red =
      apply_pixels({ColorPixel{0.48F, 0.03F, 0.02F, 1.0F}}, color_replace_state).front();
  if (!(replaced_red.green > replaced_red.red && replaced_red.green > replaced_red.blue)) {
    return fail("color replacement should visibly move the selected hue toward the target hue");
  }

  return EXIT_SUCCESS;
}
