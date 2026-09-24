#include "color_input_white_balance.h"

#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/white_balance.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photo_editor::pipeline {
namespace {

double temperature_to_kelvin(double slider_value) {
  const auto clamped = clamp_signed_percent(slider_value);
  if (clamped >= 0.0) {
    return std::lerp(5500.0, 2000.0, clamped / 100.0);
  }
  return std::lerp(5500.0, 12000.0, -clamped / 100.0);
}

std::array<double, 2> chromaticity_from_kelvin(double kelvin) {
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
  return {x, y};
}

std::array<double, 3> white_rgb_from_kelvin(double kelvin) {
  const auto chromaticity = chromaticity_from_kelvin(kelvin);
  const auto x = chromaticity[0];
  const auto y = std::max(1e-6, chromaticity[1]);
  const auto X = x / y;
  constexpr double Y = 1.0;
  const auto Z = (1.0 - x - y) / y;
  return {
      std::max(1e-6, 3.2406 * X - 1.5372 * Y - 0.4986 * Z),
      std::max(1e-6, -0.9689 * X + 1.8758 * Y + 0.0415 * Z),
      std::max(1e-6, 0.0557 * X - 0.2040 * Y + 1.0570 * Z),
  };
}

}  // namespace

Matrix3 input_white_balance_matrix(
    double temperature,
    double tint,
    const PipelineColorContext& context) {
  const auto white_xyz_from_kelvin = [&](double kelvin, double tint_value) {
    const auto chromaticity = chromaticity_from_kelvin(kelvin);
    auto xyz = xyz_from_chromaticity(Chromaticity{.x = chromaticity[0], .y = chromaticity[1]});
    const auto tint_strength = clamp_signed_percent(tint_value) / 100.0;
    if (std::abs(tint_strength) <= 1e-9) {
      return xyz;
    }
    const auto cone = chromatic_adaptation_cone_matrix(
        context.chromatic_adaptation_method);
    const auto cone_inverse = invert_matrix3(cone);
    auto response = multiply_matrix3_vector(cone, {xyz.x, xyz.y, xyz.z});
    response[0] *= std::pow(2.0, tint_strength * 0.08);
    response[1] *= std::pow(2.0, -tint_strength * 0.24);
    response[2] *= std::pow(2.0, tint_strength * 0.08);
    xyz = multiply_matrix3_xyz(
        cone_inverse,
        XyzColor{.x = response[0], .y = response[1], .z = response[2]});
    const auto normalization = std::max(1e-6, xyz.y);
    xyz.x /= normalization;
    xyz.y /= normalization;
    xyz.z /= normalization;
    return xyz;
  };

  if (std::abs(temperature) <= 1e-9 && std::abs(tint) <= 1e-9) {
    return identity_matrix3();
  }

  const auto reference_white = white_xyz_from_kelvin(5500.0, 0.0);
  const auto target_white = white_xyz_from_kelvin(temperature_to_kelvin(temperature), tint);
  auto matrix = multiply_matrix3(
      context.xyz_native_to_rgb,
      multiply_matrix3(
          chromatic_adaptation_matrix(
              reference_white,
              target_white,
              context.chromatic_adaptation_method),
          context.rgb_to_xyz_native));
  const auto transformed_neutral = apply_rgb_matrix(
      ColorPixel{.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F},
      matrix);
  const auto normalization = luminance_for_pixel(transformed_neutral, context);
  if (normalization <= 1e-6) {
    return identity_matrix3();
  }
  const auto inverse_normalization = 1.0 / normalization;
  for (auto& value : matrix.values) {
    value *= inverse_normalization;
  }
  return matrix;
}

Matrix3 white_balance_matrix(
    double temperature,
    double tint,
    const PipelineColorContext& context) {
  return input_white_balance_matrix(temperature, tint, context);
}

std::array<double, 3> preview_white_balance_multipliers(
    double temperature,
    double tint) {
  const auto reference_white = white_rgb_from_kelvin(5500.0);
  const auto target_white = white_rgb_from_kelvin(temperature_to_kelvin(temperature));
  const auto normalized_tint = clamp_signed_percent(tint) / 100.0;
  const auto red =
      (target_white[0] / reference_white[0]) * std::pow(2.0, normalized_tint * 0.08);
  const auto green =
      (target_white[1] / reference_white[1]) * std::pow(2.0, -normalized_tint * 0.24);
  const auto blue =
      (target_white[2] / reference_white[2]) * std::pow(2.0, normalized_tint * 0.08);
  const auto normalization = red * 0.2126 + green * 0.7152 + blue * 0.0722;
  if (normalization <= 1e-6) {
    return {1.0, 1.0, 1.0};
  }
  return {red / normalization, green / normalization, blue / normalization};
}

Matrix3 input_white_balance_matrix_for_kelvin(
    double kelvin,
    double tint,
    const PipelineColorContext& context) {
  const auto clamped_kelvin = std::clamp(kelvin, 2000.0, 12000.0);
  const auto slider = clamped_kelvin >= 5500.0
                          ? -((clamped_kelvin - 5500.0) / 6500.0) * 100.0
                          : ((5500.0 - clamped_kelvin) / 3500.0) * 100.0;
  return input_white_balance_matrix(slider, tint, context);
}

Matrix3 input_white_balance_matrix_between_whites(
    double source_kelvin,
    double source_tint,
    double target_kelvin,
    double target_tint,
    const PipelineColorContext& context) {
  const auto source = input_white_balance_matrix_for_kelvin(
      source_kelvin, source_tint, context);
  const auto target = input_white_balance_matrix_for_kelvin(
      target_kelvin, target_tint, context);
  return multiply_matrix3(target, invert_matrix3(source));
}

}  // namespace photo_editor::pipeline
