#pragma once

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {

// Evaluate a circular brush edge in raster pixels. The transition gets a
// one-pixel antialias band when the requested feather is narrower than one
// raster pixel, so hardness 1 stays hard without producing a jagged cutoff.
[[nodiscard]] inline double circular_brush_coverage_pixels(
    double distance_pixels,
    double radius_pixels,
    double inner_scale) {
  if (!std::isfinite(distance_pixels) || !std::isfinite(radius_pixels) ||
      !std::isfinite(inner_scale) || radius_pixels <= 0.0) {
    return 0.0;
  }

  const auto radius = radius_pixels;
  const auto inner_radius = std::clamp(inner_scale, 0.0, 1.0) * radius;
  auto lower_edge = inner_radius;
  auto upper_edge = radius;
  if (radius - inner_radius < 1.0) {
    lower_edge -= 0.5;
    upper_edge += 0.5;
  }
  if (distance_pixels <= lower_edge) {
    return 1.0;
  }
  if (distance_pixels >= upper_edge) {
    return 0.0;
  }

  const auto t = std::clamp(
      (distance_pixels - lower_edge) / std::max(1e-9, upper_edge - lower_edge),
      0.0,
      1.0);
  return 1.0 - t * t * (3.0 - 2.0 * t);
}

}  // namespace photo_editor::pipeline
