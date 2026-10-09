#include "mask_outline_simplification.h"

#include "smart_selection_polygon_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QMetaType>
#include <QSet>
#include <QVariantMap>

namespace photo_editor::desktop {
namespace {

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

}  // namespace

double normalized_outline_display_scale(double scale) {
  if (!std::isfinite(scale) || scale <= 0.0) {
    return 1.0;
  }
  const auto clamped = std::clamp(scale, 0.25, 32.0);
  return std::max(0.25, std::round(clamped * 4.0) / 4.0);
}

double outline_tolerance_for_sampled_raster(
    double reference_tolerance_pixels,
    const QSize& reference_size,
    const QSize& sampled_size) {
  const double tolerance = std::max(0.0, reference_tolerance_pixels);
  if (!reference_size.isValid() || !sampled_size.isValid() ||
      sampled_size.width() <= 0 || sampled_size.height() <= 0) return tolerance;
  const double magnification = std::max({1.0,
      static_cast<double>(reference_size.width()) / sampled_size.width(),
      static_cast<double>(reference_size.height()) / sampled_size.height()});
  return tolerance / magnification;
}

bool variant_value_is_outline_point(const QVariant& value) {
  if (value.metaType().id() != QMetaType::QVariantMap) {
    return false;
  }
  const auto point = value.toMap();
  return point.contains(QStringLiteral("x")) &&
         point.contains(QStringLiteral("y"));
}

QVector<QPointF> outline_path_to_pixel_points(
    const QVariantList& path,
    const QSize& raster_size) {
  QVector<QPointF> points;
  if (!raster_size.isValid() || raster_size.width() <= 0 ||
      raster_size.height() <= 0) {
    return points;
  }
  points.reserve(path.size());
  for (const auto& point_value : path) {
    const auto point = point_value.toMap();
    points.append(QPointF(
        clamp_unit(point.value(QStringLiteral("x")).toDouble()) *
            static_cast<double>(raster_size.width()),
        clamp_unit(point.value(QStringLiteral("y")).toDouble()) *
            static_cast<double>(raster_size.height())));
  }
  if (points.size() >= 2 &&
      distance_to_segment(points.first(), points.last(), points.last()) <= 1e-6) {
    points.removeLast();
  }
  return points;
}

double max_closed_outline_error_pixels(
    const QVector<QPointF>& original_points,
    const QVector<QPointF>& simplified_points) {
  if (original_points.isEmpty() || simplified_points.size() < 2) {
    return 0.0;
  }
  auto max_error = 0.0;
  for (const auto& original : original_points) {
    auto best_distance = std::numeric_limits<double>::max();
    for (auto index = 0; index < simplified_points.size(); ++index) {
      const auto& start = simplified_points.at(index);
      const auto& end = simplified_points.at((index + 1) % simplified_points.size());
      best_distance =
          std::min(best_distance, distance_to_segment(original, start, end));
    }
    max_error = std::max(max_error, best_distance);
  }
  return std::isfinite(max_error) ? max_error : 0.0;
}

QVector<QPointF> tolerance_simplified_closed_outline_points(
    const QVector<QPointF>& points,
    double tolerance_pixels) {
  if (points.size() <= 4) {
    return points;
  }
  QSet<QPair<double, double>> visited_points;
  const auto repeated_point_end =
      std::hypot(
          points.first().x() - points.last().x(),
          points.first().y() - points.last().y()) <= 1e-6
          ? points.size() - 1
          : points.size();
  for (auto index = 0; index < repeated_point_end; ++index) {
    const auto key = qMakePair(points.at(index).x(), points.at(index).y());
    if (visited_points.contains(key)) {
      return points;
    }
    visited_points.insert(key);
  }

  const auto clamped_tolerance = std::max(0.0, tolerance_pixels);
  QVector<QPointF> open_points = points;
  open_points.append(points.first());
  std::vector<bool> keep(static_cast<std::size_t>(open_points.size()), false);
  keep.front() = true;
  keep.back() = true;
  const auto simplify_range = [&](const auto& self, int first, int last) -> void {
    auto max_distance = 0.0;
    auto max_index = -1;
    for (auto index = first + 1; index < last; ++index) {
      const auto distance = distance_to_segment(
          open_points.at(index), open_points.at(first), open_points.at(last));
      if (distance > max_distance) {
        max_distance = distance;
        max_index = index;
      }
    }
    if (max_index >= 0 && max_distance > clamped_tolerance) {
      keep[static_cast<std::size_t>(max_index)] = true;
      self(self, first, max_index);
      self(self, max_index, last);
    }
  };
  simplify_range(simplify_range, 0, static_cast<int>(open_points.size()) - 1);

  QVector<QPointF> simplified;
  simplified.reserve(points.size());
  for (auto index = 0; index < open_points.size() - 1; ++index) {
    if (keep[static_cast<std::size_t>(index)]) {
      simplified.append(open_points.at(index));
    }
  }
  return simplified.size() >= 3 ? simplified : points;
}

QVector<QPointF> bounded_closed_outline_points(
    const QVector<QPointF>& points,
    int max_points) {
  if (max_points < 3 || points.size() <= max_points) {
    return points;
  }
  const auto point_count = static_cast<int>(points.size());
  QVector<QPointF> bounded;
  bounded.reserve(max_points);
  const auto step =
      static_cast<double>(point_count) / static_cast<double>(max_points);
  auto previous_index = -1;
  for (auto output_index = 0; output_index < max_points; ++output_index) {
    auto source_index = std::min(
        point_count - 1,
        static_cast<int>(std::floor(static_cast<double>(output_index) * step)));
    if (source_index <= previous_index) {
      source_index = std::min(point_count - 1, previous_index + 1);
    }
    bounded.append(points.at(source_index));
    previous_index = source_index;
  }
  return bounded.size() >= 3 ? bounded : points;
}

QVariantList outline_path_from_pixel_points(
    const QVector<QPointF>& points,
    const QSize& raster_size) {
  QVariantList path;
  if (!raster_size.isValid() || raster_size.width() <= 0 ||
      raster_size.height() <= 0) {
    return path;
  }
  path.reserve(points.size());
  for (const auto& point : points) {
    QVariantMap point_map;
    point_map.insert(
        QStringLiteral("x"),
        clamp_unit(point.x() / static_cast<double>(raster_size.width())));
    point_map.insert(
        QStringLiteral("y"),
        clamp_unit(point.y() / static_cast<double>(raster_size.height())));
    path.append(point_map);
  }
  return path;
}

QVariantList simplify_outline_path_with_pixel_tolerance(
    const QVariantList& path,
    const QSize& raster_size,
    double tolerance_pixels,
    OutlineSimplificationStats* stats) {
  const auto input_points = outline_path_to_pixel_points(path, raster_size);
  if (stats != nullptr) {
    stats->input_point_count += input_points.size();
    stats->tolerance_pixels = tolerance_pixels;
  }
  if (input_points.size() < 4) {
    if (stats != nullptr) {
      stats->output_point_count += path.size();
    }
    return path;
  }
  const auto simplified_points =
      tolerance_simplified_closed_outline_points(input_points, tolerance_pixels);
  const auto max_error =
      max_closed_outline_error_pixels(input_points, simplified_points);
  if (stats != nullptr) {
    stats->output_point_count += simplified_points.size();
    stats->max_error_pixels = std::max(stats->max_error_pixels, max_error);
    stats->applied = true;
  }
  return outline_path_from_pixel_points(simplified_points, raster_size);
}

QVariantList simplify_outline_paths_with_pixel_tolerance(
    const QVariantList& paths,
    const QSize& raster_size,
    double tolerance_pixels,
    OutlineSimplificationStats* stats) {
  if (paths.isEmpty() || !raster_size.isValid()) {
    return paths;
  }
  if (variant_value_is_outline_point(paths.first())) {
    return simplify_outline_path_with_pixel_tolerance(
        paths, raster_size, tolerance_pixels, stats);
  }
  QVariantList simplified_paths;
  simplified_paths.reserve(paths.size());
  for (const auto& path_value : paths) {
    const auto simplified_path = simplify_outline_path_with_pixel_tolerance(
        path_value.toList(), raster_size, tolerance_pixels, stats);
    if (simplified_path.size() >= 3) {
      simplified_paths.append(QVariant::fromValue(simplified_path));
    }
  }
  return simplified_paths;
}

}  // namespace photo_editor::desktop
