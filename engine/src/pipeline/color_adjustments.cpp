#include "photo_editor/pipeline/color_adjustments.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

constexpr double kMiddleGray = 0.18;
constexpr double kPi = std::numbers::pi_v<double>;

struct OklabColor final {
  double lightness = 0.0;
  double a = 0.0;
  double b = 0.0;
};

[[nodiscard]] double smoothstep(double edge0, double edge1, double x) {
  if (std::abs(edge1 - edge0) <= 1e-9) {
    return x < edge0 ? 0.0 : 1.0;
  }

  const auto t = clamp_unit((x - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double bell_weight(double value, double center, double radius) {
  if (radius <= 0.0) {
    return 0.0;
  }

  const auto distance = std::abs(value - center) / radius;
  if (distance >= 1.0) {
    return 0.0;
  }

  const auto t = 1.0 - distance;
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double circular_weight(double value, double center, double radius) {
  if (radius <= 0.0) {
    return 0.0;
  }

  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0 - distance);
  if (distance >= radius) {
    return 0.0;
  }

  const auto t = 1.0 - distance / radius;
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double wrap_unit(double value) {
  value -= std::floor(value);
  if (value < 0.0) {
    value += 1.0;
  }
  return value;
}

[[nodiscard]] double wrap_radians(double value) {
  while (value < 0.0) {
    value += 2.0 * kPi;
  }
  while (value >= 2.0 * kPi) {
    value -= 2.0 * kPi;
  }
  return value;
}

[[nodiscard]] double luminance_for_pixel(const ColorPixel& pixel) {
  return (pixel.red * 0.2126) + (pixel.green * 0.7152) + (pixel.blue * 0.0722);
}

[[nodiscard]] ColorPixel scale_pixel_to_luminance(const ColorPixel& pixel, double target_luminance) {
  const auto clamped_target = std::max(0.0, target_luminance);
  const auto current_luminance = luminance_for_pixel(pixel);
  if (current_luminance <= 1e-6) {
    return ColorPixel{
        .red = clamped_target,
        .green = clamped_target,
        .blue = clamped_target,
        .alpha = pixel.alpha,
    };
  }

  const auto scale = clamped_target / current_luminance;
  return ColorPixel{
      .red = std::max(0.0, pixel.red * scale),
      .green = std::max(0.0, pixel.green * scale),
      .blue = std::max(0.0, pixel.blue * scale),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] std::array<std::pair<double, double>, 6> tone_curve_control_points(const ColorAdjustmentState& state) {
  const auto point = [](double x, double delta) {
    return std::pair<double, double>{x, clamp_unit(x + (delta / 100.0) * 0.34)};
  };

  return {
      std::pair<double, double>{0.0, 0.0},
      point(0.20, state.curve_shadows),
      point(0.40, state.curve_darks),
      point(0.65, state.curve_lights),
      point(0.85, state.curve_highlights),
      std::pair<double, double>{1.0, 1.0},
  };
}

[[nodiscard]] double apply_pivoted_contrast(double value, double amount) {
  const auto safe_value = std::max(1e-6, value);
  const auto contrast = std::clamp(1.0 + amount * 0.85, 0.35, 2.4);
  return kMiddleGray * std::pow(safe_value / kMiddleGray, contrast);
}

[[nodiscard]] double apply_global_tone(double value, const ColorAdjustmentState& state) {
  auto adjusted = std::max(0.0, value);
  adjusted = apply_pivoted_contrast(adjusted, clamp_signed_percent(state.contrast) / 100.0);

  const auto stops = std::log2(std::max(adjusted, 1e-6) / kMiddleGray);
  auto local_exposure_delta = 0.0;
  local_exposure_delta += bell_weight(stops, -2.25, 2.25) * (clamp_signed_percent(state.blacks) / 100.0) * 1.05;
  local_exposure_delta += bell_weight(stops, -1.00, 1.85) * (clamp_signed_percent(state.shadows) / 100.0) * 0.72;
  local_exposure_delta += bell_weight(stops, 1.10, 1.75) * (clamp_signed_percent(state.highlights) / 100.0) * 0.68;
  local_exposure_delta += bell_weight(stops, 2.80, 2.10) * (clamp_signed_percent(state.whites) / 100.0) * 0.96;
  adjusted *= std::pow(2.0, local_exposure_delta);

  return adjusted;
}

[[nodiscard]] double shoulder_threshold_for_max_luminance(double max_luminance) {
  if (max_luminance <= 1.0) {
    return 1.0;
  }

  const auto overshoot = std::max(0.0, max_luminance - 1.0);
  const auto strength = overshoot / (overshoot + 1.0);
  return std::clamp(0.98 - strength * 0.18, 0.82, 0.98);
}

[[nodiscard]] double apply_highlight_shoulder(double value, double threshold) {
  if (value <= threshold) {
    return value;
  }

  const auto shoulder_scale = std::max(1e-6, 1.0 - threshold);
  return threshold + shoulder_scale * (1.0 - std::exp(-(value - threshold) / shoulder_scale));
}

[[nodiscard]] std::array<double, 3> white_balance_multipliers(double temperature, double tint) {
  const auto normalized_temperature = clamp_signed_percent(temperature) / 100.0;
  const auto normalized_tint = clamp_signed_percent(tint) / 100.0;
  const auto red = std::pow(2.0, normalized_temperature * 0.42 + normalized_tint * 0.08);
  const auto green = std::pow(2.0, -normalized_tint * 0.24);
  const auto blue = std::pow(2.0, -normalized_temperature * 0.42 + normalized_tint * 0.08);
  const auto normalization = (red * 0.2126) + (green * 0.7152) + (blue * 0.0722);
  if (normalization <= 1e-6) {
    return {1.0, 1.0, 1.0};
  }

  return {red / normalization, green / normalization, blue / normalization};
}

[[nodiscard]] std::vector<float> box_blur(const std::vector<float>& input, int width, int height, int radius) {
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0) {
    return input;
  }

  const auto kernel_width = radius * 2 + 1;
  std::vector<float> horizontal(input.size(), 0.0F);
  std::vector<float> output(input.size(), 0.0F);

  for (int y = 0; y < height; ++y) {
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_x = std::clamp(sample, 0, width - 1);
      sum += input[static_cast<std::size_t>(y * width + clamped_x)];
    }

    for (int x = 0; x < width; ++x) {
      horizontal[static_cast<std::size_t>(y * width + x)] = static_cast<float>(sum / kernel_width);
      const auto remove_x = std::clamp(x - radius, 0, width - 1);
      const auto add_x = std::clamp(x + radius + 1, 0, width - 1);
      sum += input[static_cast<std::size_t>(y * width + add_x)] -
             input[static_cast<std::size_t>(y * width + remove_x)];
    }
  }

  for (int x = 0; x < width; ++x) {
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_y = std::clamp(sample, 0, height - 1);
      sum += horizontal[static_cast<std::size_t>(clamped_y * width + x)];
    }

    for (int y = 0; y < height; ++y) {
      output[static_cast<std::size_t>(y * width + x)] = static_cast<float>(sum / kernel_width);
      const auto remove_y = std::clamp(y - radius, 0, height - 1);
      const auto add_y = std::clamp(y + radius + 1, 0, height - 1);
      sum += horizontal[static_cast<std::size_t>(add_y * width + x)] -
             horizontal[static_cast<std::size_t>(remove_y * width + x)];
    }
  }

  return output;
}

[[nodiscard]] OklabColor linear_rgb_to_oklab(const ColorPixel& pixel) {
  const auto red = std::max(0.0, pixel.red);
  const auto green = std::max(0.0, pixel.green);
  const auto blue = std::max(0.0, pixel.blue);

  const auto l = 0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue;
  const auto m = 0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue;
  const auto s = 0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue;

  const auto l_root = std::cbrt(std::max(0.0, l));
  const auto m_root = std::cbrt(std::max(0.0, m));
  const auto s_root = std::cbrt(std::max(0.0, s));

  return OklabColor{
      .lightness = 0.2104542553 * l_root + 0.7936177850 * m_root - 0.0040720468 * s_root,
      .a = 1.9779984951 * l_root - 2.4285922050 * m_root + 0.4505937099 * s_root,
      .b = 0.0259040371 * l_root + 0.7827717662 * m_root - 0.8086757660 * s_root,
  };
}

[[nodiscard]] ColorPixel oklab_to_linear_rgb(const OklabColor& color, double alpha) {
  const auto l_root = color.lightness + 0.3963377774 * color.a + 0.2158037573 * color.b;
  const auto m_root = color.lightness - 0.1055613458 * color.a - 0.0638541728 * color.b;
  const auto s_root = color.lightness - 0.0894841775 * color.a - 1.2914855480 * color.b;

  const auto l = l_root * l_root * l_root;
  const auto m = m_root * m_root * m_root;
  const auto s = s_root * s_root * s_root;

  return ColorPixel{
      .red = +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
      .green = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
      .blue = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
      .alpha = alpha,
  };
}

[[nodiscard]] bool in_gamut(const ColorPixel& pixel) {
  return pixel.red >= -1e-6 && pixel.red <= 1.0 + 1e-6 && pixel.green >= -1e-6 &&
         pixel.green <= 1.0 + 1e-6 && pixel.blue >= -1e-6 && pixel.blue <= 1.0 + 1e-6;
}

[[nodiscard]] ColorPixel clamp_rgb(const ColorPixel& pixel) {
  return ColorPixel{
      .red = clamp_unit(pixel.red),
      .green = clamp_unit(pixel.green),
      .blue = clamp_unit(pixel.blue),
      .alpha = clamp_unit(pixel.alpha),
  };
}

[[nodiscard]] ColorPixel gamut_fit_oklch(double lightness, double chroma, double hue, double alpha) {
  lightness = clamp_unit(lightness);
  chroma = std::max(0.0, chroma);
  hue = wrap_radians(hue);

  OklabColor candidate_lab{
      .lightness = lightness,
      .a = chroma * std::cos(hue),
      .b = chroma * std::sin(hue),
  };
  auto candidate = oklab_to_linear_rgb(candidate_lab, alpha);
  if (in_gamut(candidate)) {
    return clamp_rgb(candidate);
  }

  auto low = 0.0;
  auto high = chroma;
  for (int iteration = 0; iteration < 24; ++iteration) {
    const auto mid = (low + high) * 0.5;
    candidate_lab = OklabColor{
        .lightness = lightness,
        .a = mid * std::cos(hue),
        .b = mid * std::sin(hue),
    };
    candidate = oklab_to_linear_rgb(candidate_lab, alpha);
    if (in_gamut(candidate)) {
      low = mid;
    } else {
      high = mid;
    }
  }

  candidate_lab = OklabColor{
      .lightness = lightness,
      .a = low * std::cos(hue),
      .b = low * std::sin(hue),
  };
  return clamp_rgb(oklab_to_linear_rgb(candidate_lab, alpha));
}

}  // namespace

double clamp_signed_percent(double value) { return std::clamp(value, -100.0, 100.0); }

double clamp_unit(double value) { return std::clamp(value, 0.0, 1.0); }

double srgb_to_linear(double value) {
  const auto clamped = clamp_unit(value);
  if (clamped <= 0.04045) {
    return clamped / 12.92;
  }

  return std::pow((clamped + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
  const auto clamped = clamp_unit(value);
  if (clamped <= 0.0031308) {
    return clamped * 12.92;
  }

  return 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
}

double evaluate_tone_curve(double value, const ColorAdjustmentState& state) {
  const auto points = tone_curve_control_points(state);
  const auto clamped_value = clamp_unit(value);

  for (std::size_t index = 1; index < points.size(); ++index) {
    if (clamped_value > points[index].first && index + 1 < points.size()) {
      continue;
    }

    const auto start = points[index - 1];
    const auto end = points[index];
    const auto span = std::max(1e-6, end.first - start.first);
    const auto t = clamp_unit((clamped_value - start.first) / span);
    return clamp_unit(std::lerp(start.second, end.second, t));
  }

  return clamped_value;
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

void apply_color_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() != static_cast<std::size_t>(width * height)) {
    return;
  }

  const auto exposure_gain = std::pow(2.0, state.exposure);
  const auto white_balance = white_balance_multipliers(state.temperature, state.tint);
  const auto local_white_balance = white_balance_multipliers(state.linear_mask_temperature, 0.0);
  const auto texture_strength = clamp_signed_percent(state.texture) / 100.0;
  const auto clarity_strength = clamp_signed_percent(state.clarity) / 100.0;
  const auto dehaze_strength = clamp_signed_percent(state.dehaze) / 100.0;
  const auto saturation_strength = clamp_signed_percent(state.saturation) / 100.0;
  const auto vibrance_strength = clamp_signed_percent(state.vibrance) / 100.0;
  const auto gradient_radians = state.linear_mask_rotation * kPi / 180.0;
  const auto gradient_cosine = std::cos(gradient_radians);
  const auto gradient_sine = std::sin(gradient_radians);
  const auto gradient_feather = std::max(0.05, state.linear_mask_feather);
  std::vector<double> target_luminances(static_cast<std::size_t>(width * height), 0.0);
  auto max_target_luminance = 0.0;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      auto& pixel = (*pixels)[index];
      pixel.red = std::max(0.0, pixel.red * exposure_gain * white_balance[0]);
      pixel.green = std::max(0.0, pixel.green * exposure_gain * white_balance[1]);
      pixel.blue = std::max(0.0, pixel.blue * exposure_gain * white_balance[2]);

      if (state.linear_mask_enabled) {
        const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        const auto axis_coordinate =
            ((normalized_x - 0.5) * gradient_cosine + (normalized_y - 0.5) * gradient_sine) + 0.5;
        const auto mask_weight =
            clamp_unit(((state.linear_mask_position - axis_coordinate) / gradient_feather) + 0.5);

        if (mask_weight > 0.0) {
          const auto local_exposure_gain = std::pow(2.0, state.linear_mask_exposure * mask_weight);
          pixel.red *= 1.0 + (local_white_balance[0] - 1.0) * mask_weight;
          pixel.green *= 1.0 + (local_white_balance[1] - 1.0) * mask_weight;
          pixel.blue *= 1.0 + (local_white_balance[2] - 1.0) * mask_weight;
          pixel.red *= local_exposure_gain;
          pixel.green *= local_exposure_gain;
          pixel.blue *= local_exposure_gain;
        }
      }

      target_luminances[index] = apply_global_tone(luminance_for_pixel(pixel), state);
      max_target_luminance = std::max(max_target_luminance, target_luminances[index]);
    }
  }

  const auto shoulder_threshold = shoulder_threshold_for_max_luminance(max_target_luminance);
  for (std::size_t index = 0; index < pixels->size(); ++index) {
    auto target_luminance = target_luminances[index];
    if (shoulder_threshold < 1.0) {
      target_luminance = apply_highlight_shoulder(target_luminance, shoulder_threshold);
    }
    (*pixels)[index] =
        scale_pixel_to_luminance((*pixels)[index], evaluate_tone_curve(clamp_unit(target_luminance), state));
  }

  if (std::abs(texture_strength) > 1e-6 || std::abs(clarity_strength) > 1e-6 ||
      std::abs(dehaze_strength) > 1e-6) {
    std::vector<float> luminance_buffer(static_cast<std::size_t>(width * height), 0.0F);
    for (std::size_t index = 0; index < pixels->size(); ++index) {
      luminance_buffer[index] = static_cast<float>(luminance_for_pixel((*pixels)[index]));
    }

    const auto texture_blur = box_blur(luminance_buffer, width, height, 2);
    const auto clarity_blur = box_blur(luminance_buffer, width, height, 8);
    const auto haze_blur = box_blur(luminance_buffer, width, height, 20);

    for (std::size_t index = 0; index < pixels->size(); ++index) {
      const auto luminance = static_cast<double>(luminance_buffer[index]);
      const auto midtone_weight = bell_weight(luminance, 0.5, 0.42);
      auto target_luminance = luminance;
      target_luminance += texture_strength *
                          static_cast<double>(luminance_buffer[index] - texture_blur[index]) *
                          (0.5 + midtone_weight) * 1.10;
      target_luminance += clarity_strength *
                          static_cast<double>(luminance_buffer[index] - clarity_blur[index]) *
                          midtone_weight * 1.45;
      target_luminance += dehaze_strength *
                          static_cast<double>(luminance_buffer[index] - haze_blur[index]) * 1.75;
      (*pixels)[index] = scale_pixel_to_luminance((*pixels)[index], target_luminance);
    }
  }

  for (auto& pixel : *pixels) {
    auto oklab = linear_rgb_to_oklab(pixel);
    auto lightness = clamp_unit(oklab.lightness);
    auto chroma = std::hypot(oklab.a, oklab.b);
    auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

    if (chroma > 1e-6) {
      const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
      const auto low_chroma_weight = 1.0 - smoothstep(0.03, 0.18, chroma);
      const auto mid_lightness_weight = 1.0 - smoothstep(0.16, 0.50, std::abs(lightness - 0.55));
      const auto skin_protection =
          1.0 - 0.30 * circular_weight(normalized_hue, 0.075, 0.11) * smoothstep(0.02, 0.12, chroma);
      chroma *= std::max(0.0, 1.0 + vibrance_strength * low_chroma_weight * mid_lightness_weight *
                                         skin_protection * 1.30);
      chroma *= std::max(0.0, 1.0 + saturation_strength * 0.92 +
                                  std::max(0.0, dehaze_strength) * 0.10);

      const auto mixer_weight = channel_weight_for_selection(normalized_hue, state.color_mixer_channel);
      if (mixer_weight > 0.0) {
        hue = wrap_radians(hue + mixer_weight * (clamp_signed_percent(state.hue_adjustment) / 100.0) *
                                     0.14 * 2.0 * kPi);
        chroma *= std::max(
            0.0,
            1.0 + mixer_weight * (clamp_signed_percent(state.saturation_adjustment) / 100.0) * 0.80);
        lightness = clamp_unit(
            lightness + mixer_weight * (clamp_signed_percent(state.luminance_adjustment) / 100.0) * 0.18);
      }
    }

    pixel = gamut_fit_oklch(lightness, chroma, hue, pixel.alpha);
  }
}

}  // namespace photo_editor::pipeline
