#include "color_mixer_runtime.h"

#include "color_math_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace photo_editor::pipeline {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

void apply_selective_component(
    double amount,
    double target_hue_unit,
    double mixer_weight,
    double* chroma,
    double* hue) {
  if (chroma == nullptr || hue == nullptr) {
    return;
  }
  const auto control = clamp_signed_percent(amount) / 100.0;
  if (std::abs(control) <= 1.0e-9 || mixer_weight <= 1.0e-9) {
    return;
  }
  const auto current_a = *chroma * std::cos(*hue);
  const auto current_b = *chroma * std::sin(*hue);
  const auto direction = target_hue_unit * 2.0 * kPi + (control < 0.0 ? kPi : 0.0);
  const auto strength = std::abs(control) * mixer_weight * 0.07;
  const auto next_a = current_a + std::cos(direction) * strength;
  const auto next_b = current_b + std::sin(direction) * strength;
  *chroma = std::hypot(next_a, next_b);
  *hue = wrap_radians(std::atan2(next_b, next_a));
}

}  // namespace

std::string_view normalized_color_mixer_model(const ColorAdjustmentState& state) {
  if (state.color_mixer_model == "HSL" || state.color_mixer_model == "HSV" ||
      state.color_mixer_model == "LCH") {
    return state.color_mixer_model;
  }
  return "OKLab";
}

double rgb_hue_for_mixer_selection(const ColorPixel& pixel) {
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto delta = maximum - minimum;
  if (delta <= 1.0e-9) {
    return -1.0;
  }

  double hue = 0.0;
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
  return wrap_unit(hue / 6.0);
}

double mixer_selection_hue(
    const ColorPixel& pixel,
    double oklch_hue,
    double chroma,
    std::string_view model) {
  if (model == "HSL" || model == "HSV") {
    return rgb_hue_for_mixer_selection(pixel);
  }
  if (chroma <= 1.0e-6) {
    return -1.0;
  }
  return wrap_unit(oklch_hue / (2.0 * kPi));
}

void apply_selective_color_adjustment(
    const ColorMixerChannelAdjustment& mixer,
    double mixer_weight,
    double* lightness,
    double* chroma,
    double* hue) {
  if (lightness == nullptr || chroma == nullptr || hue == nullptr || mixer_weight <= 1.0e-9) {
    return;
  }

  apply_selective_component(mixer.selective_cyan, 0.50, mixer_weight, chroma, hue);
  apply_selective_component(mixer.selective_magenta, 0.88, mixer_weight, chroma, hue);
  apply_selective_component(mixer.selective_yellow, 0.15, mixer_weight, chroma, hue);
  *lightness -=
      mixer_weight *
      (clamp_signed_percent(mixer.selective_black) / 100.0) *
      0.18;
}

bool has_rgb_channel_mixer_adjustments(const ColorAdjustmentState& state) {
  const auto& mixer = state.rgb_channel_mixer;
  return std::abs(mixer.red_from_red - 100.0) > 1e-9 ||
         std::abs(mixer.red_from_green) > 1e-9 ||
         std::abs(mixer.red_from_blue) > 1e-9 ||
         std::abs(mixer.green_from_red) > 1e-9 ||
         std::abs(mixer.green_from_green - 100.0) > 1e-9 ||
         std::abs(mixer.green_from_blue) > 1e-9 ||
         std::abs(mixer.blue_from_red) > 1e-9 ||
         std::abs(mixer.blue_from_green) > 1e-9 ||
         std::abs(mixer.blue_from_blue - 100.0) > 1e-9;
}

ColorPixel apply_rgb_channel_mixer(
    const ColorPixel& pixel,
    const RgbChannelMixerAdjustment& mixer) {
  const auto scale = [](double value) { return value / 100.0; };
  const auto red = static_cast<double>(pixel.red);
  const auto green = static_cast<double>(pixel.green);
  const auto blue = static_cast<double>(pixel.blue);
  return ColorPixel{
      .red = static_cast<float>(std::max(
          0.0,
          red * scale(mixer.red_from_red) + green * scale(mixer.red_from_green) +
              blue * scale(mixer.red_from_blue))),
      .green = static_cast<float>(std::max(
          0.0,
          red * scale(mixer.green_from_red) + green * scale(mixer.green_from_green) +
              blue * scale(mixer.green_from_blue))),
      .blue = static_cast<float>(std::max(
          0.0,
          red * scale(mixer.blue_from_red) + green * scale(mixer.blue_from_green) +
              blue * scale(mixer.blue_from_blue))),
      .alpha = pixel.alpha,
  };
}

ColorPixel apply_black_and_white_mixer(
    const ColorPixel& pixel,
    const ColorAdjustmentState& state,
    double native_luminance) {
  auto gray = native_luminance;
  const auto mixer_hue = rgb_hue_for_mixer_selection(pixel);
  if (mixer_hue >= 0.0) {
    for (int channel = 0; channel < kColorMixerChannelCount; ++channel) {
      const auto weight = channel_weight_for_selection(
          mixer_hue,
          kColorMixerChannelNames[static_cast<std::size_t>(channel)]);
      if (weight > 0.0) {
        gray *= std::max(
            0.0,
            1.0 +
                weight *
                    (clamp_signed_percent(
                         state.black_and_white_mixer[static_cast<std::size_t>(channel)]) /
                     100.0) *
                    0.85);
      }
    }
  }
  const auto value = static_cast<float>(std::max(0.0, gray));
  return ColorPixel{.red = value, .green = value, .blue = value, .alpha = pixel.alpha};
}

bool has_per_channel_color_mixer_adjustments(const ColorAdjustmentState& state) {
  return std::any_of(
      state.color_mixer_channels.begin(),
      state.color_mixer_channels.end(),
      [](const ColorMixerChannelAdjustment& channel) {
        return std::abs(channel.hue) > 1e-9 || std::abs(channel.saturation) > 1e-9 ||
               std::abs(channel.luminance) > 1e-9 ||
               std::abs(channel.selective_cyan) > 1e-9 ||
               std::abs(channel.selective_magenta) > 1e-9 ||
               std::abs(channel.selective_yellow) > 1e-9 ||
               std::abs(channel.selective_black) > 1e-9;
      });
}

double channel_weight_for_selection(double hue, std::string_view channel) {
  if (hue < 0.0) {
    return 0.0;
  }

  struct Region final {
    std::string_view name;
    double center;
    double inner_width;
    double outer_width;
  };

  static constexpr std::array<Region, 8> kRegions{{
      {"Reds", 0.0, 0.05, 0.10},
      {"Oranges", 0.09, 0.04, 0.09},
      {"Yellows", 0.15, 0.04, 0.08},
      {"Greens", 0.32, 0.10, 0.18},
      {"Aquas", 0.50, 0.06, 0.10},
      {"Blues", 0.64, 0.08, 0.13},
      {"Purples", 0.78, 0.06, 0.10},
      {"Magentas", 0.88, 0.05, 0.10},
  }};

  for (const auto& region : kRegions) {
    if (channel != region.name) {
      continue;
    }

    auto distance = std::abs(hue - region.center);
    distance = std::min(distance, 1.0 - distance);
    if (distance <= region.inner_width) {
      return 1.0;
    }
    if (distance >= region.outer_width) {
      return 0.0;
    }
    return 1.0 - smoothstep(region.inner_width, region.outer_width, distance);
  }

  return 0.0;
}

}  // namespace photo_editor::pipeline
