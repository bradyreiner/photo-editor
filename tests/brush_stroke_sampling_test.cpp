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

  auto aspect_start = point(0.1, 0.2);
  aspect_start.size = 0.06;
  const auto dense_aspect = sampled_brush_points_for_segment(
      &aspect_start, 0.9, 0.2, 0.06, 0.8, 0.0, 0.0, 0.0,
      0.1, 0.0, false, 1.0, 0.0, 0.0, 2.0);
  const auto sparse_aspect = sampled_brush_points_for_segment(
      &aspect_start, 0.9, 0.2, 0.06, 0.8, 0.0, 0.0, 0.0,
      2.0, 0.0, false, 1.0, 0.0, 0.0, 2.0);
  if (dense_aspect.size() <= sparse_aspect.size() ||
      dense_aspect.isEmpty() || sparse_aspect.isEmpty() ||
      !close(dense_aspect.constLast().x, 0.9) ||
      !close(dense_aspect.constLast().y, 0.2) ||
      !close(sparse_aspect.constLast().x, 0.9) ||
      !close(sparse_aspect.constLast().y, 0.2)) {
    return fail("aspect-aware brush spacing should vary dab density and retain the cursor endpoint");
  }
  auto smoothed_aspect_start = point(0.1, 0.4);
  smoothed_aspect_start.size = 0.06;
  const auto smoothed_aspect = sampled_brush_points_for_segment(
      &smoothed_aspect_start, 0.9, 0.4, 0.06, 0.8, 0.0, 0.0, 0.0,
      2.0, 1.0, false, 1.0, 0.0, 0.0, 2.0);
  const auto damped_endpoint_x = std::lerp(0.9, 0.1, 0.85);
  const auto damped_path_distance = (damped_endpoint_x - smoothed_aspect_start.x) * 2.0;
  const auto damped_path_steps = static_cast<int>(std::ceil(damped_path_distance / 0.05));
  if (damped_path_steps <= 0 || smoothed_aspect.size() <= damped_path_steps ||
      !close(smoothed_aspect.at(damped_path_steps - 1).x, damped_endpoint_x) ||
      !close(smoothed_aspect.constLast().x, 0.9) ||
      !close(smoothed_aspect.constLast().y, 0.4)) {
    return fail("aspect-aware smoothing should reach its damped point before the exact cursor endpoint");
  }
  auto previous_smoothed_point = smoothed_aspect_start;
  for (const auto& sampled : smoothed_aspect) {
    if (std::hypot((sampled.x - previous_smoothed_point.x) * 2.0,
                   sampled.y - previous_smoothed_point.y) > 0.0500001) {
      return fail("aspect-aware smoothing should densely sample the cursor tail after the damped point");
    }
    previous_smoothed_point = sampled;
  }
  auto pressure_start = point(0.2, 0.2);
  pressure_start.size = 0.01;
  pressure_start.pressure = 0.2;
  const auto pressure_transition = sampled_brush_points_for_segment(
      &pressure_start, 0.8, 0.7, 0.06, 0.7, 1.0, 1.0, 1.0,
      0.75, 1.0, false, 1.0, 0.0, 0.0, 2.0);
  auto previous_pressure_point = pressure_start;
  for (const auto& sampled : pressure_transition) {
    if (std::hypot((sampled.x - previous_pressure_point.x) * 2.0,
                   sampled.y - previous_pressure_point.y) > 0.0011) {
      return fail("pressure-driven tip growth should keep the aspect-aware cursor tail dense");
    }
    previous_pressure_point = sampled;
  }
  if (pressure_transition.isEmpty() ||
      !close(pressure_transition.constLast().x, 0.8) ||
      !close(pressure_transition.constLast().y, 0.7)) {
    return fail("pressure-driven aspect-aware sampling should retain the cursor endpoint");
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
      resampled_committed_brush_stroke({path.at(0), path.at(1)}, 0.5).size() != 3) {
    return fail("committed strokes must be resampled across segment boundaries and retain endpoints");
  }

  const auto sample_event_path = [](const QVector<BrushPreviewPoint>& events, double image_aspect) {
    QVector<BrushPreviewPoint> live_points;
    if (events.isEmpty()) return live_points;
    live_points += sampled_brush_points_for_segment(
        nullptr, events.constFirst().x, events.constFirst().y,
        0.06, 0.72, 0.0, 0.0, 0.0, 0.5, 0.0, false,
        1.0, 0.0, 0.0, image_aspect);
    for (auto index = 1; index < events.size(); ++index) {
      const auto segment = sampled_brush_points_for_segment(
          &live_points.constLast(), events.at(index).x, events.at(index).y,
          0.06, 0.72, 0.0, 0.0, 0.0, 0.5, 0.0, false,
          1.0, 0.0, 0.0, image_aspect);
      live_points += segment;
    }
    return live_points;
  };
  const QVector<BrushPreviewPoint> coarse_events{
      point(0.10, 0.62), point(0.90, 0.62)};
  const QVector<BrushPreviewPoint> dense_events{
      point(0.10, 0.62), point(0.30, 0.62), point(0.50, 0.62),
      point(0.70, 0.62), point(0.90, 0.62)};
  for (const auto image_aspect : {0.5, 1.0, 2.0}) {
    const auto coarse_live = sample_event_path(coarse_events, image_aspect);
    const auto dense_live = sample_event_path(dense_events, image_aspect);
    const auto coarse_canonical =
        resampled_committed_brush_stroke(coarse_live, 0.5, image_aspect);
    const auto dense_canonical =
        resampled_committed_brush_stroke(dense_live, 0.5, image_aspect);
    const auto points_match = coarse_canonical.size() == dense_canonical.size() &&
        std::equal(coarse_canonical.cbegin(), coarse_canonical.cend(),
                   dense_canonical.cbegin(), [](const auto& left, const auto& right) {
          return close(left.x, right.x) && close(left.y, right.y) &&
                 close(left.size, right.size) && close(left.flow, right.flow) &&
                 close(left.opacity, right.opacity) &&
                 close(left.auto_mask_anchor_x, right.auto_mask_anchor_x) &&
                 close(left.auto_mask_anchor_y, right.auto_mask_anchor_y) &&
                 close(left.pressure, right.pressure) && close(left.tilt_x, right.tilt_x) &&
                 close(left.tilt_y, right.tilt_y) && left.erase == right.erase;
        });
    const auto metric_max_gap = [image_aspect](const QVector<BrushPreviewPoint>& samples) {
      const auto x_scale = std::max(1.0, image_aspect);
      const auto y_scale = std::max(1.0, 1.0 / image_aspect);
      auto maximum = 0.0;
      for (auto index = 1; index < samples.size(); ++index) {
        maximum = std::max(maximum, std::hypot(
            (samples.at(index).x - samples.at(index - 1).x) * x_scale,
            (samples.at(index).y - samples.at(index - 1).y) * y_scale));
      }
      return maximum;
    };
    if (!points_match || coarse_canonical.size() <= 2 ||
        !close(coarse_canonical.constLast().x, 0.90) ||
        !close(coarse_canonical.constLast().y, 0.62) ||
        metric_max_gap(coarse_canonical) > 0.0300001 ||
        metric_max_gap(dense_canonical) > 0.0300001) {
      return fail("committed brush canonicalization should be event-rate invariant, aspect-aware, and gap-bounded");
    }
  }

  auto detailed_endpoint = point(0.12, 0.0);
  detailed_endpoint.size = 0.24;
  detailed_endpoint.flow = 0.42;
  detailed_endpoint.opacity = 0.31;
  detailed_endpoint.auto_mask_anchor_x = 0.83;
  detailed_endpoint.auto_mask_anchor_y = 0.67;
  detailed_endpoint.pressure = 0.58;
  detailed_endpoint.tilt_x = 0.21;
  detailed_endpoint.tilt_y = -0.37;
  detailed_endpoint.erase = true;
  const auto detailed_stroke = resampled_committed_brush_stroke(
      {point(0.0, 0.0), point(0.12, 0.0), detailed_endpoint}, 0.5);
  const auto& retained_endpoint = detailed_stroke.constLast();
  if (retained_endpoint.x != detailed_endpoint.x ||
      retained_endpoint.y != detailed_endpoint.y ||
      retained_endpoint.size != detailed_endpoint.size ||
      retained_endpoint.flow != detailed_endpoint.flow ||
      retained_endpoint.opacity != detailed_endpoint.opacity ||
      retained_endpoint.auto_mask_anchor_x != detailed_endpoint.auto_mask_anchor_x ||
      retained_endpoint.auto_mask_anchor_y != detailed_endpoint.auto_mask_anchor_y ||
      retained_endpoint.pressure != detailed_endpoint.pressure ||
      retained_endpoint.tilt_x != detailed_endpoint.tilt_x ||
      retained_endpoint.tilt_y != detailed_endpoint.tilt_y ||
      retained_endpoint.erase != detailed_endpoint.erase) {
    return fail("canonical brush commits must retain every final cursor attribute even at a duplicate dab position");
  }

  const auto first_released_stroke = resampled_committed_brush_stroke(
      {point(0.0, 0.0), point(0.04, 0.0)}, 0.5);
  const auto next_released_stroke = resampled_committed_brush_stroke(
      {point(0.5, 0.0), point(0.62, 0.0)}, 0.5);
  if (first_released_stroke.size() != 2 || next_released_stroke.size() != 4 ||
      !close(next_released_stroke.at(0).x, 0.5) ||
      !close(next_released_stroke.at(1).x, 0.55) ||
      !close(next_released_stroke.at(2).x, 0.60) ||
      !close(next_released_stroke.constLast().x, 0.62)) {
    return fail("each released brush stroke should reset canonical spacing phase independently");
  }
  if (!resampled_committed_brush_stroke(
           {point(0.0, 0.0), point(1.0, 0.0)}, 0.5, 1.0, []() { return true; })
           .isEmpty()) {
    return fail("cancelled brush canonicalization should not return a partial commit path");
  }

  return EXIT_SUCCESS;
}
