#include "brush_outline_geometry.h"

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

photo_editor::desktop::BrushPreviewPoint point(double x, double y, double size = 0.1) {
  return photo_editor::desktop::BrushPreviewPoint{.x = x, .y = y, .size = size};
}

QVariantMap point_variant(double x, double y, bool erase = false) {
  QVariantMap value;
  value.insert(QStringLiteral("x"), x);
  value.insert(QStringLiteral("y"), y);
  value.insert(QStringLiteral("size"), 0.1);
  if (erase) {
    value.insert(QStringLiteral("erase"), true);
  }
  return value;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  QVariantList appended;
  append_outline_path(nullptr, {QVariantMap{}});
  append_outline_path(&appended, {QVariantMap{}});
  if (appended.size() != 1 || appended.constFirst().toList().size() != 1) {
    return fail("outline path publication must be null-safe and preserve nested path structure");
  }

  if (!outline_path_for_brush_samples({}).isEmpty()) {
    return fail("empty brush samples must publish no outline");
  }
  const auto circle = outline_path_for_brush_samples({point(0.5, 0.5)});
  if (circle.size() != 40 ||
      !close(circle.constFirst().toMap().value(QStringLiteral("x")).toDouble(), 0.6) ||
      !close(circle.constFirst().toMap().value(QStringLiteral("y")).toDouble(), 0.5)) {
    return fail("single brush samples must publish the exact forty-point circle");
  }
  const auto edge_circle = outline_path_for_brush_samples({point(0.0, 0.0, 0.2)});
  for (const auto& value : edge_circle) {
    const auto outline_point = value.toMap();
    const auto x = outline_point.value(QStringLiteral("x")).toDouble();
    const auto y = outline_point.value(QStringLiteral("y")).toDouble();
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
      return fail("brush outline coordinates must remain normalized at image edges");
    }
  }

  const auto stroke_outline = outline_path_for_brush_samples(
      {point(0.2, 0.5), point(0.5, 0.5), point(0.8, 0.5)});
  if (stroke_outline.size() != 8) {
    return fail("multi-point brush samples must publish both sides and endpoint caps");
  }

  QVariantMap stroke;
  stroke.insert(QStringLiteral("samples"), QVariantList{point_variant(0.5, 0.5)});
  QVariantMap mask;
  mask.insert(QStringLiteral("brushStrokes"), QVariantList{stroke});
  const auto stroke_paths = fast_outline_paths_for_brush_mask(mask);
  if (stroke_paths.size() != 1 || stroke_paths.constFirst().toList().size() != 40) {
    return fail("non-erase stroke histories must publish one outline per admitted stroke");
  }

  stroke.insert(QStringLiteral("operation"), QStringLiteral("erase"));
  mask.insert(QStringLiteral("brushStrokes"), QVariantList{stroke});
  mask.insert(QStringLiteral("brushPoints"), QVariantList{point_variant(0.25, 0.25)});
  const auto fallback_paths = fast_outline_paths_for_brush_mask(mask);
  if (fallback_paths.size() != 1 || fallback_paths.constFirst().toList().size() != 40) {
    return fail("erase-only stroke histories must fall back to admitted flat brush points");
  }
  mask.insert(QStringLiteral("brushPoints"), QVariantList{point_variant(0.25, 0.25, true)});
  if (!fast_outline_paths_for_brush_mask(mask).isEmpty()) {
    return fail("erase samples must not create a visible fast outline");
  }

  return EXIT_SUCCESS;
}
