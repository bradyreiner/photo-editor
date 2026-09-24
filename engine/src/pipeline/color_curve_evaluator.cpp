#include "color_curve_evaluator.h"

#include "color_working_space_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace photo_editor::pipeline {

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

  normalized_points.front().x = 0.0;
  normalized_points.back().x = 1.0;
  data.points = std::move(normalized_points);

  if (data.points.size() == 2) {
    const auto span = std::max(1e-6, data.points.back().x - data.points.front().x);
    const auto slope = (data.points.back().y - data.points.front().y) / span;
    data.tangents = {slope, slope};
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

double apply_perceptual_point_curve(double value, const CurveEvaluationData& curve) {
  if (curve.points.size() < 2) {
    return value;
  }
  // The graph covers SDR, but the working image can contain brighter and
  // negative channels. Extend its endpoints without flattening those values.
  if (value > 1.0) {
    return value * srgb_to_linear(curve.points.back().y);
  }
  if (value < 0.0) {
    return value + srgb_to_linear(curve.points.front().y);
  }
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

double evaluate_tone_curve_target(
    double target_luminance,
    const CurveEvaluationData& curve,
    bool explicit_point_curve,
    double output_headroom) {
  const auto headroom = sanitized_scene_working_headroom(output_headroom);
  if (target_luminance > 1.0 && !explicit_point_curve) {
    return std::clamp(target_luminance, 0.0, headroom);
  }
  return explicit_point_curve
             ? std::clamp(target_luminance, 0.0, headroom)
             : evaluate_curve_data(
                   std::clamp(target_luminance, 0.0, 1.0),
                   curve);
}

}  // namespace photo_editor::pipeline
