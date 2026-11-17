#include "catalog_asset_model.h"
#include "edit_recipe_mask_schema.h"
#include "editor_preview_controller.h"
#include "edit_adjustment_patch.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "render_plan_store.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>
#include <atomic>
#include <iostream>
#include <memory>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
}

namespace {

int verify_export_session_guard(const QByteArray& main_qml) {
  const auto function = [&main_qml](const QByteArray& name) {
    const auto start = main_qml.indexOf("    function " + name + "(");
    const auto end = start < 0 ? -1 : main_qml.indexOf("\n    }", start);
    return start < 0 || end < 0 ? QByteArray{} : main_qml.mid(start, end + 6 - start);
  };
  const auto resolve = function("resolvePhotoNavigation");
  const auto queue = function("queueExportFromMenu");
  const auto reset = function("resetCurrentPhotoEdits");
  if (resolve.isEmpty() || queue.isEmpty() || reset.isEmpty()) return 1;
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"qml(import QtQml
QtObject {
    id: window
    property bool photoWorkspaceActive: true
    property int currentWorkspace: 1
    property int timelineWorkspaceIndex: 3
    property int exportWorkspaceIndex: 4
    property bool editorUsesLocalAsset: false
    property string localEditorFilePath: "/synthetic/canvas.png"
    property bool editBoardsActive: false
    property string openedCatalogFilePath: "/synthetic/source.png"
    property var pendingPhotoWorkflowHistory: null
    property int snapshots: 0
    property int pickerCalls: 0
    property int queued: 0
    property int transitions: 0
    property int notices: 0
    property string queuedRecipe: ""
    property QtObject editorPreview: QtObject {
        property bool photoPaintBusy: false
        property bool brushStrokeActive: false
        property bool selectionEditSessionActive: false
        property bool selectionEditSessionDirty: false
        property bool hasAsset: true
        property string recipe: "before-session"
        property int originalResets: 0
        property int boardResets: 0
        property int selectionFinishes: 0
        property string recipeBeforeReset: ""
        property bool photoExportBusy: false
        property string photoExportError: ""
        property int directExports: 0
        property string exportedRecipe: ""
        property string exportPath: ""
        function startFlattenedPhotoExport(path) {
            directExports++
            exportedRecipe = recipe
            exportPath = path
            photoExportBusy = true
            return true
        }
        function finishSelectionEditSession() { selectionFinishes++; selectionEditSessionActive = false; return true }
        function revertPhotoToOriginal() { originalResets++; recipeBeforeReset = recipe; recipe = "original"; return true }
        function resetAdjustments() { boardResets++; recipeBeforeReset = recipe; recipe = "board-reset" }
    }
    property QtObject textSession: QtObject {
        property bool dirty: false
        property bool rejectCommit: false
        property int commits: 0
        function resolveTextSession() {
            if (rejectCommit) return false
            if (dirty) {
                commits++
                dirty = false
                window.editorPreview.recipe = "committed-session"
            }
            return true
        }
    }
    property var editWorkspaceLoader: ({item: textSession})
    property QtObject catalogAssets: QtObject {
        property string selectedAssetId: "synthetic-source"
        property string selectedMediaType: "photo"
        property bool selectedRecipeReady: true
        function requestAssetRecipes(ids) { throw new Error("Unexpected catalog request") }
    }
    property string pendingMenuExportAssetId: ""
    property QtObject exportJobs: QtObject {
        function pickDestinationFolder(title) { window.pickerCalls++; return "/synthetic/exports" }
        function queueExport(id, media, label, path, preview, recipe, preset, destination) {
            window.queued++
            window.queuedRecipe = recipe
            return true
        }
    }
    function showPhotoRecipeNotice(text, error) { notices++ }
    function currentSelectionLabel() { return "Rain / painted canvas" }
    function currentExportPayload() {
        snapshots++
        return {assetId: "synthetic-source", mediaType: "photo", displayLabel: "Source", sourcePath: openedCatalogFilePath,
            editedPreviewSource: "", adjustmentPatchJson: editorPreview.recipe}
    }
    function switchToWorkspaceIndex(index) { transitions++ }
    function openContextualExport(value) { throw new Error("Unexpected Timeline export") }
)qml" + resolve + "\n" + queue + "\n" + reset + "\n}", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/export-session-guard-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) { std::cerr << component.errorString().toStdString(); return 1; }
  auto* preview = fixture->property("editorPreview").value<QObject*>();
  auto* text = fixture->property("textSession").value<QObject*>();
  if (!preview || !text) return 1;
  const auto invoke = [&] { return QMetaObject::invokeMethod(fixture.get(), "queueExportFromMenu"); };
  const auto untouched_queue = [&] { return fixture->property("snapshots").toInt() == 0 &&
      fixture->property("pickerCalls").toInt() == 0 && fixture->property("queued").toInt() == 0 &&
      fixture->property("transitions").toInt() == 0; };
  preview->setProperty("selectionEditSessionActive", true);
  preview->setProperty("selectionEditSessionDirty", true);
  text->setProperty("dirty", true);
  if (!invoke() || !untouched_queue() || text->property("commits").toInt() != 0 ||
      preview->property("recipe").toString() != "before-session") {
    std::cerr << "Dirty selection must block export before text commit, snapshot, picker or queue\n"; return 1;
  }
  preview->setProperty("selectionEditSessionActive", false);
  preview->setProperty("selectionEditSessionDirty", false);
  text->setProperty("rejectCommit", true);
  if (!invoke() || !untouched_queue() || !text->property("dirty").toBool()) {
    std::cerr << "Rejected text commit must retain the draft and block every export side effect\n"; return 1;
  }
  text->setProperty("rejectCommit", false);
  if (!invoke() || text->property("commits").toInt() != 1 || text->property("dirty").toBool() ||
      fixture->property("snapshots").toInt() != 1 || fixture->property("pickerCalls").toInt() != 1 ||
      fixture->property("queued").toInt() != 1 || fixture->property("transitions").toInt() != 1 ||
      fixture->property("queuedRecipe").toString() != "committed-session") {
    std::cerr << "Valid text must commit once before the exported recipe snapshot and queue admission\n"; return 1;
  }
  if (!invoke() || text->property("commits").toInt() != 1 || fixture->property("queued").toInt() != 2) {
    std::cerr << "Repeated export cannot publish an unchanged text session twice\n"; return 1;
  }
  const auto reset_now = [&] { return QMetaObject::invokeMethod(fixture.get(), "resetCurrentPhotoEdits"); };
  text->setProperty("dirty", true);
  text->setProperty("rejectCommit", true);
  if (!reset_now() || preview->property("originalResets").toInt() != 0 || !text->property("dirty").toBool()) {
    std::cerr << "Rejected text must block global original reset and retain its draft\n"; return 1;
  }
  text->setProperty("rejectCommit", false);
  preview->setProperty("selectionEditSessionActive", true);
  preview->setProperty("selectionEditSessionDirty", true);
  fixture->setProperty("editBoardsActive", true);
  if (!reset_now() || preview->property("boardResets").toInt() != 0 || text->property("commits").toInt() != 1) {
    std::cerr << "Dirty selection must block Board-image reset before committing unrelated text\n"; return 1;
  }
  preview->setProperty("selectionEditSessionActive", false);
  preview->setProperty("selectionEditSessionDirty", false);
  fixture->setProperty("pendingPhotoWorkflowHistory", QVariantMap{{"pending", true}});
  if (!reset_now() || preview->property("boardResets").toInt() != 0 || text->property("commits").toInt() != 1) {
    std::cerr << "Pending workflow history must block Board-image reset\n"; return 1;
  }
  fixture->setProperty("pendingPhotoWorkflowHistory", QVariant());
  preview->setProperty("selectionEditSessionActive", true);
  if (!reset_now() || preview->property("boardResets").toInt() != 1 ||
      preview->property("selectionFinishes").toInt() != 1 || preview->property("selectionEditSessionActive").toBool() ||
      text->property("commits").toInt() != 2 || preview->property("recipeBeforeReset").toString() != "committed-session") {
    std::cerr << "Board reset must finish a clean selection and commit valid text exactly once before mutation\n"; return 1;
  }
  fixture->setProperty("editBoardsActive", false);
  if (!reset_now() || preview->property("originalResets").toInt() != 1 || text->property("commits").toInt() != 2) {
    std::cerr << "Whole-photo reset must use original restore without a duplicate text commit\n"; return 1;
  }
  fixture->setProperty("editorUsesLocalAsset", true);
  text->setProperty("dirty", true);
  text->setProperty("rejectCommit", true);
  const auto picker_before_canvas = fixture->property("pickerCalls").toInt();
  if (!invoke() || preview->property("directExports").toInt() != 0 ||
      fixture->property("pickerCalls").toInt() != picker_before_canvas) return 1;
  text->setProperty("rejectCommit", false);
  if (!invoke() || preview->property("directExports").toInt() != 1 ||
      preview->property("exportedRecipe").toString() != "committed-session" ||
      !preview->property("exportPath").toString().startsWith("/synthetic/exports/Rain _ painted canvas-") ||
      fixture->property("queued").toInt() != 2 || fixture->property("snapshots").toInt() != 2 ||
      fixture->property("pickerCalls").toInt() != picker_before_canvas + 1) {
    std::cerr << "Local canvas must export committed editor pixels without a queued catalog recipe\n"; return 1;
  }
  if (!invoke() || preview->property("directExports").toInt() != 1 ||
      fixture->property("pickerCalls").toInt() != picker_before_canvas + 1) {
    std::cerr << "Busy canvas export must not open another picker or repeat the snapshot\n"; return 1;
  }
  std::cout << "Main canvas export: committed pixels, sanitized new PNG, no catalog queue, single flight\n";
  std::cout << "Main export session guard: dirty selection/rejected text cause zero export side effects; valid text commits once before snapshot\n";
  std::cout << "Main reset session guard: rejected text, dirty selection and pending history block reset; clean sessions resolve once before scoped mutation\n";
  return 0;
}

int run_recipe_handoff_benchmark(
    const photo_editor::desktop::EditRecipeState& recipe,
    const QString& source) {
  using namespace photo_editor::desktop;
  if (recipe.mask_stack.isEmpty()) {
    std::cerr << "benchmark requires a mask in the source recipe\n";
    return 1;
  }

  auto large_recipe = recipe;
  auto mask_stack = large_recipe.mask_stack;
  auto paint_mask = mask_stack.first().toMap();
  paint_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  paint_mask.insert(QStringLiteral("segmentationWidth"), 2048);
  paint_mask.insert(QStringLiteral("segmentationHeight"), 1024);
  paint_mask.insert(QStringLiteral("segmentationData"),
                    QByteArray(2048 * 1024, 'p'));
  mask_stack[0] = paint_mask;
  large_recipe.mask_stack = mask_stack;
  const auto large_patch = canonical_asset_bound_adjustment_patch_json(
      serialize_adjustment_patch_json(large_recipe, all_adjustment_groups(),
          AdjustmentPatchScope::AssetBound));
  const auto decoded_large_patch = parse_adjustment_patch_json(large_patch);
  if (!decoded_large_patch.ok() ||
      large_patch.size() < 1024 * 1024) {
    std::cerr << "large paint/mask recipe failed validation: decoded=" << decoded_large_patch.ok()
              << " bytes=" << large_patch.size() << "\n";
    return 1;
  }

  large_recipe = *decoded_large_patch.state;

  CatalogAssetModel catalog;
  CatalogAssetItem item;
  item.asset_id = QStringLiteral("recipe-handoff-benchmark");
  item.file_path = source;
  item.media_type = QStringLiteral("photo");
  item.online = true;
  item.adjustment_patch_json = large_patch;
  catalog.replaceAssetsForTesting({item});
  if (!catalog.selectAssetById(item.asset_id, true)) return 1;
  // Photo selection requests this canonical projection before editor handoff.
  if (catalog.selectedAdjustmentPatchJson() != large_patch) return 1;

  constexpr int projection_iterations = 16;
  QElapsedTimer projection_timer;
  projection_timer.start();
  for (int iteration = 0; iteration < projection_iterations; ++iteration) {
    if (catalog.selectedAdjustmentPatchJson() != large_patch) return 1;
  }
  const auto projection_ms = static_cast<double>(projection_timer.nsecsElapsed()) / 1.0e6;
  std::cout << "recipe_projection_benchmark iterations=" << projection_iterations
            << " mean_ms=" << projection_ms / projection_iterations << '\n';

  constexpr int unchanged_iterations = 40;
  QElapsedTimer timer;
  timer.start();
  for (int iteration = 0; iteration < unchanged_iterations; ++iteration) {
    if (catalog.applyAdjustmentPatchJsonToAssetIds(
            {item.asset_id}, large_patch) != 0) {
      std::cerr << "unchanged benchmark handoff modified the recipe\n";
      return 1;
    }
  }
  const auto unchanged_ms =
      static_cast<double>(timer.nsecsElapsed()) / 1.0e6;

  constexpr int changed_iterations = 8;
  timer.restart();
  for (int iteration = 0; iteration < changed_iterations; ++iteration) {
    large_recipe.exposure = 0.125 * static_cast<double>(iteration + 1);
    const auto changed_patch = serialize_adjustment_patch_json(
        large_recipe, all_adjustment_groups(),
        AdjustmentPatchScope::AssetBound);
    if (catalog.applyAdjustmentPatchJsonToAssetIds(
            {item.asset_id}, changed_patch) != 1 ||
        catalog.selectedAdjustmentPatchJson() != changed_patch) {
      std::cerr << "changed benchmark handoff failed to persist exactly\n";
      return 1;
    }
  }
  const auto changed_ms =
      static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  std::cout << "recipe_handoff_benchmark payload_bytes="
            << large_patch.toUtf8().size()
            << " unchanged_iterations=" << unchanged_iterations
            << " unchanged_total_ms=" << unchanged_ms
            << " unchanged_mean_ms=" << unchanged_ms / unchanged_iterations
            << " changed_iterations=" << changed_iterations
            << " changed_total_ms=" << changed_ms
            << " changed_mean_ms=" << changed_ms / changed_iterations
            << '\n';
  if (!QThreadPool::globalInstance()->waitForDone(45000)) return 1;
  return 0;
}

}  // namespace

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
  const auto source = app.arguments().size() > 1 && !app.arguments()[1].startsWith("--")
      ? app.arguments()[1] : fixture;
  if (!QFileInfo::exists(source)) return 2;
  if (app.arguments().contains(QStringLiteral("--cropped-native-preview-only"))) {
    using namespace photo_editor::pipeline;
    if (!metal::shared_metal_pipeline_backend().available()) return 2;
    const auto is_raw = raw_image_supported(source);
    const auto dimensions = is_raw ? raw_image_size(source) : QImage(source).size();
    EditRecipeState state;
    state.preview_color_space = QStringLiteral("sRGB");
    state.preview_output_mode = QStringLiteral("Reference Display Target");
    state.crop_rect_x = 0.2;
    state.crop_rect_y = 0.15;
    state.crop_rect_width = 0.6;
    state.crop_rect_height = 0.7;
    for (int pass = 0; pass < 2; ++pass) {
      state.exposure = pass == 0 ? 0.0 : 0.5;
      RenderRequest request;
      request.source_asset_id = QStringLiteral("cropped-native-preview-regression");
      request.file_path = source;
      request.state = state;
      request.render_reason = pass == 0 ? QStringLiteral("crop_change") : QStringLiteral("edit_change");
      request.dirty_domain = pass == 0 ? QStringLiteral("geometry_transform") : QStringLiteral("global_tone_color");
      request.use_in_memory_preview = true;
      request.allow_native_srgb_surface = true;
      request.require_cpu_preview_image = false;
      request.require_canonical_render_plan = true;
      request.recipe_cache_identity = QStringLiteral("crop-native-%1").arg(pass);
      request.adjusted_recipe_cache_identity = request.recipe_cache_identity + QStringLiteral(":adjusted");
      request.max_edge = 640;
      request.generation = pass + 1;
      request.latest_generation = std::make_shared<std::atomic<int>>(pass + 1);
      const auto admission = RenderPlanStore::shared().admit(
          request.source_asset_id, source, state, dimensions,
          RenderPurpose::SettledPreview, DisplayRenderingTarget::SrgbSimulation);
      if (!admission.admitted) return 3;
      request.canonical_render_plan = admission.plan;
      const auto result = render_preview_request_for_test(request);
      // Publication may intentionally return CPU-readable display pixels even
      // when working adjustments execute on Metal. Both must remain usable.
      const auto publishable = result.texture_upload_surface != nullptr || !result.preview_image.isNull();
      if (!result.success || result.cancelled || !result.failure_stage.isEmpty() ||
          !result.failure_detail.isEmpty() || !publishable ||
          (!is_raw && pass == 1 && result.backend != QStringLiteral("metal"))) {
        std::cerr << "Cropped native preview failed: " << result.failure_stage.toStdString()
                  << " " << result.failure_detail.toStdString()
                  << " status=" << result.status.toStdString()
                  << " backend=" << result.backend.toStdString()
                  << " fallback=" << result.fallback_reason.toStdString()
                  << " image=" << result.preview_image.width() << 'x' << result.preview_image.height()
                  << " success=" << result.success << " cancelled=" << result.cancelled
                  << " plan=" << result.canonical_plan_error.toStdString() << '\n';
        return 1;
      }
      std::cout << "Cropped preview passed: " << request.dirty_domain.toStdString()
                << " backend=" << result.backend.toStdString()
                << " native_surface=" << (result.texture_upload_surface != nullptr)
                << " fallback=" << result.fallback_reason.toStdString() << '\n';
    }
    return 0;
  }
  if (app.arguments().contains(QStringLiteral("--recipe-projection-benchmark"))) {
    EditRecipeState benchmark_recipe;
    benchmark_recipe.mask_stack = {default_mask_definition(QStringLiteral("brush"))};
    return run_recipe_handoff_benchmark(benchmark_recipe, source);
  }

  // Exercise the function from Main.qml, not a second implementation of it.
  QFile main_source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/Main.qml"));
  if (!main_source.open(QIODevice::ReadOnly)) return 2;
  const auto main_qml = main_source.readAll();
  if (verify_export_session_guard(main_qml) != 0) return 1;
  if (app.arguments().contains(QStringLiteral("--export-session-guard-only"))) return 0;
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

  if (app.arguments().contains(QStringLiteral("--recipe-handoff-benchmark"))) {
    return run_recipe_handoff_benchmark(*expected.state, source);
  }

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
  if (!invoke("syncNow") || catalog.selectedAdjustmentPatchJson() != handed_off_json) {
    std::cerr << "repeating an unchanged editor handoff modified the catalog recipe\n";
    return 1;
  }
  const auto pretty_handoff_json = QString::fromUtf8(
      QJsonDocument::fromJson(expected_json.toUtf8())
          .toJson(QJsonDocument::Indented));
  if (catalog.applyAdjustmentPatchJsonToAssetIds(
          {item.asset_id}, pretty_handoff_json) != 0 ||
      catalog.selectedAdjustmentPatchJson() != handed_off_json) {
    std::cerr << "pretty-printed asset-bound handoff changed the recipe\n";
    return 1;
  }
  // A compact top-level envelope does not prove its inner recipe is canonical.
  auto legacy_envelope = QJsonDocument::fromJson(handed_off_json.toUtf8()).object();
  auto legacy_state = legacy_envelope.value("adjustmentState").toObject();
  auto legacy_globals = legacy_state.value("globalAdjustments").toObject();
  legacy_globals.remove("exposure");
  legacy_state.insert("globalAdjustments", legacy_globals);
  legacy_envelope.insert("adjustmentState", legacy_state);
  const auto legacy_patch = QString::fromUtf8(QJsonDocument(legacy_envelope).toJson(QJsonDocument::Compact));
  const auto normalized_patch = canonical_asset_bound_adjustment_patch_json(legacy_patch);
  if (normalized_patch.isEmpty() || normalized_patch == legacy_patch) return 1;
  CatalogAssetModel normalization_catalog;
  auto normalization_item = item;
  normalization_item.asset_id = "normalize-handoff";
  normalization_item.adjustment_patch_json = legacy_patch;
  normalization_catalog.replaceAssetsForTesting({normalization_item});
  if (!normalization_catalog.selectAssetById(normalization_item.asset_id, true) ||
      normalization_catalog.selectedAdjustmentPatchJson() != normalized_patch ||
      normalization_catalog.applyAdjustmentPatchJsonToAssetIds(
          {normalization_item.asset_id}, legacy_patch) != 1 ||
      normalization_catalog.selectedAdjustmentPatchJson() != normalized_patch) {
    std::cerr << "Exact handoff skipped legacy inner-field normalization\n";
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

  if (!catalog.selectAssetById(item.asset_id, true)) return 1;
  auto restricted_state = *expected.state;
  restricted_state.exposure = 1.75;
  const auto restricted_patch = serialize_adjustment_patch_json(
      restricted_state, all_adjustment_groups(),
      AdjustmentPatchScope::AssetBound);
  if (catalog.applyAdjustmentPatchJsonToAssetIds(
          {item.asset_id}, restricted_patch,
          {QStringLiteral("tone")}) != 1) {
    std::cerr << "explicit adjustment-group override was not applied\n";
    return 1;
  }
  const auto restricted_result =
      parse_adjustment_patch_json(catalog.selectedAdjustmentPatchJson());
  if (!restricted_result.ok() ||
      restricted_result.state->exposure != restricted_state.exposure ||
      restricted_result.state->layers != expected.state->layers ||
      restricted_result.state->mask_stack != expected.state->mask_stack) {
    std::cerr << "restricted tone handoff changed unrelated layer or mask data\n";
    return 1;
  }

  CatalogAssetModel clipboard_catalog;
  auto clipboard_target_state = *expected.state;
  clipboard_target_state.input_color_profile =
      QStringLiteral("Camera DCP: Target profile");
  clipboard_target_state.monitor_color_profile = QStringLiteral("Display P3");
  CatalogAssetItem clipboard_target;
  clipboard_target.asset_id = QStringLiteral("clipboard-target");
  clipboard_target.file_path = source;
  clipboard_target.media_type = QStringLiteral("photo");
  clipboard_target.online = true;
  clipboard_target.adjustment_patch_json = serialize_adjustment_patch_json(
      clipboard_target_state, all_adjustment_groups(),
      AdjustmentPatchScope::AssetBound);
  clipboard_catalog.replaceAssetsForTesting({clipboard_target});
  if (!clipboard_catalog.selectAssetById(clipboard_target.asset_id, true)) return 1;
  auto clipboard_source_state = *expected.state;
  clipboard_source_state.exposure = -0.75;
  clipboard_source_state.input_color_profile =
      QStringLiteral("Camera DCP: Source profile");
  clipboard_source_state.monitor_color_profile = QStringLiteral("sRGB");
  const auto clipboard_patch = serialize_adjustment_patch_json(
      clipboard_source_state, {QStringLiteral("tone")},
      AdjustmentPatchScope::SourceAgnostic);
  if (clipboard_catalog.pasteAdjustmentPatchJsonToAssetIds(
          {clipboard_target.asset_id}, clipboard_patch) != 1) {
    std::cerr << "source-agnostic clipboard recipe was not applied\n";
    return 1;
  }
  const auto clipboard_result = parse_adjustment_patch_json(
      clipboard_catalog.selectedAdjustmentPatchJson());
  if (!clipboard_result.ok() ||
      clipboard_result.state->exposure != clipboard_source_state.exposure ||
      clipboard_result.state->input_color_profile !=
          clipboard_target_state.input_color_profile ||
      clipboard_result.state->monitor_color_profile !=
          clipboard_target_state.monitor_color_profile) {
    std::cerr << "clipboard paste did not preserve target profile bindings\n";
    return 1;
  }
  if (!QThreadPool::globalInstance()->waitForDone(45000)) return 1;
  std::cout << "handoff_pixels_exact=1 layer_negative_control_changed=1 later_edits_do_not_change_snapshot=1 "
               "other_asset_unchanged=1 history_is_asset_scoped=1 width=" << output.width()
            << " height=" << output.height() << '\n';
}
