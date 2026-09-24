#include "mask_outline_simplification.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  if (!close(normalized_outline_display_scale(-1.0), 1.0) ||
      !close(normalized_outline_display_scale(
                 std::numeric_limits<double>::infinity()),
             1.0) ||
      !close(normalized_outline_display_scale(0.26), 0.25) ||
      !close(normalized_outline_display_scale(1.13), 1.25) ||
      !close(normalized_outline_display_scale(40.0), 32.0)) {
    return fail("outline display scale must reject invalid input and quantize exact bounds");
  }
  if (!variant_value_is_outline_point(point(0.0, 0.0)) ||
      variant_value_is_outline_point(QVariantList{})) {
    return fail("outline point admission must require an x/y map");
  }

  const QVariantList closed_path{
      point(0.0, 0.0), point(1.0, 0.0), point(1.0, 1.0),
      point(0.0, 1.0), point(0.0, 0.0)};
  const auto pixels = outline_path_to_pixel_points(closed_path, QSize(100, 50));
  if (pixels.size() != 4 || pixels.at(1) != QPointF(100.0, 0.0) ||
      !outline_path_to_pixel_points(closed_path, QSize{}).isEmpty()) {
    return fail("outline pixel conversion must remove a repeated endpoint and reject invalid rasters");
  }

  const QVector<QPointF> with_collinear{
      QPointF(0.0, 0.0), QPointF(5.0, 0.0), QPointF(10.0, 0.0),
      QPointF(10.0, 10.0), QPointF(0.0, 10.0)};
  const auto simplified =
      tolerance_simplified_closed_outline_points(with_collinear, 0.1);
  if (simplified.size() >= with_collinear.size() ||
      max_closed_outline_error_pixels(with_collinear, simplified) > 0.1) {
    return fail("outline simplification must remove redundant points within measured tolerance");
  }
  const QVector<QPointF> repeated{
      QPointF(0.0, 0.0), QPointF(5.0, 0.0), QPointF(5.0, 0.0),
      QPointF(10.0, 10.0), QPointF(0.0, 10.0)};
  if (tolerance_simplified_closed_outline_points(repeated, 10.0) != repeated) {
    return fail("duplicate interior outline points must fail closed without topology changes");
  }

  QVector<QPointF> many;
  for (int index = 0; index < 10; ++index) {
    many.append(QPointF(static_cast<double>(index), 0.0));
  }
  const auto bounded = bounded_closed_outline_points(many, 4);
  if (bounded.size() != 4 || bounded.at(0) != many.at(0) ||
      bounded.at(1) != many.at(2) || bounded.at(3) != many.at(7) ||
      bounded_closed_outline_points(many, 2) != many) {
    return fail("outline point budgets must sample deterministically and reject invalid limits");
  }

  const auto normalized = outline_path_from_pixel_points(
      {QPointF(-1.0, 25.0), QPointF(120.0, 50.0)}, QSize(100, 50));
  if (normalized.size() != 2 ||
      !close(normalized.at(0).toMap().value(QStringLiteral("x")).toDouble(), 0.0) ||
      !close(normalized.at(0).toMap().value(QStringLiteral("y")).toDouble(), 0.5) ||
      !close(normalized.at(1).toMap().value(QStringLiteral("x")).toDouble(), 1.0)) {
    return fail("outline path publication must normalize and clamp pixel points");
  }

  OutlineSimplificationStats stats;
  const auto simple_path = simplify_outline_path_with_pixel_tolerance(
      closed_path, QSize(100, 100), 1.5, &stats);
  if (simple_path.size() < 3 || stats.input_point_count != 4 ||
      stats.output_point_count != simple_path.size() || !stats.applied ||
      !close(stats.tolerance_pixels, 1.5) || stats.max_error_pixels > 1.5) {
    return fail("outline simplification must publish complete bounded statistics");
  }
  const QVariantList nested{
      QVariant::fromValue(closed_path), QVariant::fromValue(closed_path)};
  OutlineSimplificationStats nested_stats;
  const auto nested_result = simplify_outline_paths_with_pixel_tolerance(
      nested, QSize(100, 100), 1.5, &nested_stats);
  if (nested_result.size() != 2 || nested_stats.input_point_count != 8 ||
      !simplify_outline_paths_with_pixel_tolerance({}, QSize(100, 100), 1.5, nullptr)
           .isEmpty()) {
    return fail("nested outline simplification must preserve path structure and aggregate stats");
  }

  return EXIT_SUCCESS;
}
