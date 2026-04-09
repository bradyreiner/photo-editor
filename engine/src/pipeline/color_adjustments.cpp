#include "photo_editor/pipeline/color_adjustments.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <dispatch/dispatch.h>
#include <mutex>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
metal::MetalPipelineBackend& metal_backend() {
  static metal::MetalPipelineBackend backend;
  return backend;
}
#endif

constexpr double kMiddleGray = 0.18;
constexpr double kPi = std::numbers::pi_v<double>;

struct OklabColor final {
  double lightness = 0.0;
  double a = 0.0;
  double b = 0.0;
};

struct CurveEvaluationData final {
  std::vector<CurvePoint> points;
  std::vector<double> tangents;
};

struct LocalAdjustmentSettings final {
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double vibrance = 0.0;
  double saturation = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  double remove_strength = 0.0;
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

[[nodiscard]] double shortest_angular_delta(double from, double to) {
  auto delta = wrap_radians(to) - wrap_radians(from);
  if (delta > kPi) {
    delta -= 2.0 * kPi;
  } else if (delta < -kPi) {
    delta += 2.0 * kPi;
  }
  return delta;
}

[[nodiscard]] double luminance_for_pixel(const ColorPixel& pixel) {
  return (pixel.red * 0.2126) + (pixel.green * 0.7152) + (pixel.blue * 0.0722);
}

[[nodiscard]] bool has_per_channel_color_mixer_adjustments(const ColorAdjustmentState& state) {
  return std::any_of(
      state.color_mixer_channels.begin(), state.color_mixer_channels.end(),
      [](const ColorMixerChannelAdjustment& channel) {
        return std::abs(channel.hue) > 1e-9 || std::abs(channel.saturation) > 1e-9 ||
               std::abs(channel.luminance) > 1e-9;
      });
}

[[nodiscard]] bool has_color_grading_adjustments(const ColorAdjustmentState& state) {
  return std::abs(state.grading_shadows_hue) > 1e-9 ||
         std::abs(state.grading_shadows_saturation) > 1e-9 ||
         std::abs(state.grading_shadows_luminance) > 1e-9 ||
         std::abs(state.grading_midtones_hue) > 1e-9 ||
         std::abs(state.grading_midtones_saturation) > 1e-9 ||
         std::abs(state.grading_midtones_luminance) > 1e-9 ||
         std::abs(state.grading_highlights_hue) > 1e-9 ||
         std::abs(state.grading_highlights_saturation) > 1e-9 ||
         std::abs(state.grading_highlights_luminance) > 1e-9;
}

[[nodiscard]] bool has_legacy_color_mixer_adjustments(const ColorAdjustmentState& state) {
  return std::abs(state.hue_adjustment) > 1e-9 ||
         std::abs(state.saturation_adjustment) > 1e-9 ||
         std::abs(state.luminance_adjustment) > 1e-9;
}

[[nodiscard]] bool has_post_tone_color_adjustments(
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength) {
  return std::abs(saturation_strength) > 1e-9 ||
         std::abs(vibrance_strength) > 1e-9 ||
         has_per_channel_color_mixer_adjustments(state) ||
         has_legacy_color_mixer_adjustments(state) ||
         has_color_grading_adjustments(state);
}

[[nodiscard]] ColorPixel scale_pixel_to_luminance(const ColorPixel& pixel, double target_luminance) {
  const auto clamped_target = std::max(0.0, target_luminance);
  const auto current_luminance = luminance_for_pixel(pixel);
  const auto max_input_channel = std::max({pixel.red, pixel.green, pixel.blue});
  if (max_input_channel <= 1e-9) {
    return ColorPixel{
        .red = static_cast<float>(clamped_target),
        .green = static_cast<float>(clamped_target),
        .blue = static_cast<float>(clamped_target),
        .alpha = pixel.alpha,
    };
  }

  const auto safe_luminance = std::max(current_luminance, 1e-6);
  const auto scale = clamped_target / safe_luminance;
  return ColorPixel{
      .red = static_cast<float>(std::max(0.0, pixel.red * scale)),
      .green = static_cast<float>(std::max(0.0, pixel.green * scale)),
      .blue = static_cast<float>(std::max(0.0, pixel.blue * scale)),
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

[[nodiscard]] bool has_point_curve(std::span<const CurvePoint> points) {
  return points.size() >= 2;
}

[[nodiscard]] CurveEvaluationData build_curve_data(std::span<const CurvePoint> input_points);

[[nodiscard]] CurveEvaluationData build_parametric_curve_data(const ColorAdjustmentState& state) {
  CurveEvaluationData data;
  const auto points = tone_curve_control_points(state);
  std::vector<CurvePoint> control_points;
  control_points.reserve(points.size());
  for (const auto& [x, y] : points) {
    control_points.push_back(CurvePoint{.x = x, .y = y});
  }
  return build_curve_data(control_points);
}

[[nodiscard]] CurveEvaluationData build_curve_data(std::span<const CurvePoint> input_points) {
  CurveEvaluationData data;
  if (input_points.size() < 2) {
    return data;
  }

  data.points.assign(input_points.begin(), input_points.end());
  std::sort(data.points.begin(), data.points.end(), [](const CurvePoint& left, const CurvePoint& right) {
    return left.x < right.x;
  });

  std::vector<CurvePoint> normalized_points;
  normalized_points.reserve(data.points.size());
  for (const auto& point : data.points) {
    const auto x = clamp_unit(point.x);
    const auto y = clamp_unit(point.y);
    if (!normalized_points.empty() && std::abs(normalized_points.back().x - x) <= 1e-6) {
      normalized_points.back().y = y;
      continue;
    }
    normalized_points.push_back(CurvePoint{.x = x, .y = y});
  }

  if (normalized_points.size() < 2) {
    return {};
  }

  normalized_points.front() = CurvePoint{.x = 0.0, .y = 0.0};
  normalized_points.back() = CurvePoint{.x = 1.0, .y = 1.0};
  data.points = std::move(normalized_points);

  if (data.points.size() == 2) {
    data.tangents = {1.0, 1.0};
    return data;
  }

  const auto segment_count = data.points.size() - 1;
  std::vector<double> secants(segment_count, 0.0);
  for (std::size_t index = 0; index < segment_count; ++index) {
    const auto span = std::max(1e-6, data.points[index + 1].x - data.points[index].x);
    secants[index] = (data.points[index + 1].y - data.points[index].y) / span;
  }

  data.tangents.resize(data.points.size(), 0.0);
  data.tangents.front() = secants.front();
  data.tangents.back() = secants.back();
  for (std::size_t index = 1; index + 1 < data.points.size(); ++index) {
    if (secants[index - 1] * secants[index] <= 0.0) {
      data.tangents[index] = 0.0;
    } else {
      data.tangents[index] = 0.5 * (secants[index - 1] + secants[index]);
    }
  }

  for (std::size_t index = 0; index < segment_count; ++index) {
    if (std::abs(secants[index]) <= 1e-9) {
      data.tangents[index] = 0.0;
      data.tangents[index + 1] = 0.0;
      continue;
    }

    const auto a = data.tangents[index] / secants[index];
    const auto b = data.tangents[index + 1] / secants[index];
    const auto magnitude = std::hypot(a, b);
    if (magnitude <= 3.0) {
      continue;
    }

    const auto scale = 3.0 / magnitude;
    data.tangents[index] = scale * a * secants[index];
    data.tangents[index + 1] = scale * b * secants[index];
  }

  return data;
}

[[nodiscard]] double evaluate_curve_data(double value, const CurveEvaluationData& data) {
  if (data.points.size() < 2) {
    return clamp_unit(value);
  }

  const auto clamped_value = clamp_unit(value);
  for (std::size_t index = 0; index + 1 < data.points.size(); ++index) {
    const auto& start = data.points[index];
    const auto& end = data.points[index + 1];
    if (clamped_value > end.x && index + 2 <= data.points.size()) {
      continue;
    }

    const auto span = std::max(1e-6, end.x - start.x);
    const auto t = clamp_unit((clamped_value - start.x) / span);
    const auto t2 = t * t;
    const auto t3 = t2 * t;
    const auto h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const auto h10 = t3 - 2.0 * t2 + t;
    const auto h01 = -2.0 * t3 + 3.0 * t2;
    const auto h11 = t3 - t2;
    const auto result = h00 * start.y + h10 * span * data.tangents[index] +
                        h01 * end.y + h11 * span * data.tangents[index + 1];
    return clamp_unit(result);
  }

  return clamp_unit(clamped_value);
}

[[nodiscard]] double apply_pivoted_contrast(double value, double amount) {
  const auto safe_value = std::max(1e-6, value);
  const auto contrast = std::clamp(1.0 + amount * 0.48, 0.72, 1.52);
  const auto contrasted = kMiddleGray * std::pow(safe_value / kMiddleGray, contrast);
  const auto stop_distance = std::abs(std::log2(safe_value / kMiddleGray));
  const auto edge_protection =
      smoothstep(1.10, amount >= 0.0 ? 3.20 : 2.70, stop_distance);
  const auto blend_to_original =
      std::clamp(edge_protection * (0.40 + std::abs(amount) * 0.38), 0.0, 0.82);
  return std::lerp(contrasted, safe_value, blend_to_original);
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

[[nodiscard]] bool has_extended_range_rgb(const ColorPixel& pixel) {
  constexpr auto kTolerance = 1.0e-6F;
  return pixel.red > 1.0F + kTolerance || pixel.green > 1.0F + kTolerance ||
         pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
         pixel.green < -kTolerance || pixel.blue < -kTolerance;
}

[[nodiscard]] std::array<double, 3> white_balance_multipliers(double temperature, double tint) {
  const auto temperature_to_kelvin = [](double slider_value) {
    const auto clamped = clamp_signed_percent(slider_value);
    if (clamped >= 0.0) {
      return std::lerp(5500.0, 2000.0, clamped / 100.0);
    }
    return std::lerp(5500.0, 12000.0, -clamped / 100.0);
  };

  const auto chromaticity_from_kelvin = [](double kelvin) {
    const auto safe_kelvin = std::clamp(kelvin, 2000.0, 12000.0);
    double x = 0.3127;
    double y = 0.3290;
    if (safe_kelvin < 4000.0) {
      x = -0.2661239e9 / std::pow(safe_kelvin, 3.0) -
          0.2343589e6 / std::pow(safe_kelvin, 2.0) +
          0.8776956e3 / safe_kelvin + 0.179910;
      if (safe_kelvin < 2222.0) {
        y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
      } else {
        y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
      }
    } else {
      if (safe_kelvin <= 7000.0) {
        x = -4.6070e9 / std::pow(safe_kelvin, 3.0) +
            2.9678e6 / std::pow(safe_kelvin, 2.0) +
            0.09911e3 / safe_kelvin + 0.244063;
      } else {
        x = -2.0064e9 / std::pow(safe_kelvin, 3.0) +
            1.9018e6 / std::pow(safe_kelvin, 2.0) +
            0.24748e3 / safe_kelvin + 0.237040;
      }
      y = -3.0 * x * x + 2.87 * x - 0.275;
    }
    return std::array<double, 2>{x, y};
  };

  const auto white_rgb_from_kelvin = [&](double kelvin) {
    const auto chromaticity = chromaticity_from_kelvin(kelvin);
    const auto x = chromaticity[0];
    const auto y = std::max(1e-6, chromaticity[1]);
    const auto X = x / y;
    const auto Y = 1.0;
    const auto Z = (1.0 - x - y) / y;
    return std::array<double, 3>{
        std::max(1e-6, 3.2406 * X - 1.5372 * Y - 0.4986 * Z),
        std::max(1e-6, -0.9689 * X + 1.8758 * Y + 0.0415 * Z),
        std::max(1e-6, 0.0557 * X - 0.2040 * Y + 1.0570 * Z),
    };
  };

  const auto reference_white = white_rgb_from_kelvin(5500.0);
  const auto target_white = white_rgb_from_kelvin(temperature_to_kelvin(temperature));
  const auto normalized_tint = clamp_signed_percent(tint) / 100.0;
  auto red = (target_white[0] / reference_white[0]) * std::pow(2.0, normalized_tint * 0.08);
  auto green = (target_white[1] / reference_white[1]) * std::pow(2.0, -normalized_tint * 0.24);
  auto blue = (target_white[2] / reference_white[2]) * std::pow(2.0, normalized_tint * 0.08);
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

  const auto parallel_for = [](int count, auto&& work) {
    if (count <= 0) {
      return;
    }

    if (count < 16) {
      for (int index = 0; index < count; ++index) {
        work(index);
      }
      return;
    }

    using WorkType = std::decay_t<decltype(work)>;
    struct Context final {
      WorkType* work = nullptr;
    };

    auto* work_ptr = &work;
    auto callback = [](void* raw_context, size_t index) {
      auto* context = static_cast<Context*>(raw_context);
      (*context->work)(static_cast<int>(index));
    };

    Context context{.work = work_ptr};
    dispatch_apply_f(
        static_cast<size_t>(count),
        dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
        &context,
        callback);
  };

  const auto kernel_width = radius * 2 + 1;
  std::vector<float> horizontal(input.size(), 0.0F);
  std::vector<float> output(input.size(), 0.0F);

  parallel_for(height, [&](int y) {
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
  });

  parallel_for(width, [&](int x) {
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
  });

  return output;
}

[[nodiscard]] std::array<int, 3> gaussian_approximation_radii(double sigma) {
  if (sigma <= 0.0) {
    return {0, 0, 0};
  }

  constexpr int kPassCount = 3;
  const auto ideal_width = std::sqrt((12.0 * sigma * sigma / static_cast<double>(kPassCount)) + 1.0);
  auto lower_width = static_cast<int>(std::floor(ideal_width));
  if ((lower_width % 2) == 0) {
    --lower_width;
  }
  lower_width = std::max(1, lower_width);
  const auto upper_width = lower_width + 2;
  const auto lower_count = std::clamp(static_cast<int>(std::round(
      (12.0 * sigma * sigma -
       static_cast<double>(kPassCount * lower_width * lower_width) -
       static_cast<double>(4 * kPassCount * lower_width) -
       static_cast<double>(3 * kPassCount)) /
      static_cast<double>(-4 * lower_width - 4))), 0, kPassCount);

  std::array<int, 3> radii{};
  for (int pass = 0; pass < kPassCount; ++pass) {
    const auto width = pass < lower_count ? lower_width : upper_width;
    radii[static_cast<std::size_t>(pass)] = std::max(0, (width - 1) / 2);
  }
  return radii;
}

[[nodiscard]] std::vector<float> gaussian_approximate_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius) {
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0) {
    return input;
  }

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  if (metal.available()) {
    return metal.gaussian_approximate_blur(input, width, height, radius);
  }
#endif

  auto output = input;
  for (const auto pass_radius : gaussian_approximation_radii(static_cast<double>(radius))) {
    if (pass_radius <= 0) {
      continue;
    }
    output = box_blur(output, width, height, pass_radius);
  }
  return output;
}

[[nodiscard]] ColorPixel sample_bilinear(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double x,
    double y) {
  if (pixels.empty() || width <= 0 || height <= 0) {
    return {};
  }

  const auto clamped_x = std::clamp(x, 0.0, static_cast<double>(width - 1));
  const auto clamped_y = std::clamp(y, 0.0, static_cast<double>(height - 1));
  const auto x0 = static_cast<int>(std::floor(clamped_x));
  const auto y0 = static_cast<int>(std::floor(clamped_y));
  const auto x1 = std::min(width - 1, x0 + 1);
  const auto y1 = std::min(height - 1, y0 + 1);
  const auto fx = clamped_x - static_cast<double>(x0);
  const auto fy = clamped_y - static_cast<double>(y0);

  const auto& top_left = pixels[static_cast<std::size_t>(y0 * width + x0)];
  const auto& top_right = pixels[static_cast<std::size_t>(y0 * width + x1)];
  const auto& bottom_left = pixels[static_cast<std::size_t>(y1 * width + x0)];
  const auto& bottom_right = pixels[static_cast<std::size_t>(y1 * width + x1)];

  const auto lerp = [](double a, double b, double t) { return a * (1.0 - t) + b * t; };
  const auto mix_channel = [&](auto ColorPixel::*member) {
    const auto top = lerp(top_left.*member, top_right.*member, fx);
    const auto bottom = lerp(bottom_left.*member, bottom_right.*member, fx);
    return lerp(top, bottom, fy);
  };

  return ColorPixel{
      .red = static_cast<float>(mix_channel(&ColorPixel::red)),
      .green = static_cast<float>(mix_channel(&ColorPixel::green)),
      .blue = static_cast<float>(mix_channel(&ColorPixel::blue)),
      .alpha = static_cast<float>(mix_channel(&ColorPixel::alpha)),
  };
}

void apply_optical_corrections(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  if (pixels == nullptr || pixels->empty() || width <= 1 || height <= 1) {
    return;
  }

  const auto distortion_strength = clamp_signed_percent(state.lens_distortion) / 100.0;
  const auto vignette_strength = clamp_signed_percent(state.lens_vignette) / 100.0;
  const auto ca_strength = clamp_unit(state.chromatic_aberration / 100.0);
  if (std::abs(distortion_strength) <= 1e-6 &&
      std::abs(vignette_strength) <= 1e-6 &&
      ca_strength <= 1e-6) {
    return;
  }

  const auto source = *pixels;
  std::vector<ColorPixel> corrected(source.size());
  const auto center_x = static_cast<double>(width - 1) * 0.5;
  const auto center_y = static_cast<double>(height - 1) * 0.5;
  const auto max_radius = std::max(1.0, std::hypot(center_x, center_y));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto radius = std::hypot(dx, dy) / max_radius;
      const auto radius_sq = radius * radius;
      const auto distortion_scale =
          1.0 / std::max(0.40, 1.0 + distortion_strength * (0.55 * radius_sq + 0.20 * radius_sq * radius_sq));
      const auto sample_x = center_x + dx * distortion_scale;
      const auto sample_y = center_y + dy * distortion_scale;

      auto corrected_pixel = sample_bilinear(source, width, height, sample_x, sample_y);
      if (ca_strength > 1e-6 && radius > 1e-6) {
        const auto inverse_length = 1.0 / std::max(1e-6, std::hypot(dx, dy));
        const auto radial_x = dx * inverse_length;
        const auto radial_y = dy * inverse_length;
        const auto shift = ca_strength * radius_sq * 2.2;
        const auto red_sample =
            sample_bilinear(source, width, height, sample_x - radial_x * shift * 1.15, sample_y - radial_y * shift * 1.15);
        const auto blue_sample =
            sample_bilinear(source, width, height, sample_x - radial_x * shift * 0.85, sample_y - radial_y * shift * 0.85);
        corrected_pixel.red = red_sample.red;
        corrected_pixel.blue = blue_sample.blue;
      }

      if (std::abs(vignette_strength) > 1e-6) {
        const auto edge_weight = smoothstep(0.22, 1.0, radius);
        const auto gain = std::max(0.20, 1.0 + vignette_strength * edge_weight * 0.85);
        corrected_pixel.red = static_cast<float>(static_cast<double>(corrected_pixel.red) * gain);
        corrected_pixel.green = static_cast<float>(static_cast<double>(corrected_pixel.green) * gain);
        corrected_pixel.blue = static_cast<float>(static_cast<double>(corrected_pixel.blue) * gain);
      }

      corrected[static_cast<std::size_t>(y * width + x)] = ColorPixel{
          .red = static_cast<float>(clamp_unit(corrected_pixel.red)),
          .green = static_cast<float>(clamp_unit(corrected_pixel.green)),
          .blue = static_cast<float>(clamp_unit(corrected_pixel.blue)),
          .alpha = static_cast<float>(clamp_unit(corrected_pixel.alpha)),
      };
    }
  }

  *pixels = std::move(corrected);
}

[[nodiscard]] OklabColor linear_rgb_to_oklab(const ColorPixel& pixel) {
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));

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
      .red = static_cast<float>(+4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
      .green = static_cast<float>(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
      .blue = static_cast<float>(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s),
      .alpha = static_cast<float>(alpha),
  };
}

[[nodiscard]] bool in_gamut(const ColorPixel& pixel) {
  return pixel.red >= -1e-6 && pixel.red <= 1.0 + 1e-6 && pixel.green >= -1e-6 &&
         pixel.green <= 1.0 + 1e-6 && pixel.blue >= -1e-6 && pixel.blue <= 1.0 + 1e-6;
}

[[nodiscard]] ColorPixel clamp_rgb(const ColorPixel& pixel) {
  return ColorPixel{
      .red = static_cast<float>(clamp_unit(pixel.red)),
      .green = static_cast<float>(clamp_unit(pixel.green)),
      .blue = static_cast<float>(clamp_unit(pixel.blue)),
      .alpha = static_cast<float>(clamp_unit(pixel.alpha)),
  };
}

[[nodiscard]] std::array<double, 3> white_balance_multipliers(double temperature, double tint);
[[nodiscard]] ColorPixel gamut_fit_oklch(double lightness, double chroma, double hue, double alpha);

void apply_local_adjustment_mask(
    ColorPixel* pixel,
    double weight,
    double exposure,
    double contrast,
    double highlights,
    double shadows,
    double whites,
    double blacks,
    double temperature,
    double tint,
    double vibrance,
    double saturation,
    double hue_shift,
    double saturation_shift,
    double colorize_hue,
    double colorize_strength) {
  if (pixel == nullptr || weight <= 0.0) {
    return;
  }

  const auto local_white_balance = white_balance_multipliers(temperature * weight, tint * weight);
  const auto local_exposure_gain = std::pow(2.0, exposure * weight);
  pixel->red = static_cast<float>(static_cast<double>(pixel->red) * (1.0 + (local_white_balance[0] - 1.0) * weight));
  pixel->green = static_cast<float>(static_cast<double>(pixel->green) * (1.0 + (local_white_balance[1] - 1.0) * weight));
  pixel->blue = static_cast<float>(static_cast<double>(pixel->blue) * (1.0 + (local_white_balance[2] - 1.0) * weight));
  pixel->red = static_cast<float>(static_cast<double>(pixel->red) * local_exposure_gain);
  pixel->green = static_cast<float>(static_cast<double>(pixel->green) * local_exposure_gain);
  pixel->blue = static_cast<float>(static_cast<double>(pixel->blue) * local_exposure_gain);

  const auto weighted_contrast = contrast * weight;
  const auto weighted_highlights = highlights * weight;
  const auto weighted_shadows = shadows * weight;
  const auto weighted_whites = whites * weight;
  const auto weighted_blacks = blacks * weight;
  if (std::abs(weighted_contrast) > 1e-6 ||
      std::abs(weighted_highlights) > 1e-6 ||
      std::abs(weighted_shadows) > 1e-6 ||
      std::abs(weighted_whites) > 1e-6 ||
      std::abs(weighted_blacks) > 1e-6) {
    ColorAdjustmentState local_tone_state;
    local_tone_state.contrast = weighted_contrast;
    local_tone_state.highlights = weighted_highlights;
    local_tone_state.shadows = weighted_shadows;
    local_tone_state.whites = weighted_whites;
    local_tone_state.blacks = weighted_blacks;
    *pixel = scale_pixel_to_luminance(*pixel, apply_global_tone(luminance_for_pixel(*pixel), local_tone_state));
  }

  const auto weighted_vibrance = vibrance * weight;
  const auto weighted_saturation = saturation * weight;
  if (std::abs(weighted_vibrance) <= 1e-6 &&
      std::abs(weighted_saturation) <= 1e-6 &&
      std::abs(hue_shift) <= 1e-6 &&
      std::abs(saturation_shift) <= 1e-6 &&
      std::abs(colorize_strength) <= 1e-6) {
    return;
  }

  auto oklab = linear_rgb_to_oklab(*pixel);
  auto lightness = clamp_unit(oklab.lightness);
  auto chroma = std::hypot(oklab.a, oklab.b);
  auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

  if (chroma <= 1e-6) {
    hue = wrap_unit(colorize_hue) * 2.0 * kPi;
  } else {
    const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
    const auto low_chroma_weight = 1.0 - smoothstep(0.03, 0.18, chroma);
    const auto mid_lightness_weight = 1.0 - smoothstep(0.16, 0.50, std::abs(lightness - 0.55));
    const auto skin_protection =
        1.0 - 0.30 * circular_weight(normalized_hue, 0.08, 0.15) * smoothstep(0.02, 0.12, chroma);
    chroma *= std::max(
        0.0,
        1.0 + (clamp_signed_percent(weighted_vibrance) / 100.0) * low_chroma_weight *
                  mid_lightness_weight * skin_protection * 1.30);
    chroma *= std::max(
        0.0,
        1.0 + (clamp_signed_percent(weighted_saturation) / 100.0));
  }

  hue = wrap_radians(hue + (clamp_signed_percent(hue_shift) / 100.0) * kPi * weight);
  chroma *= std::max(
      0.0,
      1.0 + (clamp_signed_percent(saturation_shift) / 100.0) * 0.92 * weight);

  const auto colorize_amount = clamp_unit(colorize_strength / 100.0) * weight;
  if (colorize_amount > 1e-6) {
    const auto target_hue = wrap_unit(colorize_hue) * 2.0 * kPi;
    hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * colorize_amount);
    chroma = std::lerp(chroma, std::max(chroma, 0.18), colorize_amount);
  }

  *pixel = gamut_fit_oklch(lightness, chroma, hue, pixel->alpha);
}

[[nodiscard]] double radial_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorAdjustmentState& state) {
  if (!state.radial_mask_enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.05, state.radial_mask_radius_x);
  const auto radius_y = std::max(0.05, state.radial_mask_radius_y);
  const auto feather = std::clamp(state.radial_mask_feather, 0.05, 1.0);
  const auto radians = state.radial_mask_rotation * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - state.radial_mask_center_x;
  const auto translated_y = normalized_y - state.radial_mask_center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::sqrt((local_x * local_x) / (radius_x * radius_x) +
                                  (local_y * local_y) / (radius_y * radius_y));
  const auto inner_radius = std::max(0.0, 1.0 - feather);
  if (distance <= inner_radius) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_radius, 1.0, distance);
}

[[nodiscard]] double linear_mask_weight(
    double normalized_x,
    double normalized_y,
    double position,
    double feather,
    double rotation_degrees) {
  const auto radians = rotation_degrees * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto axis_coordinate = ((normalized_x - 0.5) * cosine + (normalized_y - 0.5) * sine) + 0.5;
  return clamp_unit(((position - axis_coordinate) / std::max(0.05, feather)) + 0.5);
}

[[nodiscard]] double apply_selection_expand(double base_weight, const LocalMask& mask) {
  const auto expand = clamp_signed_percent(mask.selection_expand) / 100.0;
  if (std::abs(expand) <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto threshold = std::clamp(0.5 - expand * 0.35, 0.08, 0.92);
  const auto ramp = std::clamp(0.06 + std::abs(expand) * 0.18, 0.04, 0.28);
  return smoothstep(threshold - ramp, threshold + ramp, clamp_unit(base_weight));
}

[[nodiscard]] double apply_selection_refine(double base_weight, const LocalMask& mask) {
  auto refined = apply_selection_expand(base_weight, mask);
  if (mask.selection_invert) {
    refined = 1.0 - refined;
  }
  return clamp_unit(refined);
}

[[nodiscard]] double radial_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  if (!mask.enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.05, mask.radius_x);
  const auto radius_y = std::max(0.05, mask.radius_y);
  const auto feather = std::clamp(mask.feather, 0.05, 1.0);
  const auto radians = mask.rotation * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - mask.center_x;
  const auto translated_y = normalized_y - mask.center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::sqrt((local_x * local_x) / (radius_x * radius_x) +
                                  (local_y * local_y) / (radius_y * radius_y));
  const auto inner_radius = std::max(0.0, 1.0 - feather);
  if (distance <= inner_radius) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_radius, 1.0, distance);
}

[[nodiscard]] double brush_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (!mask.segmentation_mask.empty() && mask.segmentation_width > 0 &&
      mask.segmentation_height > 0 &&
      mask.segmentation_mask.size() == expected_mask_size) {
    const auto max_x = std::max(mask.segmentation_width - 1, 0);
    const auto max_y = std::max(mask.segmentation_height - 1, 0);
    const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
    const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
    const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
    const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
    const auto ix1 = std::min(ix0 + 1, max_x);
    const auto iy1 = std::min(iy0 + 1, max_y);
    const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
    const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
    const auto index_for = [&](int x, int y) {
      return static_cast<std::size_t>(y * mask.segmentation_width + x);
    };
    const auto sample = [&](int x, int y) {
      return static_cast<double>(mask.segmentation_mask[index_for(x, y)]) / 255.0;
    };
    const auto v00 = sample(ix0, iy0);
    const auto v10 = sample(ix1, iy0);
    const auto v01 = sample(ix0, iy1);
    const auto v11 = sample(ix1, iy1);
    const auto top = v00 * (1.0 - dx) + v10 * dx;
    const auto bottom = v01 * (1.0 - dx) + v11 * dx;
    return clamp_unit(top * (1.0 - dy) + bottom * dy);
  }

  auto weight = 0.0;
  const auto softness = std::clamp(mask.feather, 0.05, 1.0);
  const auto inner_scale = std::max(0.0, 1.0 - softness);

  for (const auto& point : mask.brush_points) {
    const auto brush_size = std::max(0.01, point.size);
    const auto dx = normalized_x - point.x;
    const auto dy = normalized_y - point.y;
    const auto distance = std::sqrt(dx * dx + dy * dy) / brush_size;
    const auto point_weight =
        clamp_unit(point.flow) * (1.0 - smoothstep(inner_scale, 1.0, distance));
    weight = 1.0 - (1.0 - weight) * (1.0 - point_weight);
  }

  return clamp_unit(weight);
}

[[nodiscard]] double distance_to_segment(
    double px,
    double py,
    double ax,
    double ay,
    double bx,
    double by) {
  const auto abx = bx - ax;
  const auto aby = by - ay;
  const auto ab_len_sq = abx * abx + aby * aby;
  if (ab_len_sq <= 1e-9) {
    return std::hypot(px - ax, py - ay);
  }

  const auto t = std::clamp(((px - ax) * abx + (py - ay) * aby) / ab_len_sq, 0.0, 1.0);
  const auto closest_x = ax + abx * t;
  const auto closest_y = ay + aby * t;
  return std::hypot(px - closest_x, py - closest_y);
}

[[nodiscard]] bool polygon_contains_point(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return false;
  }

  auto inside = false;
  for (std::size_t i = 0, j = point_count - 1; i < point_count; j = i++) {
    const auto& pi = mask.polygon_points[i];
    const auto& pj = mask.polygon_points[j];
    const auto intersects =
        ((pi.y > normalized_y) != (pj.y > normalized_y)) &&
        (normalized_x <
         ((pj.x - pi.x) * (normalized_y - pi.y) / std::max(1e-9, pj.y - pi.y)) + pi.x);
    if (intersects) {
      inside = !inside;
    }
  }
  return inside;
}

[[nodiscard]] double polygon_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return 0.0;
  }

  auto min_distance = std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < point_count; ++i) {
    const auto& start = mask.polygon_points[i];
    const auto& end = mask.polygon_points[(i + 1) % point_count];
    min_distance = std::min(
        min_distance,
        distance_to_segment(normalized_x, normalized_y, start.x, start.y, end.x, end.y));
  }

  const auto feather = std::clamp(mask.feather, 0.0, 0.25);
  if (polygon_contains_point(normalized_x, normalized_y, mask)) {
    if (feather <= 1e-6) {
      return 1.0;
    }
    return 1.0 - 0.35 * smoothstep(0.0, feather, min_distance);
  }
  if (feather <= 1e-6) {
    return 0.0;
  }
  return 1.0 - smoothstep(0.0, feather, min_distance);
}

void hue_saturation_value_for_pixel(
    const ColorPixel& pixel,
    double* hue,
    double* saturation,
    double* value) {
  const auto max_channel = std::max({pixel.red, pixel.green, pixel.blue});
  const auto min_channel = std::min({pixel.red, pixel.green, pixel.blue});
  const auto delta = max_channel - min_channel;

  if (value != nullptr) {
    *value = clamp_unit(max_channel);
  }
  if (saturation != nullptr) {
    *saturation = max_channel <= 1e-6 ? 0.0 : clamp_unit(delta / max_channel);
  }
  if (hue == nullptr) {
    return;
  }

  if (delta <= 1e-6) {
    *hue = 0.0;
    return;
  }

  auto result = 0.0;
  if (max_channel == pixel.red) {
    result = std::fmod((pixel.green - pixel.blue) / delta, 6.0);
  } else if (max_channel == pixel.green) {
    result = ((pixel.blue - pixel.red) / delta) + 2.0;
  } else {
    result = ((pixel.red - pixel.green) / delta) + 4.0;
  }

  result /= 6.0;
  if (result < 0.0) {
    result += 1.0;
  }
  *hue = wrap_unit(result);
}

[[nodiscard]] double luminance_range_mask_weight(
    const ColorPixel& pixel,
    const LocalMask& mask) {
  auto min_value = std::clamp(mask.luminance_min, 0.0, 1.0);
  auto max_value = std::clamp(mask.luminance_max, 0.0, 1.0);
  if (min_value > max_value) {
    std::swap(min_value, max_value);
  }
  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto value = clamp_unit(luminance_for_pixel(pixel));
  const auto lower_weight = smoothstep(std::max(0.0, min_value - feather), min_value, value);
  const auto upper_weight =
      1.0 - smoothstep(max_value, std::min(1.0, max_value + feather), value);
  return clamp_unit(lower_weight * upper_weight);
}

[[nodiscard]] double color_range_mask_weight(
    const ColorPixel& pixel,
    const LocalMask& mask) {
  double hue = 0.0;
  double saturation = 0.0;
  hue_saturation_value_for_pixel(pixel, &hue, &saturation, nullptr);
  auto saturation_min = std::clamp(mask.saturation_min, 0.0, 1.0);
  auto saturation_max = std::clamp(mask.saturation_max, 0.0, 1.0);
  if (saturation_min > saturation_max) {
    std::swap(saturation_min, saturation_max);
  }
  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto hue_weight = circular_weight(hue, wrap_unit(mask.hue_center), std::max(0.02, mask.hue_range));
  const auto sat_lower =
      smoothstep(std::max(0.0, saturation_min - feather), saturation_min, saturation);
  const auto sat_upper =
      1.0 - smoothstep(saturation_max, std::min(1.0, saturation_max + feather), saturation);
  return clamp_unit(hue_weight * sat_lower * sat_upper);
}

[[nodiscard]] double subject_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 || mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return 0.0;
  }

  // Map normalized coordinates to mask bitmap with bilinear sampling.
  const auto max_x = std::max(mask.segmentation_width - 1, 0);
  const auto max_y = std::max(mask.segmentation_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;

  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.segmentation_width + x);
  };

  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.segmentation_mask[idx(x, y)]) / 255.0;
  };

  // Bilinear interpolation for smooth edges
  const auto v00 = sample(ix0, iy0);
  const auto v10 = sample(ix1, iy0);
  const auto v01 = sample(ix0, iy1);
  const auto v11 = sample(ix1, iy1);
  const auto top = v00 * (1.0 - dx) + v10 * dx;
  const auto bottom = v01 * (1.0 - dx) + v11 * dx;
  const auto value = top * (1.0 - dy) + bottom * dy;

  // Apply feather as a threshold ramp for edge softness control
  const auto feather = std::max(0.01, mask.feather);
  const auto low = std::max(0.0, 0.5 - feather * 0.5);
  const auto high = std::min(1.0, 0.5 + feather * 0.5);
  return smoothstep(low, high, value);
}

[[nodiscard]] double local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const LocalMask& mask) {
  if (!mask.enabled) {
    return 0.0;
  }

  if (mask.kind == "subject" || mask.kind == "object") {
    return apply_selection_refine(subject_mask_weight(normalized_x, normalized_y, mask), mask);
  }

  if (mask.kind == "radial") {
    return apply_selection_refine(radial_mask_weight(normalized_x, normalized_y, mask), mask);
  }

  if (mask.kind == "brush") {
    return apply_selection_refine(brush_mask_weight(normalized_x, normalized_y, mask), mask);
  }

  if (mask.kind == "polygon") {
    return apply_selection_refine(polygon_mask_weight(normalized_x, normalized_y, mask), mask);
  }

  if (mask.kind == "luminance") {
    return apply_selection_refine(luminance_range_mask_weight(pixel, mask), mask);
  }

  if (mask.kind == "color") {
    return apply_selection_refine(color_range_mask_weight(pixel, mask), mask);
  }

  return apply_selection_refine(
      linear_mask_weight(normalized_x, normalized_y, mask.position, mask.feather, mask.rotation),
      mask);
}

void accumulate_composite_weight(
    double raw_weight,
    std::string_view operation,
    double* composite_weight,
    bool* initialized);

[[nodiscard]] bool local_mask_set_has_adjustments(const LocalMaskSet& mask_set) {
  return std::abs(mask_set.exposure) > 1e-6 ||
         std::abs(mask_set.contrast) > 1e-6 ||
         std::abs(mask_set.highlights) > 1e-6 ||
         std::abs(mask_set.shadows) > 1e-6 ||
         std::abs(mask_set.whites) > 1e-6 ||
         std::abs(mask_set.blacks) > 1e-6 ||
         std::abs(mask_set.temperature) > 1e-6 ||
         std::abs(mask_set.tint) > 1e-6 ||
         std::abs(mask_set.vibrance) > 1e-6 ||
         std::abs(mask_set.saturation) > 1e-6 ||
         std::abs(mask_set.hue_shift) > 1e-6 ||
         std::abs(mask_set.saturation_shift) > 1e-6 ||
         std::abs(mask_set.colorize_hue) > 1e-6 ||
         std::abs(mask_set.colorize_strength) > 1e-6;
}

[[nodiscard]] double local_mask_set_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const LocalMaskSet& mask_set) {
  if (!mask_set.enabled) {
    return 0.0;
  }

  auto composite_weight = 0.0;
  auto initialized = false;
  for (const auto& source : mask_set.sources) {
    if (!source.enabled) {
      continue;
    }

    const auto raw_weight = local_mask_weight(normalized_x, normalized_y, pixel, source);
    accumulate_composite_weight(raw_weight, source.operation, &composite_weight, &initialized);
  }

  return initialized ? clamp_unit(composite_weight) : 0.0;
}

// A selection-only mask has no local adjustments and no retouch mode.
// It acts purely as a selection modifier (add/subtract/intersect) for other
// masks in the stack, so it should not prevent the composite rendering path.
[[nodiscard]] bool is_selection_only_mask(const LocalMask& mask) {
  return mask.retouch_mode.empty() &&
         std::abs(mask.exposure) <= 1e-6 &&
         std::abs(mask.contrast) <= 1e-6 &&
         std::abs(mask.highlights) <= 1e-6 &&
         std::abs(mask.shadows) <= 1e-6 &&
         std::abs(mask.whites) <= 1e-6 &&
         std::abs(mask.blacks) <= 1e-6 &&
         std::abs(mask.temperature) <= 1e-6 &&
         std::abs(mask.tint) <= 1e-6 &&
         std::abs(mask.vibrance) <= 1e-6 &&
         std::abs(mask.saturation) <= 1e-6 &&
         std::abs(mask.hue_shift) <= 1e-6 &&
         std::abs(mask.saturation_shift) <= 1e-6 &&
         std::abs(mask.colorize_hue) <= 1e-6 &&
         std::abs(mask.colorize_strength) <= 1e-6 &&
         std::abs(mask.remove_strength) <= 1e-6;
}

void accumulate_composite_weight(
    double raw_weight,
    std::string_view operation,
    double* composite_weight,
    bool* initialized) {
  if (composite_weight == nullptr || initialized == nullptr) {
    return;
  }

  if (operation == "subtract") {
    if (!*initialized) {
      return;
    }
    *composite_weight = std::max(0.0, *composite_weight - raw_weight);
    return;
  }

  if (operation == "intersect") {
    if (!*initialized) {
      return;
    }
    *composite_weight *= raw_weight;
    return;
  }

  if (!*initialized) {
    *composite_weight = raw_weight;
    *initialized = true;
    return;
  }

  *composite_weight = 1.0 - (1.0 - *composite_weight) * (1.0 - raw_weight);
}

[[nodiscard]] bool local_masks_share_adjustments(
    const std::vector<LocalMask>& masks,
    LocalAdjustmentSettings* shared_adjustments) {
  auto found_adjustment = false;
  LocalAdjustmentSettings adjustments;
  auto selection_initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    const auto can_contribute_adjustments =
        selection_initialized || (mask.operation != "subtract" && mask.operation != "intersect");
    selection_initialized = selection_initialized || mask.operation == "add";
    if (!can_contribute_adjustments) {
      continue;
    }

    // Skip selection-only masks — they refine the composite selection area
    // without contributing their own adjustments, so they should not cause
    // the renderer to fall back to the independent per-mask path.
    if (is_selection_only_mask(mask)) {
      continue;
    }

    if (!found_adjustment) {
      adjustments.exposure = mask.exposure;
      adjustments.contrast = mask.contrast;
      adjustments.highlights = mask.highlights;
      adjustments.shadows = mask.shadows;
      adjustments.whites = mask.whites;
      adjustments.blacks = mask.blacks;
      adjustments.temperature = mask.temperature;
      adjustments.tint = mask.tint;
      adjustments.vibrance = mask.vibrance;
      adjustments.saturation = mask.saturation;
      adjustments.hue_shift = mask.hue_shift;
      adjustments.saturation_shift = mask.saturation_shift;
      adjustments.colorize_hue = mask.colorize_hue;
      adjustments.colorize_strength = mask.colorize_strength;
      adjustments.remove_strength = mask.remove_strength;
      found_adjustment = true;
      continue;
    }

    if (std::abs(mask.exposure - adjustments.exposure) > 1e-6 ||
        std::abs(mask.contrast - adjustments.contrast) > 1e-6 ||
        std::abs(mask.highlights - adjustments.highlights) > 1e-6 ||
        std::abs(mask.shadows - adjustments.shadows) > 1e-6 ||
        std::abs(mask.whites - adjustments.whites) > 1e-6 ||
        std::abs(mask.blacks - adjustments.blacks) > 1e-6 ||
        std::abs(mask.temperature - adjustments.temperature) > 1e-6 ||
        std::abs(mask.tint - adjustments.tint) > 1e-6 ||
        std::abs(mask.vibrance - adjustments.vibrance) > 1e-6 ||
        std::abs(mask.saturation - adjustments.saturation) > 1e-6 ||
        std::abs(mask.hue_shift - adjustments.hue_shift) > 1e-6 ||
        std::abs(mask.saturation_shift - adjustments.saturation_shift) > 1e-6 ||
        std::abs(mask.colorize_hue - adjustments.colorize_hue) > 1e-6 ||
        std::abs(mask.colorize_strength - adjustments.colorize_strength) > 1e-6 ||
        std::abs(mask.remove_strength - adjustments.remove_strength) > 1e-6) {
      return false;
    }
  }

  if (!found_adjustment) {
    return false;
  }

  if (shared_adjustments != nullptr) {
    *shared_adjustments = adjustments;
  }
  return true;
}

[[nodiscard]] bool local_masks_all_additive(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    if (mask.operation != "add") {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool primary_local_mask_adjustments(
    const std::vector<LocalMask>& masks,
    LocalAdjustmentSettings* adjustments) {
  auto selection_initialized = false;
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    const auto can_contribute_adjustments =
        selection_initialized || (mask.operation != "subtract" && mask.operation != "intersect");
    selection_initialized = selection_initialized || mask.operation == "add";
    if (!can_contribute_adjustments) {
      continue;
    }
    // Skip selection-only masks so that the primary adjustments come from the
    // first mask that actually has values (e.g. a subject or radial mask),
    // not from a zero-adjustment refinement brush.
    if (is_selection_only_mask(mask)) {
      continue;
    }
    if (adjustments != nullptr) {
      adjustments->exposure = mask.exposure;
      adjustments->contrast = mask.contrast;
      adjustments->highlights = mask.highlights;
      adjustments->shadows = mask.shadows;
      adjustments->whites = mask.whites;
      adjustments->blacks = mask.blacks;
      adjustments->temperature = mask.temperature;
      adjustments->tint = mask.tint;
      adjustments->vibrance = mask.vibrance;
      adjustments->saturation = mask.saturation;
      adjustments->hue_shift = mask.hue_shift;
      adjustments->saturation_shift = mask.saturation_shift;
      adjustments->colorize_hue = mask.colorize_hue;
      adjustments->colorize_strength = mask.colorize_strength;
      adjustments->remove_strength = mask.remove_strength;
    }
    return true;
  }
  return false;
}

[[nodiscard]] bool has_local_mask_removal(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled || mask.remove_strength <= 1e-6) {
      continue;
    }
    if (mask.retouch_mode == "heal" || mask.retouch_mode == "clone") {
      continue;
    }
    if (mask.enabled && mask.remove_strength > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_heal(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "heal") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_clone(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled || mask.kind != "brush" || mask.operation != "add") {
      continue;
    }
    if (mask.retouch_mode == "clone" &&
        mask.clone_source_set &&
        !mask.brush_points.empty()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] double composite_local_removal_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const std::vector<LocalMask>& masks,
    std::string_view retouch_mode) {
  auto composite_weight = 0.0;
  auto initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

     if (retouch_mode == "heal") {
      if (mask.retouch_mode != "heal") {
        continue;
      }
    } else {
      if (mask.retouch_mode == "heal" || mask.retouch_mode == "clone") {
        continue;
      }
    }

    const auto strength = clamp_unit(mask.remove_strength / 100.0);
    if (strength <= 1e-6) {
      continue;
    }

    const auto weighted = local_mask_weight(normalized_x, normalized_y, pixel, mask) * strength;
    accumulate_composite_weight(weighted, mask.operation, &composite_weight, &initialized);
  }

  return initialized ? clamp_unit(composite_weight) : 0.0;
}

struct RemovalSample final {
  ColorPixel pixel;
  double confidence = 0.0;
  int distance = 0;
  bool valid = false;
};

struct RemovalCandidate final {
  ColorPixel pixel;
  double confidence = 0.0;
  double score = 0.0;
  bool valid = false;
};

[[nodiscard]] double removal_color_distance(const ColorPixel& left, const ColorPixel& right) {
  const auto dr = static_cast<double>(left.red) - static_cast<double>(right.red);
  const auto dg = static_cast<double>(left.green) - static_cast<double>(right.green);
  const auto db = static_cast<double>(left.blue) - static_cast<double>(right.blue);
  return std::sqrt(dr * dr + dg * dg + db * db);
}

[[nodiscard]] RemovalSample nearest_known_removal_sample(
    const std::vector<ColorPixel>& pixels,
    const std::vector<float>& confidence,
    int width,
    int height,
    int start_x,
    int start_y,
    int step_x,
    int step_y,
    int max_distance) {
  RemovalSample sample;
  if (width <= 0 || height <= 0 || start_x < 0 || start_y < 0 ||
      start_x >= width || start_y >= height || max_distance <= 0) {
    return sample;
  }

  for (int distance = 1; distance <= max_distance; ++distance) {
    const auto x = start_x + step_x * distance;
    const auto y = start_y + step_y * distance;
    if (x < 0 || y < 0 || x >= width || y >= height) {
      break;
    }
    const auto index = static_cast<std::size_t>(y * width + x);
    if (confidence[index] <= 1e-4F) {
      continue;
    }
    sample.pixel = pixels[index];
    sample.confidence = static_cast<double>(confidence[index]);
    sample.distance = distance;
    sample.valid = true;
    return sample;
  }

  return sample;
}

[[nodiscard]] bool has_known_neighbor(
    const std::vector<float>& confidence,
    int width,
    int height,
    int x,
    int y) {
  for (int offset_y = -1; offset_y <= 1; ++offset_y) {
    for (int offset_x = -1; offset_x <= 1; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto sample_x = x + offset_x;
      const auto sample_y = y + offset_y;
      if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
        continue;
      }
      if (confidence[static_cast<std::size_t>(sample_y * width + sample_x)] > 1e-4F) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] RemovalCandidate directional_removal_candidate(
    const std::vector<ColorPixel>& pixels,
    const std::vector<float>& confidence,
    int width,
    int height,
    int x,
    int y,
    int max_line_radius) {
  RemovalCandidate best_candidate;
  const auto sample_structure_strength =
      [&](int sample_x, int sample_y, int step_x, int step_y) {
        if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
          return 0.0;
        }
        const auto sample_index = static_cast<std::size_t>(sample_y * width + sample_x);
        if (confidence[sample_index] <= 1e-4F) {
          return 0.0;
        }
        const auto perp_x = -step_y;
        const auto perp_y = step_x;
        auto structure = 0.0;
        auto count = 0.0;
        for (const auto sign : {-1, 1}) {
          const auto neighbor_x = sample_x + perp_x * sign;
          const auto neighbor_y = sample_y + perp_y * sign;
          if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width || neighbor_y >= height) {
            continue;
          }
          const auto neighbor_index = static_cast<std::size_t>(neighbor_y * width + neighbor_x);
          if (confidence[neighbor_index] <= 1e-4F) {
            continue;
          }
          structure += removal_color_distance(pixels[sample_index], pixels[neighbor_index]);
          count += 1.0;
        }
        return count > 0.0 ? structure / count : 0.0;
      };
  constexpr std::array<std::pair<int, int>, 4> kDirections{
      std::pair<int, int>{1, 0},
      std::pair<int, int>{0, 1},
      std::pair<int, int>{1, 1},
      std::pair<int, int>{1, -1},
  };

  for (const auto& [step_x, step_y] : kDirections) {
    const auto forward = nearest_known_removal_sample(
        pixels, confidence, width, height, x, y, step_x, step_y, max_line_radius);
    const auto backward = nearest_known_removal_sample(
        pixels, confidence, width, height, x, y, -step_x, -step_y, max_line_radius);
    if (!forward.valid && !backward.valid) {
      continue;
    }

    RemovalCandidate candidate;
    if (forward.valid && backward.valid) {
      const auto forward_weight = forward.confidence / static_cast<double>(std::max(1, forward.distance));
      const auto backward_weight = backward.confidence / static_cast<double>(std::max(1, backward.distance));
      const auto weight_sum = std::max(1e-6, forward_weight + backward_weight);
      candidate.pixel = ColorPixel{
          .red = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.red) * forward_weight +
               static_cast<double>(backward.pixel.red) * backward_weight) /
                  weight_sum)),
          .green = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.green) * forward_weight +
               static_cast<double>(backward.pixel.green) * backward_weight) /
                  weight_sum)),
          .blue = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.blue) * forward_weight +
               static_cast<double>(backward.pixel.blue) * backward_weight) /
                  weight_sum)),
          .alpha = 1.0F,
      };
      const auto color_match =
          1.0 / (1.0 + removal_color_distance(forward.pixel, backward.pixel) * 6.0);
      const auto structure_strength =
          sample_structure_strength(x + step_x * forward.distance, y + step_y * forward.distance, step_x, step_y) +
          sample_structure_strength(x - step_x * backward.distance, y - step_y * backward.distance, step_x, step_y);
      const auto distance_sum = static_cast<double>(forward.distance + backward.distance);
      candidate.confidence = std::clamp(
          0.45 + 0.25 * std::min(forward.confidence, backward.confidence) + 0.20 * color_match +
              std::min(0.10, structure_strength * 0.10),
          0.0, 1.0);
      candidate.score =
          (forward_weight + backward_weight) * color_match *
          (1.0 + std::min(1.5, structure_strength * 4.0)) /
          std::max(1.0, distance_sum * 0.5);
    } else {
      const auto& sample = forward.valid ? forward : backward;
      candidate.pixel = sample.pixel;
      candidate.confidence =
          std::clamp(0.28 + 0.45 * sample.confidence, 0.0, 0.9);
      candidate.score =
          (sample.confidence / static_cast<double>(std::max(1, sample.distance))) * 0.35;
    }
    candidate.pixel.alpha = pixels[static_cast<std::size_t>(y * width + x)].alpha;
    candidate.valid = true;
    if (!best_candidate.valid || candidate.score > best_candidate.score) {
      best_candidate = candidate;
    }
  }

  RemovalCandidate neighborhood_candidate;
  double weighted_red = 0.0;
  double weighted_green = 0.0;
  double weighted_blue = 0.0;
  double total_weight = 0.0;
  double confidence_sum = 0.0;
  for (int offset_y = -2; offset_y <= 2; ++offset_y) {
    for (int offset_x = -2; offset_x <= 2; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto sample_x = x + offset_x;
      const auto sample_y = y + offset_y;
      if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
        continue;
      }
      const auto index = static_cast<std::size_t>(sample_y * width + sample_x);
      const auto sample_confidence = static_cast<double>(confidence[index]);
      if (sample_confidence <= 1e-4) {
        continue;
      }
      const auto distance_sq = static_cast<double>(offset_x * offset_x + offset_y * offset_y);
      const auto weight = sample_confidence / std::max(1.0, distance_sq);
      weighted_red += static_cast<double>(pixels[index].red) * weight;
      weighted_green += static_cast<double>(pixels[index].green) * weight;
      weighted_blue += static_cast<double>(pixels[index].blue) * weight;
      total_weight += weight;
      confidence_sum += sample_confidence;
    }
  }
  if (total_weight > 1e-6) {
    neighborhood_candidate.pixel = ColorPixel{
        .red = static_cast<float>(std::max(0.0, weighted_red / total_weight)),
        .green = static_cast<float>(std::max(0.0, weighted_green / total_weight)),
        .blue = static_cast<float>(std::max(0.0, weighted_blue / total_weight)),
        .alpha = pixels[static_cast<std::size_t>(y * width + x)].alpha,
    };
    neighborhood_candidate.confidence =
        std::clamp(0.20 + confidence_sum / (total_weight * 8.0), 0.0, 0.85);
    neighborhood_candidate.score = total_weight * 0.06;
    neighborhood_candidate.valid = true;
  }

  if (!best_candidate.valid ||
      (neighborhood_candidate.valid && neighborhood_candidate.score > best_candidate.score)) {
    return neighborhood_candidate;
  }
  return best_candidate;
}

void apply_local_removal(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const std::vector<double>& removal_mask,
    double detail_preservation,
    RemovalQuality removal_quality) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() != removal_mask.size()) {
    return;
  }

  const auto pixel_count = pixels->size();
  auto filled_pixels = *pixels;
  std::vector<float> confidence(pixel_count, 1.0F);
  std::vector<uint8_t> unresolved(pixel_count, 0);
  auto has_masked_pixels = false;
  for (std::size_t index = 0; index < pixel_count; ++index) {
    confidence[index] = static_cast<float>(1.0 - clamp_unit(removal_mask[index]));
    has_masked_pixels = has_masked_pixels || removal_mask[index] > 1e-4;
    unresolved[index] = removal_mask[index] > 1e-4 ? 1U : 0U;
  }
  if (!has_masked_pixels) {
    return;
  }

  const auto max_line_radius =
      std::clamp(std::min(width, height) / 10, 3, 24);
  auto unresolved_count = 0;
  for (const auto flag : unresolved) {
    unresolved_count += flag != 0 ? 1 : 0;
  }

  while (unresolved_count > 0) {
    auto next_pixels = filled_pixels;
    auto next_confidence = confidence;
    std::vector<std::size_t> resolved_indices;
    resolved_indices.reserve(static_cast<std::size_t>(unresolved_count));

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (unresolved[index] == 0U || !has_known_neighbor(confidence, width, height, x, y)) {
          continue;
        }

        const auto candidate = directional_removal_candidate(
            filled_pixels,
            confidence,
            width,
            height,
            x,
            y,
            max_line_radius);
        if (!candidate.valid) {
          continue;
        }

        const auto mask_weight = clamp_unit(removal_mask[index]);
        const auto blend =
            std::clamp(mask_weight * (0.78 + candidate.confidence * 0.22), 0.0, 1.0);
        next_pixels[index].red = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].red), static_cast<double>(candidate.pixel.red), blend));
        next_pixels[index].green = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].green), static_cast<double>(candidate.pixel.green), blend));
        next_pixels[index].blue = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].blue), static_cast<double>(candidate.pixel.blue), blend));
        next_confidence[index] = static_cast<float>(std::max(
            static_cast<double>(next_confidence[index]),
            std::clamp(candidate.confidence, 0.0, 1.0)));
        resolved_indices.push_back(index);
      }
    }

    if (resolved_indices.empty()) {
      break;
    }

    filled_pixels = std::move(next_pixels);
    confidence = std::move(next_confidence);
    for (const auto index : resolved_indices) {
      if (unresolved[index] == 0U) {
        continue;
      }
      unresolved[index] = 0U;
      --unresolved_count;
    }
  }

  if (unresolved_count > 0) {
    constexpr std::array<int, 5> kFillRadii{2, 4, 8, 12, 18};
    for (const auto radius : kFillRadii) {
      std::vector<float> weighted_red(pixel_count, 0.0F);
      std::vector<float> weighted_green(pixel_count, 0.0F);
      std::vector<float> weighted_blue(pixel_count, 0.0F);

      for (std::size_t index = 0; index < pixel_count; ++index) {
        weighted_red[index] = static_cast<float>(filled_pixels[index].red) * confidence[index];
        weighted_green[index] = static_cast<float>(filled_pixels[index].green) * confidence[index];
        weighted_blue[index] = static_cast<float>(filled_pixels[index].blue) * confidence[index];
      }

      const auto blurred_confidence = box_blur(confidence, width, height, radius);
      const auto blurred_red = box_blur(weighted_red, width, height, radius);
      const auto blurred_green = box_blur(weighted_green, width, height, radius);
      const auto blurred_blue = box_blur(weighted_blue, width, height, radius);

      for (std::size_t index = 0; index < pixel_count; ++index) {
        if (unresolved[index] == 0U) {
          continue;
        }
        const auto local_confidence = static_cast<double>(blurred_confidence[index]);
        if (local_confidence <= 1e-5) {
          continue;
        }

        const auto candidate = ColorPixel{
            .red = static_cast<float>(std::max(0.0, static_cast<double>(blurred_red[index]) / local_confidence)),
            .green = static_cast<float>(std::max(0.0, static_cast<double>(blurred_green[index]) / local_confidence)),
            .blue = static_cast<float>(std::max(0.0, static_cast<double>(blurred_blue[index]) / local_confidence)),
            .alpha = filled_pixels[index].alpha,
        };
        const auto blend =
            std::clamp(0.40 + (1.0 - static_cast<double>(confidence[index])) * 0.60, 0.0, 1.0);
        filled_pixels[index].red = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].red), static_cast<double>(candidate.red), blend));
        filled_pixels[index].green = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].green), static_cast<double>(candidate.green), blend));
        filled_pixels[index].blue = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].blue), static_cast<double>(candidate.blue), blend));
        confidence[index] = static_cast<float>(std::max(
            static_cast<double>(confidence[index]),
            std::clamp(local_confidence, 0.0, 1.0)));
      }
    }
  }

  if (detail_preservation > 1e-6) {
    std::vector<float> filled_red(pixel_count, 0.0F);
    std::vector<float> filled_green(pixel_count, 0.0F);
    std::vector<float> filled_blue(pixel_count, 0.0F);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      filled_red[index] = filled_pixels[index].red;
      filled_green[index] = filled_pixels[index].green;
      filled_blue[index] = filled_pixels[index].blue;
    }
    const auto blurred_red = box_blur(filled_red, width, height, 2);
    const auto blurred_green = box_blur(filled_green, width, height, 2);
    const auto blurred_blue = box_blur(filled_blue, width, height, 2);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      const auto mask_weight = clamp_unit(removal_mask[index]);
      if (mask_weight <= 1e-4) {
        continue;
      }
      const auto detail_gain = detail_preservation * mask_weight * 0.30;
      filled_pixels[index].red = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].red) +
              static_cast<double>(filled_red[index] - blurred_red[index]) * detail_gain));
      filled_pixels[index].green = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].green) +
              static_cast<double>(filled_green[index] - blurred_green[index]) * detail_gain));
      filled_pixels[index].blue = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].blue) +
              static_cast<double>(filled_blue[index] - blurred_blue[index]) * detail_gain));
    }
  }

  if (removal_quality == RemovalQuality::Final) {
    auto min_mask_x = width;
    auto min_mask_y = height;
    auto max_mask_x = -1;
    auto max_mask_y = -1;
    auto masked_pixel_count = 0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (clamp_unit(removal_mask[index]) <= 1e-4) {
          continue;
        }
        min_mask_x = std::min(min_mask_x, x);
        min_mask_y = std::min(min_mask_y, y);
        max_mask_x = std::max(max_mask_x, x);
        max_mask_y = std::max(max_mask_y, y);
        ++masked_pixel_count;
      }
    }

    if (masked_pixel_count > 0) {
      const auto search_radius = std::clamp(std::max(width, height) / 16, 12, 64);
      const auto min_search_x = std::max(0, min_mask_x - search_radius);
      const auto min_search_y = std::max(0, min_mask_y - search_radius);
      const auto max_search_x = std::min(width - 1, max_mask_x + search_radius);
      const auto max_search_y = std::min(height - 1, max_mask_y + search_radius);

      std::vector<std::size_t> candidate_indices;
      candidate_indices.reserve(
          static_cast<std::size_t>(std::max(0, (max_search_x - min_search_x + 1) *
                                                   (max_search_y - min_search_y + 1))));
      for (int y = min_search_y; y <= max_search_y; ++y) {
        for (int x = min_search_x; x <= max_search_x; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          if (clamp_unit(removal_mask[index]) > 1e-4) {
            continue;
          }
          candidate_indices.push_back(index);
        }
      }

      if (!candidate_indices.empty()) {
        const auto candidate_stride =
            std::max(1, static_cast<int>(candidate_indices.size() / 4096));
        const auto patch_radius = 2;
        auto refined_pixels = filled_pixels;
        for (int y = min_mask_y; y <= max_mask_y; ++y) {
          for (int x = min_mask_x; x <= max_mask_x; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const auto mask_weight = clamp_unit(removal_mask[index]);
            if (mask_weight <= 1e-4) {
              continue;
            }

            auto best_score = std::numeric_limits<double>::max();
            auto best_candidate = refined_pixels[index];
            auto found_candidate = false;
            for (std::size_t candidate_offset = 0;
                 candidate_offset < candidate_indices.size();
                 candidate_offset += static_cast<std::size_t>(candidate_stride)) {
              const auto candidate_index = candidate_indices[candidate_offset];
              const auto candidate_x = static_cast<int>(candidate_index % static_cast<std::size_t>(width));
              const auto candidate_y = static_cast<int>(candidate_index / static_cast<std::size_t>(width));

              auto score = 0.0;
              auto support = 0.0;
              for (int offset_y = -patch_radius; offset_y <= patch_radius; ++offset_y) {
                for (int offset_x = -patch_radius; offset_x <= patch_radius; ++offset_x) {
                  if (offset_x == 0 && offset_y == 0) {
                    continue;
                  }
                  const auto target_x = x + offset_x;
                  const auto target_y = y + offset_y;
                  const auto sample_x = candidate_x + offset_x;
                  const auto sample_y = candidate_y + offset_y;
                  if (target_x < 0 || target_y < 0 || target_x >= width || target_y >= height ||
                      sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
                    continue;
                  }

                  const auto target_index =
                      static_cast<std::size_t>(target_y * width + target_x);
                  const auto sample_index =
                      static_cast<std::size_t>(sample_y * width + sample_x);
                  if (clamp_unit(removal_mask[sample_index]) > 1e-4) {
                    continue;
                  }

                  const auto context_weight =
                      1.0 / std::max(1.0, static_cast<double>(offset_x * offset_x + offset_y * offset_y));
                  const auto target_context =
                      clamp_unit(removal_mask[target_index]) <= 0.35 ? (*pixels)[target_index]
                                                                     : filled_pixels[target_index];
                  score += removal_color_distance(target_context, filled_pixels[sample_index]) *
                           context_weight;
                  support += context_weight;
                }
              }

              if (support <= 1e-6) {
                continue;
              }
              score /= support;
              if (score < best_score) {
                best_score = score;
                best_candidate = filled_pixels[candidate_index];
                found_candidate = true;
              }
            }

            if (!found_candidate) {
              continue;
            }

            const auto blend =
                std::clamp(0.45 + mask_weight * 0.45, 0.0, 1.0);
            refined_pixels[index].red = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].red), static_cast<double>(best_candidate.red), blend));
            refined_pixels[index].green = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].green), static_cast<double>(best_candidate.green), blend));
            refined_pixels[index].blue = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].blue), static_cast<double>(best_candidate.blue), blend));
          }
        }
        filled_pixels = std::move(refined_pixels);
      }
    }
  }

  for (std::size_t index = 0; index < pixel_count; ++index) {
    const auto mask_weight = clamp_unit(removal_mask[index]);
    if (mask_weight <= 1e-4) {
      continue;
    }
    (*pixels)[index].red = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].red), static_cast<double>(filled_pixels[index].red), mask_weight));
    (*pixels)[index].green = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].green), static_cast<double>(filled_pixels[index].green), mask_weight));
    (*pixels)[index].blue = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].blue), static_cast<double>(filled_pixels[index].blue), mask_weight));
  }
}

void apply_local_clone(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  for (const auto& mask : masks) {
    if (!mask.enabled || mask.kind != "brush" || mask.operation != "add" ||
        mask.retouch_mode != "clone" || !mask.clone_source_set ||
        mask.brush_points.empty()) {
      continue;
    }

    const auto source_pixels = *pixels;
    const auto source_center_x = mask.clone_source_x * static_cast<double>(std::max(width - 1, 0));
    const auto source_center_y = mask.clone_source_y * static_cast<double>(std::max(height - 1, 0));
    const auto offset_x = mask.clone_offset_x * static_cast<double>(std::max(width - 1, 0));
    const auto offset_y = mask.clone_offset_y * static_cast<double>(std::max(height - 1, 0));
    const auto destination_origin_x = source_center_x - offset_x;
    const auto destination_origin_y = source_center_y - offset_y;
    const auto scale = std::clamp(mask.clone_scale, 0.25, 4.0);
    const auto radians = mask.clone_rotation * kPi / 180.0;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        const auto weight = local_mask_weight(normalized_x, normalized_y, source_pixels[index], mask);
        if (weight <= 1e-4) {
          continue;
        }

        const auto dx = static_cast<double>(x) - destination_origin_x;
        const auto dy = static_cast<double>(y) - destination_origin_y;
        const auto source_dx = (dx * cosine + dy * sine) / scale;
        const auto source_dy = (-dx * sine + dy * cosine) / scale;
        const auto candidate = sample_bilinear(
            source_pixels,
            width,
            height,
            source_center_x + source_dx,
            source_center_y + source_dy);
        const auto blend = clamp_unit(weight * clamp_unit(mask.brush_flow));
        (*pixels)[index].red = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].red), static_cast<double>(candidate.red), blend));
        (*pixels)[index].green = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].green), static_cast<double>(candidate.green), blend));
        (*pixels)[index].blue = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].blue), static_cast<double>(candidate.blue), blend));
      }
    }
  }
}

[[nodiscard]] double composite_local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const std::vector<LocalMask>& masks) {
  auto composite_weight = 0.0;
  auto initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    const auto raw_weight = local_mask_weight(normalized_x, normalized_y, pixel, mask);
    accumulate_composite_weight(raw_weight, mask.operation, &composite_weight, &initialized);
  }

  return initialized ? clamp_unit(composite_weight) : 0.0;
}

[[nodiscard]] std::vector<float> build_composite_local_mask_weights(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    const std::vector<LocalMask>& masks) {
  const auto pixel_count = static_cast<std::size_t>(width * height);
  std::vector<float> weights(pixel_count, 0.0F);
  if (pixels.size() != pixel_count) {
    return {};
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      auto pixel = pixels[index];
      pixel.red = static_cast<float>(
          std::max(0.0, static_cast<double>(pixel.red) * exposure_gain * white_balance[0]));
      pixel.green = static_cast<float>(
          std::max(0.0, static_cast<double>(pixel.green) * exposure_gain * white_balance[1]));
      pixel.blue = static_cast<float>(
          std::max(0.0, static_cast<double>(pixel.blue) * exposure_gain * white_balance[2]));

      const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      weights[index] = static_cast<float>(
          composite_local_mask_weight(normalized_x, normalized_y, pixel, masks));
    }
  }

  return weights;
}

[[nodiscard]] bool local_adjustments_supported_by_metal_pre_tone(
    const LocalAdjustmentSettings& adjustments) {
  return std::abs(adjustments.contrast) <= 1e-6 &&
         std::abs(adjustments.highlights) <= 1e-6 &&
         std::abs(adjustments.shadows) <= 1e-6 &&
         std::abs(adjustments.whites) <= 1e-6 &&
         std::abs(adjustments.blacks) <= 1e-6 &&
         std::abs(adjustments.tint) <= 1e-6 &&
         std::abs(adjustments.vibrance) <= 1e-6 &&
         std::abs(adjustments.saturation) <= 1e-6 &&
         std::abs(adjustments.hue_shift) <= 1e-6 &&
         std::abs(adjustments.saturation_shift) <= 1e-6 &&
         std::abs(adjustments.colorize_strength) <= 1e-6;
}

[[nodiscard]] ColorPixel gamut_fit_oklch(double lightness, double chroma, double hue, double alpha) {
  struct GamutFitResult final {
    double lightness = 0.0;
    double chroma = 0.0;
    ColorPixel pixel;
  };

  const auto search_chroma_at_lightness =
      [&](double candidate_lightness, double candidate_chroma, int iterations) -> GamutFitResult {
    candidate_lightness = clamp_unit(candidate_lightness);
    candidate_chroma = std::max(0.0, candidate_chroma);

    OklabColor candidate_lab{
        .lightness = candidate_lightness,
        .a = candidate_chroma * std::cos(hue),
        .b = candidate_chroma * std::sin(hue),
    };
    auto candidate = oklab_to_linear_rgb(candidate_lab, alpha);
    if (in_gamut(candidate)) {
      return GamutFitResult{
          .lightness = candidate_lightness,
          .chroma = candidate_chroma,
          .pixel = clamp_rgb(candidate),
      };
    }

    auto low = 0.0;
    auto high = candidate_chroma;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      const auto mid = (low + high) * 0.5;
      candidate_lab = OklabColor{
          .lightness = candidate_lightness,
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
        .lightness = candidate_lightness,
        .a = low * std::cos(hue),
        .b = low * std::sin(hue),
    };
    return GamutFitResult{
        .lightness = candidate_lightness,
        .chroma = low,
        .pixel = clamp_rgb(oklab_to_linear_rgb(candidate_lab, alpha)),
    };
  };

  lightness = clamp_unit(lightness);
  chroma = std::max(0.0, chroma);
  hue = wrap_radians(hue);

  auto best_fit = search_chroma_at_lightness(lightness, chroma, 16);
  if (best_fit.chroma > std::max(1e-4, chroma * 0.02)) {
    return best_fit.pixel;
  }

  const auto target_lightness = 0.5;
  for (int iteration = 1; iteration <= 12; ++iteration) {
    const auto t = static_cast<double>(iteration) / 12.0;
    const auto candidate_lightness = std::lerp(lightness, target_lightness, t);
    const auto candidate_fit = search_chroma_at_lightness(candidate_lightness, chroma, 16);
    if (candidate_fit.chroma > best_fit.chroma + 1e-6 ||
        (std::abs(candidate_fit.chroma - best_fit.chroma) <= 1e-6 &&
         std::abs(candidate_fit.lightness - lightness) < std::abs(best_fit.lightness - lightness))) {
      best_fit = candidate_fit;
    }
  }

  return best_fit.pixel;
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

double apply_perceptual_point_curve(double value, const CurveEvaluationData& curve) {
  return srgb_to_linear(evaluate_curve_data(linear_to_srgb(value), curve));
}

std::vector<float> build_perceptual_curve_lut(std::span<const CurvePoint> points, int sample_count) {
  if (sample_count <= 0 || !has_point_curve(points)) {
    return {};
  }

  const auto curve = build_curve_data(points);
  std::vector<float> lut(static_cast<std::size_t>(sample_count), 0.0F);
  for (int index = 0; index < sample_count; ++index) {
    const auto input = sample_count > 1
                           ? static_cast<double>(index) / static_cast<double>(sample_count - 1)
                           : 0.0;
    lut[static_cast<std::size_t>(index)] = static_cast<float>(apply_perceptual_point_curve(input, curve));
  }
  return lut;
}

double evaluate_tone_curve(double value, const ColorAdjustmentState& state) {
  if (has_point_curve(state.master_curve_points)) {
    return evaluate_curve_data(value, build_curve_data(state.master_curve_points));
  }

  return evaluate_curve_data(value, build_parametric_curve_data(state));
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

namespace {

ColorPixel apply_global_color_pipeline(
    const ColorPixel& input,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    const ColorAdjustmentState& state,
    const CurveEvaluationData& parametric_curve,
    const CurveEvaluationData& master_curve,
    const CurveEvaluationData& red_curve,
    const CurveEvaluationData& green_curve,
    const CurveEvaluationData& blue_curve,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double shoulder_threshold) {
  auto pixel = input;
  pixel.red = static_cast<float>(std::max(0.0, static_cast<double>(pixel.red) * exposure_gain * white_balance[0]));
  pixel.green = static_cast<float>(std::max(0.0, static_cast<double>(pixel.green) * exposure_gain * white_balance[1]));
  pixel.blue = static_cast<float>(std::max(0.0, static_cast<double>(pixel.blue) * exposure_gain * white_balance[2]));

  auto target_luminance = apply_global_tone(luminance_for_pixel(pixel), state);
  if (shoulder_threshold < 1.0) {
    target_luminance = apply_highlight_shoulder(target_luminance, shoulder_threshold);
  }
  const auto curve_target = has_point_curve(state.master_curve_points)
                                ? clamp_unit(target_luminance)
                                : evaluate_curve_data(clamp_unit(target_luminance), parametric_curve);
  pixel = scale_pixel_to_luminance(pixel, curve_target);

  if (master_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_perceptual_point_curve(pixel.red, master_curve));
    pixel.green = static_cast<float>(apply_perceptual_point_curve(pixel.green, master_curve));
    pixel.blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, master_curve));
  }
  if (red_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_perceptual_point_curve(pixel.red, red_curve));
  }
  if (green_curve.points.size() >= 2) {
    pixel.green = static_cast<float>(apply_perceptual_point_curve(pixel.green, green_curve));
  }
  if (blue_curve.points.size() >= 2) {
    pixel.blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, blue_curve));
  }

  if (!has_post_tone_color_adjustments(state, saturation_strength, vibrance_strength)) {
    return pixel;
  }

  auto oklab = linear_rgb_to_oklab(pixel);
  auto lightness = clamp_unit(oklab.lightness);
  auto chroma = std::hypot(oklab.a, oklab.b);
  auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

  if (chroma > 1e-6) {
    const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
    const auto low_chroma_weight = 1.0 - smoothstep(0.03, 0.18, chroma);
    const auto mid_lightness_weight = 1.0 - smoothstep(0.16, 0.50, std::abs(lightness - 0.55));
    const auto skin_protection =
        1.0 - 0.30 * circular_weight(normalized_hue, 0.08, 0.15) * smoothstep(0.02, 0.12, chroma);
    chroma *= std::max(0.0, 1.0 + vibrance_strength * low_chroma_weight * mid_lightness_weight *
                                       skin_protection * 1.30);
    chroma *= std::max(0.0, 1.0 + saturation_strength);
    chroma *= std::max(0.0, 1.0 + std::max(0.0, dehaze_strength_for_saturation) * 0.10);

    const auto use_legacy_mixer = !has_per_channel_color_mixer_adjustments(state);
    for (int ch = 0; ch < kColorMixerChannelCount; ++ch) {
      auto mixer = state.color_mixer_channels[static_cast<std::size_t>(ch)];
      if (use_legacy_mixer &&
          state.color_mixer_channel == kColorMixerChannelNames[static_cast<std::size_t>(ch)]) {
        mixer.hue = state.hue_adjustment;
        mixer.saturation = state.saturation_adjustment;
        mixer.luminance = state.luminance_adjustment;
      }
      const auto mixer_weight = channel_weight_for_selection(
          normalized_hue, kColorMixerChannelNames[static_cast<std::size_t>(ch)]);
      if (mixer_weight > 0.0) {
        hue = wrap_radians(hue + mixer_weight * (clamp_signed_percent(mixer.hue) / 100.0) *
                                     0.14 * 2.0 * kPi);
        chroma *= std::max(
            0.0,
            1.0 + mixer_weight * (clamp_signed_percent(mixer.saturation) / 100.0) * 0.80);
        lightness = clamp_unit(
            lightness + mixer_weight * (clamp_signed_percent(mixer.luminance) / 100.0) * 0.18);
      }
    }
  }

  // 3-way color grading: shadows / midtones / highlights.
  // Each range contributes an a,b shift (hue direction × saturation intensity)
  // and a lightness offset, weighted by where this pixel falls tonally.
  {
    // Reconstruct cartesian a,b from current polar (mixer may have changed them)
    auto grade_a = chroma * std::cos(hue);
    auto grade_b = chroma * std::sin(hue);

    const auto apply_grade = [&](double grade_hue, double grade_sat, double grade_lum, double weight) {
      if (weight < 1e-9) {
        return;
      }
      const auto sat_norm = clamp_signed_percent(grade_sat) / 100.0;
      const auto hue_rad = clamp_signed_percent(grade_hue) / 100.0 * 2.0 * kPi;
      const auto lum_norm = clamp_signed_percent(grade_lum) / 100.0;
      const auto intensity = std::abs(sat_norm) * 0.18 * weight;
      if (intensity > 1e-9) {
        grade_a += intensity * std::cos(hue_rad);
        grade_b += intensity * std::sin(hue_rad);
      }
      lightness = clamp_unit(lightness + lum_norm * 0.20 * weight);
    };

    const auto shadow_weight = 1.0 - smoothstep(0.15, 0.45, lightness);
    const auto highlight_weight = smoothstep(0.55, 0.85, lightness);
    const auto midtone_weight = 1.0 - shadow_weight - highlight_weight;

    apply_grade(state.grading_shadows_hue, state.grading_shadows_saturation,
                state.grading_shadows_luminance, shadow_weight);
    apply_grade(state.grading_midtones_hue, state.grading_midtones_saturation,
                state.grading_midtones_luminance, std::max(0.0, midtone_weight));
    apply_grade(state.grading_highlights_hue, state.grading_highlights_saturation,
                state.grading_highlights_luminance, highlight_weight);

    chroma = std::hypot(grade_a, grade_b);
    hue = wrap_radians(std::atan2(grade_b, grade_a));
  }

  return gamut_fit_oklch(lightness, chroma, hue, pixel.alpha);
}

}  // namespace

ColorLut3D build_color_lut(const ColorAdjustmentState& state) {
  const auto curve_points_equal = [](std::span<const CurvePoint> left, std::span<const CurvePoint> right) {
    if (left.size() != right.size()) {
      return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
      if (std::abs(left[i].x - right[i].x) > 1e-9 || std::abs(left[i].y - right[i].y) > 1e-9) {
        return false;
      }
    }
    return true;
  };
  const auto lut_inputs_equal = [&](const ColorAdjustmentState& left, const ColorAdjustmentState& right) {
    const auto equal = [](double a, double b) { return std::abs(a - b) <= 1e-9; };
    if (!equal(left.exposure, right.exposure) ||
        !equal(left.contrast, right.contrast) ||
        !equal(left.highlights, right.highlights) ||
        !equal(left.shadows, right.shadows) ||
        !equal(left.whites, right.whites) ||
        !equal(left.blacks, right.blacks) ||
        !equal(left.temperature, right.temperature) ||
        !equal(left.tint, right.tint) ||
        !equal(left.dehaze, right.dehaze) ||
        !equal(left.vibrance, right.vibrance) ||
        !equal(left.saturation, right.saturation) ||
        !equal(left.curve_shadows, right.curve_shadows) ||
        !equal(left.curve_darks, right.curve_darks) ||
        !equal(left.curve_lights, right.curve_lights) ||
        !equal(left.curve_highlights, right.curve_highlights) ||
        left.color_mixer_channel != right.color_mixer_channel ||
        !equal(left.hue_adjustment, right.hue_adjustment) ||
        !equal(left.saturation_adjustment, right.saturation_adjustment) ||
        !equal(left.luminance_adjustment, right.luminance_adjustment) ||
        !equal(left.grading_shadows_hue, right.grading_shadows_hue) ||
        !equal(left.grading_shadows_saturation, right.grading_shadows_saturation) ||
        !equal(left.grading_shadows_luminance, right.grading_shadows_luminance) ||
        !equal(left.grading_midtones_hue, right.grading_midtones_hue) ||
        !equal(left.grading_midtones_saturation, right.grading_midtones_saturation) ||
        !equal(left.grading_midtones_luminance, right.grading_midtones_luminance) ||
        !equal(left.grading_highlights_hue, right.grading_highlights_hue) ||
        !equal(left.grading_highlights_saturation, right.grading_highlights_saturation) ||
        !equal(left.grading_highlights_luminance, right.grading_highlights_luminance) ||
        !curve_points_equal(left.master_curve_points, right.master_curve_points) ||
        !curve_points_equal(left.red_curve_points, right.red_curve_points) ||
        !curve_points_equal(left.green_curve_points, right.green_curve_points) ||
        !curve_points_equal(left.blue_curve_points, right.blue_curve_points)) {
      return false;
    }

    for (std::size_t i = 0; i < left.color_mixer_channels.size(); ++i) {
      if (!equal(left.color_mixer_channels[i].hue, right.color_mixer_channels[i].hue) ||
          !equal(left.color_mixer_channels[i].saturation, right.color_mixer_channels[i].saturation) ||
          !equal(left.color_mixer_channels[i].luminance, right.color_mixer_channels[i].luminance)) {
        return false;
      }
    }
    return true;
  };
  struct LutCache final {
    std::mutex mutex;
    bool has_state = false;
    ColorAdjustmentState state;
    ColorLut3D lut;
  };
  static LutCache cache;
  {
    std::lock_guard lock(cache.mutex);
    if (cache.has_state && lut_inputs_equal(cache.state, state)) {
      return cache.lut;
    }
  }

  ColorLut3D lut;
  const auto total = static_cast<std::size_t>(kLutSize * kLutSize * kLutSize);
  lut.table.resize(total);

  const auto exposure_gain = std::pow(2.0, state.exposure);
  const auto white_balance = white_balance_multipliers(state.temperature, state.tint);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto master_curve = build_curve_data(state.master_curve_points);
  const auto red_curve = build_curve_data(state.red_curve_points);
  const auto green_curve = build_curve_data(state.green_curve_points);
  const auto blue_curve = build_curve_data(state.blue_curve_points);
  const auto saturation_strength = clamp_signed_percent(state.saturation) / 100.0;
  const auto vibrance_strength = clamp_signed_percent(state.vibrance) / 100.0;
  const auto dehaze_strength = clamp_signed_percent(state.dehaze) / 100.0;

  // Pre-pass: find max target luminance across all LUT samples for consistent
  // highlight shoulder rolloff (matches the slow-path global approach).
  auto max_target_luminance = 0.0;
  for (int ri = 0; ri < kLutSize; ++ri) {
    for (int gi = 0; gi < kLutSize; ++gi) {
      for (int bi = 0; bi < kLutSize; ++bi) {
        const auto r = static_cast<double>(ri) / static_cast<double>(kLutSize - 1);
        const auto g = static_cast<double>(gi) / static_cast<double>(kLutSize - 1);
        const auto b = static_cast<double>(bi) / static_cast<double>(kLutSize - 1);
        const auto exposed_r = std::max(0.0, r * exposure_gain * white_balance[0]);
        const auto exposed_g = std::max(0.0, g * exposure_gain * white_balance[1]);
        const auto exposed_b = std::max(0.0, b * exposure_gain * white_balance[2]);
        const auto lum = exposed_r * 0.2126 + exposed_g * 0.7152 + exposed_b * 0.0722;
        const auto target = apply_global_tone(lum, state);
        max_target_luminance = std::max(max_target_luminance, target);
      }
    }
  }
  const auto shoulder_threshold = shoulder_threshold_for_max_luminance(max_target_luminance);

  for (int ri = 0; ri < kLutSize; ++ri) {
    for (int gi = 0; gi < kLutSize; ++gi) {
      for (int bi = 0; bi < kLutSize; ++bi) {
        const auto r = static_cast<double>(ri) / static_cast<double>(kLutSize - 1);
        const auto g = static_cast<double>(gi) / static_cast<double>(kLutSize - 1);
        const auto b = static_cast<double>(bi) / static_cast<double>(kLutSize - 1);
        const auto index = static_cast<std::size_t>(
            (ri * kLutSize + gi) * kLutSize + bi);
        lut.table[index] = apply_global_color_pipeline(
            ColorPixel{
                .red = static_cast<float>(r),
                .green = static_cast<float>(g),
                .blue = static_cast<float>(b),
                .alpha = 1.0F,
            },
            exposure_gain, white_balance, state,
            parametric_curve,
            master_curve, red_curve, green_curve, blue_curve,
            saturation_strength, vibrance_strength, dehaze_strength,
            shoulder_threshold);
      }
    }
  }

  lut.shoulder_threshold = shoulder_threshold;
  lut.valid = true;
  {
    std::lock_guard lock(cache.mutex);
    cache.state = state;
    cache.lut = lut;
    cache.has_state = true;
  }
  return lut;
}

ColorPixel apply_lut(const ColorLut3D& lut, const ColorPixel& pixel) {
  if (!lut.valid) {
    return pixel;
  }

  const auto size_minus_1 = static_cast<double>(kLutSize - 1);
  const auto rf = clamp_unit(pixel.red) * size_minus_1;
  const auto gf = clamp_unit(pixel.green) * size_minus_1;
  const auto bf = clamp_unit(pixel.blue) * size_minus_1;
  const auto ri = std::min(static_cast<int>(rf), kLutSize - 2);
  const auto gi = std::min(static_cast<int>(gf), kLutSize - 2);
  const auto bi = std::min(static_cast<int>(bf), kLutSize - 2);
  const auto fr = rf - static_cast<double>(ri);
  const auto fg = gf - static_cast<double>(gi);
  const auto fb = bf - static_cast<double>(bi);

  auto idx = [](int r, int g, int b) {
    return static_cast<std::size_t>((r * kLutSize + g) * kLutSize + b);
  };

  const auto& c000 = lut.table[idx(ri, gi, bi)];
  const auto& c001 = lut.table[idx(ri, gi, bi + 1)];
  const auto& c010 = lut.table[idx(ri, gi + 1, bi)];
  const auto& c011 = lut.table[idx(ri, gi + 1, bi + 1)];
  const auto& c100 = lut.table[idx(ri + 1, gi, bi)];
  const auto& c101 = lut.table[idx(ri + 1, gi, bi + 1)];
  const auto& c110 = lut.table[idx(ri + 1, gi + 1, bi)];
  const auto& c111 = lut.table[idx(ri + 1, gi + 1, bi + 1)];

  auto lerp3 = [&](auto ColorPixel::*channel) {
    const auto c00 = c000.*channel * (1.0 - fb) + c001.*channel * fb;
    const auto c01 = c010.*channel * (1.0 - fb) + c011.*channel * fb;
    const auto c10 = c100.*channel * (1.0 - fb) + c101.*channel * fb;
    const auto c11 = c110.*channel * (1.0 - fb) + c111.*channel * fb;
    const auto c0 = c00 * (1.0 - fg) + c01 * fg;
    const auto c1 = c10 * (1.0 - fg) + c11 * fg;
    return c0 * (1.0 - fr) + c1 * fr;
  };

  auto result = ColorPixel{
      .red = static_cast<float>(lerp3(&ColorPixel::red)),
      .green = static_cast<float>(lerp3(&ColorPixel::green)),
      .blue = static_cast<float>(lerp3(&ColorPixel::blue)),
      .alpha = pixel.alpha,
  };

  // Preserve neutrality: achromatic inputs must produce achromatic outputs.
  // Trilinear interpolation across off-diagonal LUT corners can introduce
  // tiny chroma artifacts for neutral colors.
  const auto max_channel_diff = std::max({std::abs(pixel.red - pixel.green),
                                           std::abs(pixel.green - pixel.blue),
                                           std::abs(pixel.red - pixel.blue)});
  if (max_channel_diff < 1e-9) {
    const auto avg = (result.red + result.green + result.blue) / 3.0;
    result.red = static_cast<float>(avg);
    result.green = static_cast<float>(avg);
    result.blue = static_cast<float>(avg);
  }

  return result;
}

void apply_color_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() != static_cast<std::size_t>(width * height)) {
    return;
  }

  apply_optical_corrections(pixels, width, height, state);

  const auto texture_strength = clamp_signed_percent(state.texture) / 100.0;
  const auto clarity_strength = clamp_signed_percent(state.clarity) / 100.0;
  const auto dehaze_strength = clamp_signed_percent(state.dehaze) / 100.0;
  const auto sharpen_strength = clamp_signed_percent(state.sharpening) / 100.0;
  const auto sharpen_mask_strength = clamp_signed_percent(state.sharpen_masking) / 100.0;
  const auto luma_nr_strength = clamp_signed_percent(state.luminance_nr) / 100.0;
  const auto color_nr_strength = clamp_signed_percent(state.color_nr) / 100.0;
  const auto uses_native_local_mask_sets = !state.local_mask_sets.empty();
  const auto& effective_retouch_masks =
      state.retouch_masks.empty() ? state.local_masks : state.retouch_masks;
  const auto has_spatial_tone_color =
      state.linear_mask_enabled || state.radial_mask_enabled ||
      uses_native_local_mask_sets || !state.local_masks.empty() ||
      std::abs(texture_strength) > 1e-6 || std::abs(clarity_strength) > 1e-6 ||
      std::abs(dehaze_strength) > 1e-6;
  const auto has_detail_effects =
      std::abs(sharpen_strength) > 1e-6 ||
      std::abs(luma_nr_strength) > 1e-6 || std::abs(color_nr_strength) > 1e-6;
  const auto has_local_heal = has_local_mask_heal(effective_retouch_masks);
  const auto has_local_clone = has_local_mask_clone(effective_retouch_masks);
  const auto has_local_removal = has_local_mask_removal(effective_retouch_masks);

  // Fast path: no masks or spatial tone/color effects — use precomputed 3D LUT
  if (!has_spatial_tone_color) {
    const auto lut = build_color_lut(state);

    // Precompute pipeline parameters for exact neutral-pixel computation.
    // Trilinear LUT interpolation uses off-diagonal grid corners that introduce
    // small errors for achromatic inputs; computing directly avoids this.
    const auto fast_exposure_gain = std::pow(2.0, state.exposure);
    const auto fast_white_balance = white_balance_multipliers(state.temperature, state.tint);
    const auto fast_parametric_curve = build_parametric_curve_data(state);
    const auto fast_master_curve = build_curve_data(state.master_curve_points);
    const auto fast_red_curve = build_curve_data(state.red_curve_points);
    const auto fast_green_curve = build_curve_data(state.green_curve_points);
    const auto fast_blue_curve = build_curve_data(state.blue_curve_points);
    const auto fast_sat = clamp_signed_percent(state.saturation) / 100.0;
    const auto fast_vib = clamp_signed_percent(state.vibrance) / 100.0;
    const auto fast_dehaze = clamp_signed_percent(state.dehaze) / 100.0;
    // Use the same global shoulder threshold that the LUT was built with.
    const auto fast_shoulder = lut.shoulder_threshold;

    for (auto& pixel : *pixels) {
      const auto max_diff = std::max({std::abs(pixel.red - pixel.green),
                                       std::abs(pixel.green - pixel.blue),
                                       std::abs(pixel.red - pixel.blue)});
      if (max_diff < 1e-9 || has_extended_range_rgb(pixel)) {
        pixel = apply_global_color_pipeline(
            pixel, fast_exposure_gain, fast_white_balance, state,
            fast_parametric_curve,
            fast_master_curve, fast_red_curve, fast_green_curve, fast_blue_curve,
            fast_sat, fast_vib, fast_dehaze, fast_shoulder);
      } else {
        pixel = apply_lut(lut, pixel);
      }
    }
  } else {

  // Slow path: masks and/or texture/clarity/dehaze require per-pixel spatial processing
  const auto exposure_gain = std::pow(2.0, state.exposure);
  const auto white_balance = white_balance_multipliers(state.temperature, state.tint);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto master_curve = build_curve_data(state.master_curve_points);
  const auto red_curve = build_curve_data(state.red_curve_points);
  const auto green_curve = build_curve_data(state.green_curve_points);
  const auto blue_curve = build_curve_data(state.blue_curve_points);
  const auto saturation_strength = clamp_signed_percent(state.saturation) / 100.0;
  const auto vibrance_strength = clamp_signed_percent(state.vibrance) / 100.0;
  const auto uses_legacy_local_mask_stack = !state.local_masks.empty();
  const auto stack_is_additive = local_masks_all_additive(state.local_masks);
  LocalAdjustmentSettings shared_stack_adjustments;
  const auto stack_shares_adjustments =
      uses_legacy_local_mask_stack &&
      local_masks_share_adjustments(state.local_masks, &shared_stack_adjustments);
  if (uses_legacy_local_mask_stack && !stack_shares_adjustments) {
    (void)primary_local_mask_adjustments(state.local_masks, &shared_stack_adjustments);
  }
  const auto uses_composite_local_mask_path =
      uses_legacy_local_mask_stack && (stack_shares_adjustments || !stack_is_additive);
  const auto gradient_radians = state.linear_mask_rotation * kPi / 180.0;
  const auto gradient_cosine = std::cos(gradient_radians);
  const auto gradient_sine = std::sin(gradient_radians);
  const auto gradient_feather = std::max(0.05, state.linear_mask_feather);
  std::vector<float> composite_local_mask_weights;
  if (uses_composite_local_mask_path) {
    composite_local_mask_weights = build_composite_local_mask_weights(
        *pixels,
        width,
        height,
        exposure_gain,
        white_balance,
        state.local_masks);
  }
  auto ran_pre_tone_on_gpu = false;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  {
    auto& metal = metal_backend();
    const auto can_run_local_mask_stack_on_gpu =
        !uses_native_local_mask_sets &&
        uses_composite_local_mask_path &&
        composite_local_mask_weights.size() == pixels->size() &&
        local_adjustments_supported_by_metal_pre_tone(shared_stack_adjustments);
    const auto can_run_pre_tone_on_gpu =
        !uses_native_local_mask_sets &&
        (!uses_legacy_local_mask_stack || can_run_local_mask_stack_on_gpu);
    ran_pre_tone_on_gpu = can_run_pre_tone_on_gpu &&
                          metal.available() &&
                          metal.apply_pre_tone_effects(
                              pixels,
                              width,
                              height,
                              state,
                              can_run_local_mask_stack_on_gpu ? &composite_local_mask_weights : nullptr,
                              can_run_local_mask_stack_on_gpu ? shared_stack_adjustments.exposure : 0.0,
                              can_run_local_mask_stack_on_gpu ? shared_stack_adjustments.temperature : 0.0);
  }
#endif

  if (!ran_pre_tone_on_gpu) {
    std::vector<double> target_luminances(static_cast<std::size_t>(width * height), 0.0);
    auto max_target_luminance = 0.0;

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        auto& pixel = (*pixels)[index];
        pixel.red = static_cast<float>(std::max(0.0, static_cast<double>(pixel.red) * exposure_gain * white_balance[0]));
        pixel.green = static_cast<float>(std::max(0.0, static_cast<double>(pixel.green) * exposure_gain * white_balance[1]));
        pixel.blue = static_cast<float>(std::max(0.0, static_cast<double>(pixel.blue) * exposure_gain * white_balance[2]));

        const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        if (uses_native_local_mask_sets) {
          for (const auto& mask_set : state.local_mask_sets) {
            if (!local_mask_set_has_adjustments(mask_set)) {
              continue;
            }
            apply_local_adjustment_mask(
                &pixel,
                local_mask_set_weight(normalized_x, normalized_y, pixel, mask_set),
                mask_set.exposure,
                mask_set.contrast,
                mask_set.highlights,
                mask_set.shadows,
                mask_set.whites,
                mask_set.blacks,
                mask_set.temperature,
                mask_set.tint,
                mask_set.vibrance,
                mask_set.saturation,
                mask_set.hue_shift,
                mask_set.saturation_shift,
                mask_set.colorize_hue,
                mask_set.colorize_strength);
          }
        } else if (uses_legacy_local_mask_stack) {
          if (uses_composite_local_mask_path) {
            apply_local_adjustment_mask(
                &pixel,
                composite_local_mask_weights.empty()
                    ? composite_local_mask_weight(normalized_x, normalized_y, pixel, state.local_masks)
                    : static_cast<double>(composite_local_mask_weights[index]),
                shared_stack_adjustments.exposure,
                shared_stack_adjustments.contrast,
                shared_stack_adjustments.highlights,
                shared_stack_adjustments.shadows,
                shared_stack_adjustments.whites,
                shared_stack_adjustments.blacks,
                shared_stack_adjustments.temperature,
                shared_stack_adjustments.tint,
                shared_stack_adjustments.vibrance,
                shared_stack_adjustments.saturation,
                shared_stack_adjustments.hue_shift,
                shared_stack_adjustments.saturation_shift,
                shared_stack_adjustments.colorize_hue,
                shared_stack_adjustments.colorize_strength);
          } else {
            for (const auto& mask : state.local_masks) {
              apply_local_adjustment_mask(
                  &pixel,
                  local_mask_weight(normalized_x, normalized_y, pixel, mask),
                  mask.exposure,
                  mask.contrast,
                  mask.highlights,
                  mask.shadows,
                  mask.whites,
                  mask.blacks,
                  mask.temperature,
                  mask.tint,
                  mask.vibrance,
                  mask.saturation,
                  mask.hue_shift,
                  mask.saturation_shift,
                  mask.colorize_hue,
                  mask.colorize_strength);
            }
          }
        } else {
          if (state.linear_mask_enabled) {
            const auto axis_coordinate =
                ((normalized_x - 0.5) * gradient_cosine + (normalized_y - 0.5) * gradient_sine) + 0.5;
            const auto mask_weight =
                clamp_unit(((state.linear_mask_position - axis_coordinate) / gradient_feather) + 0.5);
            apply_local_adjustment_mask(
                &pixel,
                mask_weight,
                state.linear_mask_exposure,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                state.linear_mask_temperature,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0);
          }
          apply_local_adjustment_mask(
              &pixel,
              radial_mask_weight(normalized_x, normalized_y, state),
              state.radial_mask_exposure,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              state.radial_mask_temperature,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0,
              0.0);
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
      const auto curve_target = has_point_curve(state.master_curve_points)
                                    ? clamp_unit(target_luminance)
                                    : evaluate_curve_data(clamp_unit(target_luminance), parametric_curve);
      (*pixels)[index] = scale_pixel_to_luminance((*pixels)[index], curve_target);
    }
  }

  if (std::abs(texture_strength) > 1e-6 || std::abs(clarity_strength) > 1e-6 ||
      std::abs(dehaze_strength) > 1e-6) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    auto& metal = metal_backend();
    if (!metal.available() ||
        !metal.apply_spatial_tone_effects(
            pixels,
            width,
            height,
            texture_strength,
            clarity_strength,
            dehaze_strength)) {
#endif
    std::vector<float> luminance_buffer(static_cast<std::size_t>(width * height), 0.0F);
    for (std::size_t index = 0; index < pixels->size(); ++index) {
      luminance_buffer[index] = static_cast<float>(luminance_for_pixel((*pixels)[index]));
    }

    const auto texture_blur = gaussian_approximate_blur(luminance_buffer, width, height, 2);
    const auto clarity_blur = gaussian_approximate_blur(luminance_buffer, width, height, 8);
    const auto haze_blur = gaussian_approximate_blur(luminance_buffer, width, height, 20);

    for (std::size_t index = 0; index < pixels->size(); ++index) {
      const auto luminance = static_cast<double>(luminance_buffer[index]);
      const auto midtone_weight = bell_weight(luminance, 0.5, 0.42);
      const auto highlight_headroom = 1.0 - smoothstep(0.74, 0.98, luminance);
      const auto texture_signal =
          static_cast<double>(luminance_buffer[index] - texture_blur[index]);
      const auto clarity_signal =
          static_cast<double>(luminance_buffer[index] - clarity_blur[index]);
      const auto haze_signal = std::clamp(
          static_cast<double>(luminance_buffer[index] - haze_blur[index]),
          -0.12,
          0.12);
      const auto texture_gain = texture_strength >= 0.0 ? 0.82 : 0.32;
      const auto clarity_gain = clarity_strength >= 0.0 ? 0.82 : 0.24;
      const auto dehaze_gain = dehaze_strength >= 0.0 ? 0.55 : 0.20;
      auto target_luminance = luminance;
      target_luminance += texture_strength * texture_signal * (0.30 + midtone_weight * 0.55) * texture_gain;
      target_luminance += clarity_strength * clarity_signal * midtone_weight * highlight_headroom * clarity_gain;
      target_luminance += dehaze_strength * haze_signal * (0.22 + midtone_weight * 0.48) * highlight_headroom * dehaze_gain;
      (*pixels)[index] = scale_pixel_to_luminance((*pixels)[index], target_luminance);
    }
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    }
#endif
  }

  auto ran_post_color_on_gpu = false;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  {
    auto& metal = metal_backend();
    ran_post_color_on_gpu = metal.available() && metal.apply_post_color_effects(pixels, width, height, state);
  }
#endif

  if (!ran_post_color_on_gpu) {
  for (auto& pixel : *pixels) {
    if (master_curve.points.size() >= 2) {
      pixel.red = static_cast<float>(apply_perceptual_point_curve(pixel.red, master_curve));
      pixel.green = static_cast<float>(apply_perceptual_point_curve(pixel.green, master_curve));
      pixel.blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, master_curve));
    }
    if (red_curve.points.size() >= 2) {
      pixel.red = static_cast<float>(apply_perceptual_point_curve(pixel.red, red_curve));
    }
    if (green_curve.points.size() >= 2) {
      pixel.green = static_cast<float>(apply_perceptual_point_curve(pixel.green, green_curve));
    }
    if (blue_curve.points.size() >= 2) {
      pixel.blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, blue_curve));
    }

    if (!has_post_tone_color_adjustments(state, saturation_strength, vibrance_strength)) {
      continue;
    }

    auto oklab = linear_rgb_to_oklab(pixel);
    auto lightness = clamp_unit(oklab.lightness);
    auto chroma = std::hypot(oklab.a, oklab.b);
    auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

    if (chroma > 1e-6) {
      const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
      const auto low_chroma_weight = 1.0 - smoothstep(0.03, 0.18, chroma);
      const auto mid_lightness_weight = 1.0 - smoothstep(0.16, 0.50, std::abs(lightness - 0.55));
      const auto skin_protection =
          1.0 - 0.30 * circular_weight(normalized_hue, 0.08, 0.15) * smoothstep(0.02, 0.12, chroma);
      chroma *= std::max(0.0, 1.0 + vibrance_strength * low_chroma_weight * mid_lightness_weight *
                                         skin_protection * 1.30);
      chroma *= std::max(0.0, 1.0 + saturation_strength);
      chroma *= std::max(0.0, 1.0 + std::max(0.0, dehaze_strength) * 0.10);

      const auto use_legacy_mixer = !has_per_channel_color_mixer_adjustments(state);
      for (int ch = 0; ch < kColorMixerChannelCount; ++ch) {
        auto mixer = state.color_mixer_channels[static_cast<std::size_t>(ch)];
        if (use_legacy_mixer &&
            state.color_mixer_channel == kColorMixerChannelNames[static_cast<std::size_t>(ch)]) {
          mixer.hue = state.hue_adjustment;
          mixer.saturation = state.saturation_adjustment;
          mixer.luminance = state.luminance_adjustment;
        }
        const auto mixer_weight = channel_weight_for_selection(
            normalized_hue, kColorMixerChannelNames[static_cast<std::size_t>(ch)]);
        if (mixer_weight > 0.0) {
          hue = wrap_radians(hue + mixer_weight * (clamp_signed_percent(mixer.hue) / 100.0) *
                                       0.14 * 2.0 * kPi);
          chroma *= std::max(
              0.0,
              1.0 + mixer_weight * (clamp_signed_percent(mixer.saturation) / 100.0) * 0.80);
          lightness = clamp_unit(
              lightness + mixer_weight * (clamp_signed_percent(mixer.luminance) / 100.0) * 0.18);
        }
      }
    }

    // 3-way color grading (slow path)
    {
      auto grade_a = chroma * std::cos(hue);
      auto grade_b = chroma * std::sin(hue);

      const auto apply_grade = [&](double grade_hue, double grade_sat, double grade_lum, double weight) {
        if (weight < 1e-9) {
          return;
        }
        const auto sat_norm = clamp_signed_percent(grade_sat) / 100.0;
        const auto hue_rad = clamp_signed_percent(grade_hue) / 100.0 * 2.0 * kPi;
        const auto lum_norm = clamp_signed_percent(grade_lum) / 100.0;
        const auto intensity = std::abs(sat_norm) * 0.18 * weight;
        if (intensity > 1e-9) {
          grade_a += intensity * std::cos(hue_rad);
          grade_b += intensity * std::sin(hue_rad);
        }
        lightness = clamp_unit(lightness + lum_norm * 0.20 * weight);
      };

      const auto shadow_weight = 1.0 - smoothstep(0.15, 0.45, lightness);
      const auto highlight_weight = smoothstep(0.55, 0.85, lightness);
      const auto midtone_weight = 1.0 - shadow_weight - highlight_weight;

      apply_grade(state.grading_shadows_hue, state.grading_shadows_saturation,
                  state.grading_shadows_luminance, shadow_weight);
      apply_grade(state.grading_midtones_hue, state.grading_midtones_saturation,
                  state.grading_midtones_luminance, std::max(0.0, midtone_weight));
      apply_grade(state.grading_highlights_hue, state.grading_highlights_saturation,
                  state.grading_highlights_luminance, highlight_weight);

      chroma = std::hypot(grade_a, grade_b);
      hue = wrap_radians(std::atan2(grade_b, grade_a));
    }

    pixel = gamut_fit_oklch(lightness, chroma, hue, pixel.alpha);
  }
  }
  }  // end slow path (else)

  if (has_local_clone) {
    apply_local_clone(pixels, width, height, effective_retouch_masks);
  }

  if (has_local_heal) {
    std::vector<double> heal_mask(static_cast<std::size_t>(width * height), 0.0);
    auto has_nonzero_heal = false;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        heal_mask[index] = composite_local_removal_weight(
            normalized_x, normalized_y, (*pixels)[index], effective_retouch_masks, "heal");
        has_nonzero_heal = has_nonzero_heal || heal_mask[index] > 1e-4;
      }
    }
    if (has_nonzero_heal) {
      apply_local_removal(pixels, width, height, heal_mask, 0.55, removal_quality);
    }
  }

  if (has_local_removal) {
    std::vector<double> removal_mask(static_cast<std::size_t>(width * height), 0.0);
    auto has_nonzero_removal = false;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        removal_mask[index] = composite_local_removal_weight(
            normalized_x, normalized_y, (*pixels)[index], effective_retouch_masks, "");
        has_nonzero_removal = has_nonzero_removal || removal_mask[index] > 1e-4;
      }
    }
    if (has_nonzero_removal) {
      apply_local_removal(pixels, width, height, removal_mask, 0.0, removal_quality);
    }
  }

  // ── Detail pass: sharpening and noise reduction in OKLab space ──
  // Runs after both the fast (LUT) and slow (per-pixel) tone/color paths.
  if (has_detail_effects) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    auto& metal = metal_backend();
    if (metal.available() &&
        metal.apply_detail_effects(
            pixels,
            width,
            height,
            sharpen_strength,
            sharpen_mask_strength,
            luma_nr_strength,
            color_nr_strength)) {
      return;
    }
#endif

    // Convert to OKLab buffers for perceptual processing
    const auto pixel_count = static_cast<std::size_t>(width * height);
    std::vector<float> lightness_buf(pixel_count);
    std::vector<float> chroma_a_buf(pixel_count);
    std::vector<float> chroma_b_buf(pixel_count);

    for (std::size_t i = 0; i < pixel_count; ++i) {
      const auto ok = linear_rgb_to_oklab((*pixels)[i]);
      lightness_buf[i] = static_cast<float>(ok.lightness);
      chroma_a_buf[i] = static_cast<float>(ok.a);
      chroma_b_buf[i] = static_cast<float>(ok.b);
    }

    // Sharpening: unsharp mask on lightness with edge-aware masking.
    // Uses a small radius (1px) for fine detail enhancement. The masking
    // parameter protects low-contrast (smooth) areas from being sharpened.
    if (std::abs(sharpen_strength) > 1e-6) {
      const auto sharp_blur = gaussian_approximate_blur(lightness_buf, width, height, 1);
      // Build an edge map from a wider blur for masking
      const auto edge_blur = gaussian_approximate_blur(lightness_buf, width, height, 3);

      for (std::size_t i = 0; i < pixel_count; ++i) {
        const auto detail_signal = lightness_buf[i] - sharp_blur[i];
        // Edge strength: absolute local contrast used for masking
        const auto edge_strength = std::abs(lightness_buf[i] - edge_blur[i]);
        // Masking threshold: higher masking → only sharp edges get sharpened
        const auto mask_gate = sharpen_mask_strength > 1e-6
                                   ? smoothstep(0.0015, 0.012 + 0.06 * sharpen_mask_strength,
                                                static_cast<double>(edge_strength))
                                   : 1.0;
        lightness_buf[i] += static_cast<float>(
            static_cast<double>(detail_signal) * sharpen_strength * 1.9 * mask_gate);
      }
    }

    // Luminance noise reduction: edge-preserving blur on lightness.
    // Uses a box blur at radius 2 and blends toward it proportional to
    // strength, but preserves edges by reducing blend where local contrast
    // is high.
    if (std::abs(luma_nr_strength) > 1e-6) {
      const auto nr_blur = gaussian_approximate_blur(lightness_buf, width, height, 2);
      const auto nr_edge_blur = gaussian_approximate_blur(lightness_buf, width, height, 5);

      for (std::size_t i = 0; i < pixel_count; ++i) {
        const auto edge = std::abs(lightness_buf[i] - nr_edge_blur[i]);
        const auto edge_preserve = 1.0 - smoothstep(0.005, 0.04, static_cast<double>(edge));
        const auto blend = luma_nr_strength * 0.85 * edge_preserve;
        lightness_buf[i] = static_cast<float>(
            static_cast<double>(lightness_buf[i]) * (1.0 - blend) +
            static_cast<double>(nr_blur[i]) * blend);
      }
    }

    // Color noise reduction: edge-preserving blur on chrominance (a, b).
    // Chrominance noise is typically stronger than luminance noise, so
    // a wider radius and stronger blend are used.
    if (std::abs(color_nr_strength) > 1e-6) {
      const auto a_blur = gaussian_approximate_blur(chroma_a_buf, width, height, 3);
      const auto b_blur = gaussian_approximate_blur(chroma_b_buf, width, height, 3);

      for (std::size_t i = 0; i < pixel_count; ++i) {
        const auto blend = color_nr_strength * 0.90;
        chroma_a_buf[i] = static_cast<float>(
            static_cast<double>(chroma_a_buf[i]) * (1.0 - blend) +
            static_cast<double>(a_blur[i]) * blend);
        chroma_b_buf[i] = static_cast<float>(
            static_cast<double>(chroma_b_buf[i]) * (1.0 - blend) +
            static_cast<double>(b_blur[i]) * blend);
      }
    }

    // Convert back from OKLab to linear RGB
    for (std::size_t i = 0; i < pixel_count; ++i) {
      const auto L = clamp_unit(static_cast<double>(lightness_buf[i]));
      const auto a = static_cast<double>(chroma_a_buf[i]);
      const auto b = static_cast<double>(chroma_b_buf[i]);
      const auto c = std::hypot(a, b);
      const auto h = wrap_radians(std::atan2(b, a));
      (*pixels)[i] = gamut_fit_oklch(L, c, h, (*pixels)[i].alpha);
    }
  }
}

}  // namespace photo_editor::pipeline
