#include "brush_stroke_sampling.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool close(double actual, double expected) {
  return std::abs(actual - expected) < 1e-12;
}

photo_editor::desktop::BrushPreviewPoint point(double x, double y) {
  return photo_editor::desktop::BrushPreviewPoint{
      .x = x,
      .y = y,
      .size = 0.1,
      .flow = 0.8,
      .opacity = 0.7,
      .auto_mask_anchor_x = x,
      .auto_mask_anchor_y = y,
      .pressure = 1.0,
  };
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  if (!close(normalized_brush_dab_spacing(0.01, 0.05), 0.0025) ||
      !close(normalized_brush_dab_spacing(0.2, 0.1), 0.02) ||
      !close(normalized_brush_dab_spacing(0.1, 2.0), 0.05)) {
    return fail("brush spacing must apply the exact lower and upper normalized bounds");
  }

  const auto first = sampled_brush_points_for_segment(
      nullptr, 0.2, 0.3, 0.1, 0.8, 0.0, 0.0, 0.0, 0.5, 0.0, false);
  if (first.size() != 1 || !close(first.constFirst().x, 0.2) ||
      !close(first.constFirst().y, 0.3)) {
    return fail("the first brush event must publish exactly one cursor sample");
  }

  const auto origin = point(0.0, 0.0);
  const auto short_move = sampled_brush_points_for_segment(
      &origin, 0.01, 0.0, 0.1, 0.8, 0.0, 0.0, 0.0, 0.5, 0.0, false);
  if (!short_move.isEmpty()) {
    return fail("sub-threshold movement must not create redundant brush dabs");
  }
  const auto smoothed = sampled_brush_points_for_segment(
      &origin, 1.0, 0.0, 0.1, 0.8, 0.0, 0.0, 0.0, 0.5, 1.0, false);
  if (smoothed.size() != 5 || !close(smoothed.at(0).x, 0.0375) ||
      !close(smoothed.at(1).x, 0.075) || !close(smoothed.at(2).x, 0.1125) ||
      !close(smoothed.at(3).x, 0.15) || !close(smoothed.at(4).x, 1.0)) {
    return fail("smoothing must produce bounded evenly spaced dabs plus the live cursor endpoint");
  }

  auto start = point(0.0, 0.0);
  start.erase = false;
  auto end = point(1.0, 1.0);
  end.size = 0.3;
  end.flow = 0.4;
  end.opacity = 0.5;
  end.pressure = 0.2;
  end.tilt_x = 0.8;
  end.erase = true;
  const auto before = interpolated_brush_preview_point(start, end, -1.0);
  const auto middle = interpolated_brush_preview_point(start, end, 0.5);
  const auto after = interpolated_brush_preview_point(start, end, 2.0);
  if (!close(before.x, start.x) || before.erase || !close(after.x, end.x) ||
      !after.erase || !close(middle.x, 0.5) || !close(middle.size, 0.2) ||
      !close(middle.flow, 0.6) || !close(middle.opacity, 0.6) ||
      !close(middle.pressure, 0.6) || !close(middle.tilt_x, 0.4) ||
      !middle.erase) {
    return fail("brush interpolation must clamp time and interpolate every continuous field");
  }

  const QVector<BrushPreviewPoint> path{
      point(0.0, 0.0), point(0.06, 0.0), point(0.12, 0.0)};
  const auto resampled = resampled_committed_brush_stroke(path, 0.5);
  const auto repeated = resampled_committed_brush_stroke(path, 0.5);
  if (resampled.size() != 4 || !close(resampled.at(0).x, 0.0) ||
      !close(resampled.at(1).x, 0.05) || !close(resampled.at(2).x, 0.10) ||
      !close(resampled.at(3).x, 0.12) ||
      repeated.size() != resampled.size() ||
      !close(repeated.at(1).x, resampled.at(1).x) ||
      !close(repeated.at(2).x, resampled.at(2).x) ||
      resampled_committed_brush_stroke({path.at(0), path.at(1)}, 0.5).size() != 2) {
    return fail("committed strokes must be resampled across segment boundaries and retain endpoints");
  }

  return EXIT_SUCCESS;
}
