#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "app_bootstrap.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CustomPresetAuthoringTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  if (!photo_editor::desktop::AppBootstrap::ensureRuntimeDirectories()) {
    return fail("runtime directories should be available");
  }

  const auto image_path = runtime_root.filePath(QStringLiteral("authoring-source.png"));
  QImage fixture(32, 24, QImage::Format_ARGB32);
  fixture.fill(QColor(116, 142, 176));
  if (!fixture.save(image_path)) {
    return fail("authoring source image should be writable");
  }

  photo_editor::desktop::EditorPreviewController controller;
  controller.loadAsset(
      QStringLiteral("custom-preset-authoring"), image_path, QString(), QString(), QString(), QString(), QString());
  controller.setExposure(0.42);
  controller.setTemperature(17.0);
  controller.setTexture(28.0);
  controller.setRawExposure(0.7);
  const auto edit_before_save = controller.exposure();
  const auto base_save = controller.saveCustomPreset(
      QStringLiteral("Scoped Base"), QStringLiteral("Authoring"), {QStringLiteral("tone")});
  const auto base_state = photo_editor::desktop::parse_adjustment_payload_or_legacy_recipe(
                              controller.presetAdjustmentPatchJson(QStringLiteral("Scoped Base")))
                              .state.value_or(photo_editor::desktop::EditState{});
  if (!base_save.value(QStringLiteral("ok")).toBool() ||
      !qFuzzyCompare(base_state.exposure + 1.0, 1.42) ||
      !qFuzzyIsNull(base_state.temperature) || !qFuzzyIsNull(base_state.texture) ||
      !qFuzzyIsNull(base_state.raw_exposure) ||
      !qFuzzyCompare(controller.exposure() + 1.0, edit_before_save + 1.0)) {
    return fail("base preset should capture only the requested scope and preserve active edits");
  }

  const auto version_before_update =
      controller.presetVersionInfo(QStringLiteral("Scoped Base")).value(QStringLiteral("presetVersion")).toInt();
  if (controller.saveCustomPreset(
                    QStringLiteral("Scoped Base"), QStringLiteral("Authoring"), {QStringLiteral("tone")})
          .value(QStringLiteral("ok")).toBool() ||
      !controller.saveCustomPreset(
           QStringLiteral("Scoped Base"), QStringLiteral("Authoring"), {QStringLiteral("tone")}, -1, true)
           .value(QStringLiteral("ok")).toBool() ||
      controller.presetVersionInfo(QStringLiteral("Scoped Base"))
              .value(QStringLiteral("presetVersion"))
              .toInt() != version_before_update + 1) {
    return fail("duplicate creation should be rejected and explicit update should advance the preset version");
  }

  const auto built_in_state_before = controller.exposure();
  const auto built_in_duplicate = controller.duplicatePreset(
      QStringLiteral("Landscape Pop"), QStringLiteral("My Landscape Pop"));
  if (!built_in_duplicate.value(QStringLiteral("ok")).toBool() ||
      controller.presetAdjustmentPatchJson(QStringLiteral("My Landscape Pop")) !=
          controller.presetAdjustmentPatchJson(QStringLiteral("Landscape Pop")) ||
      !qFuzzyCompare(controller.exposure() + 1.0, built_in_state_before + 1.0) ||
      controller.duplicatePreset(QStringLiteral("Landscape Pop"), QStringLiteral("My Landscape Pop"))
          .value(QStringLiteral("ok")).toBool() ||
      controller.saveCustomPreset(
              QStringLiteral("Landscape Pop"), QStringLiteral("Authoring"), {QStringLiteral("tone")}, -1, true)
          .value(QStringLiteral("ok")).toBool()) {
    return fail("built-ins should duplicate without applying and reject overwrite or duplicate names");
  }

  controller.setVibrance(66.0);
  controller.addMask(QStringLiteral("linear"));
  if (controller.photoLayerCount() != 1) {
    return fail("test should prepare an adjustment layer source");
  }
  if (!controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0.31)) {
    return fail("test should prepare a layer-local exposure value");
  }
  const auto layer_state_before = controller.photoLayers().at(0).toMap();
  const auto layer_save = controller.saveCustomPreset(
      QStringLiteral("Layer Recipe"),
      QStringLiteral("Authoring"),
      {QStringLiteral("tone")},
      0);
  const auto layer_recipe = photo_editor::desktop::parse_adjustment_payload_or_legacy_recipe(
                                controller.presetAdjustmentPatchJson(QStringLiteral("Layer Recipe")))
                                .state.value_or(photo_editor::desktop::EditState{});
  if (!layer_save.value(QStringLiteral("ok")).toBool() ||
      !qFuzzyCompare(layer_recipe.exposure + 1.0, 1.31) || !qFuzzyIsNull(layer_recipe.vibrance) ||
      controller.photoLayers().at(0).toMap() != layer_state_before ||
      controller.saveCustomPreset(
              QStringLiteral("Invalid Layer Scope"),
              QStringLiteral("Authoring"),
              {QStringLiteral("raw")},
              0)
          .value(QStringLiteral("ok")).toBool()) {
    return fail("layer saves should use the layer's own scoped recipe without changing it or accepting unsupported scopes");
  }

  photo_editor::desktop::EditorPreviewController reloaded;
  if (!reloaded.isUserPreset(QStringLiteral("Scoped Base")) ||
      !reloaded.isUserPreset(QStringLiteral("My Landscape Pop")) ||
      !reloaded.isUserPreset(QStringLiteral("Layer Recipe")) ||
      reloaded.presetAdjustmentPatchJson(QStringLiteral("Layer Recipe")) !=
          controller.presetAdjustmentPatchJson(QStringLiteral("Layer Recipe"))) {
    return fail("custom preset scopes and copied library recipes should round-trip through the existing file");
  }

  const auto old_base_patch = controller.presetAdjustmentPatchJson(QStringLiteral("Scoped Base"));
  const auto old_base_version = controller.presetVersionInfo(QStringLiteral("Scoped Base"))
                                    .value(QStringLiteral("presetVersion"))
                                    .toInt();
  const auto preset_file = runtime_root.filePath(QStringLiteral("editor-presets.json"));
  if (!QFile::remove(preset_file) || !QDir().mkpath(preset_file)) {
    return fail("test should replace the preset file with a directory to force a durable write failure");
  }
  controller.setExposure(0.91);
  const auto failed_update = controller.saveCustomPreset(
      QStringLiteral("Scoped Base"), QStringLiteral("Authoring"), {QStringLiteral("tone")}, -1, true);
  if (failed_update.value(QStringLiteral("ok")).toBool() ||
      failed_update.value(QStringLiteral("error")).toString().isEmpty() ||
      controller.presetAdjustmentPatchJson(QStringLiteral("Scoped Base")) != old_base_patch ||
      controller.presetVersionInfo(QStringLiteral("Scoped Base"))
              .value(QStringLiteral("presetVersion"))
              .toInt() != old_base_version) {
    return fail("failed disk writes should report the error and restore the previous in-memory preset");
  }
  if (!QDir(preset_file).removeRecursively()) {
    return fail("test preset failure fixture should be removable");
  }

  std::cout << "custom_preset_authoring_test passed\n";
  return EXIT_SUCCESS;
}
