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

QVariantMap brush_point(double x, double y, double size, double flow) {
  auto point_map = point(x, y);
  point_map.insert(QStringLiteral("size"), size);
  point_map.insert(QStringLiteral("flow"), flow);
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

  EditRecipeState stacked_mask_state;
  QVariantMap base_linear_mask;
  base_linear_mask.insert(QStringLiteral("id"), QStringLiteral("linear-base"));
  base_linear_mask.insert(QStringLiteral("name"), QStringLiteral("Subject"));
  base_linear_mask.insert(QStringLiteral("kind"), QStringLiteral("linear"));
  base_linear_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  base_linear_mask.insert(QStringLiteral("enabled"), true);
  base_linear_mask.insert(QStringLiteral("exposure"), 0.65);
  base_linear_mask.insert(QStringLiteral("temperature"), 9.0);
  base_linear_mask.insert(QStringLiteral("feather"), 0.33);
  base_linear_mask.insert(QStringLiteral("position"), 0.58);
  base_linear_mask.insert(QStringLiteral("rotation"), 12.0);

  QVariantMap subtract_radial_mask;
  subtract_radial_mask.insert(QStringLiteral("id"), QStringLiteral("radial-cutout"));
  subtract_radial_mask.insert(QStringLiteral("name"), QStringLiteral("Exclude Window"));
  subtract_radial_mask.insert(QStringLiteral("kind"), QStringLiteral("radial"));
  subtract_radial_mask.insert(QStringLiteral("operation"), QStringLiteral("subtract"));
  subtract_radial_mask.insert(QStringLiteral("enabled"), true);
  subtract_radial_mask.insert(QStringLiteral("exposure"), 0.65);
  subtract_radial_mask.insert(QStringLiteral("temperature"), 9.0);
  subtract_radial_mask.insert(QStringLiteral("feather"), 0.18);
  subtract_radial_mask.insert(QStringLiteral("centerX"), 0.54);
  subtract_radial_mask.insert(QStringLiteral("centerY"), 0.41);
  subtract_radial_mask.insert(QStringLiteral("radiusX"), 0.16);
  subtract_radial_mask.insert(QStringLiteral("radiusY"), 0.12);
  subtract_radial_mask.insert(QStringLiteral("rotation"), -8.0);

  stacked_mask_state.mask_stack = {base_linear_mask, subtract_radial_mask};
  const auto stacked_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(stacked_mask_state).toUtf8()).object();
  const auto stacked_mask_array = stacked_document.value(QStringLiteral("maskStack")).toArray();
  if (stacked_mask_array.size() != 2) {
    return fail("structured recipe should serialize the full ordered mask stack");
  }
  if (stacked_mask_array.at(1).toObject().value(QStringLiteral("operation")).toString() !=
      QStringLiteral("subtract")) {
    return fail("structured recipe should preserve mask combine operations");
  }

  const auto reparsed_stack =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(stacked_document).toJson(QJsonDocument::Compact)));
  if (reparsed_stack.mask_stack.size() != 2 ||
      reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("name")).toString() != QStringLiteral("Subject") ||
      reparsed_stack.mask_stack.at(1).toMap().value(QStringLiteral("operation")).toString() != QStringLiteral("subtract")) {
    return fail("structured recipe should round-trip stack metadata and ordering");
  }

  if (!reparsed_stack.linear_mask_enabled ||
      !almost_equal(reparsed_stack.linear_mask_exposure, 0.65) ||
      !almost_equal(reparsed_stack.radial_mask_center_x, 0.54)) {
    return fail("structured recipe should still hydrate legacy compatibility fields from the stack");
  }

  EditRecipeState mixed_mask_state;
  QVariantMap brush_mask;
  brush_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  brush_mask.insert(QStringLiteral("name"), QStringLiteral("Painted Subject"));
  brush_mask.insert(QStringLiteral("brushSize"), 0.12);
  brush_mask.insert(QStringLiteral("brushFlow"), 0.85);
  brush_mask.insert(QStringLiteral("brushPoints"), QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.42}, {QStringLiteral("y"), 0.33}, {QStringLiteral("size"), 0.12}, {QStringLiteral("flow"), 0.85}},
      QVariantMap{{QStringLiteral("x"), 0.48}, {QStringLiteral("y"), 0.37}, {QStringLiteral("size"), 0.10}, {QStringLiteral("flow"), 0.75}},
  });

  QVariantMap luminance_mask;
  luminance_mask.insert(QStringLiteral("kind"), QStringLiteral("luminance"));
  luminance_mask.insert(QStringLiteral("name"), QStringLiteral("Highlights"));
  luminance_mask.insert(QStringLiteral("luminanceMin"), 0.62);
  luminance_mask.insert(QStringLiteral("luminanceMax"), 0.95);
  luminance_mask.insert(QStringLiteral("rangeFeather"), 0.08);

  QVariantMap color_mask;
  color_mask.insert(QStringLiteral("kind"), QStringLiteral("color"));
  color_mask.insert(QStringLiteral("name"), QStringLiteral("Skin Range"));
  color_mask.insert(QStringLiteral("hueCenter"), 0.07);
  color_mask.insert(QStringLiteral("hueRange"), 0.09);
  color_mask.insert(QStringLiteral("saturationMin"), 0.18);
  color_mask.insert(QStringLiteral("saturationMax"), 0.82);
  color_mask.insert(QStringLiteral("rangeFeather"), 0.05);

  mixed_mask_state.mask_stack = {brush_mask, luminance_mask, color_mask};
  const auto mixed_mask_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(mixed_mask_state).toUtf8()).object();
  const auto mixed_mask_array = mixed_mask_document.value(QStringLiteral("maskStack")).toArray();
  if (mixed_mask_array.size() != 3) {
    return fail("structured recipe should serialize brush and range masks");
  }
  if (!mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPoints")).isArray()) {
    return fail("brush mask serialization should retain brush points");
  }
  if (!mixed_mask_array.at(2).toObject().value(QStringLiteral("constraints")).toObject().contains(QStringLiteral("hueCenter"))) {
    return fail("color range serialization should retain range constraints");
  }

  const auto reparsed_mixed_masks =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(mixed_mask_document).toJson(QJsonDocument::Compact)));
  if (reparsed_mixed_masks.mask_stack.size() != 3 ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().size() != 2 ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(1).toMap().value(QStringLiteral("luminanceMin")).toDouble(), 0.62) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(2).toMap().value(QStringLiteral("hueRange")).toDouble(), 0.09)) {
    return fail("structured recipe should round-trip brush and range mask data");
  }

  EditRecipeState subject_mask_state;
  QVariantMap subject_mask;
  subject_mask.insert(QStringLiteral("id"), QStringLiteral("subject-ai"));
  subject_mask.insert(QStringLiteral("name"), QStringLiteral("AI Subject"));
  subject_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  subject_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  subject_mask.insert(QStringLiteral("enabled"), true);
  subject_mask.insert(QStringLiteral("exposure"), 0.45);
  subject_mask.insert(QStringLiteral("temperature"), -6.0);
  subject_mask.insert(QStringLiteral("hueShift"), 24.0);
  subject_mask.insert(QStringLiteral("saturationShift"), 18.0);
  subject_mask.insert(QStringLiteral("colorizeHue"), 0.02);
  subject_mask.insert(QStringLiteral("colorizeStrength"), 72.0);
  subject_mask.insert(QStringLiteral("removeStrength"), 55.0);
  subject_mask.insert(QStringLiteral("selectionInvert"), true);
  subject_mask.insert(QStringLiteral("selectionExpand"), 18.0);
  subject_mask.insert(QStringLiteral("feather"), 0.5);
  subject_mask.insert(QStringLiteral("segmentationData"), QByteArray::fromHex("00ff7f3f"));
  subject_mask.insert(QStringLiteral("segmentationWidth"), 2);
  subject_mask.insert(QStringLiteral("segmentationHeight"), 2);
  subject_mask_state.mask_stack = {subject_mask};

  const auto subject_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(subject_mask_state).toUtf8()).object();
  const auto subject_geometry = subject_document
                                    .value(QStringLiteral("maskStack"))
                                    .toArray()
                                    .at(0)
                                    .toObject()
                                    .value(QStringLiteral("geometry"))
                                    .toObject();
  if (!subject_geometry.contains(QStringLiteral("segmentationData")) ||
      subject_geometry.value(QStringLiteral("segmentationWidth")).toInt() != 2 ||
      subject_geometry.value(QStringLiteral("segmentationHeight")).toInt() != 2) {
    return fail("structured recipe should serialize AI subject segmentation payloads");
  }
  const auto subject_adjustments = subject_document
                                       .value(QStringLiteral("maskStack"))
                                       .toArray()
                                       .at(0)
                                       .toObject()
                                       .value(QStringLiteral("adjustments"))
                                       .toObject();
  if (!almost_equal(subject_adjustments.value(QStringLiteral("hueShift")).toDouble(), 24.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("saturationShift")).toDouble(), 18.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("colorizeStrength")).toDouble(), 72.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("removeStrength")).toDouble(), 55.0) ||
      !subject_adjustments.value(QStringLiteral("selectionInvert")).toBool() ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionExpand")).toDouble(), 18.0)) {
    return fail("structured recipe should serialize local recolor adjustments");
  }

  const auto reparsed_subject_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(subject_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_subject = reparsed_subject_mask.mask_stack.at(0).toMap();
  if (reparsed_subject_mask.mask_stack.size() != 1 ||
      reparsed_subject.value(QStringLiteral("kind")).toString() != QStringLiteral("subject") ||
      reparsed_subject.value(QStringLiteral("segmentationData")).toByteArray() != QByteArray::fromHex("00ff7f3f") ||
      !almost_equal(reparsed_subject.value(QStringLiteral("hueShift")).toDouble(), 24.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("saturationShift")).toDouble(), 18.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("colorizeHue")).toDouble(), 0.02) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("colorizeStrength")).toDouble(), 72.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("removeStrength")).toDouble(), 55.0) ||
      !reparsed_subject.value(QStringLiteral("selectionInvert")).toBool() ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionExpand")).toDouble(), 18.0) ||
      reparsed_subject.value(QStringLiteral("segmentationWidth")).toInt() != 2 ||
      reparsed_subject.value(QStringLiteral("segmentationHeight")).toInt() != 2) {
    return fail("structured recipe should round-trip AI subject segmentation payloads");
  }

  EditRecipeState object_mask_state;
  QVariantMap object_mask = subject_mask;
  object_mask.insert(QStringLiteral("id"), QStringLiteral("object-ai"));
  object_mask.insert(QStringLiteral("name"), QStringLiteral("AI Object"));
  object_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  object_mask_state.mask_stack = {object_mask};
  const auto reparsed_object_mask =
      photo_editor::desktop::parse_edit_recipe_json(photo_editor::desktop::serialize_edit_recipe_json(object_mask_state));
  if (reparsed_object_mask.mask_stack.size() != 1 ||
      reparsed_object_mask.mask_stack.at(0).toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("object")) {
    return fail("structured recipe should round-trip AI object mask kinds");
  }

  EditRecipeState polygon_mask_state;
  QVariantMap polygon_mask;
  polygon_mask.insert(QStringLiteral("id"), QStringLiteral("polygon-select"));
  polygon_mask.insert(QStringLiteral("name"), QStringLiteral("Polygon Select"));
  polygon_mask.insert(QStringLiteral("kind"), QStringLiteral("polygon"));
  polygon_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  polygon_mask.insert(QStringLiteral("enabled"), true);
  polygon_mask.insert(QStringLiteral("feather"), 0.04);
  polygon_mask.insert(
      QStringLiteral("polygonPoints"),
      QVariantList{
          point(0.1, 0.2),
          point(0.8, 0.18),
          point(0.75, 0.84),
          point(0.16, 0.80),
      });
  polygon_mask_state.mask_stack = {polygon_mask};
  const auto polygon_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(polygon_mask_state).toUtf8()).object();
  const auto polygon_geometry = polygon_document
                                    .value(QStringLiteral("maskStack"))
                                    .toArray()
                                    .at(0)
                                    .toObject()
                                    .value(QStringLiteral("geometry"))
                                    .toObject();
  if (polygon_geometry.value(QStringLiteral("polygonPoints")).toArray().size() != 4) {
    return fail("structured recipe should serialize polygon selection points");
  }
  const auto reparsed_polygon_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(polygon_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_polygon = reparsed_polygon_mask.mask_stack.at(0).toMap();
  if (reparsed_polygon.value(QStringLiteral("kind")).toString() != QStringLiteral("polygon") ||
      reparsed_polygon.value(QStringLiteral("polygonPoints")).toList().size() != 4) {
    return fail("structured recipe should round-trip polygon mask geometry");
  }

  EditRecipeState clone_brush_state;
  QVariantMap clone_brush;
  clone_brush.insert(QStringLiteral("id"), QStringLiteral("clone-brush"));
  clone_brush.insert(QStringLiteral("name"), QStringLiteral("Clone Stamp"));
  clone_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  clone_brush.insert(QStringLiteral("operation"), QStringLiteral("add"));
  clone_brush.insert(QStringLiteral("retouchMode"), QStringLiteral("clone"));
  clone_brush.insert(QStringLiteral("cloneSourceSet"), true);
  clone_brush.insert(QStringLiteral("cloneSourceX"), 0.24);
  clone_brush.insert(QStringLiteral("cloneSourceY"), 0.38);
  clone_brush.insert(QStringLiteral("cloneOffsetX"), -0.15);
  clone_brush.insert(QStringLiteral("cloneOffsetY"), 0.09);
  clone_brush.insert(QStringLiteral("cloneScale"), 1.35);
  clone_brush.insert(QStringLiteral("cloneRotation"), 24.0);
  clone_brush.insert(QStringLiteral("brushSize"), 0.07);
  clone_brush.insert(QStringLiteral("brushFlow"), 0.88);
  clone_brush.insert(
      QStringLiteral("brushPoints"),
      QVariantList{
          brush_point(0.42, 0.44, 0.07, 0.88),
          brush_point(0.48, 0.50, 0.07, 0.88),
      });
  clone_brush_state.mask_stack = {clone_brush};
  const auto clone_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(clone_brush_state).toUtf8()).object();
  const auto clone_geometry = clone_document
                                  .value(QStringLiteral("maskStack"))
                                  .toArray()
                                  .at(0)
                                  .toObject()
                                  .value(QStringLiteral("geometry"))
                                  .toObject();
  if (clone_geometry.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("clone") ||
      !clone_geometry.value(QStringLiteral("cloneSourceSet")).toBool() ||
      !almost_equal(clone_geometry.value(QStringLiteral("cloneScale")).toDouble(), 1.35) ||
      clone_geometry.value(QStringLiteral("brushPoints")).toArray().size() != 2) {
    return fail("structured recipe should serialize clone brush metadata");
  }
  const auto reparsed_clone_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(clone_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_clone = reparsed_clone_mask.mask_stack.at(0).toMap();
  if (reparsed_clone.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("clone") ||
      !reparsed_clone.value(QStringLiteral("cloneSourceSet")).toBool() ||
      !almost_equal(reparsed_clone.value(QStringLiteral("cloneOffsetX")).toDouble(), -0.15) ||
      !almost_equal(reparsed_clone.value(QStringLiteral("cloneScale")).toDouble(), 1.35) ||
      !almost_equal(reparsed_clone.value(QStringLiteral("cloneRotation")).toDouble(), 24.0) ||
      reparsed_clone.value(QStringLiteral("brushPoints")).toList().size() != 2) {
    return fail("structured recipe should round-trip clone brush metadata");
  }

  EditRecipeState optics_state;
  optics_state.sharpening = 35.0;
  optics_state.lens_distortion = -18.0;
  optics_state.lens_vignette = 26.0;
  optics_state.chromatic_aberration = 42.0;
  const auto optics_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(optics_state).toUtf8()).object();
  const auto optics_object = optics_document.value(QStringLiteral("optics")).toObject();
  if (!almost_equal(optics_object.value(QStringLiteral("lensDistortion")).toDouble(), -18.0) ||
      !almost_equal(optics_object.value(QStringLiteral("lensVignette")).toDouble(), 26.0) ||
      !almost_equal(optics_object.value(QStringLiteral("chromaticAberration")).toDouble(), 42.0)) {
    return fail("structured recipe should serialize optics controls");
  }

  const auto reparsed_optics =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(optics_document).toJson(QJsonDocument::Compact)));
  if (!almost_equal(reparsed_optics.lens_distortion, optics_state.lens_distortion) ||
      !almost_equal(reparsed_optics.lens_vignette, optics_state.lens_vignette) ||
      !almost_equal(reparsed_optics.chromatic_aberration, optics_state.chromatic_aberration)) {
    return fail("structured recipe should round-trip optics controls");
  }

  EditRecipeState base_state;
  base_state.exposure = -0.25;
  base_state.temperature = -12.0;
  base_state.crop_preset = QStringLiteral("Original");
  base_state.linear_mask_enabled = false;
  base_state.texture = 8.0;
  base_state.lens_distortion = 0.0;

  EditRecipeState source_state;
  source_state.exposure = 1.1;
  source_state.temperature = 18.0;
  source_state.crop_preset = QStringLiteral("4x5");
  source_state.linear_mask_enabled = true;
  source_state.linear_mask_exposure = 0.4;
  source_state.texture = 26.0;
  source_state.lens_distortion = 22.0;

  const auto merged_color_and_masks =
      photo_editor::desktop::merge_edit_recipe_state_by_groups(
          base_state,
          source_state,
          {QStringLiteral("color"), QStringLiteral("masks")});
  if (!almost_equal(merged_color_and_masks.temperature, 18.0) ||
      !merged_color_and_masks.linear_mask_enabled ||
      !almost_equal(merged_color_and_masks.exposure, -0.25) ||
      merged_color_and_masks.crop_preset != QStringLiteral("Original")) {
    return fail("group merge should only update the requested adjustment groups");
  }

  const auto merged_all =
      photo_editor::desktop::merge_edit_recipe_state_by_groups(base_state, source_state, {});
  if (!almost_equal(merged_all.exposure, 1.1) ||
      merged_all.crop_preset != QStringLiteral("4x5") ||
      !merged_all.linear_mask_enabled ||
      !almost_equal(merged_all.texture, 26.0) ||
      !almost_equal(merged_all.lens_distortion, 22.0)) {
    return fail("empty group selection should merge the full edit recipe");
  }

  return EXIT_SUCCESS;
}
