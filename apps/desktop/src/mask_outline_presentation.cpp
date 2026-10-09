#include "mask_outline_presentation.h"

#include "brush_matte_rebuild.h"
#include "brush_outline_geometry.h"
#include "edit_recipe_mask_schema.h"
#include "mask_outline_simplification.h"
#include "mask_outline_tracing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <QMetaType>
#include <QPointF>
#include <QVector>

namespace photo_editor::desktop {
namespace {

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

bool map_bool_value(
    const QVariantMap& map,
    const QString& key,
    bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
}

double map_double_value(
    const QVariantMap& map,
    const QString& key,
    double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

QVector<QPointF> clip_polygon_to_unit_box(QVector<QPointF> polygon) {
  enum class Axis { X, Y };
  struct ClipEdge final {
    Axis axis;
    double boundary;
    bool keep_greater;
  };
  constexpr std::array<ClipEdge, 4> edges{{
      {Axis::X, 0.0, true}, {Axis::X, 1.0, false},
      {Axis::Y, 0.0, true}, {Axis::Y, 1.0, false}}};
  const auto coordinate = [](const QPointF& point, Axis axis) {
    return axis == Axis::X ? point.x() : point.y();
  };
  for (const auto& edge : edges) {
    if (polygon.isEmpty())
      break;
    QVector<QPointF> clipped;
    clipped.reserve(polygon.size() + 2);
    auto previous = polygon.last();
    auto previous_coordinate = coordinate(previous, edge.axis);
    auto previous_inside = edge.keep_greater
        ? previous_coordinate >= edge.boundary
        : previous_coordinate <= edge.boundary;
    for (const auto& current : polygon) {
      const auto current_coordinate = coordinate(current, edge.axis);
      const auto current_inside = edge.keep_greater
          ? current_coordinate >= edge.boundary
          : current_coordinate <= edge.boundary;
      if (current_inside != previous_inside) {
        const auto denominator = current_coordinate - previous_coordinate;
        if (std::abs(denominator) > std::numeric_limits<double>::epsilon()) {
          const auto t = (edge.boundary - previous_coordinate) / denominator;
          const auto intersection = previous + (current - previous) * t;
          clipped.append(edge.axis == Axis::X
              ? QPointF(edge.boundary, intersection.y())
              : QPointF(intersection.x(), edge.boundary));
        }
      }
      if (current_inside)
        clipped.append(current);
      previous = current;
      previous_coordinate = current_coordinate;
      previous_inside = current_inside;
    }
    polygon = std::move(clipped);
  }
  return polygon;
}

QVariantList normalized_points(const QVector<QPointF>& points) {
  QVariantList path;
  path.reserve(points.size());
  for (const auto& point : points) {
    QVariantMap map;
    map.insert(QStringLiteral("x"), clamp_unit(point.x()));
    map.insert(QStringLiteral("y"), clamp_unit(point.y()));
    path.append(map);
  }
  return path;
}

}  // namespace

bool mask_selection_refinement_active(const QVariantMap& mask) {
  return map_bool_value(mask, QStringLiteral("selectionInvert"), false) ||
         std::abs(map_double_value(mask, QStringLiteral("selectionExpand"), 0.0)) > 1e-6 ||
         std::abs(map_double_value(mask, QStringLiteral("selectionSmooth"), 0.0)) > 1e-6 ||
         std::abs(map_double_value(mask, QStringLiteral("selectionFineEdge"), 0.0)) > 1e-6 ||
         std::abs(map_double_value(mask, QStringLiteral("selectionEdgeContrast"), 0.0)) > 1e-6 ||
         std::abs(map_double_value(mask, QStringLiteral("selectionDecontaminate"), 0.0)) > 1e-6;
}

QSize high_resolution_brush_outline_size(const QVariantMap& mask) {
  const auto segmentation_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto segmentation_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  auto aspect = segmentation_width > 0 && segmentation_height > 0
                    ? static_cast<double>(segmentation_width) /
                          static_cast<double>(segmentation_height)
                    : 1.0;
  if (!std::isfinite(aspect) || aspect <= 0.0) {
    aspect = 1.0;
  }

  constexpr auto kBrushOutlineMaxEdge = 2048;
  constexpr auto kBrushOutlineMinEdge = 256;
  auto width = kBrushOutlineMaxEdge;
  auto height = kBrushOutlineMaxEdge;
  if (aspect >= 1.0) {
    height = static_cast<int>(std::lround(static_cast<double>(kBrushOutlineMaxEdge) / aspect));
  } else {
    width = static_cast<int>(std::lround(static_cast<double>(kBrushOutlineMaxEdge) * aspect));
  }
  return QSize(
      std::clamp(width, kBrushOutlineMinEdge, kBrushOutlineMaxEdge),
      std::clamp(height, kBrushOutlineMinEdge, kBrushOutlineMaxEdge));
}

QVariantList traced_outline_paths_for_brush_strokes(const QVariantMap& mask) {
  if (normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()) !=
          QStringLiteral("brush") ||
      mask_selection_refinement_active(mask)) {
    return {};
  }
  const auto raster_size = high_resolution_brush_outline_size(mask);
  if (!raster_size.isValid() || raster_size.width() <= 1 || raster_size.height() <= 1) {
    return {};
  }
  const auto raster_mask = mask_with_rebuilt_brush_matte(
      mask, raster_size, QStringLiteral("active-outline"));
  if (raster_mask.isEmpty()) {
    return {};
  }
  const auto data = raster_mask.value(QStringLiteral("segmentationData")).toByteArray();
  return traced_outline_paths_for_alpha_mask_with_edge_padding(
      data,
      raster_size.width(),
      raster_size.height(),
      map_bool_value(mask, QStringLiteral("pixelSelection"), false));
}

QVariantList outline_paths_for_mask(const QVariantMap& mask) {
  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  if (kind == QStringLiteral("ellipse") || kind == QStringLiteral("rectangle")) {
    const auto center_x = map_double_value(mask, QStringLiteral("centerX"), 0.5);
    const auto center_y = map_double_value(mask, QStringLiteral("centerY"), 0.5);
    const auto radius_x = map_double_value(
        mask, QStringLiteral("radiusX"), kind == QStringLiteral("rectangle") ? 0.30 : 0.25);
    const auto radius_y = map_double_value(
        mask, QStringLiteral("radiusY"), kind == QStringLiteral("rectangle") ? 0.20 : 0.25);
    const auto radians = map_double_value(mask, QStringLiteral("rotation"), 0.0) * M_PI / 180.0;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    const auto transform_point = [&](double local_x, double local_y) {
      return QPointF(
          center_x + local_x * cosine - local_y * sine,
          center_y + local_x * sine + local_y * cosine);
    };

    QVector<QPointF> outline_points;
    if (kind == QStringLiteral("rectangle")) {
      outline_points.append(transform_point(-radius_x, -radius_y));
      outline_points.append(transform_point(radius_x, -radius_y));
      outline_points.append(transform_point(radius_x, radius_y));
      outline_points.append(transform_point(-radius_x, radius_y));
    } else {
      constexpr auto kEllipseOutlineSegments = 384;
      outline_points.reserve(kEllipseOutlineSegments);
      for (auto index = 0; index < kEllipseOutlineSegments; ++index) {
        const auto angle =
            (static_cast<double>(index) / kEllipseOutlineSegments) * M_PI * 2.0;
        outline_points.append(transform_point(
            std::cos(angle) * radius_x, std::sin(angle) * radius_y));
      }
    }
    outline_points = clip_polygon_to_unit_box(std::move(outline_points));
    QVariantList outline_paths;
    append_outline_path(&outline_paths, normalized_points(outline_points));
    return outline_paths;
  }
  if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
    QVariantList outline_paths;
    append_outline_path(&outline_paths, mask.value(QStringLiteral("polygonPoints")).toList());
    return outline_paths;
  }
  if (kind == QStringLiteral("brush") && !mask_selection_refinement_active(mask) &&
      !brush_mask_has_source_dependent_outline(mask)) {
    const auto traced_brush_outlines = traced_outline_paths_for_brush_strokes(mask);
    if (!traced_brush_outlines.isEmpty()) {
      return traced_brush_outlines;
    }
    const auto fast_brush_outlines = fast_outline_paths_for_brush_mask(mask);
    if (!fast_brush_outlines.isEmpty()) {
      return fast_brush_outlines;
    }
  }

  const auto segmentation_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto segmentation_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto segmentation_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if ((is_raster_mask_kind(kind) || kind == QStringLiteral("brush")) &&
      valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
    const auto traced_outlines = traced_outline_paths_for_alpha_mask_with_edge_padding(
        segmentation_data,
        segmentation_width,
        segmentation_height,
        map_bool_value(mask, QStringLiteral("pixelSelection"), false));
    if (!traced_outlines.isEmpty()) {
      return traced_outlines;
    }
    if (is_ai_segmentation_mask_kind(kind)) {
      return {};
    }
    const auto hull_outline = hull_points_for_segmentation_mask(mask);
    if (hull_outline.size() >= 3) {
      QVariantList outline_paths;
      append_outline_path(&outline_paths, hull_outline);
      return outline_paths;
    }
    QVariantList outline_paths;
    append_outline_path(&outline_paths, bounding_box_points_for_segmentation_mask(mask));
    return outline_paths;
  }
  return {};
}

QVariantList clean_canvas_outline_paths_for_mask(const QVariantMap& mask) {
  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  if (kind == QStringLiteral("ellipse") || kind == QStringLiteral("rectangle") ||
      kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
    return outline_paths_for_mask(mask);
  }
  const auto segmentation_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto segmentation_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto segmentation_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
    if (kind == QStringLiteral("brush") && !mask_selection_refinement_active(mask) &&
      !brush_mask_has_source_dependent_outline(mask)) {
      const auto traced_brush_outlines = traced_outline_paths_for_brush_strokes(mask);
      if (!traced_brush_outlines.isEmpty()) {
        return traced_brush_outlines;
      }
    }
    const auto traced_outlines =
        traced_outline_paths_for_alpha_mask(
            segmentation_data,
            segmentation_width,
            segmentation_height,
            map_bool_value(mask, QStringLiteral("pixelSelection"), false));
    if (!traced_outlines.isEmpty()) {
      return traced_outlines;
    }
  }
  if (kind == QStringLiteral("brush") && !mask_selection_refinement_active(mask) &&
      !brush_mask_has_source_dependent_outline(mask)) {
    const auto fast_brush_outlines = fast_outline_paths_for_brush_mask(mask);
    if (!fast_brush_outlines.isEmpty()) {
      return fast_brush_outlines;
    }
  }
  return {};
}

bool brush_mask_has_source_dependent_outline(const QVariantMap& mask) {
  if (map_bool_value(mask, QStringLiteral("brushAutoMask"), false) ||
      map_bool_value(mask, QStringLiteral("brushProtectEdges"), false)) {
    return true;
  }
  const auto strokes = mask.value(QStringLiteral("brushStrokes")).toList();
  for (const auto& stroke_value : strokes) {
    const auto settings = stroke_value.toMap().value(QStringLiteral("settings")).toMap();
    if (map_bool_value(settings, QStringLiteral("autoMask"), false) ||
        map_bool_value(settings, QStringLiteral("protectEdges"), false)) {
      return true;
    }
  }
  return false;
}

bool clean_canvas_outline_matches_final_single_mask(const QVariantMap& mask) {
  // A geometric boundary is not the final selected region when source/group
  // coverage or post-composition refinement changes its alpha.
  if (map_bool_value(mask, QStringLiteral("maskSetSelectionInvert"), false)) return false;
  for (const auto* key : {"opacity", "density", "maskSetOpacity", "maskSetDensity"}) {
    if (std::abs(map_double_value(mask, QString::fromLatin1(key), 1.0) - 1.0) > 1e-6) return false;
  }
  for (const auto* key : {"maskSetSelectionExpand", "maskSetSelectionSmooth",
                        "maskSetSelectionFineEdge", "maskSetSelectionEdgeContrast"}) {
    if (std::abs(map_double_value(mask, QString::fromLatin1(key), 0.0)) > 1e-6) return false;
  }
  if (mask_selection_refinement_active(mask) ||
      map_bool_value(mask, QStringLiteral("selectionInvert"), false) ||
      map_bool_value(mask, QStringLiteral("invert"), false)) {
    return false;
  }
  const auto operation =
      normalized_mask_operation(mask.value(QStringLiteral("operation")).toString());
  if (operation != QStringLiteral("add") && operation != QStringLiteral("replace") &&
      operation != QStringLiteral("union")) {
    return false;
  }
  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  return kind != QStringLiteral("brush") || !brush_mask_has_source_dependent_outline(mask);
}

QVariantList first_outline_path_only(const QVariantList& outline_paths) {
  if (outline_paths.isEmpty()) {
    return {};
  }
  if (outline_paths.first().metaType().id() == QMetaType::QVariantMap) {
    return outline_paths;
  }
  QVariantList single_path;
  single_path.append(outline_paths.first());
  return single_path;
}

double normalized_outline_path_area(const QVariantList& path) {
  if (path.size() < 3) {
    return 0.0;
  }
  auto twice_area = 0.0;
  for (auto index = 0; index < path.size(); ++index) {
    if (!variant_value_is_outline_point(path.at(index)) ||
        !variant_value_is_outline_point(path.at((index + 1) % path.size()))) {
      return 0.0;
    }
    const auto current = path.at(index).toMap();
    const auto next = path.at((index + 1) % path.size()).toMap();
    bool current_x_ok = false;
    bool current_y_ok = false;
    bool next_x_ok = false;
    bool next_y_ok = false;
    const auto current_x_value = current.value(QStringLiteral("x")).toDouble(&current_x_ok);
    const auto current_y_value = current.value(QStringLiteral("y")).toDouble(&current_y_ok);
    const auto next_x_value = next.value(QStringLiteral("x")).toDouble(&next_x_ok);
    const auto next_y_value = next.value(QStringLiteral("y")).toDouble(&next_y_ok);
    if (!current_x_ok || !current_y_ok || !next_x_ok || !next_y_ok ||
        !std::isfinite(current_x_value) || !std::isfinite(current_y_value) ||
        !std::isfinite(next_x_value) || !std::isfinite(next_y_value)) {
      return 0.0;
    }
    const auto current_x = clamp_unit(current_x_value);
    const auto current_y = clamp_unit(current_y_value);
    const auto next_x = clamp_unit(next_x_value);
    const auto next_y = clamp_unit(next_y_value);
    twice_area += current_x * next_y - next_x * current_y;
  }
  return std::abs(twice_area) * 0.5;
}

QVariantList filter_minor_composite_outline_paths(
    const QVariantList& outline_paths,
    OutlineNoiseFilterStats* stats) {
  if (stats != nullptr) {
    stats->input_path_count = static_cast<int>(outline_paths.size());
    stats->output_path_count = 0;
    stats->largest_area = 0.0;
    stats->minimum_secondary_area = 0.0;
    stats->applied = false;
  }
  if (outline_paths.isEmpty()) {
    return outline_paths;
  }

  const auto flat_path = variant_value_is_outline_point(outline_paths.first());
  const auto input_path_count = flat_path ? 1 : static_cast<int>(outline_paths.size());
  QVector<QVariantList> paths;
  paths.reserve(input_path_count);
  if (flat_path) {
    paths.append(outline_paths);
  } else {
    for (const auto& value : outline_paths)
      paths.append(value.toList());
  }

  QVariantList filtered_paths;
  auto largest_area = 0.0;
  for (const auto& path : paths) {
    const auto area = normalized_outline_path_area(path);
    if (area <= std::numeric_limits<double>::epsilon())
      continue;
    largest_area = std::max(largest_area, area);
    if (flat_path) {
      filtered_paths = path;
    } else {
      filtered_paths.append(QVariant::fromValue(path));
    }
  }

  if (stats != nullptr) {
    stats->input_path_count = input_path_count;
    stats->output_path_count = flat_path
        ? (filtered_paths.isEmpty() ? 0 : 1)
        : static_cast<int>(filtered_paths.size());
    stats->largest_area = largest_area;
    stats->minimum_secondary_area = 0.0;
    stats->applied = stats->output_path_count != stats->input_path_count;
  }
  return filtered_paths;
}

QVariantList outline_points_for_mask(const QVariantMap& mask) {
  const auto outline_paths = clean_canvas_outline_paths_for_mask(mask);
  if (outline_paths.isEmpty()) {
    return {};
  }
  const auto first_path = outline_paths.first().toList();
  if (!first_path.isEmpty()) {
    return first_path;
  }
  if (outline_paths.first().metaType().id() == QMetaType::QVariantMap) {
    return outline_paths;
  }
  return {};
}

}  // namespace photo_editor::desktop
