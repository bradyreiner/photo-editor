#include "photo_editor/pipeline/color_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kLabDelta = 6.0 / 29.0;

using Matrix3 = std::array<double, 9>;

[[nodiscard]] constexpr Matrix3 cone_response_matrix(
    ChromaticAdaptationMethod method) {
  if (method == ChromaticAdaptationMethod::Cat02) {
    return {
        0.7328, 0.4296, -0.1624,
        -0.7036, 1.6975, 0.0061,
        0.0030, 0.0136, 0.9834,
    };
  }
  return {
      0.8951, 0.2664, -0.1614,
      -0.7502, 1.7135, 0.0367,
      0.0389, -0.0685, 1.0296,
  };
}

[[nodiscard]] CieXyz multiply(const Matrix3& matrix, const CieXyz& value) {
  return {
      .x = matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
      .y = matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
      .z = matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z,
  };
}

[[nodiscard]] Matrix3 multiply(const Matrix3& left, const Matrix3& right) {
  Matrix3 result{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      for (std::size_t inner = 0; inner < 3; ++inner) {
        result[row * 3 + column] +=
            left[row * 3 + inner] * right[inner * 3 + column];
      }
    }
  }
  return result;
}

[[nodiscard]] Matrix3 invert(const Matrix3& matrix) {
  const auto determinant =
      matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
      matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
      matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
  const auto reciprocal = 1.0 / determinant;
  return {
      (matrix[4] * matrix[8] - matrix[5] * matrix[7]) * reciprocal,
      (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * reciprocal,
      (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * reciprocal,
      (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * reciprocal,
      (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * reciprocal,
      (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * reciprocal,
      (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * reciprocal,
      (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * reciprocal,
      (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * reciprocal,
  };
}

[[nodiscard]] double lab_forward(double value) {
  const auto threshold = kLabDelta * kLabDelta * kLabDelta;
  if (value > threshold) {
    return std::cbrt(value);
  }
  return value / (3.0 * kLabDelta * kLabDelta) + 4.0 / 29.0;
}

[[nodiscard]] double lab_inverse(double value) {
  if (value > kLabDelta) {
    return value * value * value;
  }
  return 3.0 * kLabDelta * kLabDelta * (value - 4.0 / 29.0);
}

[[nodiscard]] double radians_to_degrees(double radians) {
  return radians * 180.0 / kPi;
}

[[nodiscard]] double degrees_to_radians(double degrees) {
  return degrees * kPi / 180.0;
}

[[nodiscard]] double hue_degrees(double a, double b) {
  if (std::abs(a) <= 1.0e-15 && std::abs(b) <= 1.0e-15) {
    return 0.0;
  }
  auto hue = radians_to_degrees(std::atan2(b, a));
  if (hue < 0.0) {
    hue += 360.0;
  }
  return hue;
}

[[nodiscard]] double safe_reference_component(double value) {
  return std::max(value, 1.0e-15);
}

}  // namespace

CieLab cie_xyz_to_lab(const CieXyz& xyz, const CieXyz& reference_white) {
  const auto fx = lab_forward(xyz.x / safe_reference_component(reference_white.x));
  const auto fy = lab_forward(xyz.y / safe_reference_component(reference_white.y));
  const auto fz = lab_forward(xyz.z / safe_reference_component(reference_white.z));
  return CieLab{
      .lightness = 116.0 * fy - 16.0,
      .a = 500.0 * (fx - fy),
      .b = 200.0 * (fy - fz),
  };
}

CieXyz cie_lab_to_xyz(const CieLab& lab, const CieXyz& reference_white) {
  const auto fy = (lab.lightness + 16.0) / 116.0;
  const auto fx = fy + lab.a / 500.0;
  const auto fz = fy - lab.b / 200.0;
  return CieXyz{
      .x = reference_white.x * lab_inverse(fx),
      .y = reference_white.y * lab_inverse(fy),
      .z = reference_white.z * lab_inverse(fz),
  };
}

CieXyz chromatically_adapt_xyz(
    const CieXyz& xyz,
    const CieXyz& source_white,
    const CieXyz& target_white,
    ChromaticAdaptationMethod method) {
  const auto cone = cone_response_matrix(method);
  const auto source_response = multiply(cone, source_white);
  const auto target_response = multiply(cone, target_white);
  constexpr auto kMinimumResponse = 1.0e-15;
  const Matrix3 response_scale = {
      target_response.x / std::max(std::abs(source_response.x), kMinimumResponse), 0.0, 0.0,
      0.0, target_response.y / std::max(std::abs(source_response.y), kMinimumResponse), 0.0,
      0.0, 0.0, target_response.z / std::max(std::abs(source_response.z), kMinimumResponse),
  };
  return multiply(multiply(invert(cone), multiply(response_scale, cone)), xyz);
}

double delta_e_2000(
    const CieLab& left,
    const CieLab& right,
    double lightness_weight,
    double chroma_weight,
    double hue_weight) {
  const auto c1 = std::hypot(left.a, left.b);
  const auto c2 = std::hypot(right.a, right.b);
  const auto mean_c = (c1 + c2) * 0.5;
  const auto mean_c_7 = std::pow(mean_c, 7.0);
  constexpr auto kTwentyFive7 = 6103515625.0;
  const auto g = 0.5 * (1.0 - std::sqrt(mean_c_7 / (mean_c_7 + kTwentyFive7)));

  const auto a1_prime = (1.0 + g) * left.a;
  const auto a2_prime = (1.0 + g) * right.a;
  const auto c1_prime = std::hypot(a1_prime, left.b);
  const auto c2_prime = std::hypot(a2_prime, right.b);
  const auto h1_prime = hue_degrees(a1_prime, left.b);
  const auto h2_prime = hue_degrees(a2_prime, right.b);

  const auto delta_l_prime = right.lightness - left.lightness;
  const auto delta_c_prime = c2_prime - c1_prime;
  auto delta_h_degrees = 0.0;
  if (c1_prime > 1.0e-15 && c2_prime > 1.0e-15) {
    delta_h_degrees = h2_prime - h1_prime;
    if (delta_h_degrees > 180.0) {
      delta_h_degrees -= 360.0;
    } else if (delta_h_degrees < -180.0) {
      delta_h_degrees += 360.0;
    }
  }
  const auto delta_h_prime =
      2.0 * std::sqrt(c1_prime * c2_prime) *
      std::sin(degrees_to_radians(delta_h_degrees * 0.5));

  const auto mean_l_prime = (left.lightness + right.lightness) * 0.5;
  const auto mean_c_prime = (c1_prime + c2_prime) * 0.5;
  auto mean_h_prime = h1_prime + h2_prime;
  if (c1_prime <= 1.0e-15 || c2_prime <= 1.0e-15) {
    mean_h_prime = h1_prime + h2_prime;
  } else if (std::abs(h1_prime - h2_prime) <= 180.0) {
    mean_h_prime *= 0.5;
  } else if (mean_h_prime < 360.0) {
    mean_h_prime = (mean_h_prime + 360.0) * 0.5;
  } else {
    mean_h_prime = (mean_h_prime - 360.0) * 0.5;
  }

  const auto t =
      1.0 - 0.17 * std::cos(degrees_to_radians(mean_h_prime - 30.0)) +
      0.24 * std::cos(degrees_to_radians(2.0 * mean_h_prime)) +
      0.32 * std::cos(degrees_to_radians(3.0 * mean_h_prime + 6.0)) -
      0.20 * std::cos(degrees_to_radians(4.0 * mean_h_prime - 63.0));
  const auto lightness_offset = mean_l_prime - 50.0;
  const auto s_l = 1.0 +
                   0.015 * lightness_offset * lightness_offset /
                       std::sqrt(20.0 + lightness_offset * lightness_offset);
  const auto s_c = 1.0 + 0.045 * mean_c_prime;
  const auto s_h = 1.0 + 0.015 * mean_c_prime * t;
  const auto delta_theta =
      30.0 * std::exp(-std::pow((mean_h_prime - 275.0) / 25.0, 2.0));
  const auto mean_c_prime_7 = std::pow(mean_c_prime, 7.0);
  const auto r_c =
      2.0 * std::sqrt(mean_c_prime_7 / (mean_c_prime_7 + kTwentyFive7));
  const auto r_t = -r_c * std::sin(degrees_to_radians(2.0 * delta_theta));

  const auto safe_lightness_weight = std::max(lightness_weight, 1.0e-15);
  const auto safe_chroma_weight = std::max(chroma_weight, 1.0e-15);
  const auto safe_hue_weight = std::max(hue_weight, 1.0e-15);
  const auto l_term = delta_l_prime / (safe_lightness_weight * s_l);
  const auto c_term = delta_c_prime / (safe_chroma_weight * s_c);
  const auto h_term = delta_h_prime / (safe_hue_weight * s_h);
  return std::sqrt(std::max(
      0.0,
      l_term * l_term + c_term * c_term + h_term * h_term +
          r_t * c_term * h_term));
}

double st2084_encode_nits(double luminance_nits) {
  constexpr auto kM1 = 2610.0 / 16384.0;
  constexpr auto kM2 = 2523.0 / 32.0;
  constexpr auto kC1 = 3424.0 / 4096.0;
  constexpr auto kC2 = 2413.0 / 128.0;
  constexpr auto kC3 = 2392.0 / 128.0;
  const auto normalized = std::clamp(luminance_nits / 10000.0, 0.0, 1.0);
  const auto powered = std::pow(normalized, kM1);
  return std::pow((kC1 + kC2 * powered) / (1.0 + kC3 * powered), kM2);
}

IctcpColor rec2020_nits_to_ictcp(const LinearRgb& rgb_nits) {
  const auto l =
      (1688.0 * rgb_nits.red + 2146.0 * rgb_nits.green + 262.0 * rgb_nits.blue) /
      4096.0;
  const auto m =
      (683.0 * rgb_nits.red + 2951.0 * rgb_nits.green + 462.0 * rgb_nits.blue) /
      4096.0;
  const auto s =
      (99.0 * rgb_nits.red + 309.0 * rgb_nits.green + 3688.0 * rgb_nits.blue) /
      4096.0;
  const auto l_prime = st2084_encode_nits(l);
  const auto m_prime = st2084_encode_nits(m);
  const auto s_prime = st2084_encode_nits(s);
  return IctcpColor{
      .intensity = (2048.0 * l_prime + 2048.0 * m_prime) / 4096.0,
      .tritan = (6610.0 * l_prime - 13613.0 * m_prime + 7003.0 * s_prime) / 4096.0,
      .protan = (17933.0 * l_prime - 17390.0 * m_prime - 543.0 * s_prime) / 4096.0,
  };
}

double delta_e_itp(const IctcpColor& left, const IctcpColor& right) {
  const auto delta_i = left.intensity - right.intensity;
  const auto delta_t = left.tritan - right.tritan;
  const auto delta_p = left.protan - right.protan;
  return 720.0 * std::sqrt(
                     delta_i * delta_i + 0.25 * delta_t * delta_t +
                     delta_p * delta_p);
}

}  // namespace photo_editor::pipeline
