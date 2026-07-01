#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "edit_recipe_state.h"

namespace {

using photo_editor::desktop::EditRecipeState;
using photo_editor::desktop::EditRecipeValidationResult;

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

bool errors_contain(const QStringList& errors, const QString& expected) {
  for (const auto& error : errors) {
    if (error.contains(expected)) {
      return true;
    }
  }
  return false;
}

bool errors_contain(const EditRecipeValidationResult& result, const QString& expected) {
  return errors_contain(result.errors, expected);
}

QVariantMap required_map(const QVariantMap& parent, const QString& key) {
  return parent.value(key).toMap();
}

QString compact_json(const QJsonObject& object) {
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

int main() {
  const QString legacy_recipe =
      QStringLiteral(
          R"({"recipeVersion":2,"exposure":0.75,"brightness":18,"hdrLocalTone":32,"filmicShoulder":22,"filmicToe":-14,"adaptiveContrast":34,"levelsBlackPoint":16,"levelsGamma":-12,"levelsWhitePoint":9,"zoneDeepShadows":-8,"zoneShadows":9,"zoneMidtones":10,"zoneLights":-11,"zoneHighlights":12,"curveHighlights":20,"cropZoom":1.4,"cropPreset":"4x5","linearMaskEnabled":true,"linearMaskExposure":0.35,"linearMaskTemperature":12,"linearMaskFeather":0.3,"linearMaskPosition":0.62,"linearMaskRotation":18,"hslChannel":"Blues","hueAdjustment":14,"saturationAdjustment":-10,"luminanceAdjustment":9})");

  const auto parsed_legacy = photo_editor::desktop::parse_edit_recipe_json(legacy_recipe);
  if (!almost_equal(parsed_legacy.exposure, 0.75)) {
    return fail("legacy recipe should still parse exposure");
  }
  if (!almost_equal(parsed_legacy.brightness, 18.0)) {
    return fail("legacy recipe should still parse brightness");
  }
  if (!almost_equal(parsed_legacy.hdr_local_tone_mapping, 32.0)) {
    return fail("legacy recipe should still parse HDR local tone mapping");
  }
  if (!almost_equal(parsed_legacy.filmic_shoulder, 22.0) ||
      !almost_equal(parsed_legacy.filmic_toe, -14.0)) {
    return fail("legacy recipe should still parse filmic shoulder and toe");
  }
  if (!almost_equal(parsed_legacy.adaptive_contrast, 34.0)) {
    return fail("legacy recipe should still parse adaptive contrast");
  }
  if (!almost_equal(parsed_legacy.levels_black_point, 16.0) ||
      !almost_equal(parsed_legacy.levels_gamma, -12.0) ||
      !almost_equal(parsed_legacy.levels_white_point, 9.0)) {
    return fail("legacy recipe should still parse levels controls");
  }
  if (!almost_equal(parsed_legacy.zone_deep_shadows, -8.0) ||
      !almost_equal(parsed_legacy.zone_shadows, 9.0) ||
      !almost_equal(parsed_legacy.zone_midtones, 10.0) ||
      !almost_equal(parsed_legacy.zone_lights, -11.0) ||
      !almost_equal(parsed_legacy.zone_highlights, 12.0)) {
    return fail("legacy recipe should still parse zone controls");
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

  struct MatteCleanupDefaultCase final {
    QString kind;
    QString profile;
    double smooth = 0.0;
    double fine_edge = 0.0;
    double edge_contrast = 0.0;
    double decontaminate = 0.0;
    double feather = 0.0;
    double harden = 0.0;
    double despeckle = 0.0;
    double fill_holes = 0.0;
    double remove_islands = 0.0;
    double hole_threshold = 0.004;
    double island_threshold = 0.0015;
  };
  const std::vector<MatteCleanupDefaultCase> matte_cleanup_default_cases = {
      {QStringLiteral("hair"), QStringLiteral("hair-soft-detail"), 6.0, 65.0, 0.0, 8.0, 10.0, 0.0, 12.0, 5.0, 8.0, 0.0025, 0.0010},
      {QStringLiteral("sky"), QStringLiteral("sky-large-region"), 4.0, 0.0, 10.0, 0.0, 4.0, 0.0, 8.0, 14.0, 18.0, 0.0100, 0.0040},
      {QStringLiteral("object"), QStringLiteral("object-hard-edge"), 3.0, 0.0, 8.0, 0.0, 4.0, 0.0, 6.0, 8.0, 10.0, 0.0060, 0.0025},
      {QStringLiteral("subject"), QStringLiteral("subject-object"), 3.0, 0.0, 4.0, 0.0, 4.0, 0.0, 6.0, 8.0, 10.0, 0.0060, 0.0025},
      {QStringLiteral("brush"), QStringLiteral("brush-painted"), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 3.0, 2.0, 2.0, 0.0040, 0.0015},
      {QStringLiteral("polygon"), QStringLiteral("manual-shape"), 2.0, 0.0, 6.0, 0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0040, 0.0015},
      {QStringLiteral("lasso"), QStringLiteral("manual-shape"), 2.0, 0.0, 6.0, 0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0040, 0.0015},
      {QStringLiteral("alpha"), QStringLiteral("alpha-import"), 3.0, 0.0, 8.0, 0.0, 4.0, 0.0, 10.0, 10.0, 12.0, 0.0060, 0.0030},
  };
  for (const auto& expected : matte_cleanup_default_cases) {
    const auto mask = photo_editor::desktop::default_mask_definition(expected.kind, 1);
    if (mask.value(QStringLiteral("matteCleanupSchemaVersion")).toInt() != 1 ||
        mask.value(QStringLiteral("matteCleanupProfile")).toString() != expected.profile ||
        !mask.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool() ||
        !almost_equal(mask.value(QStringLiteral("selectionSmooth")).toDouble(), expected.smooth) ||
        !almost_equal(mask.value(QStringLiteral("selectionFineEdge")).toDouble(), expected.fine_edge) ||
        !almost_equal(mask.value(QStringLiteral("selectionEdgeContrast")).toDouble(), expected.edge_contrast) ||
        !almost_equal(mask.value(QStringLiteral("selectionDecontaminate")).toDouble(), expected.decontaminate) ||
        !almost_equal(mask.value(QStringLiteral("selectionFeather")).toDouble(), expected.feather) ||
        !almost_equal(mask.value(QStringLiteral("selectionHarden")).toDouble(), expected.harden) ||
        !almost_equal(mask.value(QStringLiteral("selectionDespeckle")).toDouble(), expected.despeckle) ||
        !almost_equal(mask.value(QStringLiteral("selectionFillHoles")).toDouble(), expected.fill_holes) ||
        !almost_equal(mask.value(QStringLiteral("selectionRemoveIslands")).toDouble(), expected.remove_islands) ||
        !almost_equal(mask.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble(), expected.hole_threshold) ||
        !almost_equal(mask.value(QStringLiteral("generatedMaskIslandRemovalThreshold")).toDouble(), expected.island_threshold)) {
      return fail("source-type matte cleanup defaults should initialize every new mask");
    }
    QVariantMap sparse_mask;
    sparse_mask.insert(QStringLiteral("kind"), expected.kind);
    sparse_mask.insert(QStringLiteral("id"), expected.kind + QStringLiteral("-sparse"));
    const auto normalized = photo_editor::desktop::normalized_mask_definition(sparse_mask);
    if (normalized.value(QStringLiteral("matteCleanupProfile")).toString() != expected.profile ||
        !almost_equal(normalized.value(QStringLiteral("selectionDespeckle")).toDouble(), expected.despeckle) ||
        !almost_equal(normalized.value(QStringLiteral("selectionFillHoles")).toDouble(), expected.fill_holes) ||
        !almost_equal(normalized.value(QStringLiteral("selectionRemoveIslands")).toDouble(), expected.remove_islands) ||
        !almost_equal(normalized.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble(), expected.hole_threshold)) {
      return fail("normalizing sparse masks should apply source-type matte cleanup fallbacks");
    }
  }

  const QString structured_recipe = photo_editor::desktop::serialize_edit_recipe_json(parsed_legacy);
  const auto structured_document = QJsonDocument::fromJson(structured_recipe.toUtf8());
  if (!structured_document.isObject()) {
    return fail("structured recipe output should be valid JSON");
  }

  const auto root = structured_document.object();
  if (root.value(QStringLiteral("recipeVersion")).toInt() != 4) {
    return fail("structured recipe output should bump the recipe version");
  }
  if (root.value(QStringLiteral("maskSetSchemaVersion")).toInt() != 2) {
    return fail("structured recipe output should declare the typed mask-set schema version");
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
  const auto mask_sets = root.value(QStringLiteral("maskSets")).toArray();
  if (mask_sets.size() != 1 ||
      mask_sets.at(0).toObject().value(QStringLiteral("sources")).toArray().size() != 1) {
    return fail("structured recipe output should also group selection masks into maskSets");
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
  if (!default_document.value(QStringLiteral("presetApplications")).isArray()) {
    return fail("structured recipe output should include preset application history");
  }
  if (!default_document.value(QStringLiteral("photoLayers")).isArray() ||
      !default_document.value(QStringLiteral("layers")).isArray()) {
    return fail("structured recipe output should include distinct photo layer placeholders");
  }
  if (!default_document.value(QStringLiteral("history")).isObject() ||
      default_document.value(QStringLiteral("history")).toObject().value(QStringLiteral("activeBranchId")).toString() !=
          QStringLiteral("main") ||
      !default_document.value(QStringLiteral("history")).toObject().value(QStringLiteral("snapshots")).isArray()) {
    return fail("structured recipe output should include recipe history state");
  }
  if (!default_document.value(QStringLiteral("guideSettings")).isObject()) {
    return fail("structured recipe output should include guide settings placeholder");
  }
  const auto default_raw_adjustments = default_document.value(QStringLiteral("rawAdjustments")).toObject();
  const auto default_global_adjustments = default_document.value(QStringLiteral("globalAdjustments")).toObject();
  const auto default_color_pipeline = default_document.value(QStringLiteral("colorPipeline")).toObject();
  const auto default_curves = default_document.value(QStringLiteral("curves")).toObject();
  const auto default_color_mixer = default_document.value(QStringLiteral("colorMixer")).toObject();
  const auto default_color_grading = default_document.value(QStringLiteral("colorGrading")).toObject();
  const auto default_crop_transform = default_document.value(QStringLiteral("cropTransform")).toObject();
  const auto default_output_transform = default_document.value(QStringLiteral("outputTransform")).toObject();
  const auto default_optics = default_document.value(QStringLiteral("optics")).toObject();
  if (default_raw_adjustments.size() != 30 || default_global_adjustments.size() != 32 ||
      default_color_pipeline.size() != 3 || default_optics.size() != 3) {
    return fail("default recipe should expose the complete raw/global/pipeline/optics adjustment groups");
  }
  if (default_curves.value(QStringLiteral("parametric")).toObject().size() != 4 ||
      !default_curves.value(QStringLiteral("channels")).toArray().isEmpty()) {
    return fail("default recipe should expose identity parametric curves and no point-curve channels");
  }
  if (default_color_mixer.size() != 5 ||
      default_color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("OKLab") ||
      default_color_mixer.value(QStringLiteral("channel")).toString() != QStringLiteral("Reds")) {
    return fail("default recipe should expose only identity color mixer fields");
  }
  if (default_color_grading.value(QStringLiteral("shadows")).toObject().size() != 3 ||
      default_color_grading.value(QStringLiteral("midtones")).toObject().size() != 3 ||
      default_color_grading.value(QStringLiteral("highlights")).toObject().size() != 3) {
    return fail("default recipe should expose identity color grading ranges");
  }
  if (default_crop_transform.size() != 16 ||
      default_crop_transform.value(QStringLiteral("preset")).toString() != QStringLiteral("Original") ||
      !almost_equal(default_crop_transform.value(QStringLiteral("scale")).toDouble(), 1.0)) {
    return fail("default recipe should expose identity crop and transform state");
  }
  if (default_output_transform.size() != 7 ||
      !almost_equal(default_output_transform.value(QStringLiteral("headroom")).toDouble(), 1.0)) {
    return fail("default recipe should expose identity output transform state");
  }
  if (default_document.value(QStringLiteral("outputTransform"))
          .toObject()
          .value(QStringLiteral("colorSpace"))
          .toString() != QStringLiteral("sRGB")) {
    return fail("structured recipe output should include the default output color space");
  }
  if (default_document.value(QStringLiteral("colorPipeline"))
          .toObject()
          .value(QStringLiteral("inputProfile"))
          .toString() != QStringLiteral("Embedded")) {
    return fail("structured recipe output should include the default input color profile");
  }
  if (default_document.value(QStringLiteral("rawAdjustments"))
          .toObject()
          .value(QStringLiteral("inputTransformEnabled"))
          .toBool() != true) {
    return fail("structured recipe output should include the default input transform enable flag");
  }
  if (default_document.value(QStringLiteral("rawAdjustments"))
          .toObject()
          .value(QStringLiteral("chromaticAdaptationMethod"))
          .toString() != QStringLiteral("Bradford")) {
    return fail("structured recipe output should include the default chromatic adaptation method");
  }
  if (default_document.value(QStringLiteral("outputTransform"))
          .toObject()
          .value(QStringLiteral("workingColorSpace"))
          .toString() != QStringLiteral("Linear ProPhoto RGB")) {
    return fail("structured recipe output should include the default working color profile");
  }
  if (default_document.value(QStringLiteral("outputTransform"))
          .toObject()
          .value(QStringLiteral("renderIntent"))
          .toString() != QStringLiteral("Perceptual")) {
    return fail("structured recipe output should include the default render intent");
  }
  if (!default_document.value(QStringLiteral("maskStack")).toArray().isEmpty()) {
    return fail("default recipe should not serialize an empty linear mask into maskStack");
  }
  if (!default_document.value(QStringLiteral("maskSets")).toArray().isEmpty() ||
      !default_document.value(QStringLiteral("retouchStack")).toArray().isEmpty()) {
    return fail("default recipe should not serialize empty mask sets or retouch stacks");
  }
  if (!default_document.value(QStringLiteral("presetApplications")).toArray().isEmpty() ||
      !default_document.value(QStringLiteral("photoLayers")).toArray().isEmpty() ||
      !default_document.value(QStringLiteral("layers")).toArray().isEmpty()) {
    return fail("default recipe should not serialize preset or layer placeholders as content");
  }

  const auto valid_default_recipe = photo_editor::desktop::validate_edit_recipe_json(default_recipe);
  if (!valid_default_recipe.valid || !valid_default_recipe.errors.isEmpty()) {
    return fail("validator should accept the serialized default structured recipe");
  }
  if (photo_editor::desktop::normalized_mask_operation(QStringLiteral("exclude")) != QStringLiteral("exclude") ||
      photo_editor::desktop::normalized_mask_operation(QStringLiteral("replace")) != QStringLiteral("replace") ||
      photo_editor::desktop::normalized_mask_operation(QStringLiteral("union")) != QStringLiteral("union")) {
    return fail("mask operation normalization should accept exclude, replace, and union");
  }

  const auto valid_legacy_recipe = photo_editor::desktop::validate_edit_recipe_json(
      QStringLiteral(R"({"exposure":0.2,"contrast":12})"));
  if (!valid_legacy_recipe.valid || !valid_legacy_recipe.errors.isEmpty()) {
    return fail("validator should accept legacy recipes without schema errors");
  }

  const auto migrated_legacy_recipe = photo_editor::desktop::migrate_edit_recipe_json(legacy_recipe);
  if (!migrated_legacy_recipe.migrated || migrated_legacy_recipe.source_version != 2 ||
      migrated_legacy_recipe.target_version != photo_editor::desktop::kCurrentEditRecipeVersion ||
      !migrated_legacy_recipe.errors.isEmpty()) {
    return fail("migration should upgrade versioned legacy recipes to the current schema");
  }
  const auto migrated_legacy_document =
      QJsonDocument::fromJson(migrated_legacy_recipe.recipe_json.toUtf8()).object();
  if (migrated_legacy_document.value(QStringLiteral("recipeVersion")).toInt() !=
          photo_editor::desktop::kCurrentEditRecipeVersion ||
      !migrated_legacy_document.value(QStringLiteral("maskStack")).isArray() ||
      !migrated_legacy_document.value(QStringLiteral("outputTransform")).isObject()) {
    return fail("migration should emit a current structured recipe document");
  }
  const auto reparsed_migrated_legacy =
      photo_editor::desktop::parse_edit_recipe_json(migrated_legacy_recipe.recipe_json);
  if (!almost_equal(reparsed_migrated_legacy.exposure, parsed_legacy.exposure) ||
      !almost_equal(reparsed_migrated_legacy.linear_mask_position, parsed_legacy.linear_mask_position) ||
      reparsed_migrated_legacy.crop_preset != parsed_legacy.crop_preset) {
    return fail("migration should preserve legacy recipe values");
  }

  const auto migrated_unversioned_recipe = photo_editor::desktop::migrate_edit_recipe_json(
      QStringLiteral(R"({"exposure":0.2,"contrast":12})"));
  if (!migrated_unversioned_recipe.migrated || migrated_unversioned_recipe.source_version != 1 ||
      !migrated_unversioned_recipe.errors.isEmpty()) {
    return fail("migration should treat unversioned legacy recipes as version 1");
  }

  const auto current_recipe_migration =
      photo_editor::desktop::migrate_edit_recipe_json(default_recipe);
  if (current_recipe_migration.migrated ||
      current_recipe_migration.source_version != photo_editor::desktop::kCurrentEditRecipeVersion ||
      current_recipe_migration.recipe_json.isEmpty() ||
      !current_recipe_migration.errors.isEmpty()) {
    return fail("migration should validate and retain current recipe documents");
  }

  const auto future_recipe_migration = photo_editor::desktop::migrate_edit_recipe_json(
      QStringLiteral(R"({"recipeVersion":999})"));
  if (future_recipe_migration.errors.isEmpty() ||
      !errors_contain(future_recipe_migration.errors, QStringLiteral("newer than supported version"))) {
    return fail("migration should reject future recipe versions");
  }

  EditRecipeState diff_after_state = default_state;
  diff_after_state.exposure = 0.5;
  diff_after_state.output_color_space = QStringLiteral("Display P3");
  diff_after_state.crop_zoom = 1.25;
  const auto recipe_diff = photo_editor::desktop::diff_edit_recipe_json(
      default_recipe,
      photo_editor::desktop::serialize_edit_recipe_json(diff_after_state));
  if (!recipe_diff.valid || !recipe_diff.errors.isEmpty() ||
      !errors_contain(recipe_diff.differences, QStringLiteral("globalAdjustments.exposure")) ||
      !errors_contain(recipe_diff.differences, QStringLiteral("outputTransform.colorSpace")) ||
      !errors_contain(recipe_diff.differences, QStringLiteral("cropTransform.zoom"))) {
    return fail("recipe diff should report human-readable changed paths");
  }

  const auto equivalent_migrated_diff = photo_editor::desktop::diff_edit_recipe_json(
      legacy_recipe,
      migrated_legacy_recipe.recipe_json);
  if (!equivalent_migrated_diff.valid || !equivalent_migrated_diff.differences.isEmpty()) {
    return fail("recipe diff should compare recipes after migration to the current schema");
  }

  const auto invalid_diff = photo_editor::desktop::diff_edit_recipe_json(
      QStringLiteral("{ bad"),
      default_recipe);
  if (invalid_diff.valid ||
      !errors_contain(invalid_diff.errors, QStringLiteral("before: recipe JSON parse error"))) {
    return fail("recipe diff should report which side failed to parse");
  }

  QTemporaryDir sidecar_dir;
  if (!sidecar_dir.isValid()) {
    return fail("sidecar temporary directory should be created");
  }
  const auto sidecar_asset_path = sidecar_dir.filePath(QStringLiteral("IMG_1001.RAF"));
  const auto expected_sidecar_path =
      sidecar_dir.filePath(QStringLiteral("IMG_1001.RAF.photo-editor-recipe.json"));
  if (photo_editor::desktop::edit_recipe_sidecar_path_for_asset(sidecar_asset_path) !=
      expected_sidecar_path) {
    return fail("sidecar path should sit next to the source asset");
  }
  const auto exported_sidecar =
      photo_editor::desktop::export_edit_recipe_sidecar(sidecar_asset_path, legacy_recipe);
  if (!exported_sidecar.ok || exported_sidecar.sidecar_path != expected_sidecar_path ||
      exported_sidecar.recipe_json.isEmpty()) {
    return fail("sidecar export should atomically write a migrated recipe");
  }
  const auto imported_sidecar =
      photo_editor::desktop::import_edit_recipe_sidecar(sidecar_asset_path);
  if (!imported_sidecar.ok || imported_sidecar.recipe_json != exported_sidecar.recipe_json) {
    return fail("sidecar import should read and migrate the stored recipe");
  }
  const auto imported_sidecar_recipe =
      photo_editor::desktop::parse_edit_recipe_json(imported_sidecar.recipe_json);
  if (!almost_equal(imported_sidecar_recipe.exposure, parsed_legacy.exposure) ||
      imported_sidecar_recipe.crop_preset != parsed_legacy.crop_preset) {
    return fail("sidecar import should preserve migrated recipe values");
  }
  const auto invalid_sidecar =
      photo_editor::desktop::export_edit_recipe_sidecar(sidecar_asset_path, QStringLiteral("{ bad"));
  if (invalid_sidecar.ok || !errors_contain(invalid_sidecar.errors, QStringLiteral("parse error"))) {
    return fail("sidecar export should reject invalid recipe JSON");
  }

  const auto invalid_json = photo_editor::desktop::validate_edit_recipe_json(QStringLiteral("{ bad"));
  if (invalid_json.valid || !errors_contain(invalid_json, QStringLiteral("parse error"))) {
    return fail("validator should report JSON parse errors");
  }

  const auto invalid_root = photo_editor::desktop::validate_edit_recipe_json(QStringLiteral("[]"));
  if (invalid_root.valid || !errors_contain(invalid_root, QStringLiteral("root must be a JSON object"))) {
    return fail("validator should reject non-object recipe roots");
  }

  auto missing_output_transform_document = default_document;
  missing_output_transform_document.remove(QStringLiteral("outputTransform"));
  const auto missing_output_transform = photo_editor::desktop::validate_edit_recipe_json(
      compact_json(missing_output_transform_document));
  if (missing_output_transform.valid ||
      !errors_contain(missing_output_transform, QStringLiteral("outputTransform must be an object"))) {
    return fail("validator should report missing structured recipe objects");
  }

  auto malformed_mask_stack_document = default_document;
  malformed_mask_stack_document.insert(QStringLiteral("maskStack"), QJsonObject{});
  const auto malformed_mask_stack = photo_editor::desktop::validate_edit_recipe_json(
      compact_json(malformed_mask_stack_document));
  if (malformed_mask_stack.valid ||
      !errors_contain(malformed_mask_stack, QStringLiteral("maskStack must be an array"))) {
    return fail("validator should report malformed structured recipe arrays");
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
  point_curve_state.luma_curve_points = {
      point(0.0, 0.0),
      point(0.5, 0.70),
      point(1.0, 1.0),
  };
  point_curve_state.color_mixer_model = QStringLiteral("HSV");
  point_curve_state.hsl_channel = QStringLiteral("Reds");
  point_curve_state.mixer_channels[0].selective_cyan = 18.0;
  point_curve_state.mixer_channels[0].selective_magenta = -9.0;
  point_curve_state.mixer_channels[0].selective_yellow = 7.0;
  point_curve_state.mixer_channels[0].selective_black = 11.0;
  point_curve_state.rgb_channel_mixer.red_from_green = 22.0;
  point_curve_state.rgb_channel_mixer.green_from_blue = -14.0;
  point_curve_state.rgb_channel_mixer.blue_from_red = 18.0;
  point_curve_state.black_and_white_enabled = true;
  point_curve_state.black_and_white_mixer[0] = 24.0;
  point_curve_state.black_and_white_mixer[5] = -18.0;
  point_curve_state.color_replacement_enabled = true;
  point_curve_state.color_replacement_source_hue = 0.02;
  point_curve_state.color_replacement_target_hue = 0.38;
  point_curve_state.color_replacement_range = 42.0;
  point_curve_state.color_replacement_softness = 64.0;
  point_curve_state.color_replacement_strength = 76.0;
  point_curve_state.color_replacement_preserve_luminance = false;
  point_curve_state.color_replacement_preserve_saturation = false;
  point_curve_state.skin_tone_protection_enabled = true;
  point_curve_state.skin_tone_line_hue = 0.09;
  point_curve_state.skin_tone_protection_range = 36.0;
  point_curve_state.skin_tone_protection_strength = 88.0;
  point_curve_state.color_harmony_enabled = true;
  point_curve_state.color_harmony_mode = QStringLiteral("Triadic");
  point_curve_state.color_harmony_base_hue = 0.04;
  point_curve_state.color_harmony_strength = 62.0;

  const auto point_curve_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(point_curve_state).toUtf8()).object();
  const auto serialized_channels = point_curve_document
                                       .value(QStringLiteral("curves"))
                                       .toObject()
                                       .value(QStringLiteral("channels"))
                                       .toArray();
  if (serialized_channels.size() != 3) {
    return fail("structured recipe should serialize non-empty point curve channels");
  }

  const auto reparsed_point_curve =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(point_curve_document).toJson(QJsonDocument::Compact)));
  if (reparsed_point_curve.master_curve_points.size() != 4 ||
      reparsed_point_curve.luma_curve_points.size() != 3 ||
      reparsed_point_curve.red_curve_points.size() != 3 ||
      reparsed_point_curve.color_mixer_model != QStringLiteral("HSV") ||
      !almost_equal(reparsed_point_curve.mixer_channels[0].selective_cyan, 18.0) ||
      !almost_equal(reparsed_point_curve.mixer_channels[0].selective_magenta, -9.0) ||
      !almost_equal(reparsed_point_curve.mixer_channels[0].selective_yellow, 7.0) ||
      !almost_equal(reparsed_point_curve.mixer_channels[0].selective_black, 11.0) ||
      !almost_equal(reparsed_point_curve.rgb_channel_mixer.red_from_green, 22.0) ||
      !almost_equal(reparsed_point_curve.rgb_channel_mixer.green_from_blue, -14.0) ||
      !almost_equal(reparsed_point_curve.rgb_channel_mixer.blue_from_red, 18.0) ||
      !reparsed_point_curve.black_and_white_enabled ||
      !almost_equal(reparsed_point_curve.black_and_white_mixer[0], 24.0) ||
      !almost_equal(reparsed_point_curve.black_and_white_mixer[5], -18.0) ||
      !reparsed_point_curve.color_replacement_enabled ||
      !almost_equal(reparsed_point_curve.color_replacement_source_hue, 0.02) ||
      !almost_equal(reparsed_point_curve.color_replacement_target_hue, 0.38) ||
      !almost_equal(reparsed_point_curve.color_replacement_range, 42.0) ||
      !almost_equal(reparsed_point_curve.color_replacement_softness, 64.0) ||
      !almost_equal(reparsed_point_curve.color_replacement_strength, 76.0) ||
      reparsed_point_curve.color_replacement_preserve_luminance ||
      reparsed_point_curve.color_replacement_preserve_saturation ||
      !reparsed_point_curve.skin_tone_protection_enabled ||
      !almost_equal(reparsed_point_curve.skin_tone_line_hue, 0.09) ||
      !almost_equal(reparsed_point_curve.skin_tone_protection_range, 36.0) ||
      !almost_equal(reparsed_point_curve.skin_tone_protection_strength, 88.0) ||
      !reparsed_point_curve.color_harmony_enabled ||
      reparsed_point_curve.color_harmony_mode != QStringLiteral("Triadic") ||
      !almost_equal(reparsed_point_curve.color_harmony_base_hue, 0.04) ||
      !almost_equal(reparsed_point_curve.color_harmony_strength, 62.0)) {
    return fail("structured recipe should round-trip point curves and color mixer model");
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

  EditRecipeState output_transform_state;
  output_transform_state.output_color_space = QStringLiteral("Display P3");
  const auto output_transform_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(output_transform_state).toUtf8()).object();
  if (output_transform_document.value(QStringLiteral("outputTransform"))
          .toObject()
          .value(QStringLiteral("colorSpace"))
          .toString() != QStringLiteral("Display P3")) {
    return fail("structured recipe should serialize the requested output color space");
  }
  const auto reparsed_output_transform =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(output_transform_document).toJson(QJsonDocument::Compact)));
  if (reparsed_output_transform.output_color_space != QStringLiteral("Display P3")) {
    return fail("structured recipe should round-trip the output color space");
  }

  EditRecipeState color_pipeline_state;
  color_pipeline_state.input_color_profile = QStringLiteral("DCP: Canon EOS R5 Adobe Standard");
  color_pipeline_state.working_color_profile = QStringLiteral("Linear Rec.2020");
  color_pipeline_state.preview_color_space = QStringLiteral("Display P3");
  color_pipeline_state.monitor_color_profile = QStringLiteral("sRGB");
  color_pipeline_state.soft_proof_enabled = true;
  color_pipeline_state.soft_proof_profile = QStringLiteral("Adobe RGB");
  color_pipeline_state.output_color_space = QStringLiteral("Rec.2020");
  color_pipeline_state.output_headroom = 2.0;
  color_pipeline_state.render_intent = QStringLiteral("Relative Colorimetric");
  color_pipeline_state.raw_input_transform_enabled = false;
  color_pipeline_state.highlight_reconstruction_mode = QStringLiteral("Disabled");
  color_pipeline_state.chromatic_adaptation_method = QStringLiteral("CAT02");
  color_pipeline_state.raw_camera_matching_look = QStringLiteral("Landscape");
  const auto color_pipeline_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(color_pipeline_state).toUtf8()).object();
  if (color_pipeline_document.value(QStringLiteral("colorPipeline"))
          .toObject()
          .value(QStringLiteral("inputProfile"))
          .toString() != QStringLiteral("Camera DCP: Canon EOS R5 Adobe Standard")) {
    return fail("structured recipe should serialize DCP camera input profiles");
  }
  if (color_pipeline_document.value(QStringLiteral("outputTransform"))
          .toObject()
          .value(QStringLiteral("previewColorSpace"))
          .toString() != QStringLiteral("Display P3")) {
    return fail("structured recipe should serialize the preview output color space");
  }
  if (color_pipeline_document.value(QStringLiteral("outputTransform"))
          .toObject()
          .value(QStringLiteral("colorSpace"))
          .toString() != QStringLiteral("Rec.2020")) {
    return fail("structured recipe should serialize expanded output color spaces");
  }
  if (color_pipeline_document.value(QStringLiteral("outputTransform"))
          .toObject()
          .value(QStringLiteral("monitorProfile"))
          .toString() != QStringLiteral("sRGB")) {
    return fail("structured recipe should serialize monitor display profiles");
  }
  if (!color_pipeline_document.value(QStringLiteral("outputTransform"))
           .toObject()
           .value(QStringLiteral("softProofEnabled"))
           .toBool() ||
      color_pipeline_document.value(QStringLiteral("outputTransform"))
              .toObject()
              .value(QStringLiteral("softProofProfile"))
              .toString() != QStringLiteral("Adobe RGB")) {
    return fail("structured recipe should serialize soft proof output profiles");
  }
  if (std::abs(color_pipeline_document.value(QStringLiteral("outputTransform"))
                   .toObject()
                   .value(QStringLiteral("headroom"))
                   .toDouble() -
               2.0) > 1.0e-9) {
    return fail("structured recipe should serialize scene-referred output headroom");
  }
  if (color_pipeline_document.value(QStringLiteral("rawAdjustments"))
          .toObject()
          .value(QStringLiteral("highlightReconstructionMode"))
          .toString() != QStringLiteral("Disabled")) {
    return fail("structured recipe should serialize the highlight reconstruction mode");
  }
  if (color_pipeline_document.value(QStringLiteral("rawAdjustments"))
          .toObject()
          .value(QStringLiteral("cameraMatchingLook"))
          .toString() != QStringLiteral("Landscape")) {
    return fail("structured recipe should serialize camera matching looks");
  }
  const auto reparsed_color_pipeline =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(color_pipeline_document).toJson(QJsonDocument::Compact)));
  if (reparsed_color_pipeline.input_color_profile != QStringLiteral("Camera DCP: Canon EOS R5 Adobe Standard") ||
      reparsed_color_pipeline.preview_color_space != QStringLiteral("Display P3") ||
      reparsed_color_pipeline.monitor_color_profile != QStringLiteral("sRGB") ||
      !reparsed_color_pipeline.soft_proof_enabled ||
      reparsed_color_pipeline.soft_proof_profile != QStringLiteral("Adobe RGB") ||
      reparsed_color_pipeline.working_color_profile != QStringLiteral("Linear Rec.2020") ||
      reparsed_color_pipeline.output_color_space != QStringLiteral("Rec.2020") ||
      std::abs(reparsed_color_pipeline.output_headroom - 2.0) > 1.0e-9 ||
      reparsed_color_pipeline.render_intent != QStringLiteral("Relative Colorimetric") ||
      reparsed_color_pipeline.raw_input_transform_enabled != false ||
      reparsed_color_pipeline.highlight_reconstruction_mode != QStringLiteral("Disabled") ||
      reparsed_color_pipeline.raw_camera_matching_look != QStringLiteral("Landscape") ||
      reparsed_color_pipeline.chromatic_adaptation_method != QStringLiteral("CAT02")) {
    return fail("structured recipe should round-trip color pipeline state");
  }

  EditRecipeState snapshot_state;
  snapshot_state.exposure = 0.35;
  const auto first_snapshot_state = photo_editor::desktop::append_named_recipe_snapshot(
      snapshot_state,
      QStringLiteral("Base tone"),
      QStringLiteral("main"));
  EditRecipeState branch_state = first_snapshot_state;
  branch_state.exposure = -0.2;
  const auto branched_snapshot_state = photo_editor::desktop::append_named_recipe_snapshot(
      branch_state,
      QStringLiteral("Matte branch"),
      QStringLiteral("matte"),
      first_snapshot_state.recipe_snapshots.at(0).toMap().value(QStringLiteral("id")).toString());
  const auto snapshot_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(branched_snapshot_state).toUtf8()).object();
  const auto history = snapshot_document.value(QStringLiteral("history")).toObject();
  const auto snapshots = history.value(QStringLiteral("snapshots")).toArray();
  if (history.value(QStringLiteral("activeBranchId")).toString() != QStringLiteral("matte") ||
      snapshots.size() != 2 ||
      snapshots.at(0).toObject().value(QStringLiteral("name")).toString() != QStringLiteral("Base tone") ||
      snapshots.at(1).toObject().value(QStringLiteral("branchId")).toString() != QStringLiteral("matte") ||
      snapshots.at(1).toObject().value(QStringLiteral("parentSnapshotId")).toString().isEmpty() ||
      snapshots.at(1).toObject().value(QStringLiteral("recipeJson")).toString().isEmpty()) {
    return fail("structured recipe should serialize named snapshots and branch lineage");
  }
  const auto reparsed_snapshots =
      photo_editor::desktop::parse_edit_recipe_json(compact_json(snapshot_document));
  if (reparsed_snapshots.active_history_branch != QStringLiteral("matte") ||
      reparsed_snapshots.recipe_snapshots.size() != 2) {
    return fail("structured recipe should round-trip named snapshot history and active branch");
  }
  const auto valid_snapshot_recipe =
      photo_editor::desktop::validate_edit_recipe_json(compact_json(snapshot_document));
  if (!valid_snapshot_recipe.valid || !valid_snapshot_recipe.errors.isEmpty()) {
    return fail("validator should accept named recipe snapshots and branch metadata");
  }

  EditRecipeState recipe_system_state;
  recipe_system_state.crop_preset = QStringLiteral("4x5");
  recipe_system_state.output_color_space = QStringLiteral("Display P3");
  recipe_system_state.preset_applications = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("preset-application-1")},
          {QStringLiteral("name"), QStringLiteral("Portrait Warmth")},
          {QStringLiteral("source"), QStringLiteral("user")},
          {QStringLiteral("groups"), QVariantList{QStringLiteral("tone"), QStringLiteral("color")}},
      },
  };
  recipe_system_state.layers = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("adjustment-layer-1")},
          {QStringLiteral("kind"), QStringLiteral("adjustment")},
          {QStringLiteral("name"), QStringLiteral("Subject Polish")},
          {QStringLiteral("opacity"), 0.75},
          {QStringLiteral("blendMode"), QStringLiteral("normal")},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("text-layer-1")},
          {QStringLiteral("kind"), QStringLiteral("text")},
          {QStringLiteral("name"), QStringLiteral("Title")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 0.82},
          {QStringLiteral("blendMode"), QStringLiteral("screen")},
          {QStringLiteral("text"), QStringLiteral("Layered Caption")},
          {QStringLiteral("textColor"), QStringLiteral("#33ccaa")},
          {QStringLiteral("fontFamily"), QStringLiteral("Avenir")},
          {QStringLiteral("fontSize"), 64},
          {QStringLiteral("fontWeight"), QStringLiteral("bold")},
          {QStringLiteral("textAlignment"), QStringLiteral("center")},
          {QStringLiteral("pixelWidth"), 640},
          {QStringLiteral("pixelHeight"), 192},
          {QStringLiteral("x"), 80},
          {QStringLiteral("y"), 96},
          {QStringLiteral("scale"), 1.25},
          {QStringLiteral("rotation"), -8.0},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("fill-layer-1")},
          {QStringLiteral("kind"), QStringLiteral("fill")},
          {QStringLiteral("name"), QStringLiteral("Color Wash")},
          {QStringLiteral("enabled"), false},
          {QStringLiteral("opacity"), 0.35},
          {QStringLiteral("blendMode"), QStringLiteral("multiply")},
          {QStringLiteral("fillColor"), QStringLiteral("#223344")},
          {QStringLiteral("maskData"), QByteArray(4, char(0xff))},
          {QStringLiteral("maskWidth"), 2},
          {QStringLiteral("maskHeight"), 2},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("group-layer-1")},
          {QStringLiteral("kind"), QStringLiteral("group")},
          {QStringLiteral("name"), QStringLiteral("Finishing Group")},
          {QStringLiteral("groupId"), QStringLiteral("group-layer-1")},
          {QStringLiteral("expanded"), true},
      },
  };
  const auto recipe_system_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(recipe_system_state).toUtf8()).object();
  if (recipe_system_document.value(QStringLiteral("presetApplications")).toArray().size() != 1 ||
      recipe_system_document.value(QStringLiteral("photoLayers")).toArray().size() != 4 ||
      recipe_system_document.value(QStringLiteral("layers")).toArray().size() != 4 ||
      recipe_system_document.value(QStringLiteral("cropTransform")).toObject().value(QStringLiteral("preset")).toString() !=
          QStringLiteral("4x5") ||
      recipe_system_document.value(QStringLiteral("outputTransform")).toObject().value(QStringLiteral("colorSpace")).toString() !=
          QStringLiteral("Display P3")) {
    return fail("structured recipe should represent presets, layers, crops, and output intent together");
  }
  const auto reparsed_recipe_system =
      photo_editor::desktop::parse_edit_recipe_json(compact_json(recipe_system_document));
  if (reparsed_recipe_system.preset_applications.size() != 1 ||
      reparsed_recipe_system.layers.size() != 4 ||
      reparsed_recipe_system.crop_preset != QStringLiteral("4x5") ||
      reparsed_recipe_system.output_color_space != QStringLiteral("Display P3")) {
    return fail("structured recipe should round-trip preset and layer recipe slots");
  }
  const auto reparsed_text_layer = reparsed_recipe_system.layers.at(1).toMap();
  const auto reparsed_fill_layer = reparsed_recipe_system.layers.at(2).toMap();
  const auto reparsed_group_layer = reparsed_recipe_system.layers.at(3).toMap();
  if (reparsed_text_layer.value(QStringLiteral("id")).toString() != QStringLiteral("text-layer-1") ||
      reparsed_text_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Layered Caption") ||
      reparsed_text_layer.value(QStringLiteral("fontFamily")).toString() != QStringLiteral("Avenir") ||
      reparsed_text_layer.value(QStringLiteral("fontSize")).toInt() != 64 ||
      reparsed_text_layer.value(QStringLiteral("fontWeight")).toString() != QStringLiteral("bold") ||
      reparsed_text_layer.value(QStringLiteral("textAlignment")).toString() != QStringLiteral("center") ||
      reparsed_text_layer.value(QStringLiteral("pixelWidth")).toInt() != 640 ||
      !almost_equal(reparsed_text_layer.value(QStringLiteral("scale")).toDouble(), 1.25) ||
      reparsed_fill_layer.value(QStringLiteral("enabled")).toBool() ||
      reparsed_fill_layer.value(QStringLiteral("maskData")).toByteArray().size() != 4 ||
      reparsed_group_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("group")) {
    return fail("structured recipe should round-trip a rich photo layer stack with text, masks, transforms, and groups");
  }
  auto photo_layer_precedence_document = recipe_system_document;
  photo_layer_precedence_document.insert(
      QStringLiteral("layers"),
      QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("carousel-layer")}}});
  const auto reparsed_photo_layers =
      photo_editor::desktop::parse_edit_recipe_json(compact_json(photo_layer_precedence_document));
  if (reparsed_photo_layers.layers.size() != 4 ||
      reparsed_photo_layers.layers.at(0).toMap().value(QStringLiteral("id")).toString() !=
          QStringLiteral("adjustment-layer-1")) {
    return fail("photoLayers should be the authoritative image-edit layer stack when legacy layers also exist");
  }

  const QStringList layer_color_channels{
      QStringLiteral("Reds"),
      QStringLiteral("Oranges"),
      QStringLiteral("Yellows"),
      QStringLiteral("Greens"),
      QStringLiteral("Aquas"),
      QStringLiteral("Blues"),
      QStringLiteral("Purples"),
      QStringLiteral("Magentas"),
  };
  QVariantMap layer_color_mixer_channels;
  QVariantMap layer_bw_channels;
  for (int index = 0; index < layer_color_channels.size(); ++index) {
    const auto channel = layer_color_channels.at(index);
    layer_color_mixer_channels.insert(
        channel,
        QVariantMap{
            {QStringLiteral("hue"), -40.0 + index * 11.0},
            {QStringLiteral("saturation"), 32.0 - index * 5.0},
            {QStringLiteral("luminance"), -18.0 + index * 4.0},
            {QStringLiteral("selectiveCyan"), -25.0 + index * 3.0},
            {QStringLiteral("selectiveMagenta"), 22.0 - index * 2.0},
            {QStringLiteral("selectiveYellow"), -16.0 + index * 6.0},
            {QStringLiteral("selectiveBlack"), 9.0 - index * 1.5},
        });
    layer_bw_channels.insert(channel, -32.0 + index * 9.0);
  }
  QVariantList layer_roundtrip_lut_table;
  for (int index = 0; index < 8; ++index) {
    layer_roundtrip_lut_table.append(QVariantMap{
        {QStringLiteral("red"), index % 2 == 0 ? 1.0 : 0.25},
        {QStringLiteral("green"), 0.125 * index},
        {QStringLiteral("blue"), 1.0 - 0.1 * index},
        {QStringLiteral("alpha"), 1.0},
    });
  }
  QVariantMap layer_color_roundtrip_adjustments;
  layer_color_roundtrip_adjustments.insert(
      QStringLiteral("colorMixer"),
      QVariantMap{
          {QStringLiteral("model"), QStringLiteral("HSV")},
          {QStringLiteral("channel"), QStringLiteral("Magentas")},
          {QStringLiteral("channels"), layer_color_mixer_channels},
          {QStringLiteral("rgbChannelMixer"),
           QVariantMap{
               {QStringLiteral("redFromRed"), 112.0},
               {QStringLiteral("redFromGreen"), -18.0},
               {QStringLiteral("redFromBlue"), 6.0},
               {QStringLiteral("greenFromRed"), 9.0},
               {QStringLiteral("greenFromGreen"), 93.0},
               {QStringLiteral("greenFromBlue"), -7.0},
               {QStringLiteral("blueFromRed"), -11.0},
               {QStringLiteral("blueFromGreen"), 16.0},
               {QStringLiteral("blueFromBlue"), 105.0},
           }},
          {QStringLiteral("blackAndWhite"),
           QVariantMap{
               {QStringLiteral("enabled"), true},
               {QStringLiteral("channels"), layer_bw_channels},
           }},
          {QStringLiteral("colorReplacement"),
           QVariantMap{
               {QStringLiteral("enabled"), true},
               {QStringLiteral("sourceHue"), 0.18},
               {QStringLiteral("targetHue"), 0.67},
               {QStringLiteral("range"), 42.0},
               {QStringLiteral("softness"), 61.0},
               {QStringLiteral("strength"), 73.0},
               {QStringLiteral("preserveLuminance"), false},
               {QStringLiteral("preserveSaturation"), true},
           }},
          {QStringLiteral("skinToneProtection"),
           QVariantMap{
               {QStringLiteral("enabled"), true},
               {QStringLiteral("lineHue"), 0.11},
               {QStringLiteral("range"), 28.0},
               {QStringLiteral("strength"), 54.0},
           }},
          {QStringLiteral("colorHarmony"),
           QVariantMap{
               {QStringLiteral("enabled"), true},
               {QStringLiteral("mode"), QStringLiteral("Triadic")},
               {QStringLiteral("baseHue"), 0.21},
               {QStringLiteral("strength"), 37.0},
           }},
      });
  layer_color_roundtrip_adjustments.insert(
      QStringLiteral("colorGrading"),
      QVariantMap{
          {QStringLiteral("shadows"),
           QVariantMap{
               {QStringLiteral("hue"), 0.08},
               {QStringLiteral("saturation"), 21.0},
               {QStringLiteral("luminance"), -7.0},
           }},
          {QStringLiteral("midtones"),
           QVariantMap{
               {QStringLiteral("hue"), 0.42},
               {QStringLiteral("saturation"), 16.0},
               {QStringLiteral("luminance"), 5.0},
           }},
          {QStringLiteral("highlights"),
           QVariantMap{
               {QStringLiteral("hue"), 0.78},
               {QStringLiteral("saturation"), 12.0},
               {QStringLiteral("luminance"), 9.0},
           }},
          {QStringLiteral("amount"), 88.0},
          {QStringLiteral("balance"), -24.0},
      });
  layer_color_roundtrip_adjustments.insert(
      QStringLiteral("lutStack"),
      QVariantList{
          QVariantMap{
              {QStringLiteral("enabled"), true},
              {QStringLiteral("strength"), 64.0},
              {QStringLiteral("name"), QStringLiteral("Layer Round Trip LUT")},
              {QStringLiteral("size"), 2},
              {QStringLiteral("table"), layer_roundtrip_lut_table},
              {QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0}},
              {QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0}},
              {QStringLiteral("inputColorSpace"), QStringLiteral("sRGB")},
              {QStringLiteral("outputColorSpace"), QStringLiteral("Display P3")},
          },
      });
  EditRecipeState layer_color_roundtrip_state;
  layer_color_roundtrip_state.layers = {
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("layer-local-color-roundtrip")},
          {QStringLiteral("kind"), QStringLiteral("adjustment")},
          {QStringLiteral("name"), QStringLiteral("Layer Local Color Round Trip")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 0.84},
          {QStringLiteral("blendMode"), QStringLiteral("screen")},
          {QStringLiteral("selectionScoped"), true},
          {QStringLiteral("selectionMaskSetId"), QStringLiteral("mask-set-roundtrip")},
          {QStringLiteral("temperature"), 19.0},
          {QStringLiteral("tint"), -13.0},
          {QStringLiteral("vibrance"), 27.0},
          {QStringLiteral("saturation"), -18.0},
          {QStringLiteral("hueShift"), 24.0},
          {QStringLiteral("saturationShift"), -9.0},
          {QStringLiteral("colorizeHue"), 0.62},
          {QStringLiteral("colorizeStrength"), 41.0},
          {QStringLiteral("curveShadows"), -12.0},
          {QStringLiteral("curveDarks"), 8.0},
          {QStringLiteral("curveLights"), 23.0},
          {QStringLiteral("curveHighlights"), -16.0},
          {QStringLiteral("masterCurvePoints"),
           QVariantList{point(0.0, 0.0), point(0.45, 0.38), point(1.0, 1.0)}},
          {QStringLiteral("lumaCurvePoints"),
           QVariantList{point(0.0, 0.0), point(0.52, 0.61), point(1.0, 1.0)}},
          {QStringLiteral("redCurvePoints"),
           QVariantList{point(0.0, 0.0), point(0.50, 0.57), point(1.0, 1.0)}},
          {QStringLiteral("greenCurvePoints"),
           QVariantList{point(0.0, 0.0), point(0.50, 0.48), point(1.0, 1.0)}},
          {QStringLiteral("blueCurvePoints"),
           QVariantList{point(0.0, 0.0), point(0.50, 0.43), point(1.0, 1.0)}},
          {QStringLiteral("adjustments"), layer_color_roundtrip_adjustments},
      },
  };
  const auto layer_color_roundtrip_json =
      photo_editor::desktop::serialize_edit_recipe_json(layer_color_roundtrip_state);
  const auto layer_color_reparsed =
      photo_editor::desktop::parse_edit_recipe_json(layer_color_roundtrip_json);
  const auto layer_color_reserialized =
      photo_editor::desktop::serialize_edit_recipe_json(layer_color_reparsed);
  const auto layer_color_reparsed_again =
      photo_editor::desktop::parse_edit_recipe_json(layer_color_reserialized);
  if (layer_color_reparsed_again.layers.size() != 1) {
    return fail("layer-local color recipe round trip should preserve the adjustment layer");
  }
  const auto roundtrip_layer = layer_color_reparsed_again.layers.constFirst().toMap();
  const auto roundtrip_adjustments = required_map(roundtrip_layer, QStringLiteral("adjustments"));
  const auto roundtrip_color_mixer =
      required_map(roundtrip_adjustments, QStringLiteral("colorMixer"));
  const auto roundtrip_channels = required_map(roundtrip_color_mixer, QStringLiteral("channels"));
  const auto roundtrip_rgb_mixer =
      required_map(roundtrip_color_mixer, QStringLiteral("rgbChannelMixer"));
  const auto roundtrip_black_and_white =
      required_map(roundtrip_color_mixer, QStringLiteral("blackAndWhite"));
  const auto roundtrip_bw_channels =
      required_map(roundtrip_black_and_white, QStringLiteral("channels"));
  const auto roundtrip_replacement =
      required_map(roundtrip_color_mixer, QStringLiteral("colorReplacement"));
  const auto roundtrip_skin =
      required_map(roundtrip_color_mixer, QStringLiteral("skinToneProtection"));
  const auto roundtrip_harmony =
      required_map(roundtrip_color_mixer, QStringLiteral("colorHarmony"));
  const auto roundtrip_grading =
      required_map(roundtrip_adjustments, QStringLiteral("colorGrading"));
  const auto roundtrip_grading_highlights =
      required_map(roundtrip_grading, QStringLiteral("highlights"));
  const auto roundtrip_lut_stack = roundtrip_adjustments.value(QStringLiteral("lutStack")).toList();
  if (roundtrip_layer.value(QStringLiteral("id")).toString() !=
          QStringLiteral("layer-local-color-roundtrip") ||
      roundtrip_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          QStringLiteral("mask-set-roundtrip") ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("temperature")).toDouble(), 19.0) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("tint")).toDouble(), -13.0) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("vibrance")).toDouble(), 27.0) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("saturation")).toDouble(), -18.0) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("hueShift")).toDouble(), 24.0) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("saturationShift")).toDouble(), -9.0) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("colorizeHue")).toDouble(), 0.62) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("colorizeStrength")).toDouble(), 41.0) ||
      !almost_equal(roundtrip_layer.value(QStringLiteral("curveShadows")).toDouble(), -12.0) ||
      roundtrip_layer.value(QStringLiteral("masterCurvePoints")).toList().size() != 3 ||
      roundtrip_layer.value(QStringLiteral("blueCurvePoints")).toList().size() != 3 ||
      roundtrip_color_mixer.value(QStringLiteral("model")).toString() != QStringLiteral("HSV") ||
      roundtrip_color_mixer.value(QStringLiteral("channel")).toString() !=
          QStringLiteral("Magentas") ||
      !almost_equal(roundtrip_rgb_mixer.value(QStringLiteral("redFromGreen")).toDouble(), -18.0) ||
      !roundtrip_black_and_white.value(QStringLiteral("enabled")).toBool() ||
      !roundtrip_replacement.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(roundtrip_replacement.value(QStringLiteral("targetHue")).toDouble(), 0.67) ||
      roundtrip_replacement.value(QStringLiteral("preserveLuminance")).toBool() ||
      !roundtrip_replacement.value(QStringLiteral("preserveSaturation")).toBool() ||
      !roundtrip_skin.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(roundtrip_skin.value(QStringLiteral("range")).toDouble(), 28.0) ||
      !roundtrip_harmony.value(QStringLiteral("enabled")).toBool() ||
      roundtrip_harmony.value(QStringLiteral("mode")).toString() != QStringLiteral("Triadic") ||
      !almost_equal(roundtrip_grading.value(QStringLiteral("amount")).toDouble(), 88.0) ||
      !almost_equal(roundtrip_grading.value(QStringLiteral("balance")).toDouble(), -24.0) ||
      !almost_equal(roundtrip_grading_highlights.value(QStringLiteral("hue")).toDouble(), 0.78) ||
      roundtrip_lut_stack.size() != 1 ||
      roundtrip_lut_stack.constFirst().toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Layer Round Trip LUT") ||
      roundtrip_lut_stack.constFirst()
              .toMap()
              .value(QStringLiteral("table"))
              .toList()
              .size() != 8) {
    return fail("layer-local color recipe JSON should round-trip direct, curve, nested color, grading, and LUT controls");
  }
  for (int index = 0; index < layer_color_channels.size(); ++index) {
    const auto channel = layer_color_channels.at(index);
    const auto roundtrip_channel = required_map(roundtrip_channels, channel);
    if (!almost_equal(roundtrip_channel.value(QStringLiteral("hue")).toDouble(), -40.0 + index * 11.0) ||
        !almost_equal(roundtrip_channel.value(QStringLiteral("selectiveYellow")).toDouble(), -16.0 + index * 6.0) ||
        !almost_equal(roundtrip_bw_channels.value(channel).toDouble(), -32.0 + index * 9.0)) {
      return fail("layer-local color recipe JSON should round-trip every mixer channel");
    }
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
  base_linear_mask.insert(QStringLiteral("outputId"), QStringLiteral("subject-alpha"));
  base_linear_mask.insert(QStringLiteral("reusableOutput"), true);
  base_linear_mask.insert(QStringLiteral("name"), QStringLiteral("Subject"));
  base_linear_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("subject-set"));
  base_linear_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Subject"));
  base_linear_mask.insert(QStringLiteral("kind"), QStringLiteral("linear"));
  base_linear_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  base_linear_mask.insert(QStringLiteral("enabled"), true);
  base_linear_mask.insert(QStringLiteral("opacity"), 0.72);
  base_linear_mask.insert(QStringLiteral("density"), 0.64);
  base_linear_mask.insert(QStringLiteral("invert"), true);
  base_linear_mask.insert(QStringLiteral("blendMode"), QStringLiteral("screen"));
  base_linear_mask.insert(QStringLiteral("exposure"), 0.65);
  base_linear_mask.insert(QStringLiteral("temperature"), 9.0);
  base_linear_mask.insert(QStringLiteral("feather"), 0.33);
  base_linear_mask.insert(QStringLiteral("position"), 0.58);
  base_linear_mask.insert(QStringLiteral("rotation"), 12.0);

  QVariantMap subtract_radial_mask;
  subtract_radial_mask.insert(QStringLiteral("id"), QStringLiteral("radial-cutout"));
  subtract_radial_mask.insert(QStringLiteral("parentId"), QStringLiteral("linear-base"));
  subtract_radial_mask.insert(QStringLiteral("name"), QStringLiteral("Exclude Window"));
  subtract_radial_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("subject-set"));
  subtract_radial_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Subject"));
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
  if (stacked_mask_array.at(1).toObject().value(QStringLiteral("parentId")).toString() !=
      QStringLiteral("linear-base")) {
    return fail("structured recipe should preserve parent-child mask nesting metadata");
  }
  if (!almost_equal(stacked_mask_array.at(0).toObject().value(QStringLiteral("opacity")).toDouble(), 0.72) ||
      !almost_equal(stacked_mask_array.at(0).toObject().value(QStringLiteral("density")).toDouble(), 0.64) ||
      !stacked_mask_array.at(0).toObject().value(QStringLiteral("invert")).toBool() ||
      stacked_mask_array.at(0).toObject().value(QStringLiteral("outputId")).toString() != QStringLiteral("subject-alpha") ||
      !stacked_mask_array.at(0).toObject().value(QStringLiteral("reusableOutput")).toBool() ||
      stacked_mask_array.at(0).toObject().value(QStringLiteral("blendMode")).toString() != QStringLiteral("screen")) {
    return fail("structured recipe should preserve per-mask opacity, density, invert, reusable output, and blend mode");
  }
  const auto stacked_mask_sets = stacked_document.value(QStringLiteral("maskSets")).toArray();
  if (stacked_mask_sets.size() != 1 ||
      stacked_mask_sets.at(0).toObject().value(QStringLiteral("sources")).toArray().size() != 2) {
    return fail("structured recipe should group related selection sources into one mask set");
  }
  if (stacked_mask_sets.at(0).toObject().value(QStringLiteral("schemaVersion")).toInt() != 2 ||
      stacked_mask_sets.at(0).toObject().value(QStringLiteral("adjustmentsSchemaVersion")).toInt() != 1 ||
      stacked_mask_sets.at(0).toObject().value(QStringLiteral("componentSchemaVersion")).toInt() != 1) {
    return fail("structured recipe should write explicit typed mask-set schema fields");
  }
  const auto stacked_sources = stacked_mask_sets.at(0).toObject().value(QStringLiteral("sources")).toArray();
  if (stacked_sources.at(0).toObject().value(QStringLiteral("componentSchemaVersion")).toInt() != 1 ||
      stacked_sources.at(1).toObject().value(QStringLiteral("componentSchemaVersion")).toInt() != 1) {
    return fail("structured recipe should write explicit typed mask component schema fields");
  }
  if (stacked_sources.at(0).toObject().contains(QStringLiteral("adjustments")) ||
      stacked_sources.at(0).toObject().contains(QStringLiteral("exposure")) ||
      stacked_sources.at(0).toObject().contains(QStringLiteral("adjustmentOverrides")) ||
      stacked_sources.at(1).toObject().contains(QStringLiteral("adjustments")) ||
      stacked_sources.at(1).toObject().contains(QStringLiteral("exposure")) ||
      stacked_sources.at(1).toObject().contains(QStringLiteral("adjustmentOverrides"))) {
    return fail("grouped mask sources should store normal adjustments only at the mask-set level");
  }
  if (!almost_equal(stacked_sources.at(0).toObject().value(QStringLiteral("opacity")).toDouble(), 0.72) ||
      !almost_equal(stacked_sources.at(0).toObject().value(QStringLiteral("density")).toDouble(), 0.64) ||
      !stacked_sources.at(0).toObject().value(QStringLiteral("selection")).toObject().value(QStringLiteral("invert")).toBool() ||
      stacked_sources.at(0).toObject().value(QStringLiteral("outputId")).toString() != QStringLiteral("subject-alpha") ||
      !stacked_sources.at(0).toObject().value(QStringLiteral("reusableOutput")).toBool() ||
      stacked_sources.at(0).toObject().value(QStringLiteral("blendMode")).toString() != QStringLiteral("screen")) {
    return fail("grouped mask sources should preserve per-mask opacity, density, invert, reusable output, and blend mode");
  }
  const auto typed_stacked_sets =
      photo_editor::desktop::typed_mask_set_models_from_stack(stacked_mask_state.mask_stack);
  if (typed_stacked_sets.size() != 1 ||
      typed_stacked_sets.at(0).id != QStringLiteral("subject-set") ||
      typed_stacked_sets.at(0).components.size() != 2 ||
      !almost_equal(typed_stacked_sets.at(0).adjustments.value(QStringLiteral("exposure")).toDouble(), 0.65) ||
      !almost_equal(typed_stacked_sets.at(0).adjustments.value(QStringLiteral("temperature")).toDouble(), 9.0)) {
    return fail("typed mask model should group flat selection sources into one mask-set adjustment model");
  }
  if (typed_stacked_sets.at(0).components.at(0).metadata.value(QStringLiteral("id")).toString() !=
          QStringLiteral("linear-base") ||
      !almost_equal(typed_stacked_sets.at(0).components.at(0).geometry.value(QStringLiteral("position")).toDouble(), 0.58) ||
      typed_stacked_sets.at(0).components.at(0).geometry.contains(QStringLiteral("exposure")) ||
      typed_stacked_sets.at(0).components.at(0).geometry.contains(QStringLiteral("maskSetOpacity")) ||
      typed_stacked_sets.at(0).components.at(0).matte.contains(QStringLiteral("exposure")) ||
      !typed_stacked_sets.at(0).components.at(0).adjustment_overrides.isEmpty() ||
      !typed_stacked_sets.at(0).components.at(1).adjustment_overrides.isEmpty() ||
      typed_stacked_sets.at(0).components.at(1).metadata.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("subtract")) {
    return fail("typed mask model should separate component metadata, geometry, matte data, and set adjustments");
  }
  const auto typed_sources =
      photo_editor::desktop::typed_mask_source_models_from_stack(stacked_mask_state.mask_stack);
  if (typed_sources.size() != 2 ||
      typed_sources.at(0).flat_index != 0 ||
      typed_sources.at(0).metadata.value(QStringLiteral("id")).toString() != QStringLiteral("linear-base") ||
      typed_sources.at(0).metadata.contains(QStringLiteral("exposure")) ||
      !almost_equal(typed_sources.at(0).adjustments.value(QStringLiteral("exposure")).toDouble(), 0.65) ||
      !almost_equal(typed_sources.at(0).geometry.value(QStringLiteral("position")).toDouble(), 0.58) ||
      typed_sources.at(0).geometry.contains(QStringLiteral("exposure")) ||
      !typed_sources.at(0).constraints.contains(QStringLiteral("refineLuminanceMin")) ||
      typed_sources.at(0).constraints.contains(QStringLiteral("position")) ||
      typed_sources.at(0).toJsonObject().value(QStringLiteral("modelSchemaVersion")).toInt() != 1 ||
      typed_sources.at(0)
              .toJsonObject()
              .value(QStringLiteral("geometry"))
              .toObject()
              .value(QStringLiteral("position"))
              .toDouble() != 0.58 ||
      !almost_equal(
          typed_sources.at(0).toLegacyVariantMap().value(QStringLiteral("exposure")).toDouble(),
          0.65)) {
    return fail("typed mask source model should expose explicit metadata, adjustments, geometry, constraints, and JSON serialization");
  }

  EditRecipeState override_mask_state;
  auto override_radial_mask = subtract_radial_mask;
  override_radial_mask.insert(QStringLiteral("exposure"), 1.25);
  override_radial_mask.insert(QStringLiteral("temperature"), -11.0);
  override_mask_state.mask_stack = {base_linear_mask, override_radial_mask};
  const auto override_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(override_mask_state).toUtf8()).object();
  const auto override_sources =
      override_document.value(QStringLiteral("maskSets"))
          .toArray()
          .at(0)
          .toObject()
          .value(QStringLiteral("sources"))
          .toArray();
  const auto override_adjustments =
      override_sources.at(1).toObject().value(QStringLiteral("adjustmentOverrides")).toObject();
  const auto typed_override_sets =
      photo_editor::desktop::typed_mask_set_models_from_stack(override_mask_state.mask_stack);
  if (override_sources.at(0).toObject().contains(QStringLiteral("adjustmentOverrides")) ||
      override_adjustments.size() != 2 ||
      !almost_equal(override_adjustments.value(QStringLiteral("exposure")).toDouble(), 1.25) ||
      !almost_equal(override_adjustments.value(QStringLiteral("temperature")).toDouble(), -11.0) ||
      typed_override_sets.size() != 1 ||
      !almost_equal(typed_override_sets.at(0)
                        .components.at(1)
                        .adjustment_overrides.value(QStringLiteral("exposure"))
                        .toDouble(),
                    1.25) ||
      typed_override_sets.at(0).components.at(1).geometry.contains(QStringLiteral("exposure"))) {
    return fail("typed mask components should store only deliberate per-source adjustment overrides");
  }
  const auto reparsed_override_stack = photo_editor::desktop::parse_edit_recipe_json(
      QString::fromUtf8(QJsonDocument(override_document).toJson(QJsonDocument::Compact)));
  if (reparsed_override_stack.mask_stack.size() != 2 ||
      !almost_equal(reparsed_override_stack.mask_stack.at(0).toMap().value(QStringLiteral("exposure")).toDouble(), 0.65) ||
      !almost_equal(reparsed_override_stack.mask_stack.at(1).toMap().value(QStringLiteral("exposure")).toDouble(), 1.25) ||
      !almost_equal(reparsed_override_stack.mask_stack.at(1).toMap().value(QStringLiteral("temperature")).toDouble(), -11.0)) {
    return fail("grouped mask adjustment overrides should round-trip over mask-set adjustments");
  }

  const auto reparsed_stack =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(stacked_document).toJson(QJsonDocument::Compact)));
  if (reparsed_stack.mask_stack.size() != 2 ||
      reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("name")).toString() != QStringLiteral("Subject") ||
      reparsed_stack.mask_stack.at(1).toMap().value(QStringLiteral("operation")).toString() != QStringLiteral("subtract") ||
      reparsed_stack.mask_stack.at(1).toMap().value(QStringLiteral("parentId")).toString() != QStringLiteral("linear-base") ||
      !almost_equal(reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("opacity")).toDouble(), 0.72) ||
      !almost_equal(reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("density")).toDouble(), 0.64) ||
      !reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("invert")).toBool() ||
      reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("outputId")).toString() != QStringLiteral("subject-alpha") ||
      !reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("reusableOutput")).toBool() ||
      reparsed_stack.mask_stack.at(0).toMap().value(QStringLiteral("blendMode")).toString() != QStringLiteral("screen")) {
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
  brush_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("painted-subject"));
  brush_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Painted Subject"));
  brush_mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Detail"));
  brush_mask.insert(QStringLiteral("brushSize"), 0.12);
  brush_mask.insert(QStringLiteral("brushFlow"), 0.85);
  brush_mask.insert(QStringLiteral("brushHardness"), 0.42);
  brush_mask.insert(QStringLiteral("brushSpacing"), 0.36);
  brush_mask.insert(QStringLiteral("brushSmoothing"), 0.27);
  brush_mask.insert(QStringLiteral("brushPressureSize"), 0.74);
  brush_mask.insert(QStringLiteral("brushPressureFlow"), 0.63);
  brush_mask.insert(QStringLiteral("brushPressureOpacity"), 0.52);
  brush_mask.insert(QStringLiteral("brushBuildup"), false);
  brush_mask.insert(QStringLiteral("brushEraseMode"), true);
  brush_mask.insert(QStringLiteral("brushAutoMask"), true);
  brush_mask.insert(QStringLiteral("brushAutoMaskTolerance"), 0.12);
  brush_mask.insert(QStringLiteral("brushAutoMaskContinuousSampling"), true);
  brush_mask.insert(QStringLiteral("brushProtectEdges"), true);
  brush_mask.insert(QStringLiteral("brushRefineMode"), true);
  brush_mask.insert(QStringLiteral("refineBrushRecalculate"), true);
  brush_mask.insert(QStringLiteral("brushRefineTargetId"), QStringLiteral("subject-ai"));
  brush_mask.insert(QStringLiteral("brushRefineTargetName"), QStringLiteral("AI Subject"));
  brush_mask.insert(QStringLiteral("brushRefineTargetKind"), QStringLiteral("subject"));
  brush_mask.insert(QStringLiteral("selectionSmooth"), 37.0);
  brush_mask.insert(QStringLiteral("selectionFineEdge"), 58.0);
  brush_mask.insert(QStringLiteral("brushPoints"), QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.42}, {QStringLiteral("y"), 0.33}, {QStringLiteral("size"), 0.12}, {QStringLiteral("flow"), 0.85}, {QStringLiteral("opacity"), 0.58}, {QStringLiteral("autoMaskAnchorX"), 0.41}, {QStringLiteral("autoMaskAnchorY"), 0.32}, {QStringLiteral("pressure"), 0.54}, {QStringLiteral("tiltX"), 0.25}, {QStringLiteral("tiltY"), -0.15}, {QStringLiteral("erase"), true}},
      QVariantMap{{QStringLiteral("x"), 0.48}, {QStringLiteral("y"), 0.37}, {QStringLiteral("size"), 0.10}, {QStringLiteral("flow"), 0.75}},
  });
  brush_mask.insert(QStringLiteral("segmentationData"), QByteArray::fromHex("00ff7f3f"));
  brush_mask.insert(QStringLiteral("segmentationWidth"), 2);
  brush_mask.insert(QStringLiteral("segmentationHeight"), 2);

  QVariantMap luminance_mask;
  luminance_mask.insert(QStringLiteral("kind"), QStringLiteral("luminance"));
  luminance_mask.insert(QStringLiteral("name"), QStringLiteral("Highlights"));
  luminance_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("painted-subject"));
  luminance_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Painted Subject"));
  luminance_mask.insert(QStringLiteral("luminanceMin"), 0.62);
  luminance_mask.insert(QStringLiteral("luminanceMax"), 0.95);
  luminance_mask.insert(QStringLiteral("rangeFeather"), 0.08);
  luminance_mask.insert(
      QStringLiteral("luminanceSamples"),
      QVariantList{
          QVariantMap{
              {QStringLiteral("x"), 0.30},
              {QStringLiteral("y"), 0.18},
              {QStringLiteral("width"), 0.20},
              {QStringLiteral("height"), 0.12},
              {QStringLiteral("luminanceMin"), 0.61},
              {QStringLiteral("luminanceMax"), 0.86},
              {QStringLiteral("luminanceMean"), 0.72},
              {QStringLiteral("pixelCount"), 48},
          },
      });

  QVariantMap color_mask;
  color_mask.insert(QStringLiteral("kind"), QStringLiteral("color"));
  color_mask.insert(QStringLiteral("name"), QStringLiteral("Skin Range"));
  color_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("painted-subject"));
  color_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Painted Subject"));
  color_mask.insert(QStringLiteral("hueCenter"), 0.07);
  color_mask.insert(QStringLiteral("hueRange"), 0.09);
  color_mask.insert(QStringLiteral("saturationMin"), 0.18);
  color_mask.insert(QStringLiteral("saturationMax"), 0.82);
  color_mask.insert(QStringLiteral("rangeFeather"), 0.05);
  color_mask.insert(
      QStringLiteral("colorSamples"),
      QVariantList{
          QVariantMap{
              {QStringLiteral("x"), 0.24},
              {QStringLiteral("y"), 0.36},
              {QStringLiteral("hue"), 0.05},
              {QStringLiteral("saturation"), 0.74},
              {QStringLiteral("luminance"), 0.42},
              {QStringLiteral("red"), 214},
              {QStringLiteral("green"), 74},
              {QStringLiteral("blue"), 42},
              {QStringLiteral("hex"), QStringLiteral("#D64A2A")},
          },
          QVariantMap{
              {QStringLiteral("x"), 0.42},
              {QStringLiteral("y"), 0.52},
              {QStringLiteral("hue"), 0.08},
              {QStringLiteral("saturation"), 0.58},
              {QStringLiteral("luminance"), 0.48},
              {QStringLiteral("red"), 190},
              {QStringLiteral("green"), 120},
              {QStringLiteral("blue"), 86},
              {QStringLiteral("hex"), QStringLiteral("#BE7856")},
          },
      });

  QVariantMap lasso_mask;
  lasso_mask.insert(QStringLiteral("kind"), QStringLiteral("lasso"));
  lasso_mask.insert(QStringLiteral("name"), QStringLiteral("Freehand Selection"));
  lasso_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("painted-subject"));
  lasso_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Painted Subject"));
  lasso_mask.insert(QStringLiteral("polygonPoints"), QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.20}, {QStringLiteral("y"), 0.20}},
      QVariantMap{{QStringLiteral("x"), 0.70}, {QStringLiteral("y"), 0.22}},
      QVariantMap{{QStringLiteral("x"), 0.62}, {QStringLiteral("y"), 0.72}},
      QVariantMap{{QStringLiteral("x"), 0.24}, {QStringLiteral("y"), 0.68}},
  });

  QVariantMap depth_mask;
  depth_mask.insert(QStringLiteral("kind"), QStringLiteral("depth"));
  depth_mask.insert(QStringLiteral("name"), QStringLiteral("Depth Range"));
  depth_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("painted-subject"));
  depth_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Painted Subject"));
  depth_mask.insert(QStringLiteral("depthMin"), 0.18);
  depth_mask.insert(QStringLiteral("depthMax"), 0.66);
  depth_mask.insert(QStringLiteral("rangeFeather"), 0.07);
  depth_mask.insert(
      QStringLiteral("depthSamples"),
      QVariantList{
          QVariantMap{
              {QStringLiteral("x"), 0.75},
              {QStringLiteral("y"), 0.25},
              {QStringLiteral("depthMin"), 0.50},
              {QStringLiteral("depthMax"), 0.92},
              {QStringLiteral("depthMean"), 0.71},
              {QStringLiteral("pixelCount"), 4},
          },
      });
  depth_mask.insert(QStringLiteral("depthData"), QByteArray::fromHex("001080ff"));
  depth_mask.insert(QStringLiteral("depthWidth"), 2);
  depth_mask.insert(QStringLiteral("depthHeight"), 2);

  mixed_mask_state.mask_stack = {brush_mask, luminance_mask, color_mask, lasso_mask, depth_mask};
  const auto mixed_mask_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(mixed_mask_state).toUtf8()).object();
  const auto mixed_mask_array = mixed_mask_document.value(QStringLiteral("maskStack")).toArray();
  if (mixed_mask_array.size() != 5) {
    return fail("structured recipe should serialize brush and range masks");
  }
  if (!mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPoints")).isArray() ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushStrokes")).toArray().size() != 1 ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushStrokes")).toArray().at(0).toObject().value(QStringLiteral("samples")).toArray().size() != 2 ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushStrokes")).toArray().at(0).toObject().value(QStringLiteral("createdAtOrder")).toInt() != 1 ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushStrokes")).toArray().at(0).toObject().value(QStringLiteral("operation")).toString() != QStringLiteral("add") ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPreset")).toString() != QStringLiteral("Detail") ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushHardness")).toDouble(), 0.42) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushSpacing")).toDouble(), 0.36) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushSmoothing")).toDouble(), 0.27) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPressureSize")).toDouble(), 0.74) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPressureFlow")).toDouble(), 0.63) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPressureOpacity")).toDouble(), 0.52) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("adjustments")).toObject().value(QStringLiteral("selectionSmooth")).toDouble(), 37.0) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("adjustments")).toObject().value(QStringLiteral("selectionFineEdge")).toDouble(), 58.0) ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushBuildup")).toBool() ||
      !mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushEraseMode")).toBool() ||
      !mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushAutoMask")).toBool() ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushAutoMaskTolerance")).toDouble(), 0.12) ||
      !mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool() ||
      !mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushProtectEdges")).toBool() ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPoints")).toArray().at(0).toObject().value(QStringLiteral("autoMaskAnchorX")).toDouble(), 0.41) ||
      !almost_equal(mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushPoints")).toArray().at(0).toObject().value(QStringLiteral("autoMaskAnchorY")).toDouble(), 0.32) ||
      !mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushRefineMode")).toBool() ||
      !mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("refineBrushRecalculate")).toBool() ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushRefineTargetId")).toString() != QStringLiteral("subject-ai") ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushRefineTargetName")).toString() != QStringLiteral("AI Subject") ||
      mixed_mask_array.at(0).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("brushRefineTargetKind")).toString() != QStringLiteral("subject")) {
    return fail("brush mask serialization should retain brush points");
  }
  QJsonObject grouped_brush_geometry;
  const auto grouped_sources =
      mixed_mask_document.value(QStringLiteral("maskSets"))
          .toArray()
          .at(0)
          .toObject()
          .value(QStringLiteral("sources"))
          .toArray();
  for (const auto& source_value : grouped_sources) {
    if (!source_value.isObject()) {
      continue;
    }
    const auto source_object = source_value.toObject();
    if (source_object.value(QStringLiteral("kind")).toString() != QStringLiteral("brush")) {
      continue;
    }
    grouped_brush_geometry = source_object.value(QStringLiteral("geometry")).toObject();
    break;
  }
  if (!grouped_brush_geometry.contains(QStringLiteral("segmentationData"))) {
    return fail("grouped brush serialization should retain raster mask payloads");
  }
  if (mixed_mask_array.at(1).toObject().value(QStringLiteral("constraints")).toObject().value(QStringLiteral("luminanceSamples")).toArray().size() != 1) {
    return fail("luminance range serialization should retain sampled patch constraints");
  }
  if (!mixed_mask_array.at(2).toObject().value(QStringLiteral("constraints")).toObject().contains(QStringLiteral("hueCenter")) ||
      mixed_mask_array.at(2).toObject().value(QStringLiteral("constraints")).toObject().value(QStringLiteral("colorSamples")).toArray().size() != 2) {
    return fail("color range serialization should retain range constraints");
  }
  if (!mixed_mask_array.at(3).toObject().value(QStringLiteral("geometry")).toObject().value(QStringLiteral("polygonPoints")).isArray()) {
    return fail("lasso serialization should retain freehand polygon points");
  }
  if (!mixed_mask_array.at(4).toObject().value(QStringLiteral("constraints")).toObject().contains(QStringLiteral("depthMin")) ||
      mixed_mask_array.at(4).toObject().value(QStringLiteral("constraints")).toObject().value(QStringLiteral("depthSamples")).toArray().size() != 1 ||
      !mixed_mask_array.at(4).toObject().value(QStringLiteral("geometry")).toObject().contains(QStringLiteral("depthData"))) {
    return fail("depth range serialization should retain depth constraints and payloads");
  }
  if (mixed_mask_document.value(QStringLiteral("maskSets")).toArray().size() != 1 ||
      mixed_mask_document.value(QStringLiteral("maskSets")).toArray().at(0).toObject().value(QStringLiteral("sources")).toArray().size() != 5) {
    return fail("structured recipe should serialize grouped contributor sources into one mask set");
  }

  const auto reparsed_mixed_masks =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(mixed_mask_document).toJson(QJsonDocument::Compact)));
  if (reparsed_mixed_masks.mask_stack.size() != 5 ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().size() != 2 ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushStrokes")).toList().size() != 1 ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushStrokes")).toList().first().toMap().value(QStringLiteral("samples")).toList().size() != 2 ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushStrokes")).toList().first().toMap().value(QStringLiteral("createdAtOrder")).toInt() != 1 ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushStrokes")).toList().first().toMap().value(QStringLiteral("operation")).toString() != QStringLiteral("add") ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().first().toMap().value(QStringLiteral("pressure")).toDouble(), 0.54) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().first().toMap().value(QStringLiteral("opacity")).toDouble(), 0.58) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushHardness")).toDouble(), 0.42) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushSpacing")).toDouble(), 0.36) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushSmoothing")).toDouble(), 0.27) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPressureSize")).toDouble(), 0.74) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPressureFlow")).toDouble(), 0.63) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPressureOpacity")).toDouble(), 0.52) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("selectionSmooth")).toDouble(), 37.0) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("selectionFineEdge")).toDouble(), 58.0) ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushBuildup")).toBool() ||
      !reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushEraseMode")).toBool() ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPreset")).toString() != QStringLiteral("Detail") ||
      !reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushAutoMask")).toBool() ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushAutoMaskTolerance")).toDouble(), 0.12) ||
      !reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool() ||
      !reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushProtectEdges")).toBool() ||
      !reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushRefineMode")).toBool() ||
      !reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("refineBrushRecalculate")).toBool() ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushRefineTargetId")).toString() != QStringLiteral("subject-ai") ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushRefineTargetName")).toString() != QStringLiteral("AI Subject") ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushRefineTargetKind")).toString() != QStringLiteral("subject") ||
      !reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().first().toMap().value(QStringLiteral("erase")).toBool() ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().first().toMap().value(QStringLiteral("autoMaskAnchorX")).toDouble(), 0.41) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().first().toMap().value(QStringLiteral("autoMaskAnchorY")).toDouble(), 0.32) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().first().toMap().value(QStringLiteral("tiltX")).toDouble(), 0.25) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().first().toMap().value(QStringLiteral("tiltY")).toDouble(), -0.15) ||
      reparsed_mixed_masks.mask_stack.at(0).toMap().value(QStringLiteral("segmentationData")).toByteArray() != QByteArray::fromHex("00ff7f3f") ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(1).toMap().value(QStringLiteral("luminanceMin")).toDouble(), 0.62) ||
      reparsed_mixed_masks.mask_stack.at(1).toMap().value(QStringLiteral("luminanceSamples")).toList().size() != 1 ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(1).toMap().value(QStringLiteral("luminanceSamples")).toList().first().toMap().value(QStringLiteral("luminanceMean")).toDouble(), 0.72) ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(2).toMap().value(QStringLiteral("hueRange")).toDouble(), 0.09) ||
      reparsed_mixed_masks.mask_stack.at(2).toMap().value(QStringLiteral("colorSamples")).toList().size() != 2 ||
      reparsed_mixed_masks.mask_stack.at(2).toMap().value(QStringLiteral("colorSamples")).toList().at(1).toMap().value(QStringLiteral("hex")).toString() != QStringLiteral("#BE7856") ||
      reparsed_mixed_masks.mask_stack.at(3).toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("lasso") ||
      reparsed_mixed_masks.mask_stack.at(3).toMap().value(QStringLiteral("polygonPoints")).toList().size() != 4 ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(4).toMap().value(QStringLiteral("depthMin")).toDouble(), 0.18) ||
      reparsed_mixed_masks.mask_stack.at(4).toMap().value(QStringLiteral("depthSamples")).toList().size() != 1 ||
      !almost_equal(reparsed_mixed_masks.mask_stack.at(4).toMap().value(QStringLiteral("depthSamples")).toList().first().toMap().value(QStringLiteral("depthMean")).toDouble(), 0.71) ||
      reparsed_mixed_masks.mask_stack.at(4).toMap().value(QStringLiteral("depthData")).toByteArray() != QByteArray::fromHex("001080ff")) {
    return fail("structured recipe should round-trip brush, range, lasso, and depth mask data");
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
  subject_mask.insert(QStringLiteral("selectionEdgeContrast"), 62.0);
  subject_mask.insert(QStringLiteral("selectionDecontaminate"), 36.0);
  subject_mask.insert(QStringLiteral("selectionGrow"), 12.0);
  subject_mask.insert(QStringLiteral("selectionShrink"), 8.0);
  subject_mask.insert(QStringLiteral("selectionFeather"), 22.0);
  subject_mask.insert(QStringLiteral("selectionBlur"), 18.0);
  subject_mask.insert(QStringLiteral("selectionHarden"), 44.0);
  subject_mask.insert(QStringLiteral("selectionDespeckle"), 11.0);
  subject_mask.insert(QStringLiteral("selectionFillHoles"), 7.0);
  subject_mask.insert(QStringLiteral("selectionRemoveIslands"), 9.0);
  subject_mask.insert(QStringLiteral("matteCleanupSchemaVersion"), 1);
  subject_mask.insert(QStringLiteral("matteCleanupProfile"), QStringLiteral("subject-object"));
  subject_mask.insert(QStringLiteral("matteCleanupDefaultsApplied"), true);
  subject_mask.insert(QStringLiteral("maskSetSelectionInvert"), true);
  subject_mask.insert(QStringLiteral("maskSetSelectionExpand"), -12.0);
  subject_mask.insert(QStringLiteral("maskSetSelectionSmooth"), 14.0);
  subject_mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 16.0);
  subject_mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 18.0);
  subject_mask.insert(QStringLiteral("maskSetDensity"), 0.72);
  subject_mask.insert(QStringLiteral("maskSetOpacity"), 0.64);
  subject_mask.insert(QStringLiteral("feather"), 0.5);
  subject_mask.insert(QStringLiteral("segmentationData"), QByteArray::fromHex("00ff7f3f"));
  subject_mask.insert(QStringLiteral("segmentationWidth"), 2);
  subject_mask.insert(QStringLiteral("segmentationHeight"), 2);
  subject_mask.insert(QStringLiteral("confidenceData"), QByteArray::fromHex("10203040"));
  subject_mask.insert(QStringLiteral("confidenceWidth"), 2);
  subject_mask.insert(QStringLiteral("confidenceHeight"), 2);
  subject_mask.insert(QStringLiteral("confidenceMapSchemaVersion"), 1);
  subject_mask.insert(QStringLiteral("schemaVersion"), 1);
  subject_mask.insert(QStringLiteral("sourceSchemaVersion"), 1);
  subject_mask.insert(QStringLiteral("rasterSchemaVersion"), 1);
  QVariantMap generated_dependencies;
  generated_dependencies.insert(QStringLiteral("sourceDimensions"), QStringLiteral("2x2"));
  generated_dependencies.insert(QStringLiteral("runtime"), QStringLiteral("Vision Semantic Mask"));
  generated_dependencies.insert(QStringLiteral("modelVersion"), QStringLiteral("semantic-mask-v1"));
  generated_dependencies.insert(QStringLiteral("promptType"), QStringLiteral("semantic:subject"));
  QVariantMap generated_metadata;
  generated_metadata.insert(QStringLiteral("schemaVersion"), 1);
  generated_metadata.insert(QStringLiteral("sourceSchemaVersion"), 1);
  generated_metadata.insert(QStringLiteral("rasterSchemaVersion"), 1);
  generated_metadata.insert(QStringLiteral("generatedOutputSchemaVersion"), 1);
  generated_metadata.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  generated_metadata.insert(QStringLiteral("runtime"), QStringLiteral("Vision Semantic Mask"));
  generated_metadata.insert(QStringLiteral("modelName"), QStringLiteral("Vision Semantic Mask"));
  generated_metadata.insert(QStringLiteral("modelVersion"), QStringLiteral("semantic-mask-v1"));
  generated_metadata.insert(QStringLiteral("promptType"), QStringLiteral("semantic:subject"));
  generated_metadata.insert(QStringLiteral("confidenceMapAvailable"), true);
  generated_metadata.insert(QStringLiteral("confidenceMapSchemaVersion"), 1);
  generated_metadata.insert(QStringLiteral("confidenceResolution"), QStringLiteral("2x2"));
  generated_metadata.insert(QStringLiteral("sourceWidth"), 2);
  generated_metadata.insert(QStringLiteral("sourceHeight"), 2);
  generated_metadata.insert(QStringLiteral("sourceResolution"), QStringLiteral("2x2"));
  generated_metadata.insert(QStringLiteral("dependencies"), generated_dependencies);
  subject_mask.insert(QStringLiteral("generatedMaskMetadata"), generated_metadata);
  subject_mask.insert(QStringLiteral("generatedMaskStale"), false);
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
      subject_geometry.value(QStringLiteral("segmentationHeight")).toInt() != 2 ||
      subject_geometry.value(QStringLiteral("rasterSchemaVersion")).toInt() != 1) {
    return fail("structured recipe should serialize AI subject segmentation payloads");
  }
  if (!subject_geometry.contains(QStringLiteral("confidenceData")) ||
      subject_geometry.value(QStringLiteral("confidenceWidth")).toInt() != 2 ||
      subject_geometry.value(QStringLiteral("confidenceHeight")).toInt() != 2 ||
      subject_geometry.value(QStringLiteral("confidenceMapSchemaVersion")).toInt() != 1) {
    return fail("structured recipe should serialize generated mask confidence payloads");
  }
  const auto subject_generated_metadata =
      subject_geometry.value(QStringLiteral("generatedMaskMetadata")).toObject();
  const auto subject_generated_dependencies =
      subject_generated_metadata.value(QStringLiteral("dependencies")).toObject();
  if (subject_generated_metadata.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      subject_generated_metadata.value(QStringLiteral("generatedOutputSchemaVersion")).toInt() != 1 ||
      subject_generated_metadata.value(QStringLiteral("modelName")).toString() !=
          QStringLiteral("Vision Semantic Mask") ||
      subject_generated_metadata.value(QStringLiteral("modelVersion")).toString() !=
          QStringLiteral("semantic-mask-v1") ||
      subject_generated_metadata.value(QStringLiteral("promptType")).toString() !=
          QStringLiteral("semantic:subject") ||
      !subject_generated_metadata.value(QStringLiteral("confidenceMapAvailable")).toBool() ||
      subject_generated_metadata.value(QStringLiteral("confidenceMapSchemaVersion")).toInt() != 1 ||
      subject_generated_metadata.value(QStringLiteral("confidenceResolution")).toString() !=
          QStringLiteral("2x2") ||
      subject_generated_metadata.value(QStringLiteral("sourceResolution")).toString() !=
          QStringLiteral("2x2") ||
      subject_generated_dependencies.value(QStringLiteral("promptType")).toString() !=
          QStringLiteral("semantic:subject")) {
    return fail("structured recipe should serialize generated mask provenance metadata");
  }
  const auto serialized_subject = subject_document
                                      .value(QStringLiteral("maskStack"))
                                      .toArray()
                                      .at(0)
                                      .toObject();
  if (serialized_subject.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      serialized_subject.value(QStringLiteral("sourceSchemaVersion")).toInt() != 1 ||
      serialized_subject.value(QStringLiteral("toolRole")).toString() != QStringLiteral("selection") ||
      !serialized_subject.value(QStringLiteral("committed")).toBool()) {
    return fail("structured recipe should preserve selection mask tool role and committed state");
  }
  const auto subject_adjustments = subject_document
                                        .value(QStringLiteral("maskStack"))
                                        .toArray()
                                        .at(0)
                                        .toObject()
                                        .value(QStringLiteral("adjustments"))
                                        .toObject();
  const auto subject_constraints = subject_document
                                       .value(QStringLiteral("maskStack"))
                                       .toArray()
                                       .at(0)
                                       .toObject()
                                       .value(QStringLiteral("constraints"))
                                       .toObject();
  if (!almost_equal(subject_adjustments.value(QStringLiteral("hueShift")).toDouble(), 24.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("saturationShift")).toDouble(), 18.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("colorizeStrength")).toDouble(), 72.0) ||
      !subject_adjustments.value(QStringLiteral("selectionInvert")).toBool() ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionExpand")).toDouble(), 18.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionEdgeContrast")).toDouble(), 62.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionDecontaminate")).toDouble(), 36.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionGrow")).toDouble(), 12.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionShrink")).toDouble(), 8.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionFeather")).toDouble(), 22.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionBlur")).toDouble(), 18.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionHarden")).toDouble(), 44.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionDespeckle")).toDouble(), 11.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionFillHoles")).toDouble(), 7.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("selectionRemoveIslands")).toDouble(), 9.0) ||
      !subject_adjustments.value(QStringLiteral("maskSetSelectionInvert")).toBool() ||
      !almost_equal(subject_adjustments.value(QStringLiteral("maskSetSelectionExpand")).toDouble(), -12.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("maskSetSelectionSmooth")).toDouble(), 14.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble(), 16.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble(), 18.0) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("maskSetDensity")).toDouble(), 0.72) ||
      !almost_equal(subject_adjustments.value(QStringLiteral("maskSetOpacity")).toDouble(), 0.64) ||
      subject_constraints.value(QStringLiteral("matteCleanupSchemaVersion")).toInt() != 1 ||
      subject_constraints.value(QStringLiteral("matteCleanupProfile")).toString() != QStringLiteral("subject-object") ||
      !subject_constraints.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool()) {
    return fail("structured recipe should serialize local recolor adjustments");
  }

  const auto reparsed_subject_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(subject_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_subject = reparsed_subject_mask.mask_stack.at(0).toMap();
  if (reparsed_subject_mask.mask_stack.size() != 1 ||
      reparsed_subject.value(QStringLiteral("kind")).toString() != QStringLiteral("subject") ||
      reparsed_subject.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      reparsed_subject.value(QStringLiteral("sourceSchemaVersion")).toInt() != 1 ||
      reparsed_subject.value(QStringLiteral("rasterSchemaVersion")).toInt() != 1 ||
      reparsed_subject.value(QStringLiteral("toolRole")).toString() != QStringLiteral("selection") ||
      !reparsed_subject.value(QStringLiteral("committed")).toBool() ||
      reparsed_subject.value(QStringLiteral("segmentationData")).toByteArray() != QByteArray::fromHex("00ff7f3f") ||
      reparsed_subject.value(QStringLiteral("confidenceData")).toByteArray() != QByteArray::fromHex("10203040") ||
      reparsed_subject.value(QStringLiteral("confidenceWidth")).toInt() != 2 ||
      reparsed_subject.value(QStringLiteral("confidenceHeight")).toInt() != 2 ||
      reparsed_subject.value(QStringLiteral("confidenceMapSchemaVersion")).toInt() != 1 ||
      reparsed_subject.value(QStringLiteral("generatedMaskMetadata")).toMap().value(QStringLiteral("modelVersion")).toString() !=
          QStringLiteral("semantic-mask-v1") ||
      !reparsed_subject.value(QStringLiteral("generatedMaskMetadata")).toMap().value(QStringLiteral("confidenceMapAvailable")).toBool() ||
      reparsed_subject.value(QStringLiteral("generatedMaskMetadata")).toMap().value(QStringLiteral("dependencies")).toMap().value(QStringLiteral("promptType")).toString() !=
          QStringLiteral("semantic:subject") ||
      !almost_equal(reparsed_subject.value(QStringLiteral("hueShift")).toDouble(), 24.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("saturationShift")).toDouble(), 18.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("colorizeHue")).toDouble(), 0.02) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("colorizeStrength")).toDouble(), 72.0) ||
      !reparsed_subject.value(QStringLiteral("selectionInvert")).toBool() ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionExpand")).toDouble(), 18.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionEdgeContrast")).toDouble(), 62.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionDecontaminate")).toDouble(), 36.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionGrow")).toDouble(), 12.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionShrink")).toDouble(), 8.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionFeather")).toDouble(), 22.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionBlur")).toDouble(), 18.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionHarden")).toDouble(), 44.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionDespeckle")).toDouble(), 11.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionFillHoles")).toDouble(), 7.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("selectionRemoveIslands")).toDouble(), 9.0) ||
      !reparsed_subject.value(QStringLiteral("maskSetSelectionInvert")).toBool() ||
      !almost_equal(reparsed_subject.value(QStringLiteral("maskSetSelectionExpand")).toDouble(), -12.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("maskSetSelectionSmooth")).toDouble(), 14.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble(), 16.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble(), 18.0) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("maskSetDensity")).toDouble(), 0.72) ||
      !almost_equal(reparsed_subject.value(QStringLiteral("maskSetOpacity")).toDouble(), 0.64) ||
      reparsed_subject.value(QStringLiteral("matteCleanupSchemaVersion")).toInt() != 1 ||
      reparsed_subject.value(QStringLiteral("matteCleanupProfile")).toString() != QStringLiteral("subject-object") ||
      !reparsed_subject.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool() ||
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

  EditRecipeState semantic_mask_state;
  QVariantList semantic_masks;
  for (const auto& kind : {
           QStringLiteral("sky"),
           QStringLiteral("background"),
           QStringLiteral("people"),
           QStringLiteral("face"),
           QStringLiteral("hair"),
           QStringLiteral("clothing"),
       }) {
    QVariantMap semantic_mask = subject_mask;
    semantic_mask.insert(QStringLiteral("id"), QStringLiteral("%1-ai").arg(kind));
    semantic_mask.insert(QStringLiteral("name"), QStringLiteral("AI %1").arg(kind));
    semantic_mask.insert(QStringLiteral("kind"), kind);
    semantic_masks.append(semantic_mask);
  }
  QVariantMap person_alias_mask = subject_mask;
  person_alias_mask.insert(QStringLiteral("id"), QStringLiteral("person-ai"));
  person_alias_mask.insert(QStringLiteral("kind"), QStringLiteral("person"));
  semantic_masks.append(person_alias_mask);
  semantic_mask_state.mask_stack = semantic_masks;
  const auto reparsed_semantic_masks =
      photo_editor::desktop::parse_edit_recipe_json(photo_editor::desktop::serialize_edit_recipe_json(semantic_mask_state));
  if (reparsed_semantic_masks.mask_stack.size() != 7) {
    return fail("structured recipe should preserve expanded AI mask targets");
  }
  const QStringList expected_semantic_kinds = {
      QStringLiteral("sky"),
      QStringLiteral("background"),
      QStringLiteral("people"),
      QStringLiteral("face"),
      QStringLiteral("hair"),
      QStringLiteral("clothing"),
      QStringLiteral("people"),
  };
  for (int index = 0; index < expected_semantic_kinds.size(); ++index) {
    const auto mask = reparsed_semantic_masks.mask_stack.at(index).toMap();
    if (mask.value(QStringLiteral("kind")).toString() != expected_semantic_kinds.at(index) ||
        mask.value(QStringLiteral("segmentationData")).toByteArray() != QByteArray::fromHex("00ff7f3f") ||
        mask.value(QStringLiteral("segmentationWidth")).toInt() != 2 ||
        mask.value(QStringLiteral("segmentationHeight")).toInt() != 2) {
      return fail("structured recipe should round-trip semantic AI mask payloads");
    }
  }

  EditRecipeState alpha_mask_state;
  QVariantMap alpha_mask = subject_mask;
  alpha_mask.insert(QStringLiteral("id"), QStringLiteral("imported-alpha"));
  alpha_mask.insert(QStringLiteral("name"), QStringLiteral("Imported Alpha"));
  alpha_mask.insert(QStringLiteral("kind"), QStringLiteral("alpha"));
  alpha_mask.insert(QStringLiteral("solo"), true);
  alpha_mask.insert(QStringLiteral("locked"), true);
  alpha_mask.insert(QStringLiteral("alphaSourcePath"), QStringLiteral("/tmp/matte.png"));
  alpha_mask.insert(QStringLiteral("alphaImportInvert"), true);
  alpha_mask.insert(QStringLiteral("alphaImportThreshold"), 0.42);
  alpha_mask.insert(QStringLiteral("alphaImportResampleMode"), QStringLiteral("Match Source"));
  alpha_mask.insert(QStringLiteral("refineLuminanceMin"), 0.25);
  alpha_mask.insert(QStringLiteral("refineLuminanceMax"), 0.88);
  alpha_mask.insert(QStringLiteral("refineHueCenter"), 0.12);
  alpha_mask.insert(QStringLiteral("refineHueRange"), 0.18);
  alpha_mask.insert(QStringLiteral("refineSaturationMin"), 0.30);
  alpha_mask.insert(QStringLiteral("refineSaturationMax"), 0.92);
  alpha_mask.insert(QStringLiteral("refineDepthMin"), 0.22);
  alpha_mask.insert(QStringLiteral("refineDepthMax"), 0.72);
  alpha_mask.insert(QStringLiteral("refineEdgeConfidenceMin"), 0.44);
  alpha_mask.insert(QStringLiteral("refineRangeFeather"), 0.06);
  alpha_mask_state.mask_stack = {alpha_mask};
  const auto reparsed_alpha_mask =
      photo_editor::desktop::parse_edit_recipe_json(photo_editor::desktop::serialize_edit_recipe_json(alpha_mask_state));
  const auto reparsed_alpha = reparsed_alpha_mask.mask_stack.at(0).toMap();
  if (reparsed_alpha_mask.mask_stack.size() != 1 ||
      reparsed_alpha.value(QStringLiteral("kind")).toString() != QStringLiteral("alpha") ||
      !reparsed_alpha.value(QStringLiteral("solo")).toBool() ||
      !reparsed_alpha.value(QStringLiteral("locked")).toBool() ||
      reparsed_alpha.value(QStringLiteral("alphaSourcePath")).toString() != QStringLiteral("/tmp/matte.png") ||
      !reparsed_alpha.value(QStringLiteral("alphaImportInvert")).toBool() ||
      !almost_equal(reparsed_alpha.value(QStringLiteral("alphaImportThreshold")).toDouble(), 0.42) ||
      reparsed_alpha.value(QStringLiteral("alphaImportResampleMode")).toString() != QStringLiteral("Match Source") ||
      !almost_equal(reparsed_alpha.value(QStringLiteral("refineLuminanceMin")).toDouble(), 0.25) ||
      !almost_equal(reparsed_alpha.value(QStringLiteral("refineHueCenter")).toDouble(), 0.12) ||
      !almost_equal(reparsed_alpha.value(QStringLiteral("refineSaturationMin")).toDouble(), 0.30) ||
      !almost_equal(reparsed_alpha.value(QStringLiteral("refineDepthMax")).toDouble(), 0.72) ||
      !almost_equal(reparsed_alpha.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble(), 0.44) ||
      !almost_equal(reparsed_alpha.value(QStringLiteral("refineRangeFeather")).toDouble(), 0.06) ||
      reparsed_alpha.value(QStringLiteral("segmentationData")).toByteArray() != QByteArray::fromHex("00ff7f3f") ||
      reparsed_alpha.value(QStringLiteral("segmentationWidth")).toInt() != 2 ||
      reparsed_alpha.value(QStringLiteral("segmentationHeight")).toInt() != 2) {
    return fail("structured recipe should round-trip custom alpha image mask payloads");
  }

  QVariantMap malformed_subject_mask = subject_mask;
  malformed_subject_mask.insert(QStringLiteral("segmentationData"), QByteArray::fromHex("ff"));
  malformed_subject_mask.insert(QStringLiteral("segmentationWidth"), 2);
  malformed_subject_mask.insert(QStringLiteral("segmentationHeight"), 2);
  const auto normalized_malformed_subject =
      photo_editor::desktop::normalized_mask_definition(malformed_subject_mask, 1);
  if (normalized_malformed_subject.contains(QStringLiteral("segmentationData")) ||
      normalized_malformed_subject.contains(QStringLiteral("segmentationWidth")) ||
      normalized_malformed_subject.contains(QStringLiteral("segmentationHeight"))) {
    return fail("mask normalization should discard malformed segmentation payloads");
  }
  if (!photo_editor::desktop::valid_segmentation_payload(QByteArray(4, '\0'), 2, 2)) {
    return fail("mask payload validation should accept matching bounded payload dimensions");
  }
  if (photo_editor::desktop::valid_segmentation_payload(
          QByteArray(4, '\0'),
          photo_editor::desktop::kMaxMaskPayloadEdge + 1,
          1)) {
    return fail("mask payload validation should reject oversized payload dimensions");
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

  EditRecipeState shape_mask_state;
  QVariantMap rectangle_mask;
  rectangle_mask.insert(QStringLiteral("id"), QStringLiteral("rectangle-select"));
  rectangle_mask.insert(QStringLiteral("name"), QStringLiteral("Rectangle Select"));
  rectangle_mask.insert(QStringLiteral("kind"), QStringLiteral("rectangle"));
  rectangle_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  rectangle_mask.insert(QStringLiteral("enabled"), true);
  rectangle_mask.insert(QStringLiteral("feather"), 0.09);
  rectangle_mask.insert(QStringLiteral("centerX"), 0.46);
  rectangle_mask.insert(QStringLiteral("centerY"), 0.58);
  rectangle_mask.insert(QStringLiteral("radiusX"), 0.28);
  rectangle_mask.insert(QStringLiteral("radiusY"), 0.16);
  rectangle_mask.insert(QStringLiteral("rotation"), 14.0);
  QVariantMap ellipse_mask;
  ellipse_mask.insert(QStringLiteral("id"), QStringLiteral("ellipse-select"));
  ellipse_mask.insert(QStringLiteral("name"), QStringLiteral("Ellipse Select"));
  ellipse_mask.insert(QStringLiteral("kind"), QStringLiteral("ellipse"));
  ellipse_mask.insert(QStringLiteral("operation"), QStringLiteral("subtract"));
  ellipse_mask.insert(QStringLiteral("enabled"), true);
  ellipse_mask.insert(QStringLiteral("feather"), 0.11);
  ellipse_mask.insert(QStringLiteral("centerX"), 0.53);
  ellipse_mask.insert(QStringLiteral("centerY"), 0.44);
  ellipse_mask.insert(QStringLiteral("radiusX"), 0.24);
  ellipse_mask.insert(QStringLiteral("radiusY"), 0.19);
  ellipse_mask.insert(QStringLiteral("rotation"), -18.0);
  shape_mask_state.mask_stack = {rectangle_mask, ellipse_mask};
  const auto shape_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(shape_mask_state).toUtf8()).object();
  const auto shape_stack = shape_document.value(QStringLiteral("maskStack")).toArray();
  const auto rectangle_geometry = shape_stack.at(0).toObject().value(QStringLiteral("geometry")).toObject();
  const auto ellipse_geometry = shape_stack.at(1).toObject().value(QStringLiteral("geometry")).toObject();
  if (shape_stack.size() != 2 ||
      shape_stack.at(0).toObject().value(QStringLiteral("kind")).toString() != QStringLiteral("rectangle") ||
      shape_stack.at(1).toObject().value(QStringLiteral("kind")).toString() != QStringLiteral("ellipse") ||
      !almost_equal(rectangle_geometry.value(QStringLiteral("radiusY")).toDouble(), 0.16) ||
      !almost_equal(ellipse_geometry.value(QStringLiteral("centerX")).toDouble(), 0.53)) {
    return fail("structured recipe should serialize rectangle and ellipse selection geometry");
  }
  const auto reparsed_shape_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(shape_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_rectangle = reparsed_shape_mask.mask_stack.at(0).toMap();
  const auto reparsed_ellipse = reparsed_shape_mask.mask_stack.at(1).toMap();
  if (reparsed_rectangle.value(QStringLiteral("kind")).toString() != QStringLiteral("rectangle") ||
      reparsed_ellipse.value(QStringLiteral("kind")).toString() != QStringLiteral("ellipse") ||
      !almost_equal(reparsed_rectangle.value(QStringLiteral("radiusX")).toDouble(), 0.28) ||
      !almost_equal(reparsed_ellipse.value(QStringLiteral("rotation")).toDouble(), -18.0)) {
    return fail("structured recipe should round-trip rectangle and ellipse mask geometry");
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
  clone_brush.insert(QStringLiteral("cloneAligned"), false);
  clone_brush.insert(
      QStringLiteral("cloneSources"),
      QVariantList{
          QVariantMap{
              {QStringLiteral("name"), QStringLiteral("Source 1")},
              {QStringLiteral("x"), 0.24},
              {QStringLiteral("y"), 0.38},
              {QStringLiteral("scale"), 1.35},
              {QStringLiteral("rotation"), 24.0},
              {QStringLiteral("aligned"), false},
          },
          QVariantMap{
              {QStringLiteral("name"), QStringLiteral("Source 2")},
              {QStringLiteral("x"), 0.68},
              {QStringLiteral("y"), 0.72},
              {QStringLiteral("scale"), 0.85},
              {QStringLiteral("rotation"), -12.0},
              {QStringLiteral("aligned"), true},
          },
      });
  clone_brush.insert(QStringLiteral("cloneSourceIndex"), 1);
  clone_brush.insert(QStringLiteral("brushSize"), 0.07);
  clone_brush.insert(QStringLiteral("brushFlow"), 0.88);
  clone_brush.insert(QStringLiteral("committed"), false);
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
      clone_document.value(QStringLiteral("maskStack")).toArray().at(0).toObject().value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
      clone_document.value(QStringLiteral("maskStack")).toArray().at(0).toObject().value(QStringLiteral("committed")).toBool() ||
      clone_document.value(QStringLiteral("retouchStack")).toArray().size() != 1 ||
      !clone_geometry.value(QStringLiteral("cloneSourceSet")).toBool() ||
      !almost_equal(clone_geometry.value(QStringLiteral("cloneScale")).toDouble(), 1.35) ||
      clone_geometry.value(QStringLiteral("cloneAligned")).toBool(true) ||
      clone_geometry.value(QStringLiteral("cloneSources")).toArray().size() != 2 ||
      clone_geometry.value(QStringLiteral("cloneSourceIndex")).toInt() != 1 ||
      clone_geometry.value(QStringLiteral("brushPoints")).toArray().size() != 2) {
    return fail("structured recipe should serialize clone brush metadata");
  }
  const auto reparsed_clone_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(clone_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_clone = reparsed_clone_mask.mask_stack.at(0).toMap();
  if (reparsed_clone.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("clone") ||
      reparsed_clone.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
      reparsed_clone.value(QStringLiteral("committed")).toBool() ||
      !reparsed_clone.value(QStringLiteral("cloneSourceSet")).toBool() ||
      !almost_equal(reparsed_clone.value(QStringLiteral("cloneOffsetX")).toDouble(), -0.15) ||
      !almost_equal(reparsed_clone.value(QStringLiteral("cloneScale")).toDouble(), 1.35) ||
      !almost_equal(reparsed_clone.value(QStringLiteral("cloneRotation")).toDouble(), 24.0) ||
      reparsed_clone.value(QStringLiteral("cloneAligned"), true).toBool() ||
      reparsed_clone.value(QStringLiteral("cloneSources")).toList().size() != 2 ||
      reparsed_clone.value(QStringLiteral("cloneSourceIndex")).toInt() != 1 ||
      !almost_equal(
          reparsed_clone.value(QStringLiteral("cloneSources"))
              .toList()
              .at(1)
              .toMap()
              .value(QStringLiteral("scale"))
              .toDouble(),
          0.85) ||
      reparsed_clone.value(QStringLiteral("brushPoints")).toList().size() != 2) {
    return fail("structured recipe should round-trip clone brush metadata");
  }

  EditRecipeState heal_brush_state;
  QVariantMap heal_brush;
  heal_brush.insert(QStringLiteral("id"), QStringLiteral("heal-brush"));
  heal_brush.insert(QStringLiteral("name"), QStringLiteral("Heal Brush"));
  heal_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  heal_brush.insert(QStringLiteral("operation"), QStringLiteral("add"));
  heal_brush.insert(QStringLiteral("retouchMode"), QStringLiteral("heal"));
  heal_brush.insert(QStringLiteral("removeStrength"), 82.0);
  heal_brush.insert(QStringLiteral("healTextureBlend"), 0.24);
  heal_brush.insert(QStringLiteral("healColorBlend"), 0.82);
  heal_brush.insert(QStringLiteral("brushSize"), 0.06);
  heal_brush.insert(QStringLiteral("brushFlow"), 0.78);
  heal_brush.insert(QStringLiteral("committed"), false);
  heal_brush.insert(
      QStringLiteral("brushPoints"),
      QVariantList{brush_point(0.36, 0.44, 0.06, 0.78)});
  heal_brush_state.mask_stack = {heal_brush};
  const auto heal_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(heal_brush_state).toUtf8()).object();
  const auto heal_geometry = heal_document
                                 .value(QStringLiteral("maskStack"))
                                 .toArray()
                                 .at(0)
                                 .toObject()
                                 .value(QStringLiteral("geometry"))
                                 .toObject();
  if (heal_geometry.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("heal") ||
      !almost_equal(heal_geometry.value(QStringLiteral("healTextureBlend")).toDouble(), 0.24) ||
      !almost_equal(heal_geometry.value(QStringLiteral("healColorBlend")).toDouble(), 0.82)) {
    return fail("structured recipe should serialize heal texture and color blend controls");
  }
  const auto reparsed_heal_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(heal_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_heal = reparsed_heal_mask.mask_stack.at(0).toMap();
  if (reparsed_heal.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("heal") ||
      !almost_equal(reparsed_heal.value(QStringLiteral("healTextureBlend")).toDouble(), 0.24) ||
      !almost_equal(reparsed_heal.value(QStringLiteral("healColorBlend")).toDouble(), 0.82)) {
    return fail("structured recipe should round-trip heal texture and color blend controls");
  }

  EditRecipeState patch_brush_state;
  QVariantMap patch_brush;
  patch_brush.insert(QStringLiteral("id"), QStringLiteral("patch-brush"));
  patch_brush.insert(QStringLiteral("name"), QStringLiteral("Patch"));
  patch_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  patch_brush.insert(QStringLiteral("operation"), QStringLiteral("add"));
  patch_brush.insert(QStringLiteral("retouchMode"), QStringLiteral("patch"));
  patch_brush.insert(QStringLiteral("patchSourceSet"), true);
  patch_brush.insert(QStringLiteral("patchSourceX"), 0.12);
  patch_brush.insert(QStringLiteral("patchSourceY"), 0.18);
  patch_brush.insert(QStringLiteral("patchSourceWidth"), 0.24);
  patch_brush.insert(QStringLiteral("patchSourceHeight"), 0.16);
  patch_brush.insert(QStringLiteral("brushSize"), 0.08);
  patch_brush.insert(QStringLiteral("brushFlow"), 0.86);
  patch_brush.insert(QStringLiteral("committed"), false);
  patch_brush.insert(
      QStringLiteral("brushPoints"),
      QVariantList{brush_point(0.42, 0.52, 0.08, 0.86)});
  patch_brush_state.mask_stack = {patch_brush};
  const auto patch_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(patch_brush_state).toUtf8()).object();
  const auto patch_geometry = patch_document
                                  .value(QStringLiteral("maskStack"))
                                  .toArray()
                                  .at(0)
                                  .toObject()
                                  .value(QStringLiteral("geometry"))
                                  .toObject();
  if (patch_geometry.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("patch") ||
      !patch_geometry.value(QStringLiteral("patchSourceSet")).toBool() ||
      !almost_equal(patch_geometry.value(QStringLiteral("patchSourceX")).toDouble(), 0.12) ||
      !almost_equal(patch_geometry.value(QStringLiteral("patchSourceY")).toDouble(), 0.18) ||
      !almost_equal(patch_geometry.value(QStringLiteral("patchSourceWidth")).toDouble(), 0.24) ||
      !almost_equal(patch_geometry.value(QStringLiteral("patchSourceHeight")).toDouble(), 0.16)) {
    return fail("structured recipe should serialize patch source region controls");
  }
  const auto reparsed_patch_mask =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(patch_document).toJson(QJsonDocument::Compact)));
  const auto reparsed_patch = reparsed_patch_mask.mask_stack.at(0).toMap();
  if (reparsed_patch.value(QStringLiteral("retouchMode")).toString() != QStringLiteral("patch") ||
      !reparsed_patch.value(QStringLiteral("patchSourceSet")).toBool() ||
      !almost_equal(reparsed_patch.value(QStringLiteral("patchSourceX")).toDouble(), 0.12) ||
      !almost_equal(reparsed_patch.value(QStringLiteral("patchSourceY")).toDouble(), 0.18) ||
      !almost_equal(reparsed_patch.value(QStringLiteral("patchSourceWidth")).toDouble(), 0.24) ||
      !almost_equal(reparsed_patch.value(QStringLiteral("patchSourceHeight")).toDouble(), 0.16)) {
    return fail("structured recipe should round-trip patch source region controls");
  }

  EditRecipeState remove_object_state;
  QVariantMap remove_object_mask;
  remove_object_mask.insert(QStringLiteral("id"), QStringLiteral("remove-object"));
  remove_object_mask.insert(QStringLiteral("name"), QStringLiteral("Remove Object"));
  remove_object_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  remove_object_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  remove_object_mask.insert(QStringLiteral("removeStrength"), 100.0);
  remove_object_mask.insert(QStringLiteral("segmentationData"), QByteArray::fromHex("ff00"));
  remove_object_mask.insert(QStringLiteral("segmentationWidth"), 2);
  remove_object_mask.insert(QStringLiteral("segmentationHeight"), 1);
  remove_object_mask.insert(QStringLiteral("inpaintPatchData"), QByteArray("patch-png"));
  remove_object_mask.insert(QStringLiteral("inpaintPatchX"), 3);
  remove_object_mask.insert(QStringLiteral("inpaintPatchY"), 5);
  remove_object_mask.insert(QStringLiteral("inpaintPatchWidth"), 7);
  remove_object_mask.insert(QStringLiteral("inpaintPatchHeight"), 9);
  remove_object_mask.insert(QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB"));
  remove_object_mask.insert(QStringLiteral("inpaintModelName"), QStringLiteral("cpu-diffusion-inpaint-v1"));
  remove_object_mask.insert(QStringLiteral("inpaintPatchPayloadBytes"), 1234);
  remove_object_mask.insert(QStringLiteral("inpaintCandidatePayloadBytes"), 5678);
  remove_object_mask.insert(QStringLiteral("inpaintCandidateEvictedCount"), 2);
  remove_object_mask.insert(QStringLiteral("inpaintSourceMaskData"), QByteArray::fromHex("ff00"));
  remove_object_mask.insert(QStringLiteral("inpaintSourceMaskWidth"), 2);
  remove_object_mask.insert(QStringLiteral("inpaintSourceMaskHeight"), 1);
  remove_object_mask.insert(QStringLiteral("inpaintSourceBoundsX"), 0);
  remove_object_mask.insert(QStringLiteral("inpaintSourceBoundsY"), 0);
  remove_object_mask.insert(QStringLiteral("inpaintSourceBoundsWidth"), 1);
  remove_object_mask.insert(QStringLiteral("inpaintSourceBoundsHeight"), 1);
  QVariantList remove_object_candidates;
  for (int candidate_index = 0; candidate_index < 3; ++candidate_index) {
    QVariantMap candidate;
    candidate.insert(QStringLiteral("patchData"), QByteArray("candidate-png-") + QByteArray::number(candidate_index));
    candidate.insert(QStringLiteral("patchX"), 3);
    candidate.insert(QStringLiteral("patchY"), 5);
    candidate.insert(QStringLiteral("patchWidth"), 7);
    candidate.insert(QStringLiteral("patchHeight"), 9);
    candidate.insert(QStringLiteral("patchColorSpace"), QStringLiteral("sRGB"));
    candidate.insert(QStringLiteral("modelName"), QStringLiteral("cpu-diffusion-inpaint-v1"));
    candidate.insert(QStringLiteral("index"), candidate_index);
    candidate.insert(QStringLiteral("seed"), 43 + candidate_index);
    remove_object_candidates.append(candidate);
  }
  remove_object_mask.insert(QStringLiteral("inpaintCandidates"), remove_object_candidates);
  remove_object_mask.insert(QStringLiteral("inpaintCandidateCount"), remove_object_candidates.size());
  remove_object_mask.insert(QStringLiteral("inpaintCandidateIndex"), 1);
  remove_object_mask.insert(QStringLiteral("inpaintSelectedVariationIndex"), 1);
  remove_object_mask.insert(QStringLiteral("inpaintSelectedVariationSeed"), 44);
  remove_object_mask.insert(
      QStringLiteral("inpaintSelectedVariationModelName"),
      QStringLiteral("cpu-diffusion-inpaint-v1"));
  remove_object_mask.insert(QStringLiteral("inpaintPrompt"), QStringLiteral("preserve fine grain texture"));
  remove_object_mask.insert(QStringLiteral("inpaintSeed"), 43);
  remove_object_mask.insert(QStringLiteral("inpaintBlendAmount"), 0.42);
  remove_object_mask.insert(QStringLiteral("inpaintSeamFeather"), 6.0);
  remove_object_mask.insert(QStringLiteral("inpaintColorMatch"), 0.63);
  remove_object_mask.insert(QStringLiteral("inpaintQualityMode"), QStringLiteral("final"));
  remove_object_mask.insert(QStringLiteral("inpaintFinalRequired"), false);
  remove_object_mask.insert(QStringLiteral("inpaintFallback"), true);
  remove_object_mask.insert(QStringLiteral("inpaintFallbackReason"), QStringLiteral("LaMa unavailable"));
  remove_object_mask.insert(QStringLiteral("inpaintRegionKind"), QStringLiteral("large-object"));
  remove_object_mask.insert(QStringLiteral("inpaintEdgeContinuation"), true);
  remove_object_mask.insert(QStringLiteral("inpaintTextureSynthesis"), true);
  remove_object_mask.insert(QStringLiteral("inpaintTextureStrength"), 0.34);
  remove_object_mask.insert(QStringLiteral("inpaintRegionCoverage"), 0.42);
  remove_object_mask.insert(QStringLiteral("inpaintRegionX"), 3);
  remove_object_mask.insert(QStringLiteral("inpaintRegionY"), 5);
  remove_object_mask.insert(QStringLiteral("inpaintRegionWidth"), 7);
  remove_object_mask.insert(QStringLiteral("inpaintRegionHeight"), 9);
  remove_object_state.mask_stack = {remove_object_mask};
  const auto remove_object_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(remove_object_state).toUtf8()).object();
  const auto remove_object_geometry = remove_object_document
                                          .value(QStringLiteral("maskStack"))
                                          .toArray()
                                          .at(0)
                                          .toObject()
                                          .value(QStringLiteral("geometry"))
                                          .toObject();
  if (remove_object_document.value(QStringLiteral("maskStack")).toArray().at(0).toObject().value(QStringLiteral("toolRole")).toString() !=
          QStringLiteral("retouch") ||
      remove_object_document.value(QStringLiteral("retouchStack")).toArray().size() != 1 ||
      remove_object_document.value(QStringLiteral("retouchStack")).toArray().at(0).toObject().value(QStringLiteral("retouchOrder")).toInt() != 1 ||
      remove_object_geometry.value(QStringLiteral("inpaintPatchData")).toString().isEmpty() ||
      remove_object_geometry.value(QStringLiteral("inpaintPatchSchemaVersion")).toInt() != 1 ||
      remove_object_geometry.value(QStringLiteral("inpaintPatchColorSpace")).toString() !=
          QStringLiteral("sRGB") ||
      remove_object_geometry.value(QStringLiteral("inpaintPatchWidth")).toInt() != 7 ||
      remove_object_geometry.value(QStringLiteral("inpaintPatchPayloadBytes")).toInt() != 1234 ||
      remove_object_geometry.value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt() != 5678 ||
      remove_object_geometry.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt() != 2 ||
      remove_object_geometry.value(QStringLiteral("inpaintSourceMaskData")).toString().isEmpty() ||
      remove_object_geometry.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt() != 1 ||
      remove_object_geometry.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt() != 1 ||
      remove_object_geometry.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() != 44 ||
      !remove_object_geometry.value(QStringLiteral("inpaintFallback")).toBool() ||
      remove_object_geometry.value(QStringLiteral("inpaintPrompt")).toString() !=
          QStringLiteral("preserve fine grain texture") ||
      remove_object_geometry.value(QStringLiteral("inpaintSeed")).toInt() != 43 ||
      std::abs(remove_object_geometry.value(QStringLiteral("inpaintBlendAmount")).toDouble() - 0.42) > 1e-6 ||
      std::abs(remove_object_geometry.value(QStringLiteral("inpaintSeamFeather")).toDouble() - 6.0) > 1e-6 ||
      std::abs(remove_object_geometry.value(QStringLiteral("inpaintColorMatch")).toDouble() - 0.63) > 1e-6 ||
      remove_object_geometry.value(QStringLiteral("inpaintQualityMode")).toString() != QStringLiteral("final") ||
      remove_object_geometry.value(QStringLiteral("inpaintFinalRequired")).toBool(true) ||
      remove_object_geometry.value(QStringLiteral("inpaintCandidates")).toArray().size() != 3 ||
      remove_object_geometry.value(QStringLiteral("inpaintRegionKind")).toString() !=
          QStringLiteral("large-object") ||
      !remove_object_geometry.value(QStringLiteral("inpaintEdgeContinuation")).toBool() ||
      !remove_object_geometry.value(QStringLiteral("inpaintTextureSynthesis")).toBool()) {
    return fail("remove-only object masks should serialize into the retouch stack");
  }
  const auto reparsed_remove_object =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(remove_object_document).toJson(QJsonDocument::Compact)));
  if (reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("toolRole")).toString() !=
          QStringLiteral("retouch") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("retouchOrder")).toInt() != 1 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          QByteArray("patch-png") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintPatchSchemaVersion")).toInt() != 1 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintPatchColorSpace")).toString() !=
          QStringLiteral("sRGB") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintModelName")).toString() !=
          QStringLiteral("cpu-diffusion-inpaint-v1") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintPatchPayloadBytes")).toInt() != 1234 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt() != 5678 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintCandidateEvictedCount")).toInt() != 2 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintSourceMaskData")).toByteArray() !=
          QByteArray::fromHex("ff00") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintSourceBoundsWidth")).toInt() != 1 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintSelectedVariationIndex")).toInt() != 1 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintSelectedVariationSeed")).toInt() != 44 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintSelectedVariationModelName")).toString() !=
          QStringLiteral("cpu-diffusion-inpaint-v1") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintPrompt")).toString() !=
          QStringLiteral("preserve fine grain texture") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintSeed")).toInt() != 43 ||
      std::abs(reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintBlendAmount")).toDouble() -
               0.42) > 1e-6 ||
      std::abs(reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintSeamFeather")).toDouble() -
               6.0) > 1e-6 ||
      std::abs(reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintColorMatch")).toDouble() -
               0.63) > 1e-6 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintQualityMode")).toString() !=
          QStringLiteral("final") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintFinalRequired"), true).toBool() ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintCandidates")).toList().size() != 3 ||
      reparsed_remove_object.mask_stack.at(0)
              .toMap()
              .value(QStringLiteral("inpaintCandidates"))
              .toList()
              .at(1)
              .toMap()
              .value(QStringLiteral("patchColorSpace"))
              .toString() != QStringLiteral("sRGB") ||
      !reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintFallback")).toBool() ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintFallbackReason")).toString() !=
          QStringLiteral("LaMa unavailable") ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintRegionKind")).toString() !=
          QStringLiteral("large-object") ||
      !reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintEdgeContinuation")).toBool() ||
      !reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintTextureSynthesis")).toBool() ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintTextureStrength")).toDouble() < 0.3 ||
      reparsed_remove_object.mask_stack.at(0).toMap().value(QStringLiteral("inpaintRegionCoverage")).toDouble() < 0.4) {
    return fail("remove-only object masks should round-trip as retouch entries with inpaint patch payloads");
  }

  EditRecipeState retouch_order_state;
  auto remove_order_mask = photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 1);
  remove_order_mask.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  remove_order_mask.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  remove_order_mask.insert(QStringLiteral("name"), QStringLiteral("First remove"));
  remove_order_mask.insert(QStringLiteral("retouchOrder"), 99);
  auto heal_order_mask = photo_editor::desktop::default_mask_definition(QStringLiteral("brush"), 2);
  heal_order_mask.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  heal_order_mask.insert(QStringLiteral("retouchMode"), QStringLiteral("heal"));
  heal_order_mask.insert(QStringLiteral("name"), QStringLiteral("Second heal"));
  heal_order_mask.insert(QStringLiteral("retouchOrder"), 1);
  retouch_order_state.mask_stack = {remove_order_mask, heal_order_mask};
  const auto retouch_order_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(retouch_order_state).toUtf8()).object();
  const auto retouch_order_mask_stack = retouch_order_document.value(QStringLiteral("maskStack")).toArray();
  const auto retouch_order_stack = retouch_order_document.value(QStringLiteral("retouchStack")).toArray();
  if (retouch_order_mask_stack.size() != 2 || retouch_order_stack.size() != 2 ||
      retouch_order_mask_stack.at(0).toObject().value(QStringLiteral("retouchOrder")).toInt() != 1 ||
      retouch_order_mask_stack.at(1).toObject().value(QStringLiteral("retouchOrder")).toInt() != 2 ||
      retouch_order_stack.at(0).toObject().value(QStringLiteral("retouchOrder")).toInt() != 1 ||
      retouch_order_stack.at(1).toObject().value(QStringLiteral("retouchOrder")).toInt() != 2) {
    return fail("retouch order should serialize as compact deterministic recipe order");
  }
  const auto reparsed_retouch_order = photo_editor::desktop::parse_edit_recipe_json(
      QString::fromUtf8(QJsonDocument(retouch_order_document).toJson(QJsonDocument::Compact)));
  if (reparsed_retouch_order.mask_stack.size() != 2 ||
      reparsed_retouch_order.mask_stack.at(0).toMap().value(QStringLiteral("retouchOrder")).toInt() != 1 ||
      reparsed_retouch_order.mask_stack.at(1).toMap().value(QStringLiteral("retouchOrder")).toInt() != 2 ||
      reparsed_retouch_order.mask_stack.at(0).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("remove") ||
      reparsed_retouch_order.mask_stack.at(1).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("heal")) {
    return fail("retouch order should round-trip through retouchStack without stale order values");
  }

  const auto valid_remove_recipe = photo_editor::desktop::validate_edit_recipe_json(
      compact_json(remove_object_document));
  if (!valid_remove_recipe.valid || !valid_remove_recipe.errors.isEmpty()) {
    return fail("validator should accept structured recipes with valid retouch payloads");
  }

  auto invalid_segmentation_document = remove_object_document;
  auto invalid_segmentation_masks =
      invalid_segmentation_document.value(QStringLiteral("maskStack")).toArray();
  auto invalid_segmentation_mask = invalid_segmentation_masks.at(0).toObject();
  auto invalid_segmentation_geometry =
      invalid_segmentation_mask.value(QStringLiteral("geometry")).toObject();
  invalid_segmentation_geometry.insert(
      QStringLiteral("segmentationData"),
      QString::fromLatin1(QByteArray::fromHex("ff").toBase64()));
  invalid_segmentation_geometry.insert(QStringLiteral("segmentationWidth"), 2);
  invalid_segmentation_geometry.insert(QStringLiteral("segmentationHeight"), 2);
  invalid_segmentation_mask.insert(QStringLiteral("geometry"), invalid_segmentation_geometry);
  invalid_segmentation_masks.replace(0, invalid_segmentation_mask);
  invalid_segmentation_document.insert(QStringLiteral("maskStack"), invalid_segmentation_masks);

  const auto invalid_segmentation = photo_editor::desktop::validate_edit_recipe_json(
      compact_json(invalid_segmentation_document));
  if (invalid_segmentation.valid ||
      !errors_contain(invalid_segmentation, QStringLiteral("segmentation payload size does not match dimensions"))) {
    return fail("validator should report malformed segmentation payload dimensions");
  }

  auto oversized_confidence_document = remove_object_document;
  auto oversized_confidence_masks =
      oversized_confidence_document.value(QStringLiteral("maskStack")).toArray();
  auto oversized_confidence_mask = oversized_confidence_masks.at(0).toObject();
  auto oversized_confidence_geometry =
      oversized_confidence_mask.value(QStringLiteral("geometry")).toObject();
  oversized_confidence_geometry.insert(
      QStringLiteral("confidenceData"),
      QString::fromLatin1(QByteArray::fromHex("ff").toBase64()));
  oversized_confidence_geometry.insert(
      QStringLiteral("confidenceWidth"),
      photo_editor::desktop::kMaxMaskPayloadEdge + 1);
  oversized_confidence_geometry.insert(QStringLiteral("confidenceHeight"), 1);
  oversized_confidence_mask.insert(QStringLiteral("geometry"), oversized_confidence_geometry);
  oversized_confidence_masks.replace(0, oversized_confidence_mask);
  oversized_confidence_document.insert(QStringLiteral("maskStack"), oversized_confidence_masks);

  const auto oversized_confidence = photo_editor::desktop::validate_edit_recipe_json(
      compact_json(oversized_confidence_document));
  if (oversized_confidence.valid ||
      !errors_contain(oversized_confidence, QStringLiteral("confidence payload size does not match dimensions"))) {
    return fail("validator should report oversized generated confidence payload dimensions");
  }

  EditRecipeState optics_state;
  optics_state.sharpening = 35.0;
  optics_state.sharpen_radius = 2.2;
  optics_state.sharpen_detail = 58.0;
  optics_state.sharpen_masking = 44.0;
  optics_state.lens_distortion = -18.0;
  optics_state.lens_vignette = 26.0;
  optics_state.chromatic_aberration = 42.0;
  optics_state.perspective_vertical = -31.0;
  optics_state.perspective_horizontal = 17.0;
  optics_state.upright_mode = QStringLiteral("Guided");
  optics_state.upright_vertical_guide_angle = 12.0;
  optics_state.upright_horizontal_guide_angle = -8.0;
  optics_state.transform_rotation = 3.5;
  optics_state.transform_scale = 1.25;
  optics_state.transform_skew_x = -14.0;
  optics_state.transform_skew_y = 9.0;
  optics_state.liquify_center_x = 0.42;
  optics_state.liquify_center_y = 0.58;
  optics_state.liquify_radius = 0.33;
  optics_state.liquify_strength = 72.0;
  optics_state.content_aware_crop_fill = true;
  const auto optics_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(optics_state).toUtf8()).object();
  const auto global_adjustments_object = optics_document.value(QStringLiteral("globalAdjustments")).toObject();
  const auto optics_object = optics_document.value(QStringLiteral("optics")).toObject();
  const auto optics_crop_object = optics_document.value(QStringLiteral("cropTransform")).toObject();
  if (!almost_equal(global_adjustments_object.value(QStringLiteral("sharpening")).toDouble(), 35.0) ||
      !almost_equal(global_adjustments_object.value(QStringLiteral("sharpenRadius")).toDouble(), 2.2) ||
      !almost_equal(global_adjustments_object.value(QStringLiteral("sharpenDetail")).toDouble(), 58.0) ||
      !almost_equal(global_adjustments_object.value(QStringLiteral("sharpenMasking")).toDouble(), 44.0) ||
      !almost_equal(optics_object.value(QStringLiteral("lensDistortion")).toDouble(), -18.0) ||
      !almost_equal(optics_object.value(QStringLiteral("lensVignette")).toDouble(), 26.0) ||
      !almost_equal(optics_object.value(QStringLiteral("chromaticAberration")).toDouble(), 42.0) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("perspectiveVertical")).toDouble(), -31.0) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("perspectiveHorizontal")).toDouble(), 17.0) ||
      optics_crop_object.value(QStringLiteral("uprightMode")).toString() != QStringLiteral("Guided") ||
      !almost_equal(optics_crop_object.value(QStringLiteral("uprightVerticalGuideAngle")).toDouble(), 12.0) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("uprightHorizontalGuideAngle")).toDouble(), -8.0) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("rotation")).toDouble(), 3.5) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("scale")).toDouble(), 1.25) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("skewX")).toDouble(), -14.0) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("skewY")).toDouble(), 9.0) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("liquifyCenterX")).toDouble(), 0.42) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("liquifyCenterY")).toDouble(), 0.58) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("liquifyRadius")).toDouble(), 0.33) ||
      !almost_equal(optics_crop_object.value(QStringLiteral("liquifyStrength")).toDouble(), 72.0) ||
      !optics_crop_object.value(QStringLiteral("contentAwareFill")).toBool()) {
    return fail("structured recipe should serialize optics and geometry controls");
  }

  const auto reparsed_optics =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(optics_document).toJson(QJsonDocument::Compact)));
  if (!almost_equal(reparsed_optics.lens_distortion, optics_state.lens_distortion) ||
      !almost_equal(reparsed_optics.sharpening, optics_state.sharpening) ||
      !almost_equal(reparsed_optics.sharpen_radius, optics_state.sharpen_radius) ||
      !almost_equal(reparsed_optics.sharpen_detail, optics_state.sharpen_detail) ||
      !almost_equal(reparsed_optics.sharpen_masking, optics_state.sharpen_masking) ||
      !almost_equal(reparsed_optics.lens_vignette, optics_state.lens_vignette) ||
      !almost_equal(reparsed_optics.chromatic_aberration, optics_state.chromatic_aberration) ||
      !almost_equal(reparsed_optics.perspective_vertical, optics_state.perspective_vertical) ||
      !almost_equal(reparsed_optics.perspective_horizontal, optics_state.perspective_horizontal) ||
      reparsed_optics.upright_mode != optics_state.upright_mode ||
      !almost_equal(reparsed_optics.upright_vertical_guide_angle, optics_state.upright_vertical_guide_angle) ||
      !almost_equal(reparsed_optics.upright_horizontal_guide_angle, optics_state.upright_horizontal_guide_angle) ||
      !almost_equal(reparsed_optics.transform_rotation, optics_state.transform_rotation) ||
      !almost_equal(reparsed_optics.transform_scale, optics_state.transform_scale) ||
      !almost_equal(reparsed_optics.transform_skew_x, optics_state.transform_skew_x) ||
      !almost_equal(reparsed_optics.transform_skew_y, optics_state.transform_skew_y) ||
      !almost_equal(reparsed_optics.liquify_center_x, optics_state.liquify_center_x) ||
      !almost_equal(reparsed_optics.liquify_center_y, optics_state.liquify_center_y) ||
      !almost_equal(reparsed_optics.liquify_radius, optics_state.liquify_radius) ||
      !almost_equal(reparsed_optics.liquify_strength, optics_state.liquify_strength) ||
      !reparsed_optics.content_aware_crop_fill) {
    return fail("structured recipe should round-trip optics and geometry controls");
  }

  EditRecipeState raw_state;
  raw_state.raw_exposure = 0.85;
  raw_state.raw_boost = 0.42;
  raw_state.raw_shadow_boost = 1.35;
  raw_state.raw_detail = 0.66;
  raw_state.raw_chroma_noise_reduction = 0.47;
  raw_state.raw_moire_reduction = 0.36;
  raw_state.raw_hot_dead_pixel_cleanup = 0.52;
  raw_state.raw_false_color_suppression = 0.28;
  raw_state.raw_demosaic_quality = QStringLiteral("Full");
  raw_state.raw_highlight_recovery = false;
  raw_state.raw_gamut_mapping = false;
  raw_state.raw_lens_correction = false;
  raw_state.raw_white_balance_override = true;
  raw_state.raw_white_balance_temperature = 6100.0;
  raw_state.raw_white_balance_tint = -18.0;
  raw_state.raw_black_level = 0.012;
  raw_state.raw_white_level = 1.18;
  raw_state.raw_calibration_hue = -12.0;
  raw_state.raw_calibration_saturation = 18.0;
  raw_state.raw_red_primary_hue = 9.0;
  raw_state.raw_red_primary_saturation = 22.0;
  raw_state.raw_green_primary_hue = -7.0;
  raw_state.raw_green_primary_saturation = -14.0;
  raw_state.raw_blue_primary_hue = 11.0;
  raw_state.raw_blue_primary_saturation = 16.0;
  const auto raw_document =
      QJsonDocument::fromJson(photo_editor::desktop::serialize_edit_recipe_json(raw_state).toUtf8()).object();
  const auto raw_object = raw_document.value(QStringLiteral("rawAdjustments")).toObject();
  if (!almost_equal(raw_object.value(QStringLiteral("exposure")).toDouble(), 0.85) ||
      !almost_equal(raw_object.value(QStringLiteral("boost")).toDouble(), 0.42) ||
      !almost_equal(raw_object.value(QStringLiteral("shadowBoost")).toDouble(), 1.35) ||
      !almost_equal(raw_object.value(QStringLiteral("detail")).toDouble(), 0.66) ||
      !almost_equal(raw_object.value(QStringLiteral("chromaNoiseReduction")).toDouble(), 0.47) ||
      !almost_equal(raw_object.value(QStringLiteral("moireReduction")).toDouble(), 0.36) ||
      !almost_equal(raw_object.value(QStringLiteral("hotDeadPixelCleanup")).toDouble(), 0.52) ||
      !almost_equal(raw_object.value(QStringLiteral("falseColorSuppression")).toDouble(), 0.28) ||
      raw_object.value(QStringLiteral("demosaicQuality")).toString() != QStringLiteral("Full") ||
      raw_object.value(QStringLiteral("highlightRecovery")).toBool() ||
      raw_object.value(QStringLiteral("gamutMapping")).toBool() ||
      raw_object.value(QStringLiteral("lensCorrection")).toBool() ||
      !raw_object.value(QStringLiteral("whiteBalanceOverride")).toBool() ||
      !almost_equal(raw_object.value(QStringLiteral("whiteBalanceTemperature")).toDouble(), 6100.0) ||
      !almost_equal(raw_object.value(QStringLiteral("whiteBalanceTint")).toDouble(), -18.0) ||
      !almost_equal(raw_object.value(QStringLiteral("blackLevel")).toDouble(), 0.012) ||
      !almost_equal(raw_object.value(QStringLiteral("whiteLevel")).toDouble(), 1.18) ||
      !almost_equal(raw_object.value(QStringLiteral("calibrationHue")).toDouble(), -12.0) ||
      !almost_equal(raw_object.value(QStringLiteral("calibrationSaturation")).toDouble(), 18.0) ||
      !almost_equal(raw_object.value(QStringLiteral("redPrimaryHue")).toDouble(), 9.0) ||
      !almost_equal(raw_object.value(QStringLiteral("redPrimarySaturation")).toDouble(), 22.0) ||
      !almost_equal(raw_object.value(QStringLiteral("greenPrimaryHue")).toDouble(), -7.0) ||
      !almost_equal(raw_object.value(QStringLiteral("greenPrimarySaturation")).toDouble(), -14.0) ||
      !almost_equal(raw_object.value(QStringLiteral("bluePrimaryHue")).toDouble(), 11.0) ||
      !almost_equal(raw_object.value(QStringLiteral("bluePrimarySaturation")).toDouble(), 16.0)) {
    return fail("structured recipe should serialize RAW decode controls");
  }

  const auto reparsed_raw =
      photo_editor::desktop::parse_edit_recipe_json(QString::fromUtf8(QJsonDocument(raw_document).toJson(QJsonDocument::Compact)));
  if (!almost_equal(reparsed_raw.raw_exposure, raw_state.raw_exposure) ||
      !almost_equal(reparsed_raw.raw_boost, raw_state.raw_boost) ||
      !almost_equal(reparsed_raw.raw_shadow_boost, raw_state.raw_shadow_boost) ||
      !almost_equal(reparsed_raw.raw_detail, raw_state.raw_detail) ||
      !almost_equal(reparsed_raw.raw_chroma_noise_reduction, raw_state.raw_chroma_noise_reduction) ||
      !almost_equal(reparsed_raw.raw_moire_reduction, raw_state.raw_moire_reduction) ||
      !almost_equal(reparsed_raw.raw_hot_dead_pixel_cleanup, raw_state.raw_hot_dead_pixel_cleanup) ||
      !almost_equal(reparsed_raw.raw_false_color_suppression, raw_state.raw_false_color_suppression) ||
      reparsed_raw.raw_demosaic_quality != raw_state.raw_demosaic_quality ||
      reparsed_raw.raw_highlight_recovery != raw_state.raw_highlight_recovery ||
      reparsed_raw.raw_gamut_mapping != raw_state.raw_gamut_mapping ||
      reparsed_raw.raw_lens_correction != raw_state.raw_lens_correction ||
      reparsed_raw.raw_white_balance_override != raw_state.raw_white_balance_override ||
      !almost_equal(reparsed_raw.raw_white_balance_temperature, raw_state.raw_white_balance_temperature) ||
      !almost_equal(reparsed_raw.raw_white_balance_tint, raw_state.raw_white_balance_tint) ||
      !almost_equal(reparsed_raw.raw_black_level, raw_state.raw_black_level) ||
      !almost_equal(reparsed_raw.raw_white_level, raw_state.raw_white_level) ||
      !almost_equal(reparsed_raw.raw_calibration_hue, raw_state.raw_calibration_hue) ||
      !almost_equal(reparsed_raw.raw_calibration_saturation, raw_state.raw_calibration_saturation) ||
      !almost_equal(reparsed_raw.raw_red_primary_hue, raw_state.raw_red_primary_hue) ||
      !almost_equal(reparsed_raw.raw_red_primary_saturation, raw_state.raw_red_primary_saturation) ||
      !almost_equal(reparsed_raw.raw_green_primary_hue, raw_state.raw_green_primary_hue) ||
      !almost_equal(reparsed_raw.raw_green_primary_saturation, raw_state.raw_green_primary_saturation) ||
      !almost_equal(reparsed_raw.raw_blue_primary_hue, raw_state.raw_blue_primary_hue) ||
      !almost_equal(reparsed_raw.raw_blue_primary_saturation, raw_state.raw_blue_primary_saturation)) {
    return fail("structured recipe should round-trip RAW decode controls");
  }

  EditRecipeState base_state;
  base_state.raw_exposure = -0.3;
  base_state.raw_gamut_mapping = true;
  base_state.exposure = -0.25;
  base_state.temperature = -12.0;
  base_state.crop_preset = QStringLiteral("Original");
  base_state.linear_mask_enabled = false;
  base_state.texture = 8.0;
  base_state.lens_distortion = 0.0;

  EditRecipeState source_state;
  source_state.raw_exposure = 1.1;
  source_state.raw_boost = 0.65;
  source_state.raw_shadow_boost = 1.4;
  source_state.raw_chroma_noise_reduction = 0.58;
  source_state.raw_moire_reduction = 0.33;
  source_state.raw_hot_dead_pixel_cleanup = 0.44;
  source_state.raw_false_color_suppression = 0.31;
  source_state.raw_demosaic_quality = QStringLiteral("Preview");
  source_state.raw_gamut_mapping = false;
  source_state.raw_black_level = 0.018;
  source_state.raw_white_level = 1.12;
  source_state.raw_calibration_hue = 15.0;
  source_state.raw_calibration_saturation = -20.0;
  source_state.raw_red_primary_hue = -8.0;
  source_state.raw_red_primary_saturation = 24.0;
  source_state.raw_green_primary_hue = 6.0;
  source_state.raw_green_primary_saturation = -18.0;
  source_state.raw_blue_primary_hue = -12.0;
  source_state.raw_blue_primary_saturation = 10.0;
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
      !almost_equal(merged_color_and_masks.raw_exposure, -0.3) ||
      !almost_equal(merged_color_and_masks.exposure, -0.25) ||
      merged_color_and_masks.crop_preset != QStringLiteral("Original")) {
    return fail("group merge should only update the requested adjustment groups");
  }

  const auto merged_raw =
      photo_editor::desktop::merge_edit_recipe_state_by_groups(
          base_state,
          source_state,
          {QStringLiteral("raw")});
  if (!almost_equal(merged_raw.raw_exposure, 1.1) ||
      !almost_equal(merged_raw.raw_boost, 0.65) ||
      !almost_equal(merged_raw.raw_shadow_boost, 1.4) ||
      !almost_equal(merged_raw.raw_chroma_noise_reduction, 0.58) ||
      !almost_equal(merged_raw.raw_moire_reduction, 0.33) ||
      !almost_equal(merged_raw.raw_hot_dead_pixel_cleanup, 0.44) ||
      !almost_equal(merged_raw.raw_false_color_suppression, 0.31) ||
      merged_raw.raw_demosaic_quality != QStringLiteral("Preview") ||
      merged_raw.raw_gamut_mapping ||
      !almost_equal(merged_raw.raw_black_level, 0.018) ||
      !almost_equal(merged_raw.raw_white_level, 1.12) ||
      !almost_equal(merged_raw.raw_calibration_hue, 15.0) ||
      !almost_equal(merged_raw.raw_calibration_saturation, -20.0) ||
      !almost_equal(merged_raw.raw_red_primary_hue, -8.0) ||
      !almost_equal(merged_raw.raw_red_primary_saturation, 24.0) ||
      !almost_equal(merged_raw.raw_green_primary_hue, 6.0) ||
      !almost_equal(merged_raw.raw_green_primary_saturation, -18.0) ||
      !almost_equal(merged_raw.raw_blue_primary_hue, -12.0) ||
      !almost_equal(merged_raw.raw_blue_primary_saturation, 10.0) ||
      !almost_equal(merged_raw.exposure, -0.25) ||
      !almost_equal(merged_raw.temperature, -12.0)) {
    return fail("raw group merge should only update RAW decode controls");
  }

  const auto merged_all =
      photo_editor::desktop::merge_edit_recipe_state_by_groups(base_state, source_state, {});
  if (!almost_equal(merged_all.exposure, 1.1) ||
      !almost_equal(merged_all.raw_exposure, 1.1) ||
      merged_all.crop_preset != QStringLiteral("4x5") ||
      !merged_all.linear_mask_enabled ||
      !almost_equal(merged_all.texture, 26.0) ||
      !almost_equal(merged_all.lens_distortion, 22.0)) {
    return fail("empty group selection should merge the full edit recipe");
  }

  return EXIT_SUCCESS;
}
