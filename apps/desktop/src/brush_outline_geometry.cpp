#include "brush_outline_geometry.h"

#include "brush_input_schema.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QPointF>

namespace photo_editor::desktop {
namespace {

void append_outline_point(QVariantList* path, double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), std::clamp(x, 0.0, 1.0));
  point.insert(QStringLiteral("y"), std::clamp(y, 0.0, 1.0));
  path->append(point);
}

}  // namespace

void append_outline_path(QVariantList* outline_paths, const QVariantList& outline) {
  if (outline_paths != nullptr) {
    outline_paths->append(QVariant::fromValue(outline));
  }
}

QVariantList outline_path_for_brush_samples(
    const QVector<BrushPreviewPoint>& samples) {
  if (samples.isEmpty()) {
    return {};
  }

  constexpr auto kCircleSegments = 40;
  if (samples.size() == 1) {
    const auto& point = samples.constFirst();
    const auto radius = std::clamp(point.size, 0.004, 0.5);
    QVariantList circle;
    circle.reserve(kCircleSegments);
    for (auto index = 0; index < kCircleSegments; ++index) {
      const auto angle =
          (static_cast<double>(index) / static_cast<double>(kCircleSegments)) *
          std::numbers::pi * 2.0;
      append_outline_point(
          &circle,
          point.x + std::cos(angle) * radius,
          point.y + std::sin(angle) * radius);
    }
    return circle;
  }

  QVector<QPointF> left_points;
  QVector<QPointF> right_points;
  left_points.reserve(samples.size());
  right_points.reserve(samples.size());
  const auto sample_count = static_cast<int>(samples.size());
  const auto step = std::max(1, static_cast<int>(std::ceil(sample_count / 140.0)));
  for (auto index = 0; index < sample_count; index += step) {
    const auto& point = samples.at(index);
    const auto& previous = samples.at(std::max(0, index - 1));
    const auto& next = samples.at(std::min(sample_count - 1, index + 1));
    auto dx = next.x - previous.x;
    auto dy = next.y - previous.y;
    auto length = std::hypot(dx, dy);
    if (length <= 1e-6) {
      dx = 1.0;
      dy = 0.0;
      length = 1.0;
    }
    const auto radius = std::clamp(point.size, 0.004, 0.5);
    const auto normal_x = -dy / length;
    const auto normal_y = dx / length;
    left_points.append(
        QPointF(point.x + normal_x * radius, point.y + normal_y * radius));
    right_points.append(
        QPointF(point.x - normal_x * radius, point.y - normal_y * radius));
  }
  if (left_points.size() < 2 || right_points.size() < 2) {
    return {};
  }

  QVariantList outline;
  outline.reserve(left_points.size() + right_points.size() + 2);
  const auto start = samples.constFirst();
  const auto start_radius = std::clamp(start.size, 0.004, 0.5);
  append_outline_point(&outline, start.x - start_radius, start.y);
  for (const auto& point : left_points) {
    append_outline_point(&outline, point.x(), point.y());
  }
  const auto end = samples.constLast();
  const auto end_radius = std::clamp(end.size, 0.004, 0.5);
  append_outline_point(&outline, end.x + end_radius, end.y);
  for (auto index = static_cast<int>(right_points.size()) - 1; index >= 0; --index) {
    const auto& point = right_points.at(index);
    append_outline_point(&outline, point.x(), point.y());
  }
  return outline.size() >= 4 ? outline : QVariantList{};
}

QVariantList fast_outline_paths_for_brush_mask(const QVariantMap& mask) {
  QVariantList outline_paths;
  const auto strokes = mask.value(QStringLiteral("brushStrokes")).toList();
  for (const auto& stroke_value : strokes) {
    const auto stroke = stroke_value.toMap();
    if (stroke.value(QStringLiteral("operation")).toString() == QStringLiteral("erase")) {
      continue;
    }
    QVector<BrushPreviewPoint> samples;
    const auto sample_values = stroke.value(QStringLiteral("samples")).toList();
    samples.reserve(sample_values.size());
    for (const auto& sample_value : sample_values) {
      const auto point =
          brush_preview_point_from_variant(sample_value, 0.5, 0.5, 0.08, 1.0);
      if (!point.erase) {
        samples.append(point);
      }
    }
    const auto outline = outline_path_for_brush_samples(samples);
    if (!outline.isEmpty()) {
      append_outline_path(&outline_paths, outline);
    }
  }
  if (!outline_paths.isEmpty()) {
    return outline_paths;
  }

  QVector<BrushPreviewPoint> points;
  const auto point_values = mask.value(QStringLiteral("brushPoints")).toList();
  points.reserve(point_values.size());
  for (const auto& point_value : point_values) {
    const auto point =
        brush_preview_point_from_variant(point_value, 0.5, 0.5, 0.08, 1.0);
    if (!point.erase) {
      points.append(point);
    }
  }
  const auto outline = outline_path_for_brush_samples(points);
  if (!outline.isEmpty()) {
    append_outline_path(&outline_paths, outline);
  }
  return outline_paths;
}

}  // namespace photo_editor::desktop
