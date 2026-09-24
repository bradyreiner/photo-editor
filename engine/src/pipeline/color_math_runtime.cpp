#include "color_math_runtime.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace photo_editor::pipeline {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

}  // namespace

double smoothstep(double edge0, double edge1, double value) {
  if (std::abs(edge1 - edge0) <= 1e-9) {
    return value < edge0 ? 0.0 : 1.0;
  }
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

double bell_weight(double value, double center, double radius) {
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

double circular_weight(double value, double center, double radius) {
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

double circular_feathered_selection_weight(
    double value,
    double center,
    double radius,
    double softness) {
  if (radius <= 0.0) {
    return 0.0;
  }
  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0 - distance);
  if (distance >= radius) {
    return 0.0;
  }
  const auto feather = std::clamp(softness, 0.0, 1.0) * radius;
  const auto inner_radius = radius - feather;
  if (distance <= inner_radius || feather <= 1.0e-9) {
    return 1.0;
  }
  return 1.0 - smoothstep(inner_radius, radius, distance);
}

double wrap_unit(double value) {
  value -= std::floor(value);
  if (value < 0.0) {
    value += 1.0;
  }
  return value;
}

double wrap_radians(double value) {
  while (value < 0.0) {
    value += 2.0 * kPi;
  }
  while (value >= 2.0 * kPi) {
    value -= 2.0 * kPi;
  }
  return value;
}

double shortest_angular_delta(double from, double to) {
  auto delta = wrap_radians(to) - wrap_radians(from);
  if (delta > kPi) {
    delta -= 2.0 * kPi;
  } else if (delta < -kPi) {
    delta += 2.0 * kPi;
  }
  return delta;
}

}  // namespace photo_editor::pipeline
