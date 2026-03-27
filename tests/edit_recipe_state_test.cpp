#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "edit_recipe_state.h"

namespace {

using photo_editor::desktop::EditRecipeState;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap point(double x, double y) {
  QVariantMap point_map;
  point_map.insert(QStringLiteral("x"), x);
  point_map.insert(QStringLiteral("y"), y);
  return point_map;
}

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

}  // namespace

int main() {
  const QString legacy_recipe =
      QStringLiteral(
          R"({"recipeVersion":2,"exposure":0.75,"curveHighlights":20,"cropZoom":1.4,"cropPreset":"4x5","linearMaskEnabled":true,"linearMaskExposure":0.35,"linearMaskTemperature":12,"linearMaskFeather":0.3,"linearMaskPosition":0.62,"linearMaskRotation":18,"hslChannel":"Blues","hueAdjustment":14,"saturationAdjustment":-10,"luminanceAdjustment":9})");

  const auto parsed_legacy = photo_editor::desktop::parse_edit_recipe_json(legacy_recipe);
  if (!almost_equal(parsed_legacy.exposure, 0.75)) {
    return fail("legacy recipe should still parse exposure");
  }
  if (parsed_legacy.crop_preset != QStringLiteral("4x5")) {
    return fail("legacy recipe should still parse crop preset");
  }
  if (!parsed_legacy.linear_mask_enabled || !almost_equal(parsed_legacy.linear_mask_position, 0.62)) {
    return fail("legacy recipe should still parse linear mask settings");
  }
  if (parsed_legacy.hsl_channel != QStringLiteral("Blues")) {
    return fail("legacy recipe should still parse color mixer channel");
  }

  const QString structured_recipe = photo_editor::desktop::serialize_edit_recipe_json(parsed_legacy);
  const auto structured_document = QJsonDocument::fromJson(structured_recipe.toUtf8());
  if (!structured_document.isObject()) {
    return fail("structured recipe output should be valid JSON");
  }

  const auto root = structured_document.object();
  if (root.value(QStringLiteral("recipeVersion")).toInt() != 3) {
    return fail("structured recipe output should bump the recipe version");
  }
  if (!root.value(QStringLiteral("globalAdjustments")).isObject()) {
    return fail("structured recipe output should contain global adjustments");
  }
  if (!root.value(QStringLiteral("curves")).isObject()) {
    return fail("structured recipe output should contain curve data");
  }
  if (!root.value(QStringLiteral("cropTransform")).isObject()) {
    return fail("structured recipe output should contain crop data");
  }

  const auto mask_stack = root.value(QStringLiteral("maskStack")).toArray();
  if (mask_stack.size() != 1 || !mask_stack.at(0).isObject()) {
    return fail("structured recipe output should serialize the current linear mask into maskStack");
  }

  const auto reparsed_structured = photo_editor::desktop::parse_edit_recipe_json(structured_recipe);
  if (!almost_equal(reparsed_structured.linear_mask_rotation, parsed_legacy.linear_mask_rotation) ||
      !almost_equal(reparsed_structured.crop_zoom, parsed_legacy.crop_zoom) ||
      reparsed_structured.crop_preset != parsed_legacy.crop_preset) {
    return fail("structured recipe should round-trip without losing current edit state");
  }

  EditRecipeState default_state;
  const QString default_recipe = photo_editor::desktop::serialize_edit_recipe_json(default_state);
  const auto default_document = QJsonDocument::fromJson(default_recipe.toUtf8()).object();
  if (!default_document.value(QStringLiteral("overlayItems")).isArray()) {
    return fail("structured recipe output should include overlayItems placeholder");
  }
  if (!default_document.value(QStringLiteral("guideSettings")).isObject()) {
    return fail("structured recipe output should include guide settings placeholder");
  }
  if (!default_document.value(QStringLiteral("maskStack")).toArray().isEmpty()) {
    return fail("default recipe should not serialize an empty linear mask into maskStack");
  }

  EditRecipeState point_curve_state;
  point_curve_state.master_curve_points = {
      point(0.0, 0.0),
      point(0.3, 0.18),
      point(0.7, 0.86),
      point(1.0, 1.0),
  };
  point_curve_state.red_curve_points = {
      point(0.0, 0.0),
      point(0.5, 0.65),
      point(1.0, 1.0),
  };

  const auto point_curve_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(point_curve_state).toUtf8()).object();
  const auto serialized_channels = point_curve_document
                                       .value(QStringLiteral("curves"))
                                       .toObject()
                                       .value(QStringLiteral("channels"))
                                       .toArray();
  if (serialized_channels.size() != 2) {
    return fail("structured recipe should serialize non-empty point curve channels");
  }

  const auto reparsed_point_curve =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(point_curve_document).toJson(QJsonDocument::Compact)));
  if (reparsed_point_curve.master_curve_points.size() != 4 || reparsed_point_curve.red_curve_points.size() != 3) {
    return fail("structured recipe should round-trip point curve control points");
  }

  EditRecipeState crop_rect_state;
  crop_rect_state.crop_preset = QStringLiteral("3x2");
  crop_rect_state.crop_zoom = 1.35;
  crop_rect_state.crop_rect_x = 0.12;
  crop_rect_state.crop_rect_y = 0.08;
  crop_rect_state.crop_rect_width = 0.71;
  crop_rect_state.crop_rect_height = 0.48;

  const auto crop_rect_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(crop_rect_state).toUtf8()).object();
  const auto crop_transform = crop_rect_document.value(QStringLiteral("cropTransform")).toObject();
  if (!almost_equal(crop_transform.value(QStringLiteral("x")).toDouble(), crop_rect_state.crop_rect_x) ||
      !almost_equal(crop_transform.value(QStringLiteral("width")).toDouble(), crop_rect_state.crop_rect_width) ||
      crop_transform.value(QStringLiteral("preset")).toString() != crop_rect_state.crop_preset) {
    return fail("structured recipe should serialize explicit crop rectangle geometry");
  }

  const auto reparsed_crop_rect =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(crop_rect_document).toJson(QJsonDocument::Compact)));
  if (!almost_equal(reparsed_crop_rect.crop_rect_y, crop_rect_state.crop_rect_y) ||
      !almost_equal(reparsed_crop_rect.crop_rect_height, crop_rect_state.crop_rect_height) ||
      reparsed_crop_rect.crop_preset != crop_rect_state.crop_preset) {
    return fail("structured recipe should round-trip explicit crop rectangle geometry");
  }

  EditRecipeState radial_mask_state;
  radial_mask_state.radial_mask_enabled = true;
  radial_mask_state.radial_mask_exposure = 0.55;
  radial_mask_state.radial_mask_temperature = -18.0;
  radial_mask_state.radial_mask_feather = 0.38;
  radial_mask_state.radial_mask_center_x = 0.42;
  radial_mask_state.radial_mask_center_y = 0.61;
  radial_mask_state.radial_mask_radius_x = 0.31;
  radial_mask_state.radial_mask_radius_y = 0.22;
  radial_mask_state.radial_mask_rotation = -27.0;

  const auto radial_mask_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(radial_mask_state).toUtf8()).object();
  const auto radial_mask_stack = radial_mask_document.value(QStringLiteral("maskStack")).toArray();
  if (radial_mask_stack.size() != 1 ||
      radial_mask_stack.at(0).toObject().value(QStringLiteral("kind")).toString() != QStringLiteral("radial")) {
    return fail("structured recipe should serialize a radial mask entry");
  }

  const auto reparsed_radial_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(radial_mask_document).toJson(QJsonDocument::Compact)));
  if (!reparsed_radial_mask.radial_mask_enabled ||
      !almost_equal(reparsed_radial_mask.radial_mask_center_x, radial_mask_state.radial_mask_center_x) ||
      !almost_equal(reparsed_radial_mask.radial_mask_radius_y, radial_mask_state.radial_mask_radius_y) ||
      !almost_equal(reparsed_radial_mask.radial_mask_rotation, radial_mask_state.radial_mask_rotation)) {
    return fail("structured recipe should round-trip radial mask geometry");
  }

  return EXIT_SUCCESS;
}
