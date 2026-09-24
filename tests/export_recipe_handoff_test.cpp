#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "edit_adjustment_patch.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/export-handoff-test");
  QGuiApplication app(argc, argv);
  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(45000); }
  } drain_workers;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime.filePath("preferences"));
  using namespace photo_editor::desktop;
  QImage image(64, 48, QImage::Format_RGB32);
  image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  image.fill(QColor(40, 65, 90));
  const auto fixture = runtime.filePath("base.png");
  const auto placed = runtime.filePath("placed.png");
  if (!image.save(fixture)) return 2;
  image.fill(QColor(150, 80, 35));
  if (!image.save(placed)) return 2;
  const auto source = app.arguments().size() > 1 ? app.arguments()[1] : fixture;
  if (!QFileInfo::exists(source)) return 2;

  // Exercise the function from Main.qml, not a second implementation of it.
  QFile main_source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/Main.qml"));
  if (!main_source.open(QIODevice::ReadOnly)) return 2;
  const auto main_qml = main_source.readAll();
  const auto start = main_qml.indexOf("    function syncEditorRecipeToCatalog(nextAssetId) {");
  const auto end = start < 0 ? -1 : main_qml.indexOf("\n    }", start);
  if (start < 0 || end < 0) return 2;
  const auto sync_function = main_qml.mid(start, end + 6 - start);

  CatalogAssetModel catalog;
  CatalogAssetItem item;
  item.asset_id = "handoff-test"; item.file_path = source;
  item.media_type = "photo"; item.online = true;
  auto catalog_state = append_named_recipe_snapshot(EditRecipeState{}, "Existing review", "review-branch");
  if (!raw_image_supported(source)) catalog_state.input_color_profile = "Display P3";
  catalog_state.preset_applications = {QVariantMap{{"presetId", "handoff-look"}, {"amount", 0.5}}};
  item.adjustment_patch_json = serialize_adjustment_patch_json(catalog_state, all_adjustment_groups(),
      AdjustmentPatchScope::AssetBound);
  auto other = item; other.asset_id = "other-asset";
  other.adjustment_patch_json.clear();
  catalog.replaceAssetsForTesting({item, other});
  if (!catalog.selectAssetById(item.asset_id, true)) return 2;
  EditorPreviewController editor;
  editor.loadAsset(item.asset_id, source, {}, {}, {}, {}, item.adjustment_patch_json);
  const auto loaded = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  if (!loaded.ok() || loaded.state->active_history_branch != catalog_state.active_history_branch ||
      loaded.state->recipe_snapshots != catalog_state.recipe_snapshots ||
      loaded.state->input_color_profile != catalog_state.input_color_profile ||
      loaded.state->preset_applications != catalog_state.preset_applications) {
    std::cerr << "Editor load discarded existing recipe history metadata\n";
    return 1;
  }
  if (!editor.addSourceLayer(placed, "Placed image")) return 1;
  editor.addMask("linear");
  editor.setActiveMaskExposure(0.6);
  editor.setOutputColorSpace("Display P3");
  if (editor.createEditHistorySnapshot("Before export").isEmpty()) return 1;
  const auto expected_json = editor.currentAssetBoundAdjustmentPatchJson();
  const auto expected = parse_adjustment_patch_json(expected_json);
  if (!expected.ok() || expected.state->layers.size() < 2 || expected.state->mask_stack.isEmpty()) return 1;

  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData("import QtQml\nQtObject {\n"
      "required property var editorPreview\nrequired property var catalogAssets\n"
      "property bool editorUsesLocalAsset: false\n"
      "function syncNow() { syncEditorRecipeToCatalog() }\n"
      "function syncSame() { syncEditorRecipeToCatalog(editorPreview.assetId) }\n" + sync_function + "\n}",
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/export-handoff-fixture.qml")));
  std::unique_ptr<QObject> object(component.createWithInitialProperties({
      {"editorPreview", QVariant::fromValue(&editor)}, {"catalogAssets", QVariant::fromValue(&catalog)}}));
  if (!object) { std::cerr << component.errorString().toStdString(); return 1; }
  const auto invoke = [&](const char* method) { return QMetaObject::invokeMethod(object.get(), method); };
  const auto initial = catalog.selectedAdjustmentPatchJson();
  if (!invoke("syncSame") || catalog.selectedAdjustmentPatchJson() != initial) return 1;
  object->setProperty("editorUsesLocalAsset", true);
  if (!invoke("syncNow") || catalog.selectedAdjustmentPatchJson() != initial) return 1;
  object->setProperty("editorUsesLocalAsset", false);
  if (!invoke("syncNow")) return 1;
  const auto handed_off_json = catalog.selectedAdjustmentPatchJson();
  const auto handed_off = parse_adjustment_patch_json(handed_off_json);
  if (!handed_off.ok() || handed_off_json != expected_json) {
    std::cerr << "Asset-bound handoff mismatch: expected_layers=" << expected.state->layers.size()
              << " actual_layers=" << (handed_off.ok() ? handed_off.state->layers.size() : -1)
              << " expected_masks=" << expected.state->mask_stack.size()
              << " actual_masks=" << (handed_off.ok() ? handed_off.state->mask_stack.size() : -1) << '\n';
    QThreadPool::globalInstance()->waitForDone(45000);
    return 1;
  }
  std::cout << "complete_asset_bound_recipe_preserved=1 layers=" << handed_off.state->layers.size()
            << " masks=" << handed_off.state->mask_stack.size() << std::endl;
  // ExportView consumes this exact catalog patch. Freeze it before later edits.
  editor.setExposure(1.25);
  editor.undo();
  const auto undone = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  if (!undone.ok() || undone.state->recipe_snapshots != catalog_state.recipe_snapshots ||
      undone.state->active_history_branch != catalog_state.active_history_branch) return 1;
  editor.redo();
  if (!invoke("syncNow") || catalog.selectedAdjustmentPatchJson() == handed_off_json) return 1;
  const auto target = QColorSpace(QColorSpace::DisplayP3);
  const auto reference = render_flattened_output_file(item.asset_id, source, *expected.state,
      target, QImage::Format_RGBA64);
  const auto output = render_flattened_output_file(item.asset_id, source, *handed_off.state,
      target, QImage::Format_RGBA64);
  if (reference.isNull() || reference != output || reference.colorSpace() != output.colorSpace()) return 1;
  auto without_layers = *expected.state; without_layers.layers.clear();
  const auto control = render_flattened_output_file(item.asset_id, source, without_layers,
      target, QImage::Format_RGBA64);
  if (control.isNull() || control == reference) return 1;
  if (!catalog.selectAssetById(other.asset_id, true) || !catalog.selectedAdjustmentPatchJson().isEmpty()) return 1;
  editor.loadAsset("fresh-handoff-asset", fixture, {}, {}, {}, {}, {});
  const auto fresh = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  if (!fresh.ok() || !fresh.state->recipe_snapshots.isEmpty() ||
      fresh.state->active_history_branch != "main") {
    std::cerr << "Recipe history leaked into a different asset\n";
    return 1;
  }
  if (!QThreadPool::globalInstance()->waitForDone(45000)) return 1;
  std::cout << "handoff_pixels_exact=1 layer_negative_control_changed=1 later_edits_do_not_change_snapshot=1 "
               "other_asset_unchanged=1 history_is_asset_scoped=1 width=" << output.width()
            << " height=" << output.height() << '\n';
}
