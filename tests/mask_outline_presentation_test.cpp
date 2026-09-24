#include "mask_outline_presentation.h"

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

QVariantList square(double left, double top, double size) {
  return {
      point(left, top), point(left + size, top),
      point(left + size, top + size), point(left, top + size)};
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
  if (outline_paths_for_mask(ellipse).first().toList().size() != 72) {
    return fail("ellipse masks must publish the bounded 72-point presentation path");
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
  if (!close(normalized_outline_path_area(large), 0.64) ||
      !close(normalized_outline_path_area({}), 0.0)) {
    return fail("outline area must measure normalized closed geometry exactly");
  }
  const QVariantList nested_paths{
      QVariant::fromValue(large), QVariant::fromValue(medium), QVariant::fromValue(tiny)};
  OutlineNoiseFilterStats stats;
  const auto filtered = filter_minor_composite_outline_paths(nested_paths, &stats);
  if (filtered.size() != 2 || stats.input_path_count != 3 ||
      stats.output_path_count != 2 || !stats.applied ||
      !close(stats.largest_area, 0.64) ||
      !close(stats.minimum_secondary_area, 0.0256)) {
    return fail("composite filtering must retain material paths and report exact noise policy");
  }
  if (first_outline_path_only(nested_paths).size() != 1 ||
      first_outline_path_only(large) != large) {
    return fail("single-path fallback must preserve flat paths and select one nested path");
  }

  return EXIT_SUCCESS;
}
