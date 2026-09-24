#include "brush_stroke_sampling.h"

#include "brush_input_schema.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {

constexpr double kMinimumNormalizedBrushDabSpacing = 0.0025;
constexpr double kMaximumNormalizedBrushDabSpacing = 0.05;

bool brush_points_same_position(
    const BrushPreviewPoint& left, const BrushPreviewPoint& right) {
  return std::hypot(left.x - right.x, left.y - right.y) <= 1e-9;
}

}  // namespace

double normalized_brush_dab_spacing(double brush_size, double brush_spacing) {
  return std::clamp(
      std::max(0.01, brush_size) * std::clamp(brush_spacing, 0.05, 2.0),
      kMinimumNormalizedBrushDabSpacing,
      kMaximumNormalizedBrushDabSpacing);
}

QVector<BrushPreviewPoint> sampled_brush_points_for_segment(
    const BrushPreviewPoint* last_point,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double pressure_size_response,
    double pressure_flow_response,
    double pressure_opacity_response,
    double brush_spacing,
    double brush_smoothing,
    bool erase,
    double pressure,
    double tilt_x,
    double tilt_y) {
  QVector<BrushPreviewPoint> sampled_points;
  const auto cursor_point = brush_preview_point(
      normalized_x,
      normalized_y,
      brush_size,
      brush_flow,
      pressure_size_response,
      pressure_flow_response,
      pressure_opacity_response,
      pressure,
      tilt_x,
      tilt_y,
      erase);
  const auto smoothing_amount = std::clamp(brush_smoothing, 0.0, 1.0) * 0.85;
  if (last_point != nullptr) {
    normalized_x = std::lerp(normalized_x, last_point->x, smoothing_amount);
    normalized_y = std::lerp(normalized_y, last_point->y, smoothing_amount);
  }
  const auto clamped_point = brush_preview_point(
      normalized_x,
      normalized_y,
      brush_size,
      brush_flow,
      pressure_size_response,
      pressure_flow_response,
      pressure_opacity_response,
      pressure,
      tilt_x,
      tilt_y,
      erase);

  if (last_point == nullptr) {
    sampled_points.append(cursor_point);
    return sampled_points;
  }

  const auto distance =
      std::hypot(clamped_point.x - last_point->x, clamped_point.y - last_point->y);
  const auto cursor_distance =
      std::hypot(cursor_point.x - last_point->x, cursor_point.y - last_point->y);
  const auto spacing = normalized_brush_dab_spacing(clamped_point.size, brush_spacing);
  if (distance <= spacing * 0.35) {
    if (smoothing_amount > 0.0 && cursor_distance > spacing * 0.35) {
      sampled_points.append(cursor_point);
    }
    return sampled_points;
  }

  const auto steps = std::max(1, static_cast<int>(std::ceil(distance / spacing)));
  sampled_points.reserve(steps + 1);
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    sampled_points.append(brush_preview_point(
        std::lerp(last_point->x, clamped_point.x, t),
        std::lerp(last_point->y, clamped_point.y, t),
        brush_size,
        brush_flow,
        pressure_size_response,
        pressure_flow_response,
        pressure_opacity_response,
        std::lerp(last_point->pressure, clamped_point.pressure, t),
        std::lerp(last_point->tilt_x, clamped_point.tilt_x, t),
        std::lerp(last_point->tilt_y, clamped_point.tilt_y, t),
        erase));
  }
  const auto& sampled_endpoint = sampled_points.constLast();
  const auto endpoint_distance =
      std::hypot(cursor_point.x - sampled_endpoint.x, cursor_point.y - sampled_endpoint.y);
  if (smoothing_amount > 0.0 && endpoint_distance > 0.000001) {
    sampled_points.append(cursor_point);
  }
  return sampled_points;
}

BrushPreviewPoint interpolated_brush_preview_point(
    const BrushPreviewPoint& start,
    const BrushPreviewPoint& end,
    double t) {
  const auto clamped_t = std::clamp(t, 0.0, 1.0);
  return BrushPreviewPoint{
      .x = std::lerp(start.x, end.x, clamped_t),
      .y = std::lerp(start.y, end.y, clamped_t),
      .size = std::lerp(start.size, end.size, clamped_t),
      .flow = std::lerp(start.flow, end.flow, clamped_t),
      .opacity = std::lerp(start.opacity, end.opacity, clamped_t),
      .auto_mask_anchor_x =
          std::lerp(start.auto_mask_anchor_x, end.auto_mask_anchor_x, clamped_t),
      .auto_mask_anchor_y =
          std::lerp(start.auto_mask_anchor_y, end.auto_mask_anchor_y, clamped_t),
      .pressure = std::lerp(start.pressure, end.pressure, clamped_t),
      .tilt_x = std::lerp(start.tilt_x, end.tilt_x, clamped_t),
      .tilt_y = std::lerp(start.tilt_y, end.tilt_y, clamped_t),
      .erase = clamped_t < 0.5 ? start.erase : end.erase,
  };
}

QVector<BrushPreviewPoint> resampled_committed_brush_stroke(
    const QVector<BrushPreviewPoint>& stroke_points,
    double brush_spacing) {
  if (stroke_points.size() <= 2) {
    return stroke_points;
  }

  const auto spacing =
      normalized_brush_dab_spacing(stroke_points.constFirst().size, brush_spacing);
  QVector<BrushPreviewPoint> resampled_points;
  resampled_points.reserve(stroke_points.size());
  resampled_points.append(stroke_points.constFirst());

  auto distance_to_next_dab = spacing;
  for (auto index = 1; index < stroke_points.size(); ++index) {
    const auto segment_start = stroke_points.at(index - 1);
    const auto segment_end = stroke_points.at(index);
    const auto segment_length =
        std::hypot(segment_end.x - segment_start.x, segment_end.y - segment_start.y);
    if (segment_length <= 1e-9) {
      continue;
    }

    auto consumed = 0.0;
    auto remaining_segment = segment_length;
    while (remaining_segment + 1e-9 >= distance_to_next_dab) {
      consumed += distance_to_next_dab;
      const auto t = consumed / segment_length;
      resampled_points.append(
          interpolated_brush_preview_point(segment_start, segment_end, t));
      remaining_segment = segment_length - consumed;
      distance_to_next_dab = spacing;
    }
    distance_to_next_dab -= remaining_segment;
  }

  const auto final_point = stroke_points.constLast();
  if (resampled_points.isEmpty() ||
      !brush_points_same_position(resampled_points.constLast(), final_point)) {
    resampled_points.append(final_point);
  }
  return resampled_points;
}

}  // namespace photo_editor::desktop
