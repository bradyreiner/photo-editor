#include "mask_outline_presentation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include <QPointF>
#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool close(double actual, double expected) {
  return std::abs(actual - expected) < 1e-12;
}

QVariantMap point(double x, double y) {
  QVariantMap value;
  value.insert(QStringLiteral("x"), x);
  value.insert(QStringLiteral("y"), y);
  return value;
}

QVariantList square(double left, double top, double size) {
  return {
      point(left, top), point(left + size, top),
      point(left + size, top + size), point(left, top + size)};
}

double point_to_segment_distance(
    const QPointF& point,
    const QPointF& start,
    const QPointF& end) {
  const auto delta = end - start;
  const auto length_squared = QPointF::dotProduct(delta, delta);
  const auto t = length_squared <= 0.0
      ? 0.0
      : std::clamp(QPointF::dotProduct(point - start, delta) / length_squared, 0.0, 1.0);
  return std::hypot(point.x() - (start.x() + t * delta.x()),
                    point.y() - (start.y() + t * delta.y()));
}

double ellipse_chord_error_pixels(
    const QVariantList& path,
    double center,
    double radius,
    double canvas_edge) {
  auto maximum_error = 0.0;
  const auto tau = 2.0 * std::acos(-1.0);
  for (auto index = 0; index < path.size(); ++index) {
    const auto first = path.at(index).toMap();
    const auto second = path.at((index + 1) % path.size()).toMap();
    auto first_angle = std::atan2(first.value(QStringLiteral("y")).toDouble() - center,
                                  first.value(QStringLiteral("x")).toDouble() - center);
    auto second_angle = std::atan2(second.value(QStringLiteral("y")).toDouble() - center,
                                   second.value(QStringLiteral("x")).toDouble() - center);
    if (first_angle < 0.0) first_angle += tau;
    if (second_angle < 0.0) second_angle += tau;
    if (second_angle <= first_angle) second_angle += tau;
    const auto midpoint_angle = (first_angle + second_angle) * 0.5;
    const QPointF exact_midpoint(
        (center + radius * std::cos(midpoint_angle)) * canvas_edge,
        (center + radius * std::sin(midpoint_angle)) * canvas_edge);
    const QPointF start(first.value(QStringLiteral("x")).toDouble() * canvas_edge,
                        first.value(QStringLiteral("y")).toDouble() * canvas_edge);
    const QPointF end(second.value(QStringLiteral("x")).toDouble() * canvas_edge,
                      second.value(QStringLiteral("y")).toDouble() * canvas_edge);
    maximum_error = std::max(maximum_error,
        point_to_segment_distance(exact_midpoint, start, end));
  }
  return maximum_error;
}

QVariantMap vector_mask(const QString& kind) {
  QVariantMap mask;
  mask.insert(QStringLiteral("kind"), kind);
  mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask.insert(QStringLiteral("centerX"), 0.5);
  mask.insert(QStringLiteral("centerY"), 0.5);
  mask.insert(QStringLiteral("radiusX"), 0.25);
  mask.insert(QStringLiteral("radiusY"), 0.20);
  return mask;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  auto rectangle = vector_mask(QStringLiteral("rectangle"));
  const auto rectangle_paths = outline_paths_for_mask(rectangle);
  if (rectangle_paths.size() != 1 || rectangle_paths.first().toList().size() != 4 ||
      outline_points_for_mask(rectangle).size() != 4) {
    return fail("rectangle masks must publish one exact four-corner path");
  }
  auto ellipse = vector_mask(QStringLiteral("ellipse"));
  ellipse.insert(QStringLiteral("radiusX"), 0.45);
  ellipse.insert(QStringLiteral("radiusY"), 0.45);
  const auto ellipse_path = outline_paths_for_mask(ellipse).first().toList();
  if (ellipse_path.size() < 3 ||
      ellipse_chord_error_pixels(ellipse_path, 0.5, 0.45, 10000.0) >= 0.2) {
    return fail("ellipse presentation should stay within a subpixel contour error at high zoom");
  }

  auto clipped_rectangle = vector_mask(QStringLiteral("rectangle"));
  clipped_rectangle.insert(QStringLiteral("centerX"), 0.04);
  clipped_rectangle.insert(QStringLiteral("centerY"), 0.5);
  clipped_rectangle.insert(QStringLiteral("radiusX"), 0.2);
  clipped_rectangle.insert(QStringLiteral("radiusY"), 0.1);
  clipped_rectangle.insert(QStringLiteral("rotation"), 45.0);
  const auto clipped_path = outline_paths_for_mask(clipped_rectangle).first().toList();
  const auto expected_first_clip_y = 0.5 - (0.2 + 0.1) * std::sqrt(0.5) +
      (0.0 - (0.04 - (0.2 - 0.1) * std::sqrt(0.5))) /
          ((0.04 + (0.2 + 0.1) * std::sqrt(0.5)) -
           (0.04 - (0.2 - 0.1) * std::sqrt(0.5))) *
          (2.0 * 0.2 * std::sqrt(0.5));
  const auto expected_second_clip_y = 0.5 + (0.2 + 0.1) * std::sqrt(0.5) -
      ((0.04 + (0.2 - 0.1) * std::sqrt(0.5)) /
       ((0.04 + (0.2 - 0.1) * std::sqrt(0.5)) -
        (0.04 - (0.2 + 0.1) * std::sqrt(0.5)))) *
          (2.0 * 0.2 * std::sqrt(0.5));
  if (clipped_path.size() != 4 ||
      std::abs(clipped_path.at(0).toMap().value(QStringLiteral("x")).toDouble()) > 1e-12 ||
      std::abs(clipped_path.at(0).toMap().value(QStringLiteral("y")).toDouble() - expected_first_clip_y) > 1e-9 ||
      std::abs(clipped_path.at(3).toMap().value(QStringLiteral("x")).toDouble()) > 1e-12 ||
      std::abs(clipped_path.at(3).toMap().value(QStringLiteral("y")).toDouble() - expected_second_clip_y) > 1e-9) {
    return fail("rotated rectangles crossing the image edge should use true polygon intersections");
  }
  auto polygon = vector_mask(QStringLiteral("polygon"));
  polygon.insert(QStringLiteral("polygonPoints"), square(0.1, 0.2, 0.4));
  if (outline_paths_for_mask(polygon).first().toList() !=
      polygon.value(QStringLiteral("polygonPoints")).toList()) {
    return fail("polygon presentation must preserve authored point order");
  }

  QVariantMap wide;
  wide.insert(QStringLiteral("segmentationWidth"), 4000);
  wide.insert(QStringLiteral("segmentationHeight"), 1000);
  QVariantMap extreme;
  extreme.insert(QStringLiteral("segmentationWidth"), 10000);
  extreme.insert(QStringLiteral("segmentationHeight"), 1);
  if (high_resolution_brush_outline_size(wide) != QSize(2048, 512) ||
      high_resolution_brush_outline_size(extreme) != QSize(2048, 256) ||
      high_resolution_brush_outline_size({}) != QSize(2048, 2048)) {
    return fail("brush outline rasters must preserve aspect within exact resolution bounds");
  }

  QVariantMap refined;
  refined.insert(QStringLiteral("selectionSmooth"), 0.01);
  QVariantMap inactive;
  inactive.insert(QStringLiteral("selectionSmooth"), 0.0);
  if (!mask_selection_refinement_active(refined) ||
      mask_selection_refinement_active(inactive)) {
    return fail("selection refinement admission must use exact nonzero controls");
  }

  QVariantMap source_dependent;
  source_dependent.insert(QStringLiteral("brushAutoMask"), true);
  QVariantMap stroke_settings;
  stroke_settings.insert(QStringLiteral("protectEdges"), true);
  QVariantMap stroke;
  stroke.insert(QStringLiteral("settings"), stroke_settings);
  QVariantMap nested_source_dependent;
  nested_source_dependent.insert(
      QStringLiteral("brushStrokes"), QVariantList{stroke});
  if (!brush_mask_has_source_dependent_outline(source_dependent) ||
      !brush_mask_has_source_dependent_outline(nested_source_dependent) ||
      brush_mask_has_source_dependent_outline({})) {
    return fail("brush source-dependence must inspect current and historical settings");
  }

  if (!clean_canvas_outline_matches_final_single_mask(rectangle)) {
    return fail("additive vector masks must admit clean-canvas final-outline reuse");
  }
  auto subtract = rectangle;
  subtract.insert(QStringLiteral("operation"), QStringLiteral("subtract"));
  auto inverted = rectangle;
  inverted.insert(QStringLiteral("invert"), true);
  auto auto_brush = source_dependent;
  auto_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  auto_brush.insert(QStringLiteral("operation"), QStringLiteral("add"));
  if (clean_canvas_outline_matches_final_single_mask(subtract) ||
      clean_canvas_outline_matches_final_single_mask(inverted) ||
      clean_canvas_outline_matches_final_single_mask(auto_brush)) {
    return fail("composed, inverted, or source-dependent masks must require final geometry");
  }

  const auto large = square(0.1, 0.1, 0.8);
  const auto medium = square(0.1, 0.1, 0.2);
  const auto tiny = square(0.1, 0.1, 0.01);
  auto degenerate = QVariantList{point(0.2, 0.2), point(0.5, 0.5), point(0.8, 0.8)};
  auto invalid = square(0.1, 0.1, 0.2);
  invalid[1] = QVariant::fromValue(QVariantMap{{QStringLiteral("x"), std::numeric_limits<double>::quiet_NaN()},
                                               {QStringLiteral("y"), 0.1}});
  if (!close(normalized_outline_path_area(large), 0.64) ||
      !close(normalized_outline_path_area({}), 0.0)) {
    return fail("outline area must measure normalized closed geometry exactly");
  }
  const QVariantList nested_paths{
      QVariant::fromValue(large), QVariant::fromValue(medium), QVariant::fromValue(tiny),
      QVariant::fromValue(degenerate), QVariant::fromValue(invalid)};
  OutlineNoiseFilterStats stats;
  const auto filtered = filter_minor_composite_outline_paths(nested_paths, &stats);
  if (filtered.size() != 3 || stats.input_path_count != 5 ||
      stats.output_path_count != 3 || !stats.applied ||
      !close(stats.largest_area, 0.64) ||
      !close(stats.minimum_secondary_area, 0.0) ||
      filtered.at(2).toList() != tiny) {
    return fail("composite outlines must preserve every valid island and hole while reporting filtered invalid contours");
  }
  if (first_outline_path_only(nested_paths).size() != 1 ||
      first_outline_path_only(large) != large) {
    return fail("single-path fallback must preserve flat paths and select one nested path");
  }

  return EXIT_SUCCESS;
}
