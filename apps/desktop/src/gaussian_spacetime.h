#pragma once
#include "gaussian_scene_data.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace photo_editor::desktop {
struct CompiledGaussianSpaceTime {
  GaussianPoint spatial;
  GaussianTemporalPoint temporal;
};

inline bool valid_gaussian_conditional(const GaussianPoint& point) {
  for (const auto v : point.center_opacity) if (!std::isfinite(v)) return false;
  for (const auto v : point.covariance_a) if (!std::isfinite(v)) return false;
  for (const auto v : point.covariance_b) if (!std::isfinite(v)) return false;
  for (const auto v : point.sh) if (!std::isfinite(v)) return false;
  const auto& a = point.covariance_a; const auto& b = point.covariance_b;
  const double pivot0 = a[0];
  if (pivot0 <= 0 || point.center_opacity[3] < 0 || point.center_opacity[3] > 1) return false;
  const double pivot1 = double(a[3])-double(a[1])*a[1]/pivot0;
  if (pivot1 <= 0) return false;
  const double off = double(b[0])-double(a[1])*a[2]/pivot0;
  const double pivot2 = double(b[1])-double(a[2])*a[2]/pivot0-off*off/pivot1;
  return std::isfinite(pivot2) && pivot2 > 0;
}

inline bool valid_gaussian_temporal_factor(const GaussianTemporalPoint& point) {
  const auto& f = point.covariance_factor;
  for (const auto v : f) if (!std::isfinite(v) || std::abs(v) > 1e6F) return false;
  const double determinant = double(f[0])*(double(f[5])*f[10]-double(f[9])*f[6])-
      double(f[4])*(double(f[1])*f[10]-double(f[9])*f[2])+
      double(f[8])*(double(f[1])*f[6]-double(f[5])*f[2]);
  return std::isfinite(determinant) && determinant != 0;
}

// Full covariance input, row-major in XYZ/local-seconds coordinates. This
// factors the joint density into a conditional spatial Gaussian and temporal
// marginal (Schur complement), not interpolation of independent static frames.
inline std::optional<CompiledGaussianSpaceTime> compile_gaussian_spacetime(
    const std::array<double, 4>& mean, const std::array<double, 16>& covariance,
    double peak_opacity, const std::array<float, 48>& appearance = {}) {
  for (const auto v : mean) if (!std::isfinite(v) || std::abs(v) > 1e10) return {};
  for (const auto v : covariance) if (!std::isfinite(v) || std::abs(v) > 1e12) return {};
  for (const auto v : appearance) if (!std::isfinite(v)) return {};
  if (!std::isfinite(peak_opacity) || peak_opacity < 0 || peak_opacity > 1) return {};
  // Reject non-symmetric or non-positive-definite inputs; never repair an
  // invalid trained covariance silently. Compute in double before float upload.
  std::array<double, 16> lower{};
  const std::array<size_t, 4> order{3,0,1,2};  // Condition on time first.
  for (size_t row = 0; row < 4; ++row) for (size_t col = 0; col <= row; ++col) {
    const auto a = covariance[order[row]*4+order[col]], b = covariance[order[col]*4+order[row]];
    if (std::abs(a-b) > 1e-12 * std::max({1e-30, std::abs(a), std::abs(b)})) return {};
    double residual = (a+b)*0.5;
    for (size_t k = 0; k < col; ++k) residual -= lower[row*4+k]*lower[col*4+k];
    if (row == col) {
      if (residual <= 0) return {};
      lower[row*4+col] = std::sqrt(residual);
    } else lower[row*4+col] = residual/lower[col*4+col];
  }
  CompiledGaussianSpaceTime result;
  result.spatial.sh = appearance;
  result.spatial.center_opacity[3] = static_cast<float>(peak_opacity);
  result.temporal.velocity_time[3] = static_cast<float>(mean[3]);
  const auto variance = covariance[15];
  result.temporal.normalization[0] = static_cast<float>(1/std::sqrt(variance));
  if (!std::isfinite(result.temporal.normalization[0]) || result.temporal.normalization[0] <= 0) return {};
  double conditioned[3][3]{};
  for (size_t row = 0; row < 3; ++row) {
    result.spatial.center_opacity[row] = static_cast<float>(mean[row]);
    const auto velocity = covariance[row*4+3]/variance;
    result.temporal.velocity_time[row] = static_cast<float>(velocity);
    if (!std::isfinite(result.temporal.velocity_time[row])) return {};
    for (size_t col = 0; col < 3; ++col) {
      result.temporal.covariance_factor[col*4+row] = static_cast<float>(lower[(row+1)*4+col+1]);
      for (size_t k = 1; k < 4; ++k)
        conditioned[row][col] += lower[(row+1)*4+k]*lower[(col+1)*4+k];
    }
  }
  result.spatial.covariance_a = {static_cast<float>(conditioned[0][0]), static_cast<float>(conditioned[0][1]),
      static_cast<float>(conditioned[0][2]), static_cast<float>(conditioned[1][1])};
  result.spatial.covariance_b = {static_cast<float>(conditioned[1][2]), static_cast<float>(conditioned[2][2]), 0, 0};
  // Retain the square root for rendering. The convenience float covariance
  // above need not remain SPD for extremely flat but valid Gaussians.
  if (!valid_gaussian_temporal_factor(result.temporal)) return {};
  return result;
}

inline std::array<float, 3> gaussian_spacetime_center(const GaussianPoint& point,
    const GaussianTemporalPoint& temporal, float local_seconds) {
  const float dt = local_seconds - temporal.velocity_time[3];
  return {std::fma(temporal.velocity_time[0], dt, point.center_opacity[0]),
      std::fma(temporal.velocity_time[1], dt, point.center_opacity[1]),
      std::fma(temporal.velocity_time[2], dt, point.center_opacity[2])};
}
}  // namespace photo_editor::desktop
