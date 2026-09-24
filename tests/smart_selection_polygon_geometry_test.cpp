#include "smart_selection_polygon_geometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

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

  if (!single_smart_select_candidate_instance_ids({}, 0).isEmpty() ||
      single_smart_select_candidate_instance_ids({2, 4, 8}, -1) != QVector<int>{2} ||
      single_smart_select_candidate_instance_ids({2, 4, 8}, 99) != QVector<int>{8}) {
    return fail("single-candidate selection must fail empty and clamp both index bounds");
  }
  if (!close(
          polygon_cross(QPointF(0.0, 0.0), QPointF(1.0, 0.0), QPointF(0.0, 1.0)),
          1.0)) {
    return fail("polygon cross products must preserve counter-clockwise orientation");
  }

  QVariantMap invalid;
  invalid.insert(QStringLiteral("x"), 0.5);
  const auto normalized = normalized_polygon_points(
      {point(-1.0, 2.0), invalid, point(0.25, 0.75)});
  if (normalized.size() != 2 || normalized.at(0) != QPointF(0.0, 1.0) ||
      normalized.at(1) != QPointF(0.25, 0.75)) {
    return fail("polygon normalization must clamp coordinates and omit incomplete rows");
  }

  const QVariantList square{
      point(0.0, 0.0), point(1.0, 0.0), point(1.0, 1.0), point(0.0, 1.0)};
  const QVariantList bow_tie{
      point(0.0, 0.0), point(1.0, 1.0), point(0.0, 1.0), point(1.0, 0.0)};
  if (polygon_points_self_intersect(square) ||
      !polygon_points_self_intersect(bow_tie) ||
      polygon_points_self_intersect({point(0.0, 0.0), point(1.0, 0.0), point(0.5, 1.0)})) {
    return fail("polygon intersection must reject bow ties without rejecting valid polygons");
  }

  const auto variants =
      variant_list_from_polygon_points({QPointF(-1.0, 0.5), QPointF(2.0, 0.75)});
  if (variants.size() != 2 ||
      !close(variants.at(0).toMap().value(QStringLiteral("x")).toDouble(), 0.0) ||
      !close(variants.at(1).toMap().value(QStringLiteral("x")).toDouble(), 1.0)) {
    return fail("polygon variant publication must preserve order and normalized bounds");
  }

  if (!close(
          distance_to_segment(QPointF(0.5, 1.0), QPointF(0.0, 0.0), QPointF(1.0, 0.0)),
          1.0) ||
      !close(
          distance_to_segment(QPointF(3.0, 4.0), QPointF(0.0, 0.0), QPointF(0.0, 0.0)),
          5.0)) {
    return fail("point-to-segment distance must handle projection and degenerate segments");
  }

  const QVector<QPointF> with_collinear{
      QPointF(0.0, 0.0), QPointF(0.5, 0.0), QPointF(1.0, 0.0),
      QPointF(1.0, 1.0), QPointF(0.0, 1.0)};
  const auto simplified = simplified_closed_polygon_points(with_collinear, 0.01);
  if (simplified.size() != 4 || simplified.contains(QPointF(0.5, 0.0))) {
    return fail("closed polygon simplification must remove collinear points and retain corners");
  }

  const QVector<QPointF> triangle{
      QPointF(0.0, 0.0), QPointF(1.0, 0.0), QPointF(0.0, 1.0)};
  const auto smoothed = smoothed_closed_polygon_points(triangle);
  if (smoothed.size() != 6 || smoothed.at(0) != QPointF(0.25, 0.0) ||
      smoothed.at(1) != QPointF(0.75, 0.0) ||
      smoothed_closed_polygon_points({QPointF(0.0, 0.0)}).size() != 1) {
    return fail("closed polygon smoothing must publish exact quarter points and preserve short input");
  }

  return EXIT_SUCCESS;
}
