#include "smart_selection_polygon_geometry.h"

#include <algorithm>
#include <cmath>

#include <QVariantMap>

namespace photo_editor::desktop {
namespace {

bool point_on_segment(
    const QPointF& start,
    const QPointF& end,
    const QPointF& point) {
  constexpr double kEpsilon = 1e-10;
  if (std::abs(polygon_cross(start, end, point)) > kEpsilon) {
    return false;
  }
  return point.x() >= std::min(start.x(), end.x()) - kEpsilon &&
         point.x() <= std::max(start.x(), end.x()) + kEpsilon &&
         point.y() >= std::min(start.y(), end.y()) - kEpsilon &&
         point.y() <= std::max(start.y(), end.y()) + kEpsilon;
}

int segment_orientation(
    const QPointF& start,
    const QPointF& end,
    const QPointF& point) {
  constexpr double kEpsilon = 1e-10;
  const auto cross = polygon_cross(start, end, point);
  if (std::abs(cross) <= kEpsilon) {
    return 0;
  }
  return cross > 0.0 ? 1 : -1;
}

bool segments_intersect(
    const QPointF& first_start,
    const QPointF& first_end,
    const QPointF& second_start,
    const QPointF& second_end) {
  const auto first_second_start =
      segment_orientation(first_start, first_end, second_start);
  const auto first_second_end =
      segment_orientation(first_start, first_end, second_end);
  const auto second_first_start =
      segment_orientation(second_start, second_end, first_start);
  const auto second_first_end =
      segment_orientation(second_start, second_end, first_end);
  if (first_second_start != first_second_end &&
      second_first_start != second_first_end) {
    return true;
  }
  return (first_second_start == 0 &&
          point_on_segment(first_start, first_end, second_start)) ||
         (first_second_end == 0 &&
          point_on_segment(first_start, first_end, second_end)) ||
         (second_first_start == 0 &&
          point_on_segment(second_start, second_end, first_start)) ||
         (second_first_end == 0 &&
          point_on_segment(second_start, second_end, first_end));
}

}  // namespace

QVector<int> single_smart_select_candidate_instance_ids(
    const QVector<int>& candidate_instance_ids,
    int candidate_index) {
  if (candidate_instance_ids.isEmpty()) {
    return {};
  }
  const auto bounded_index = std::clamp(
      candidate_index, 0, static_cast<int>(candidate_instance_ids.size()) - 1);
  return {candidate_instance_ids.at(bounded_index)};
}

double polygon_cross(
    const QPointF& origin,
    const QPointF& left,
    const QPointF& right) {
  return (left.x() - origin.x()) * (right.y() - origin.y()) -
         (left.y() - origin.y()) * (right.x() - origin.x());
}

QVector<QPointF> normalized_polygon_points(const QVariantList& points) {
  QVector<QPointF> normalized_points;
  normalized_points.reserve(points.size());
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (!point.contains(QStringLiteral("x")) ||
        !point.contains(QStringLiteral("y"))) {
      continue;
    }
    normalized_points.append(QPointF(
        std::clamp(point.value(QStringLiteral("x")).toDouble(), 0.0, 1.0),
        std::clamp(point.value(QStringLiteral("y")).toDouble(), 0.0, 1.0)));
  }
  return normalized_points;
}

bool polygon_points_self_intersect(const QVariantList& points) {
  const auto normalized_points = normalized_polygon_points(points);
  const auto point_count = normalized_points.size();
  if (point_count < 4) {
    return false;
  }
  for (int first_index = 0; first_index < point_count; ++first_index) {
    const auto first_next = (first_index + 1) % point_count;
    for (int second_index = first_index + 1;
         second_index < point_count;
         ++second_index) {
      const auto second_next = (second_index + 1) % point_count;
      const auto adjacent_edges = first_index == second_index ||
                                  first_next == second_index ||
                                  second_next == first_index;
      if (!adjacent_edges &&
          segments_intersect(
              normalized_points.at(first_index),
              normalized_points.at(first_next),
              normalized_points.at(second_index),
              normalized_points.at(second_next))) {
        return true;
      }
    }
  }
  return false;
}

QVariantList variant_list_from_polygon_points(
    const QVector<QPointF>& points) {
  QVariantList point_list;
  point_list.reserve(points.size());
  for (const auto& point : points) {
    QVariantMap point_map;
    point_map.insert(QStringLiteral("x"), std::clamp(point.x(), 0.0, 1.0));
    point_map.insert(QStringLiteral("y"), std::clamp(point.y(), 0.0, 1.0));
    point_list.append(point_map);
  }
  return point_list;
}

double distance_to_segment(
    const QPointF& point,
    const QPointF& start,
    const QPointF& end) {
  const auto segment_x = end.x() - start.x();
  const auto segment_y = end.y() - start.y();
  const auto segment_length_squared =
      segment_x * segment_x + segment_y * segment_y;
  if (segment_length_squared <= 1e-12) {
    return std::hypot(point.x() - start.x(), point.y() - start.y());
  }
  const auto t = std::clamp(
      ((point.x() - start.x()) * segment_x +
       (point.y() - start.y()) * segment_y) /
          segment_length_squared,
      0.0,
      1.0);
  const auto projected =
      QPointF(start.x() + segment_x * t, start.y() + segment_y * t);
  return std::hypot(point.x() - projected.x(), point.y() - projected.y());
}

QVector<QPointF> simplified_closed_polygon_points(
    const QVector<QPointF>& points,
    double tolerance) {
  const auto point_count = points.size();
  if (point_count <= 3) {
    return points;
  }
  const auto clamped_tolerance = std::clamp(tolerance, 0.001, 0.08);
  QVector<QPointF> simplified;
  simplified.reserve(point_count);
  for (int index = 0; index < point_count; ++index) {
    const auto previous = points.at((index + point_count - 1) % point_count);
    const auto current = points.at(index);
    const auto next = points.at((index + 1) % point_count);
    if (distance_to_segment(current, previous, next) > clamped_tolerance) {
      simplified.append(current);
    }
  }
  return simplified.size() >= 3 ? simplified : points;
}

QVector<QPointF> smoothed_closed_polygon_points(
    const QVector<QPointF>& points) {
  const auto point_count = points.size();
  if (point_count < 3) {
    return points;
  }
  QVector<QPointF> smoothed;
  smoothed.reserve(point_count * 2);
  for (int index = 0; index < point_count; ++index) {
    const auto current = points.at(index);
    const auto next = points.at((index + 1) % point_count);
    smoothed.append(QPointF(
        std::clamp(current.x() * 0.75 + next.x() * 0.25, 0.0, 1.0),
        std::clamp(current.y() * 0.75 + next.y() * 0.25, 0.0, 1.0)));
    smoothed.append(QPointF(
        std::clamp(current.x() * 0.25 + next.x() * 0.75, 0.0, 1.0),
        std::clamp(current.y() * 0.25 + next.y() * 0.75, 0.0, 1.0)));
  }
  return smoothed;
}

}  // namespace photo_editor::desktop
