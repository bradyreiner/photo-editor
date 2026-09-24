#include "catalog_asset_model.h"
#include "app_bootstrap.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "job_runner.h"
#include "job_store.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrl>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QString adjustment_patch_for_asset(
    const QVariantMap& asset) {
  return asset
      .value(
          QStringLiteral(
              "adjustmentPatchJson"))
      .toString();
}

photo_editor::desktop::EditRecipeState
adjustment_state_from_patch(
    const QString& patch_json) {
  const auto decoded =
      photo_editor::desktop::
          parse_adjustment_patch_json(
              patch_json);
  return decoded.state.value_or(
      photo_editor::desktop::
          EditRecipeState{});
}

photo_editor::desktop::EditRecipeState
adjustment_state_for_asset(
    const QVariantMap& asset) {
  return adjustment_state_from_patch(
      adjustment_patch_for_asset(
          asset));
}

photo_editor::desktop::CatalogAssetItem make_asset(const QString& asset_id, const QString& file_name = QString()) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = QStringLiteral("/tmp/%1").arg(file_name.isEmpty() ? asset_id + QStringLiteral(".jpg") : file_name);
  item.capture_date = QStringLiteral("Mar 28, 2026");
  item.capture_date_value = QDate(2026, 3, 28);
  item.lens_model = QStringLiteral("RF 28-70mm F2L USM");
  item.iso_label = QStringLiteral("ISO 400");
  item.shutter_speed = QStringLiteral("1/250 s");
  item.aperture_label = QStringLiteral("f/2.8");
  item.imported_at = QStringLiteral("Mar 28, 2026  1:00 PM");
  item.imported_at_value = QDateTime(QDate(2026, 3, 28), QTime(13, 0));
  item.media_type = QStringLiteral("photo");
  item.online = true;
  return item;
}

bool write_file(const QString& file_path) {
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }

  QFile file(file_path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  file.write("test");
  return file.flush();
}

bool write_ppm_fixture(const QString& file_path) {
  static const auto kPpmBytes = QByteArrayLiteral(
      "P3\n"
      "2 2\n"
      "255\n"
      "93 114 130   93 114 130\n"
      "93 114 130   93 114 130\n");
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }

  QFile file(file_path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  file.write(kPpmBytes);
  return file.flush();
}

QString test_thumbnail_cache_path(
    const QString& file_path,
    const QString& adjustment_payload = {}) {
  const auto canonical_patch =
      photo_editor::desktop::
          canonical_asset_bound_adjustment_patch_json(
              adjustment_payload);
  const auto identity_bytes =
      !canonical_patch.isEmpty()
          ? QByteArrayLiteral("canonical-patch:") +
                canonical_patch.toUtf8()
          : adjustment_payload.trimmed().isEmpty()
                ? QByteArrayLiteral(
                      "default-adjustments")
                : QByteArrayLiteral(
                      "invalid-adjustment-payload:") +
                      adjustment_payload.toUtf8();
  const auto adjustment_identity =
      QString::fromLatin1(
          QCryptographicHash::hash(
              identity_bytes,
              QCryptographicHash::Sha256)
              .toHex());
  const auto key =
      QStringLiteral(
          "photo-v5-canonical-adjustments:%1:%2")
          .arg(file_path, adjustment_identity);
  const auto hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QDir(photo_editor::desktop::AppBootstrap::cacheLocation())
      .filePath(QStringLiteral("thumbnails/%1.jpg").arg(QString::fromLatin1(hash)));
}

bool wait_for_thumbnail(
    photo_editor::desktop::CatalogAssetModel* model,
    int row,
    int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();

  while (timer.elapsed() < timeout_ms) {
    const auto thumbnail =
        model->data(model->index(row, 0), photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
    if (!thumbnail.isEmpty()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }

  return false;
}

QString wait_for_thumbnail_change(
    photo_editor::desktop::CatalogAssetModel* model,
    int row,
    const QString& previous_thumbnail,
    int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();

  while (timer.elapsed() < timeout_ms) {
    const auto thumbnail =
        model->data(model->index(row, 0), photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
    if (!thumbnail.isEmpty() && thumbnail != previous_thumbnail) {
      return thumbnail;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }

  return {};
}

bool wait_for_thumbnail_stale_detection(
    photo_editor::desktop::CatalogAssetModel* model,
    int row,
    int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();

  while (timer.elapsed() < timeout_ms) {
    if (model->data(
            model->index(row, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailStaleDetectedRole).toBool()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }

  return false;
}

bool wait_for_import_completion(
    photo_editor::desktop::CatalogAssetModel* model,
    int timeout_ms = 15000) {
  QElapsedTimer timer;
  timer.start();

  while (timer.elapsed() < timeout_ms) {
    if (!model->importing()) {
      return true;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }

  return !model->importing();
}

}  // namespace

int run_catalog_similarity_search_contract();

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CatalogAssetModelTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  if (app.arguments().contains(QStringLiteral("--similarity-binding-contract")))
    return run_catalog_similarity_search_contract();

  {
    photo_editor::desktop::CatalogAssetModel model;
    model.replaceAssetsForTesting({make_asset(QStringLiteral("startup-preserved"))});
    model.selectIndex(0);
    model.beginCatalogStartup();
    if (!model.loading() || !model.busy() || !model.errorMessage().isEmpty()) {
      return fail("catalog startup must be visibly pending until readiness is established");
    }
    model.reportCatalogStartupFailure(QStringLiteral("Catalog connection failed"));
    if (model.loading() || model.busy() || model.errorMessage().isEmpty() ||
        model.totalAssetCount() != 1 || model.selectedAssetId() != QStringLiteral("startup-preserved")) {
      return fail("catalog startup failure must show an error without clearing assets or selection");
    }
    model.beginCatalogStartup();
    if (!model.loading() || !model.errorMessage().isEmpty() || model.totalAssetCount() != 1) {
      return fail("retrying catalog startup must clear the old error and preserve existing assets");
    }
  }

  {
    photo_editor::desktop::CatalogAssetModel model;
    const auto a = make_asset(QStringLiteral("visual-a"));
    const auto b = make_asset(QStringLiteral("visual-b"));
    const auto c = make_asset(QStringLiteral("visual-c"));
    model.replaceAssetsForTesting({a, b, c});
    model.selectAll();
    model.setVisualSearchResults({c.asset_id, a.asset_id, c.asset_id, QStringLiteral("unknown")});
    if (!model.visualSearchActive() || model.assetCount() != 2 ||
        model.assetAt(0).value("assetId").toString() != c.asset_id ||
        model.selectedAssetIds().contains(b.asset_id)) {
      return fail("visual search should rank, deduplicate, ignore unknown IDs and prune hidden selection");
    }
    const auto gallery = model.galleryAssets();
    if (gallery.size() != 2 || gallery[0].toMap().value("assetId").toString() != c.asset_id ||
        gallery[1].toMap().value("assetId").toString() != a.asset_id ||
        gallery[0].toMap().contains("adjustmentPatchJson")) {
      return fail("gallery projection must preserve visible ordering without recipe payloads");
    }
    if (model.galleryAssetAt(0) != gallery[0].toMap() ||
        !model.galleryAssetAt(-1).isEmpty() || !model.galleryAssetAt(2).isEmpty())
      return fail("single-row gallery projection must match visible rows and reject invalid indices");
    model.setSearchFilter(QStringLiteral("visual-a"));
    if (model.assetCount() != 1 || model.assetAt(0).value("assetId").toString() != a.asset_id) {
      return fail("visual search must intersect existing metadata filters");
    }
    if (model.galleryAssets().size() != 1 ||
        model.galleryAssets()[0].toMap().value("assetId").toString() != a.asset_id)
      return fail("gallery projection must follow filtered membership");
    model.clearVisualSearchResults();
    if (model.visualSearchActive() || model.assetCount() != 1) {
      return fail("clearing visual results must preserve metadata filters");
    }
    model.setSearchFilter({});
    if (app.arguments().contains(QStringLiteral("--gallery-projection-contract"))) {
      if (model.galleryAssets().size() != 3)
        return fail("clearing gallery filters must restore every row");
      std::cout << "Gallery projection ordering, filtering and recipe exclusion passed\n";
      return EXIT_SUCCESS;
    }
    model.setVisualSearchResults({});
    if (model.assetCount() != 0 || !model.visualSearchActive() || !model.selectedAssetIds().isEmpty()) {
      return fail("zero visual matches must not expose or select the whole catalog");
    }
    model.clearFilters();
    if (model.visualSearchActive() || model.assetCount() != 3) {
      return fail("clear all filters should restore the library after empty visual search");
    }
  }

  {
    auto canon = make_asset(QStringLiteral("filter-canon"), QStringLiteral("CANON_0001.jpg"));
    canon.camera_label = QStringLiteral("Canon EOS R5");
    canon.thumbnail_source = QStringLiteral("image://facet/canon");
    canon.capture_date = QStringLiteral("Jan 15, 2025");
    canon.capture_date_value = QDate(2025, 1, 15);
    auto sony = make_asset(QStringLiteral("filter-sony"), QStringLiteral("SONY_0001.jpg"));
    sony.camera_label = QStringLiteral("Sony A7 IV");
    sony.thumbnail_source = QStringLiteral("image://facet/sony");
    sony.capture_date = QStringLiteral("Aug 4, 2026");
    sony.capture_date_value = QDate(2026, 8, 4);
    auto unknown = make_asset(QStringLiteral("filter-unknown"), QStringLiteral("UNKNOWN_0001.jpg"));
    unknown.camera_label.clear();
    unknown.thumbnail_source = QStringLiteral("image://facet/unknown");
    unknown.capture_date = QStringLiteral("Unknown capture date");
    unknown.capture_date_value = {};

    photo_editor::desktop::CatalogAssetModel facet_model;
    facet_model.replaceAssetsForTesting({sony, unknown, canon});
    if (facet_model.cameraLabels() != QStringList{QStringLiteral("Canon EOS R5"), QStringLiteral("Sony A7 IV")} ||
        facet_model.captureYears() != QStringList{QStringLiteral("2026"), QStringLiteral("2025")}) {
      return fail("library facets should expose sorted distinct cameras and descending capture years");
    }
    facet_model.setCameraFilter(QStringLiteral("Canon EOS R5"));
    if (facet_model.assetCount() != 1 || facet_model.assetAt(0).value(QStringLiteral("assetId")).toString() != canon.asset_id) {
      return fail("camera facets should filter by exact normalized camera label");
    }
    facet_model.clearFilters();
    facet_model.setCaptureDateFromFilter(QStringLiteral("2026-08-01"));
    facet_model.setCaptureDateToFilter(QStringLiteral("2026-08-31"));
    if (facet_model.assetCount() != 1 || facet_model.assetAt(0).value(QStringLiteral("assetId")).toString() != sony.asset_id) {
      return fail("capture date range filters should be inclusive and exclude unknown dates");
    }
    facet_model.setCaptureDateFromFilter(QStringLiteral("not-a-date"));
    if (facet_model.captureDateFromFilter() != QStringLiteral("2026-08-01")) {
      return fail("invalid capture date filter input should not replace the last valid boundary");
    }
    facet_model.clearFilters();
    facet_model.setCameraFilter(QStringLiteral("unknown"));
    if (facet_model.assetCount() != 1 || facet_model.assetAt(0).value(QStringLiteral("assetId")).toString() != unknown.asset_id) {
      return fail("camera facets should keep an explicit unknown-camera bucket");
    }
  }

  {
    photo_editor::desktop::CatalogAssetModel model;
    model.setInteractiveRenderingActive(true);
    QVector<photo_editor::desktop::CatalogAssetItem> large_catalog;
    for (int i = 0; i < 1000; ++i) {
      auto asset = make_asset(QStringLiteral("viewport-%1").arg(i), QStringLiteral("photo.jpg"));
      asset.online = true;
      asset.file_path = QStringLiteral("/isolated-thumbnail-proof/%1.jpg").arg(i);
      large_catalog.push_back(asset);
    }
    model.replaceAssetsForTesting(large_catalog);
    model.setVisibleAssetRange(0, 9);
    if (model.pendingThumbnailWorkerCount() > 29)
      return fail("thumbnail demand must be bounded by viewport and prefetch, not catalog size");
    model.setVisibleAssetRange(900, 909);
    if (model.pendingThumbnailWorkerCount() > 47)
      return fail("scrolling must drop obsolete thumbnail requests");
    model.setVisibleAssetRange(-1, -1);
    if (model.pendingThumbnailWorkerCount() > 1)
      return fail("hidden thumbnails must leave only the selected photo queued");
    model.replaceAssetsForTesting({});
    model.setInteractiveRenderingActive(false);
    model.setInteractiveRenderingActive(true);
    model.setInteractiveRenderingActive(true);
    if (!model.interactiveRenderingActive() ||
        model.interactiveRenderingPriorityDepth() != 2) {
      return fail("interactive rendering priority should support nested agent proof leases");
    }
    if (!model.waitForInteractiveThumbnailWorkers(25)) {
      return fail("an idle thumbnail scheduler should quiesce immediately for interactive rendering");
    }
    model.setInteractiveRenderingActive(false);
    if (!model.interactiveRenderingActive() ||
        model.interactiveRenderingPriorityDepth() != 1) {
      return fail("releasing a nested interactive rendering lease should keep thumbnail work deferred");
    }
    model.setInteractiveRenderingActive(false);
    if (model.interactiveRenderingActive() ||
        model.interactiveRenderingPriorityDepth() != 0) {
      return fail("releasing the final interactive rendering lease should resume normal thumbnail scheduling");
    }
    model.replaceAssetsForTesting({
        make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg")),
        make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg")),
        make_asset(QStringLiteral("asset-3"), QStringLiteral("IMG_0003.jpg")),
    });
    model.selectIndex(0);

    if (model.selectedAssetCount() != 0) {
      return fail("opening a photo should not auto-select it for batch actions");
    }
    if (model.selectedRating() != 0 || model.selectedPicked() || model.selectedRejected() ||
        !model.selectedColorLabel().isEmpty()) {
      return fail("new selections should start with default culling state");
    }

    model.setSelectedRating(4);
    model.toggleSelectedPick();
    model.toggleSelectedReject();
    model.setSelectedColorLabel(QStringLiteral("green"));

    if (model.selectedRating() != 4) {
      return fail("selected rating should update");
    }
    if (model.selectedPicked()) {
      return fail("rejecting a selection should clear picked state");
    }
    if (!model.selectedRejected()) {
      return fail("selected asset should be marked rejected");
    }
    if (model.selectedColorLabel() != QStringLiteral("green")) {
      return fail("selected asset should expose its color label");
    }
    if (model.data(model.index(0, 0), photo_editor::desktop::CatalogAssetModel::RatingRole).toInt() != 4) {
      return fail("model role should expose the updated rating");
    }
    if (!model.data(model.index(0, 0), photo_editor::desktop::CatalogAssetModel::RejectedRole).toBool()) {
      return fail("model role should expose the rejected state");
    }
    if (model.data(model.index(0, 0), photo_editor::desktop::CatalogAssetModel::ColorLabelRole).toString() !=
        QStringLiteral("green")) {
      return fail("model role should expose the color label");
    }
    if (!model.canUndoCulling() || model.cullingUndoLabel() != QStringLiteral("color label")) {
      return fail("culling changes should expose an undo operation");
    }

    model.undoCulling();
    if (!model.selectedColorLabel().isEmpty() || !model.canRedoCulling()) {
      return fail("undo should restore the previous color label and offer redo");
    }
    model.redoCulling();
    if (model.selectedColorLabel() != QStringLiteral("green") || model.canRedoCulling()) {
      return fail("redo should restore the color label and consume redo history");
    }
    if (model.selectedBurstSize() != 3 || model.selectedBurstPosition() != 1) {
      return fail("burst metadata should group sequential related assets");
    }
    if (model.selectedLensModel() != QStringLiteral("RF 28-70mm F2L USM")) {
      return fail("selected lens metadata should be exposed");
    }
    if (model.selectedIso() != QStringLiteral("ISO 400")) {
      return fail("selected iso metadata should be exposed");
    }
    if (model.selectedShutterSpeed() != QStringLiteral("1/250 s")) {
      return fail("selected shutter metadata should be exposed");
    }
    if (model.selectedAperture() != QStringLiteral("f/2.8")) {
      return fail("selected aperture metadata should be exposed");
    }

    const auto selected_asset = model.assetAt(0);
    if (selected_asset.value(QStringLiteral("iso")).toString() != QStringLiteral("ISO 400")) {
      return fail("asset maps should include iso metadata");
    }
    if (selected_asset.value(QStringLiteral("shutterSpeed")).toString() != QStringLiteral("1/250 s")) {
      return fail("asset maps should include shutter metadata");
    }
    if (selected_asset.value(QStringLiteral("aperture")).toString() != QStringLiteral("f/2.8")) {
      return fail("asset maps should include aperture metadata");
    }
    const auto catalog_asset_by_id =
        model.assetById(QStringLiteral("asset-2"));
    if (catalog_asset_by_id.value(QStringLiteral("assetId")).toString() !=
            QStringLiteral("asset-2") ||
        !catalog_asset_by_id.value(QStringLiteral("filePath"))
             .toString()
             .endsWith(QStringLiteral("IMG_0002.jpg")) ||
        !model.assetById(QStringLiteral("missing-asset")).isEmpty()) {
      return fail("catalog asset lookup should resolve thumbnail metadata directly by stable asset id");
    }

    model.toggleSelectionIndex(0);
    model.toggleSelectionIndex(1);
    if (model.selectedAssetCount() != 2 || !model.assetSelected(QStringLiteral("asset-1")) ||
        !model.assetSelected(QStringLiteral("asset-2"))) {
      return fail("toggle selection should build a multi-photo batch selection");
    }

    model.clearSelection();
    model.selectAll();
    if (model.selectedAssetCount() != 3 || !model.assetSelected(QStringLiteral("asset-3"))) {
      return fail("select all should include every visible asset in the batch selection");
    }
    model.clearSelection();
    model.toggleSelectionIndex(0);
    model.toggleSelectionIndex(1);

    photo_editor::desktop::EditRecipeState copied_state;
    copied_state.exposure = 1.15;
    copied_state.temperature = 18.0;
    copied_state.crop_preset = QStringLiteral("4x5");
    const auto copied_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        copied_state, {QStringLiteral("tone")},
        photo_editor::desktop::AdjustmentPatchScope::SourceAgnostic);
    if (model.applyAdjustmentPatchJsonToAssetIds(
            model.selectedAssetIds(), copied_patch) != 2) {
      return fail("batch adjustment patch apply should update every selected photo");
    }

    const auto asset_1_recipe =
        adjustment_state_from_patch(
            model.data(
                     model.index(0, 0),
                     photo_editor::desktop::
                         CatalogAssetModel::
                             AdjustmentPatchJsonRole)
                .toString());
    const auto asset_2_recipe =
        adjustment_state_from_patch(
            model.data(
                     model.index(1, 0),
                     photo_editor::desktop::
                         CatalogAssetModel::
                             AdjustmentPatchJsonRole)
                .toString());
    if (std::abs(asset_1_recipe.exposure - 1.15) > 1e-6 || std::abs(asset_2_recipe.exposure - 1.15) > 1e-6) {
      return fail("tone-only sync should copy tone settings into all selected recipe states");
    }
    if (std::abs(asset_1_recipe.temperature) > 1e-6 || std::abs(asset_2_recipe.temperature) > 1e-6) {
      return fail("tone-only sync should leave color settings untouched");
    }
    photo_editor::desktop::EditRecipeState preset_state;
    preset_state.exposure = -0.4;
    preset_state.temperature = 12.0;
    const auto preset_recipe =
        photo_editor::desktop::serialize_edit_recipe_json(
            preset_state);
    const auto preset_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        preset_state, {QStringLiteral("color")},
        photo_editor::desktop::AdjustmentPatchScope::SourceAgnostic);
    if (model.applyAdjustmentPatchJsonToAssetIds(
            model.selectedAssetIds(), preset_patch) != 2) {
      return fail("batch preset patch apply should update every selected photo");
    }
    const auto preset_asset_1_recipe =
        adjustment_state_from_patch(
            model.data(
                     model.index(0, 0),
                     photo_editor::desktop::
                         CatalogAssetModel::
                             AdjustmentPatchJsonRole)
                .toString());
    const auto preset_asset_2_recipe =
        adjustment_state_from_patch(
            model.data(
                     model.index(1, 0),
                     photo_editor::desktop::
                         CatalogAssetModel::
                             AdjustmentPatchJsonRole)
                .toString());
    if (std::abs(preset_asset_1_recipe.temperature - 12.0) > 1e-6 ||
        std::abs(preset_asset_2_recipe.temperature - 12.0) > 1e-6) {
      return fail("batch preset apply should copy selected preset groups into every target");
    }
    if (std::abs(preset_asset_1_recipe.exposure - 1.15) > 1e-6 ||
        std::abs(preset_asset_2_recipe.exposure - 1.15) > 1e-6) {
      return fail("batch preset apply should leave excluded groups untouched");
    }
    if (!model.canUndoBatchRecipe()) {
      return fail("multi-photo recipe application should expose one-step batch undo");
    }
    const auto batch_history = model.batchRecipeHistoryEntries();
    const auto batch_history_current_index = model.batchRecipeHistoryCurrentIndex();
    const auto latest_batch_history = batch_history.constLast().toMap();
    if (batch_history.size() < 2 ||
        batch_history_current_index != batch_history.size() - 1 ||
        latest_batch_history.value(QStringLiteral("label")).toString() !=
            QStringLiteral("Synced adjustments across 2 photos") ||
        latest_batch_history.value(QStringLiteral("detail")).toString() !=
            QStringLiteral("2 photos changed together") ||
        latest_batch_history.value(QStringLiteral("scope")).toString() !=
            QStringLiteral("Batch") ||
        !latest_batch_history.value(QStringLiteral("current")).toBool() ||
        !batch_history.at(0).toMap().value(QStringLiteral("restorable")).toBool()) {
      return fail("batch edits should expose ordered, user-facing, restorable history entries");
    }
    model.undoBatchRecipe();
    const auto undone_asset_1_recipe =
        adjustment_state_from_patch(
            model.data(
                     model.index(0, 0),
                     photo_editor::desktop::
                         CatalogAssetModel::
                             AdjustmentPatchJsonRole)
                .toString());
    if (std::abs(undone_asset_1_recipe.exposure - 1.15) > 1e-6 ||
        std::abs(undone_asset_1_recipe.temperature) > 1e-6 || !model.canRedoBatchRecipe()) {
      return fail("batch undo should restore the complete prior multi-photo state in one operation");
    }
    model.redoBatchRecipe();
    const auto redone_asset_1_recipe =
        adjustment_state_from_patch(
            model.data(
                     model.index(0, 0),
                     photo_editor::desktop::
                         CatalogAssetModel::
                             AdjustmentPatchJsonRole)
                .toString());
    if (std::abs(redone_asset_1_recipe.temperature - 12.0) > 1e-6) {
      return fail("batch redo should restore the synchronized adjustment groups");
    }
    model.restoreBatchRecipeHistoryIndex(batch_history_current_index - 1);
    if (model.batchRecipeHistoryCurrentIndex() != batch_history_current_index - 1 ||
        !model.canRedoBatchRecipe()) {
      return fail("batch history should restore the recipes before the batch in one selected step");
    }
    model.restoreBatchRecipeHistoryIndex(batch_history_current_index);
    const auto restored_batch_recipe =
        adjustment_state_from_patch(
            model.data(
                     model.index(0, 0),
                     photo_editor::desktop::
                         CatalogAssetModel::
                             AdjustmentPatchJsonRole)
                .toString());
    if (model.batchRecipeHistoryCurrentIndex() != batch_history_current_index ||
        std::abs(restored_batch_recipe.temperature - 12.0) > 1e-6) {
      return fail("batch history should restore a selected applied batch state exactly");
    }
    model.setImportPresetAdjustmentPatchJson(
        preset_patch,
        {QStringLiteral("color")});
    model.clearImportPresetAdjustmentPatchJson();

    model.toggleSelectionIndex(1);
    model.toggleSelectionIndex(0);
    if (model.selectedAssetCount() != 0) {
      return fail("manual selection should clear back to zero items");
    }

    model.selectIndex(1);
    model.setSelectedRating(2);
    model.toggleSelectedPick();

    if (model.visiblePickedCount() != 1 || model.visibleRejectedCount() != 1 || model.visibleRatedCount() != 2) {
      return fail("visible culling stats should track the current gallery state");
    }

    model.setReviewFilter(QStringLiteral("picked"));
    if (model.assetCount() != 1 || model.selectedAssetId() != QStringLiteral("asset-2")) {
      return fail("picked review filter should isolate picked assets");
    }

    model.setReviewFilter(QStringLiteral("all"));
    model.setMinimumRatingFilter(3);
    if (model.assetCount() != 1 || model.selectedAssetId() != QStringLiteral("asset-1")) {
      return fail("minimum rating filter should isolate the highest-rated asset");
    }

    model.setMinimumRatingFilter(0);

    const auto visible_assets = model.visibleAssets();
    if (visible_assets.size() != 3 ||
        visible_assets.at(0).toMap().value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1")) {
      return fail("visible asset helper should expose the current filtered gallery order");
    }

    model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Shoot A"));
    model.addAssetToProject(QStringLiteral("asset-3"), QStringLiteral("Shoot A"));
    const auto project_assets = model.assetsForProject(QStringLiteral("Shoot A"));
    const auto unassigned_assets = model.assetsForProject(QStringLiteral("unassigned"));
    if (project_assets.size() != 2 ||
        project_assets.at(0).toMap().value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
        project_assets.at(1).toMap().value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-3") ||
        unassigned_assets.size() != 1 ||
        unassigned_assets.at(0).toMap().value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-2")) {
      return fail("project asset helper should expose album/project and unassigned scopes");
    }

    model.setSearchFilter(QStringLiteral("IMG_0003"));
    const auto filtered_assets = model.visibleAssets();
    if (filtered_assets.size() != 1 ||
        filtered_assets.first().toMap().value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-3")) {
      return fail("visible asset helper should honor the current search filter");
    }
    model.setSearchFilter(QString());
  }

  {
    constexpr int kBatchAssetCount = 20;
    QVector<photo_editor::desktop::CatalogAssetItem> batch_assets;
    QVector<QString>
        original_adjustment_patch_jsons;
    batch_assets.reserve(kBatchAssetCount);
    original_adjustment_patch_jsons.reserve(
        kBatchAssetCount);
    for (int index = 0; index < kBatchAssetCount; ++index) {
      auto item = make_asset(
          QStringLiteral("batch-%1").arg(index, 2, 10, QChar('0')),
          QStringLiteral("BATCH_%1.ppm").arg(index, 2, 10, QChar('0')));
      item.file_path = runtime_root.filePath(
          QStringLiteral("batch-sources/BATCH_%1.ppm").arg(index, 2, 10, QChar('0')));
      if (!write_ppm_fixture(item.file_path)) {
        return fail("20-photo batch source fixtures should be written");
      }
      photo_editor::desktop::EditRecipeState initial_state;
      initial_state.exposure = -0.2 + static_cast<double>(index) * 0.01;
      initial_state.temperature = static_cast<double>(index);
      initial_state.crop_preset = index % 2 == 0 ? QStringLiteral("4x5") : QStringLiteral("16x9");
      initial_state.crop_rect_x = static_cast<double>(index) / 100.0;
      initial_state.linear_mask_enabled = true;
      initial_state.linear_mask_exposure = static_cast<double>(index) / 10.0;
      item.adjustment_patch_json =
          photo_editor::desktop::
              serialize_adjustment_patch_json(
                  initial_state,
                  photo_editor::desktop::
                      all_adjustment_groups(),
                  photo_editor::desktop::
                      AdjustmentPatchScope::
                          AssetBound);
      original_adjustment_patch_jsons.push_back(
          item.adjustment_patch_json);
      batch_assets.push_back(item);
    }

    photo_editor::desktop::CatalogAssetModel batch_model;
    batch_model.replaceAssetsForTesting(batch_assets);
    batch_model.setVisibleAssetRange(0, kBatchAssetCount - 1);
    batch_model.selectAll();
    QVector<QString> original_thumbnail_sources;
    original_thumbnail_sources.reserve(kBatchAssetCount);
    for (int index = 0; index < kBatchAssetCount; ++index) {
      if (!wait_for_thumbnail(&batch_model, index, 5000)) {
        std::cerr << "thumbnail index=" << index
                  << " active=" << batch_model.activeThumbnailWorkerCount()
                  << " pending=" << batch_model.pendingThumbnailWorkerCount()
                  << " priorityActive="
                  << batch_model.interactiveRenderingActive() << '\n';
        return fail("20-photo batch should publish initial thumbnails");
      }
      original_thumbnail_sources.push_back(
          batch_model.data(
                         batch_model.index(index, 0),
                         photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole)
              .toString());
    }

    photo_editor::desktop::EditRecipeState source_state;
    source_state.exposure = 0.85;
    source_state.temperature = -14.0;
    source_state.crop_preset = QStringLiteral("1x1");
    source_state.linear_mask_enabled = false;
    const auto source_patch_json = photo_editor::desktop::serialize_adjustment_patch_json(
        source_state,
        {QStringLiteral("tone"), QStringLiteral("color")},
        photo_editor::desktop::AdjustmentPatchScope::SourceAgnostic);
    if (batch_model.applyAdjustmentPatchJsonToAssetIds(
            batch_model.selectedAssetIds(),
            source_patch_json) != kBatchAssetCount) {
      return fail("20-photo selective batch should update every included target");
    }

    QImage export_source(24, 16, QImage::Format_ARGB32);
    export_source.fill(QColor(96, 112, 128));
    QVector<QImage> original_export_outputs;
    original_export_outputs.reserve(kBatchAssetCount);
    QVector<QString> updated_thumbnail_sources;
    updated_thumbnail_sources.reserve(kBatchAssetCount);
    for (int index = 0; index < kBatchAssetCount; ++index) {
      const auto updated_state =
          adjustment_state_for_asset(
              batch_model.assetAt(index));
      const auto original_state =
          adjustment_state_from_patch(
              original_adjustment_patch_jsons.at(
                  index));
      if (std::abs(updated_state.exposure - 0.85) > 1e-6 ||
          std::abs(updated_state.temperature + 14.0) > 1e-6) {
        return fail("20-photo selective batch should synchronize light and color");
      }
      if (updated_state.crop_preset != original_state.crop_preset ||
          std::abs(updated_state.crop_rect_x - original_state.crop_rect_x) > 1e-6 ||
          updated_state.linear_mask_enabled != original_state.linear_mask_enabled ||
          std::abs(updated_state.linear_mask_exposure - original_state.linear_mask_exposure) > 1e-6) {
        return fail("20-photo selective batch should preserve excluded crop and mask state");
      }
      const auto original_output = photo_editor::desktop::render_output_image_with_backend(
          export_source,
          original_state,
          QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32,
          photo_editor::desktop::RenderBackendPreference::Auto);
      const auto updated_output = photo_editor::desktop::render_output_image_with_backend(
          export_source,
          updated_state,
          QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32,
          photo_editor::desktop::RenderBackendPreference::Auto);
      if (original_output.isNull() || updated_output.isNull() || original_output == updated_output) {
        return fail("batch-updated recipes should produce changed final export pixels");
      }
      original_export_outputs.push_back(original_output);
      const auto updated_thumbnail = wait_for_thumbnail_change(
          &batch_model,
          index,
          original_thumbnail_sources.at(index),
          5000);
      if (updated_thumbnail.isEmpty()) {
        return fail("batch-updated recipes should refresh every target thumbnail");
      }
      updated_thumbnail_sources.push_back(updated_thumbnail);
    }

    batch_model.undoBatchRecipe();
    for (int index = 0; index < kBatchAssetCount; ++index) {
      const auto restored_asset =
          batch_model.assetAt(index);
      if (adjustment_patch_for_asset(
              restored_asset) !=
          original_adjustment_patch_jsons.at(
              index)) {
        return fail("one batch undo should restore all 20 original canonical adjustments");
      }
      const auto restored_output = photo_editor::desktop::render_output_image_with_backend(
          export_source,
          adjustment_state_for_asset(
              restored_asset),
          QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32,
          photo_editor::desktop::RenderBackendPreference::Auto);
      if (restored_output != original_export_outputs.at(index)) {
        return fail("batch undo should restore every final export output");
      }
      if (wait_for_thumbnail_change(&batch_model, index, updated_thumbnail_sources.at(index), 5000).isEmpty()) {
        return fail("batch undo should refresh every restored thumbnail");
      }
    }
  }

  {
    constexpr int kCullingAssetCount = 500;
    constexpr qint64 kInteractiveP95BudgetMs = 50;
    QVector<photo_editor::desktop::CatalogAssetItem> culling_assets;
    culling_assets.reserve(kCullingAssetCount);
    for (int index = 0; index < kCullingAssetCount; ++index) {
      culling_assets.push_back(
          make_asset(
              QStringLiteral("culling-%1").arg(index, 3, 10, QChar('0')),
              QStringLiteral("CULL_%1.jpg").arg(index, 3, 10, QChar('0'))));
    }

    photo_editor::desktop::CatalogAssetModel culling_model;
    culling_model.replaceAssetsForTesting(culling_assets);
    QVector<qint64> visible_decision_latencies_ms;
    visible_decision_latencies_ms.reserve(kCullingAssetCount);

    for (int index = 0; index < kCullingAssetCount; ++index) {
      culling_model.selectIndex(index);
      if (culling_model.selectedAssetId() != culling_assets.at(index).asset_id) {
        return fail("scripted culling should keep the requested photo active");
      }

      QElapsedTimer decision_timer;
      decision_timer.start();
      switch (index % 4) {
        case 0:
          culling_model.setSelectedRating((index % 5) + 1);
          break;
        case 1:
          culling_model.toggleSelectedReject();
          break;
        case 2:
          culling_model.toggleSelectedPick();
          break;
        default:
          culling_model.setSelectedColorLabel(QStringLiteral("blue"));
          break;
      }
      if (index + 1 < kCullingAssetCount) {
        culling_model.selectRelative(1);
      }
      visible_decision_latencies_ms.push_back(decision_timer.elapsed());

      const auto reviewed_asset = culling_model.assetAt(index);
      if ((index % 4 == 0 && reviewed_asset.value(QStringLiteral("rating")).toInt() == 0) ||
          (index % 4 == 1 && !reviewed_asset.value(QStringLiteral("rejected")).toBool()) ||
          (index % 4 == 2 && !reviewed_asset.value(QStringLiteral("picked")).toBool()) ||
          (index % 4 == 3 && reviewed_asset.value(QStringLiteral("colorLabel")).toString() !=
                                   QStringLiteral("blue"))) {
        return fail("scripted culling should preserve every keyboard-equivalent review decision");
      }
      if (index + 1 < kCullingAssetCount &&
          culling_model.selectedAssetId() != culling_assets.at(index + 1).asset_id) {
        return fail("scripted culling should advance to exactly the next photo");
      }
    }

    std::sort(visible_decision_latencies_ms.begin(), visible_decision_latencies_ms.end());
    const auto p95_index = static_cast<qsizetype>(
        std::ceil(static_cast<double>(visible_decision_latencies_ms.size()) * 0.95) - 1.0);
    const auto p95_latency_ms = visible_decision_latencies_ms.at(std::max<qsizetype>(0, p95_index));
    std::cout << "culling_500_photo_p95_ms=" << p95_latency_ms << '\n';
    if (p95_latency_ms > kInteractiveP95BudgetMs) {
      return fail("scripted culling p95 should meet the 50 ms visible-feedback budget");
    }

    culling_model.setReviewFilter(QStringLiteral("rejected"));
    if (culling_model.assetCount() != kCullingAssetCount / 4) {
      return fail("review filters should immediately reflect the 500-photo culling pass");
    }
    culling_model.setReviewFilter(QStringLiteral("picked"));
    if (culling_model.assetCount() != kCullingAssetCount / 4) {
      return fail("pick filters should not expose stale state after scripted culling");
    }
    culling_model.setReviewFilter(QStringLiteral("all"));
  }

  {
    photo_editor::desktop::CatalogAssetModel model;
    const auto filters = model.importFileNameFilters();
    if (!filters.contains(QStringLiteral("*.cr2")) || !filters.contains(QStringLiteral("*.CR2"))) {
      return fail("import file filters should include Canon RAW extensions in lower and upper case");
    }
    if (!filters.contains(QStringLiteral("*.jpg")) || !filters.contains(QStringLiteral("*.JPG"))) {
      return fail("import file filters should include common raster extensions in lower and upper case");
    }
  }

  {
    photo_editor::desktop::CatalogAssetModel reloaded_model;
    reloaded_model.replaceAssetsForTesting({
        make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg")),
        make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg")),
        make_asset(QStringLiteral("asset-3"), QStringLiteral("IMG_0003.jpg")),
    });
    reloaded_model.selectIndex(0);

    if (reloaded_model.selectedRating() != 4) {
      return fail("rating should persist across model reloads");
    }
    if (!reloaded_model.selectedRejected() || reloaded_model.selectedPicked()) {
      return fail("pick and reject state should persist consistently");
    }
    if (reloaded_model.selectedColorLabel() != QStringLiteral("green")) {
      return fail("color labels should persist across model reloads");
    }

    const auto persisted_recipe =
        adjustment_state_from_patch(
            reloaded_model
                .selectedAdjustmentPatchJson());
    if (std::abs(persisted_recipe.exposure - 1.15) > 1e-6) {
      return fail("explicit recipe overrides should persist across model reloads");
    }

    reloaded_model.setReviewFilter(QStringLiteral("picked"));
    if (reloaded_model.assetCount() != 1 || reloaded_model.selectedAssetId() != QStringLiteral("asset-2")) {
      return fail("review filters should work after reloading persisted culling state");
    }

    reloaded_model.setReviewFilter(QStringLiteral("all"));
    reloaded_model.selectIndex(0);

    reloaded_model.clearSelectedCullState();
  }

  {
    photo_editor::desktop::CatalogAssetModel cleared_model;
    cleared_model.replaceAssetsForTesting({
        make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg")),
        make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg")),
        make_asset(QStringLiteral("asset-3"), QStringLiteral("IMG_0003.jpg")),
    });
    cleared_model.selectIndex(0);

    if (cleared_model.selectedRating() != 0 || cleared_model.selectedPicked() || cleared_model.selectedRejected() ||
        !cleared_model.selectedColorLabel().isEmpty()) {
      return fail("clearing culling state should remove persisted review metadata");
    }
  }

  {
    auto duplicate_a = make_asset(QStringLiteral("dup-1"), QStringLiteral("PORTRAIT.jpg"));
    duplicate_a.capture_date = QStringLiteral("Mar 29, 2026");
    duplicate_a.capture_date_value = QDate(2026, 3, 29);
    duplicate_a.lens_model = QStringLiteral("50mm");

    auto duplicate_b = make_asset(QStringLiteral("dup-2"), QStringLiteral("PORTRAIT copy.jpg"));
    duplicate_b.capture_date = duplicate_a.capture_date;
    duplicate_b.capture_date_value = duplicate_a.capture_date_value;
    duplicate_b.lens_model = duplicate_a.lens_model;

    auto duplicate_c = make_asset(QStringLiteral("dup-3"), QStringLiteral("PORTRAIT final.jpg"));
    duplicate_c.capture_date = duplicate_a.capture_date;
    duplicate_c.capture_date_value = duplicate_a.capture_date_value;
    duplicate_c.lens_model = duplicate_a.lens_model;

    auto sync_peer = make_asset(QStringLiteral("dup-4"), QStringLiteral("SCENE.jpg"));
    sync_peer.capture_date = duplicate_a.capture_date;
    sync_peer.capture_date_value = duplicate_a.capture_date_value;
    sync_peer.lens_model = duplicate_a.lens_model;

    photo_editor::desktop::CatalogAssetModel intelligence_model;
    intelligence_model.replaceAssetsForTesting({duplicate_a, duplicate_b, duplicate_c, sync_peer});
    intelligence_model.selectIndex(0);

    if (intelligence_model.selectedDuplicateGroupSize() != 3) {
      return fail("duplicate grouping should cluster copy and variant filenames together");
    }
    if (!intelligence_model.selectedBestShotSuggested()) {
      return fail("the canonical duplicate should be suggested as the best shot");
    }
    if (intelligence_model.selectedSyncGroupSize() != 4) {
      return fail("sync suggestions should group frames shot on the same camera, lens, and day");
    }

    const auto duplicate_variant = intelligence_model.assetAt(1);
    if (duplicate_variant.value(QStringLiteral("duplicateGroupSize")).toInt() != 3) {
      return fail("duplicate intelligence should be exposed through the asset map");
    }
    if (duplicate_variant.value(QStringLiteral("bestShotSuggested")).toBool()) {
      return fail("only the best duplicate candidate should be marked as suggested");
    }
  }

  {
    const auto image_path = runtime_root.filePath(QStringLiteral("thumbs/IMG_async.ppm"));
    if (!write_ppm_fixture(image_path)) {
      return fail("thumbnail source image should be created");
    }

    auto thumbnail_asset = make_asset(QStringLiteral("asset-thumb"), QStringLiteral("IMG_async.ppm"));
    thumbnail_asset.file_path = image_path;
    thumbnail_asset.location_path = image_path;

    photo_editor::desktop::CatalogAssetModel thumbnail_model;
    thumbnail_model.replaceAssetsForTesting({thumbnail_asset});

    const auto initial_thumbnail =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
    if (!initial_thumbnail.isEmpty()) {
      return fail("thumbnail generation should no longer block model replacement");
    }
    if (!wait_for_thumbnail(&thumbnail_model, 0)) {
      return fail("background thumbnail generation should eventually populate the model role");
    }
  }

  {
    const auto raw_path = runtime_root.filePath(QStringLiteral("thumbs/BROKEN_RAW.CR3"));
    if (!write_file(raw_path)) {
      return fail("raw thumbnail fallback source file should be created");
    }

    if (photo_editor::desktop::raw_image_supported(raw_path)) {
      const auto corrupt_cache_path = test_thumbnail_cache_path(raw_path);
      if (!QDir().mkpath(QFileInfo(corrupt_cache_path).dir().absolutePath())) {
        return fail("raw thumbnail corrupt cache directory should be created");
      }
      QFile corrupt_cache(corrupt_cache_path);
      if (!corrupt_cache.open(QIODevice::WriteOnly)) {
        return fail("raw thumbnail corrupt cache file should be writable");
      }
      corrupt_cache.write("not a readable thumbnail");
      corrupt_cache.close();

      auto raw_asset = make_asset(QStringLiteral("asset-raw-thumb"), QStringLiteral("BROKEN_RAW.CR3"));
      raw_asset.file_path = raw_path;
      raw_asset.location_path = raw_path;

      photo_editor::desktop::CatalogAssetModel raw_thumbnail_model;
      raw_thumbnail_model.replaceAssetsForTesting({raw_asset});

      if (!wait_for_thumbnail(&raw_thumbnail_model, 0)) {
        return fail("RAW thumbnail generation should surface a placeholder instead of staying empty");
      }

      const auto raw_thumbnail =
          raw_thumbnail_model.data(
              raw_thumbnail_model.index(0, 0),
              photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
      const QUrl raw_thumbnail_url(raw_thumbnail);
      if (!raw_thumbnail_url.hasQuery()) {
        return fail("RAW fallback thumbnails should include immutable version query parameters");
      }

      const QImage raw_thumbnail_image(raw_thumbnail_url.toLocalFile());
      if (raw_thumbnail_image.isNull() || raw_thumbnail_image.width() < 200 ||
          raw_thumbnail_image.height() < 150) {
        return fail("RAW thumbnail fallback should replace bad cache entries with a readable error thumbnail");
      }
    }
  }

  {
    const auto image_path = runtime_root.filePath(QStringLiteral("thumbs/IMG_recipe.ppm"));
    if (!write_ppm_fixture(image_path)) {
      return fail("recipe thumbnail source image should be created");
    }

    auto thumbnail_asset = make_asset(QStringLiteral("asset-recipe-thumb"), QStringLiteral("IMG_recipe.ppm"));
    thumbnail_asset.file_path = image_path;
    thumbnail_asset.location_path = image_path;

    photo_editor::desktop::CatalogAssetModel thumbnail_model;
    thumbnail_model.replaceAssetsForTesting({thumbnail_asset});

    if (!wait_for_thumbnail(&thumbnail_model, 0)) {
      return fail("initial recipe thumbnail should be generated");
    }
    if (!thumbnail_model.assetAt(0).value("searchPreviewCurrent").toBool()) {
      return fail("current durable thumbnail should be available to similarity indexing");
    }

    const auto initial_thumbnail =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
    if (!QUrl(initial_thumbnail).hasQuery()) {
      return fail("thumbnail sources should include immutable version query parameters for QML image caching");
    }
    const QImage initial_image(QUrl(initial_thumbnail).toLocalFile());
    if (initial_image.isNull()) {
      return fail("initial thumbnail image should be readable from disk");
    }

    photo_editor::desktop::EditRecipeState edited_state;
    edited_state.exposure = 1.0;
    const auto edited_recipe = photo_editor::desktop::serialize_edit_recipe_json(edited_state);
    const auto edited_patch =
        photo_editor::desktop::canonical_asset_bound_adjustment_patch_json(
            edited_recipe);
    if (thumbnail_model.applyTransientAdjustmentPatchJsonToAssetIds(
            {QStringLiteral("asset-recipe-thumb")},
            edited_patch,
            {QStringLiteral("tone")}) != 1) {
      return fail("transient recipe apply should publish the live recipe state");
    }
    const auto transient_recipe = adjustment_state_from_patch(
        thumbnail_model
            .data(
                thumbnail_model.index(0, 0),
                photo_editor::desktop::CatalogAssetModel::AdjustmentPatchJsonRole)
            .toString());
    const auto transient_thumbnail =
        thumbnail_model
            .data(
                thumbnail_model.index(0, 0),
                photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole)
            .toString();
    if (std::abs(transient_recipe.exposure - 1.0) > 1e-6 ||
        transient_thumbnail != initial_thumbnail) {
      return fail("transient recipe state should update the live asset without invalidating its durable thumbnail");
    }
    if (thumbnail_model.assetAt(0).value("searchPreviewCurrent").toBool()) {
      return fail("a retained thumbnail must not be indexed as the current transient edit");
    }
    if (thumbnail_model.applyAdjustmentPatchJsonToAssetIds(
            {QStringLiteral("asset-recipe-thumb")},
            edited_patch,
            {QStringLiteral("tone")}) != 1) {
      return fail("validated recipe apply should promote an identical transient preview into durable state");
    }

    const auto edited_thumbnail = wait_for_thumbnail_change(&thumbnail_model, 0, initial_thumbnail);
    if (edited_thumbnail.isEmpty()) {
      return fail("thumbnail should be regenerated after applying recipe edits");
    }
    if (!QUrl(edited_thumbnail).hasQuery()) {
      return fail("edited thumbnail sources should keep immutable version query parameters");
    }

    const QImage edited_image(QUrl(edited_thumbnail).toLocalFile());
    if (edited_image.isNull()) {
      return fail("edited thumbnail image should be readable from disk");
    }

    const auto initial_value = qGray(initial_image.pixel(0, 0));
    const auto edited_value = qGray(edited_image.pixel(0, 0));
    if (edited_value <= initial_value) {
      return fail("edited thumbnail should reflect brighter exposure");
    }
  }

  {
    const auto image_path = runtime_root.filePath(QStringLiteral("thumbs/IMG_recipe_race.ppm"));
    if (!write_ppm_fixture(image_path)) {
      return fail("recipe race thumbnail source image should be created");
    }

    auto thumbnail_asset = make_asset(QStringLiteral("asset-recipe-race-thumb"), QStringLiteral("IMG_recipe_race.ppm"));
    thumbnail_asset.file_path = image_path;
    thumbnail_asset.location_path = image_path;

    photo_editor::desktop::CatalogAssetModel thumbnail_model;
    thumbnail_model.replaceAssetsForTesting({thumbnail_asset});

    photo_editor::desktop::EditRecipeState edited_state;
    edited_state.exposure = 1.0;
    const auto edited_recipe = photo_editor::desktop::serialize_edit_recipe_json(edited_state);
    if (thumbnail_model.applyAdjustmentPatchJsonToAssetIds(
            {QStringLiteral("asset-recipe-race-thumb")},
            photo_editor::desktop::
                canonical_asset_bound_adjustment_patch_json(
                    edited_recipe),
            {QStringLiteral("tone")}) != 1) {
      return fail("recipe apply should update an asset with an active thumbnail task");
    }
    if (thumbnail_model.applyThumbnailResultForTesting(
            QStringLiteral("asset-recipe-race-thumb"),
            image_path,
            QString(),
            QStringLiteral("file:///stale-catalog-thumbnail.jpg"))) {
      return fail("stale injected catalog thumbnail result should not apply over the current recipe");
    }

    if (!wait_for_thumbnail_stale_detection(&thumbnail_model, 0)) {
      return fail("catalog grid should expose stale thumbnail recipe mismatch metadata");
    }
    const auto stale_reason =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailStaleDetectionReasonRole).toString();
    const auto stale_previous_hash =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailStalePreviousRecipeHashRole).toString();
    const auto stale_current_hash =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailStaleCurrentRecipeHashRole).toString();
    const auto stale_signature =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailStaleDetectionSignatureRole).toString();
    if (stale_reason != QStringLiteral("catalog_thumbnail_recipe_mismatch") ||
        !thumbnail_model.data(
             thumbnail_model.index(0, 0),
             photo_editor::desktop::CatalogAssetModel::ThumbnailStaleMismatchRole).toBool() ||
        stale_previous_hash.isEmpty() ||
        stale_previous_hash == stale_current_hash ||
        !stale_signature.contains(QStringLiteral("catalog-thumbnail-stale|detected:true|mismatch:true")) ||
        !stale_signature.contains(stale_previous_hash) ||
        !stale_signature.contains(stale_current_hash)) {
      return fail("catalog stale thumbnail metadata should include reason, mismatch, old hash, and current hash");
    }
    if (thumbnail_model.assetAt(0).value("searchPreviewCurrent").toBool()) {
      return fail("a stale recipe thumbnail must not be advertised to similarity indexing");
    }

    if (!wait_for_thumbnail(&thumbnail_model, 0)) {
      return fail("stale in-flight thumbnail results should schedule the current recipe thumbnail");
    }

    const auto edited_thumbnail =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
    const QUrl edited_thumbnail_url(edited_thumbnail);
    if (edited_thumbnail_url.toLocalFile() != test_thumbnail_cache_path(image_path, edited_recipe)) {
      return fail("thumbnail cache path should be keyed by the canonical current adjustments");
    }
    if (!thumbnail_model.assetAt(0).value("searchPreviewCurrent").toBool()) {
      return fail("a newly validated current thumbnail must become searchable despite historical stale-result diagnostics");
    }

    const QImage edited_image(edited_thumbnail_url.toLocalFile());
    if (edited_image.isNull() || qGray(edited_image.pixel(0, 0)) <= 93) {
      return fail("active-task recipe thumbnail should reflect the edited exposure");
    }
  }

  // A named bounded contract runs through the rendered-thumbnail/provenance
  // assertions above, without entering the later external import-job workflow.
  // The default invocation still executes the complete catalog regression.
  if (app.arguments().contains(QStringLiteral("--search-preview-contract"))) {
    std::cout << "current/transient/stale/recovered search preview provenance passed\n";
    return EXIT_SUCCESS;
  }

  {
    auto base_asset = make_asset(QStringLiteral("variant-source"), QStringLiteral("IMG_variant.jpg"));
    base_asset.file_path = runtime_root.filePath(QStringLiteral("variants/IMG_variant.jpg"));
    base_asset.location_path = base_asset.file_path;
    if (!write_file(base_asset.file_path)) {
      return fail("virtual copy source file should be created");
    }

    photo_editor::desktop::CatalogAssetModel variant_model;
    variant_model.replaceAssetsForTesting({base_asset});
    variant_model.selectIndex(0);

    const auto virtual_copy_id =
        variant_model.createVirtualCopyForSelectedAsset(QStringLiteral("Black and White"));
    if (virtual_copy_id.isEmpty()) {
      return fail("creating a selected virtual copy should return a variant asset id");
    }
    if (variant_model.assetCount() != 2 || variant_model.totalAssetCount() != 2) {
      return fail("virtual copies should appear as additional catalog recipe variants");
    }
    if (!variant_model.selectedVirtualCopy() ||
        variant_model.selectedSourceAssetId() != QStringLiteral("variant-source") ||
        variant_model.selectedVariantName() != QStringLiteral("Black and White") ||
        variant_model.selectedFilePath() != base_asset.file_path) {
      return fail("virtual copies should keep the source file and expose variant metadata");
    }
    const auto variant_ids = variant_model.recipeVariantAssetIds(QStringLiteral("variant-source"));
    if (variant_ids.size() != 2 || variant_ids.at(0) != QStringLiteral("variant-source") ||
        variant_ids.at(1) != virtual_copy_id) {
      return fail("recipe variants should be discoverable from the source asset");
    }

    photo_editor::desktop::EditRecipeState variant_recipe;
    variant_recipe.exposure = -0.75;
    if (variant_model.applyAdjustmentPatchJsonToAssetIds(
            {virtual_copy_id},
            photo_editor::desktop::
                serialize_adjustment_patch_json(
                    variant_recipe,
                    {QStringLiteral("tone")},
                    photo_editor::desktop::
                        AdjustmentPatchScope::
                            AssetBound),
            {QStringLiteral("tone")}) != 1) {
      return fail("recipe edits should apply independently to virtual copies");
    }
    const auto base_recipe =
        adjustment_state_from_patch(
            variant_model
                .data(
                    variant_model.index(0, 0),
                    photo_editor::desktop::
                        CatalogAssetModel::
                            AdjustmentPatchJsonRole)
                .toString());
    const auto copy_recipe =
        adjustment_state_from_patch(
            variant_model
                .data(
                    variant_model.index(1, 0),
                    photo_editor::desktop::
                        CatalogAssetModel::
                            AdjustmentPatchJsonRole)
                .toString());
    if (std::abs(base_recipe.exposure) > 1e-6 || std::abs(copy_recipe.exposure + 0.75) > 1e-6) {
      return fail("virtual copy recipes should not overwrite the source asset recipe");
    }

    photo_editor::desktop::CatalogAssetModel reloaded_variant_model;
    reloaded_variant_model.replaceAssetsForTesting({base_asset});
    const auto reloaded_variant_ids =
        reloaded_variant_model.recipeVariantAssetIds(QStringLiteral("variant-source"));
    if (reloaded_variant_model.assetCount() != 2 ||
        reloaded_variant_ids.size() != 2 ||
        reloaded_variant_ids.at(1) != virtual_copy_id) {
      return fail("virtual copies should persist across catalog model reloads");
    }
    reloaded_variant_model.selectIndex(1);
    const auto reloaded_copy_recipe =
        adjustment_state_from_patch(
            reloaded_variant_model
                .selectedAdjustmentPatchJson());
    if (!reloaded_variant_model.selectedVirtualCopy() ||
        reloaded_variant_model.selectedVariantName() != QStringLiteral("Black and White") ||
        std::abs(reloaded_copy_recipe.exposure + 0.75) > 1e-6) {
      return fail("persisted virtual copies should restore metadata and independent recipes");
    }

    const auto crop_variant_id = reloaded_variant_model.createCropVariantForAsset(
        QStringLiteral("variant-source"),
        QStringLiteral("Square Crop"),
        QStringLiteral("1:1"));
    if (crop_variant_id.isEmpty()) {
      return fail("crop variants should be creatable as virtual copies");
    }
    const auto crop_variant_ids =
        reloaded_variant_model.recipeVariantAssetIds(QStringLiteral("variant-source"));
    if (crop_variant_ids.size() != 3 || crop_variant_ids.at(2) != crop_variant_id) {
      return fail("crop variants should be listed with the source asset recipe variants");
    }
    auto crop_variant_row = -1;
    for (int row = 0; row < reloaded_variant_model.assetCount(); ++row) {
      if (reloaded_variant_model.assetAt(row).value(QStringLiteral("assetId")).toString() == crop_variant_id) {
        crop_variant_row = row;
        break;
      }
    }
    if (crop_variant_row < 0) {
      return fail("crop variant virtual copy should appear in the visible catalog");
    }
    const auto crop_variant_map = reloaded_variant_model.assetAt(crop_variant_row);
    const auto crop_variant_recipe =
        adjustment_state_for_asset(
            crop_variant_map);
    const auto source_variant_recipe =
        adjustment_state_for_asset(
            reloaded_variant_model.assetAt(0));
    if (!crop_variant_map.value(QStringLiteral("virtualCopy")).toBool() ||
        crop_variant_map.value(QStringLiteral("sourceAssetId")).toString() != QStringLiteral("variant-source") ||
        crop_variant_recipe.crop_preset != QStringLiteral("1:1") ||
        source_variant_recipe.crop_preset == QStringLiteral("1:1")) {
      return fail("crop variants should store crop changes on the virtual copy only");
    }
  }

  const auto relinked_file_path = runtime_root.filePath(QStringLiteral("relinked/day-01/IMG_0100.jpg"));
  const auto online_file_path = runtime_root.filePath(QStringLiteral("library/day-01/IMG_0200.jpg"));
  if (!write_file(relinked_file_path) || !write_file(online_file_path)) {
    return fail("test source files should be created");
  }

  auto offline_asset = make_asset(QStringLiteral("asset-offline"), QStringLiteral("IMG_0100.jpg"));
  offline_asset.file_path = runtime_root.filePath(QStringLiteral("missing/day-01/IMG_0100.jpg"));
  offline_asset.location_path = offline_asset.file_path;
  offline_asset.online = false;

  auto online_asset = make_asset(QStringLiteral("asset-online"), QStringLiteral("IMG_0200.jpg"));
  online_asset.file_path = online_file_path;
  online_asset.location_path = online_file_path;
  online_asset.online = true;

  {
    photo_editor::desktop::CatalogAssetModel relink_model;
    relink_model.replaceAssetsForTesting({offline_asset, online_asset});
    relink_model.selectIndex(0);

    if (relink_model.visibleOfflineCount() != 1 || relink_model.visibleOnlineCount() != 1) {
      return fail("visible availability stats should track offline and online assets");
    }

    relink_model.setAvailabilityFilter(QStringLiteral("offline"));
    if (relink_model.assetCount() != 1 || relink_model.selectedAssetId() != QStringLiteral("asset-offline")) {
      return fail("offline availability filter should isolate unavailable assets");
    }
    if (relink_model.selectedOnline()) {
      return fail("offline filter selection should remain offline before relinking");
    }

    const auto relinked_count =
        relink_model.relinkSelectedAssetToFolder(QUrl::fromLocalFile(runtime_root.filePath(QStringLiteral("relinked"))).toString());
    if (relinked_count != 1) {
      return fail("relinking should find a matching file in the chosen folder");
    }
    if (!relink_model.selectedOnline()) {
      return fail("relinked asset should become online immediately");
    }
    if (QDir::cleanPath(relink_model.selectedFilePath()) != QDir::cleanPath(relinked_file_path)) {
      return fail("relinking should update the selected file path");
    }

    relink_model.setAvailabilityFilter(QStringLiteral("all"));
    if (relink_model.visibleOfflineCount() != 0 || relink_model.visibleOnlineCount() != 2) {
      return fail("availability stats should refresh after relinking");
    }
  }

  {
    photo_editor::desktop::CatalogAssetModel reloaded_relink_model;
    reloaded_relink_model.replaceAssetsForTesting({offline_asset, online_asset});
    reloaded_relink_model.selectIndex(0);

    if (QDir::cleanPath(reloaded_relink_model.selectedFilePath()) != QDir::cleanPath(relinked_file_path)) {
      return fail("persisted relink paths should override the original offline source path");
    }
    if (!reloaded_relink_model.selectedOnline()) {
      return fail("persisted relink paths should restore online state when the replacement file exists");
    }

    if (!QFile::remove(relinked_file_path)) {
      return fail("relinked source file should be removable for refresh testing");
    }

    reloaded_relink_model.refreshOnlineState();
    if (reloaded_relink_model.selectedOnline()) {
      return fail("refreshing availability should mark removed relink targets offline");
    }

    reloaded_relink_model.setAvailabilityFilter(QStringLiteral("offline"));
    if (reloaded_relink_model.assetCount() != 1 ||
        reloaded_relink_model.selectedAssetId() != QStringLiteral("asset-offline")) {
      return fail("offline filter should continue to work after a relink target disappears");
    }
  }

  {
    const auto recovery_root = runtime_root.filePath(QStringLiteral("offline-recovery"));
    const auto recovered_first_path =
        runtime_root.filePath(QStringLiteral("offline-recovery/moved/card-a/RECOVER_0101.jpg"));
    const auto recovered_second_path =
        runtime_root.filePath(QStringLiteral("offline-recovery/moved/card-b/RECOVER_0102.jpg"));
    const auto cached_preview_path =
        runtime_root.filePath(QStringLiteral("offline-recovery/cache/RECOVER_0101-preview.jpg"));
    if (!write_file(recovered_first_path) || !write_file(recovered_second_path) ||
        !write_file(cached_preview_path)) {
      return fail("offline recovery fixtures should be created");
    }

    auto moved_first = make_asset(QStringLiteral("recovery-asset-1"), QStringLiteral("RECOVER_0101.jpg"));
    moved_first.file_path = runtime_root.filePath(QStringLiteral("offline-recovery/original/card-a/RECOVER_0101.jpg"));
    moved_first.location_path = moved_first.file_path;
    moved_first.thumbnail_source = QUrl::fromLocalFile(cached_preview_path).toString();
    moved_first.adjustment_patch_json =
        photo_editor::desktop::
            canonical_asset_bound_adjustment_patch_json(
                QStringLiteral(
                    "{\"exposure\":0.625,\"contrast\":0.15}"));
    moved_first.import_mode = QStringLiteral("referenced");
    moved_first.rating = 4;
    moved_first.online = false;

    auto moved_second = make_asset(QStringLiteral("recovery-asset-2"), QStringLiteral("RECOVER_0102.jpg"));
    moved_second.file_path = runtime_root.filePath(QStringLiteral("offline-recovery/original/card-b/RECOVER_0102.jpg"));
    moved_second.location_path = moved_second.file_path;
    moved_second.adjustment_patch_json =
        photo_editor::desktop::
            canonical_asset_bound_adjustment_patch_json(
                QStringLiteral(
                    "{\"exposure\":-0.375}"));
    moved_second.import_mode = QStringLiteral("referenced");
    moved_second.rating = 5;
    moved_second.online = false;

    photo_editor::desktop::PersistentJobRecord blocked_export;
    blocked_export.job_id = QStringLiteral("offline-recovery-export-proof");
    blocked_export.job_kind = QStringLiteral("asset_export");
    blocked_export.asset_id = moved_first.asset_id;
    blocked_export.display_label = QStringLiteral("RECOVER_0101.jpg");
    blocked_export.source_path = moved_first.file_path;
    blocked_export.recipe_json =
        moved_first.adjustment_patch_json;
    blocked_export.destination_path = runtime_root.filePath(QStringLiteral("offline-recovery/exports"));
    blocked_export.status = QStringLiteral("Failed");
    blocked_export.error_message = QStringLiteral("The export source is no longer available.");
    blocked_export.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    blocked_export.updated_at = blocked_export.created_at;
    if (!photo_editor::desktop::upsertPersistedJobRecord(blocked_export, nullptr)) {
      return fail("blocked export proof job should be persisted");
    }

    photo_editor::desktop::CatalogAssetModel recovery_model;
    recovery_model.replaceAssetsForTesting({moved_first, moved_second});
    recovery_model.selectIndex(0);
    recovery_model.setSelectedRating(4);
    const auto moved_first_adjustments =
        moved_first.adjustment_patch_json;
    if (recovery_model.selectedAvailabilityState() != QStringLiteral("moved-file") ||
        recovery_model.selectedThumbnailSource() != moved_first.thumbnail_source ||
        recovery_model.selectedAdjustmentPatchJson() != moved_first_adjustments ||
        recovery_model.selectedRating() != 4) {
      std::cerr << "offline_recovery_initial state="
                << recovery_model.selectedAvailabilityState().toStdString()
                << " thumbnail=" << recovery_model.selectedThumbnailSource().toStdString()
                << " expected_thumbnail=" << moved_first.thumbnail_source.toStdString()
                << " adjustments="
                << recovery_model
                       .selectedAdjustmentPatchJson()
                       .toStdString()
                << " rating=" << recovery_model.selectedRating() << '\n';
      return fail("offline moved originals should preserve cached previews, metadata, ratings, and edit recipes");
    }
    if (recovery_model.markSelectedAssetPermanentlyMissing(false) ||
        recovery_model.selectedAvailabilityState() != QStringLiteral("moved-file")) {
      return fail("permanently missing state should require explicit confirmation");
    }
    if (!recovery_model.markSelectedAssetPermanentlyMissing(true) ||
        recovery_model.selectedAvailabilityState() != QStringLiteral("permanently-missing") ||
        recovery_model.selectedAdjustmentPatchJson() != moved_first_adjustments) {
      return fail("confirmed permanently missing state should retain the catalog identity and edit recipe");
    }

    const auto bulk_relinked = recovery_model.relinkOfflineAssetsToFolder(
        QUrl::fromLocalFile(recovery_root + QStringLiteral("/moved")).toString());
    if (bulk_relinked != 2 || recovery_model.visibleOfflineCount() != 0 ||
        recovery_model.visibleOnlineCount() != 2) {
      return fail("bulk relink should restore every matching offline original in a nested folder tree");
    }
    if (recovery_model.selectedAssetId() != moved_first.asset_id ||
        QDir::cleanPath(recovery_model.selectedFilePath()) != QDir::cleanPath(recovered_first_path) ||
        recovery_model.selectedAdjustmentPatchJson() != moved_first_adjustments ||
        recovery_model.selectedRating() != 4) {
      return fail("bulk relink should preserve selected asset identity, edits, and metadata");
    }

    auto resumed_export_found = false;
    for (const auto& record : photo_editor::desktop::loadPersistedJobRecords()) {
      if (record.job_id != blocked_export.job_id) {
        continue;
      }
      if (record.status != QStringLiteral("Queued") ||
          QDir::cleanPath(record.source_path) != QDir::cleanPath(recovered_first_path) ||
          QJsonDocument::fromJson(record.recipe_json.toUtf8()).object() !=
              QJsonDocument::fromJson(
                  moved_first
                      .adjustment_patch_json
                      .toUtf8())
                  .object() ||
          QDir::cleanPath(record.destination_path) != QDir::cleanPath(blocked_export.destination_path) ||
          !record.metadata_json.value(QStringLiteral("resumedAfterRelink")).toBool()) {
        std::cerr << "offline_recovery_export status=" << record.status.toStdString()
                  << " source=" << record.source_path.toStdString()
                  << " expected_source=" << recovered_first_path.toStdString()
                  << " recipe=" << record.recipe_json.toStdString()
                  << " destination=" << record.destination_path.toStdString()
                  << " resumed="
                  << record.metadata_json.value(QStringLiteral("resumedAfterRelink")).toBool()
                  << '\n';
        return fail("relink should requeue blocked exports with the recovered source while preserving recipe and destination");
      }
      resumed_export_found = true;
      break;
    }
    if (!resumed_export_found) {
      return fail("relink should resume an export blocked by a missing source");
    }

    photo_editor::desktop::CatalogAssetModel relaunched_recovery_model;
    relaunched_recovery_model.replaceAssetsForTesting({moved_first, moved_second});
    relaunched_recovery_model.selectIndex(0);
    if (relaunched_recovery_model.selectedAssetId() != moved_first.asset_id ||
        QDir::cleanPath(relaunched_recovery_model.selectedFilePath()) != QDir::cleanPath(recovered_first_path) ||
        relaunched_recovery_model.selectedAdjustmentPatchJson() != moved_first_adjustments ||
        relaunched_recovery_model.selectedRating() != 4 ||
        !relaunched_recovery_model.selectedOnline()) {
      return fail("relaunch should restore relinked paths without changing identities, edits, ratings, or availability");
    }

    if (!photo_editor::desktop::removePersistedJobRecord(blocked_export.job_id, nullptr)) {
      return fail("offline recovery proof job should be removable");
    }

    auto drive_offline = make_asset(QStringLiteral("recovery-drive-offline"), QStringLiteral("DRIVE_0001.jpg"));
    drive_offline.file_path = QStringLiteral("/Volumes/VIREL_DISCONNECTED_RECOVERY_CARD/DCIM/DRIVE_0001.jpg");
    drive_offline.location_path = drive_offline.file_path;
    drive_offline.online = false;
    photo_editor::desktop::CatalogAssetModel state_model;
    state_model.replaceAssetsForTesting({drive_offline});
    state_model.selectIndex(0);
    if (state_model.selectedAvailabilityState() != QStringLiteral("drive-offline")) {
      return fail("missing removable-media roots should be classified as an offline drive");
    }

    const auto unreadable_path =
        runtime_root.filePath(QStringLiteral("offline-recovery/permissions/LOCKED_0001.jpg"));
    if (!write_file(unreadable_path) ||
        !QFile::setPermissions(unreadable_path, QFileDevice::WriteOwner)) {
      return fail("permission recovery fixture should be created");
    }
    auto permission_denied = make_asset(QStringLiteral("recovery-permission"), QStringLiteral("LOCKED_0001.jpg"));
    permission_denied.file_path = unreadable_path;
    permission_denied.location_path = unreadable_path;
    permission_denied.online = false;
    state_model.replaceAssetsForTesting({permission_denied});
    state_model.selectIndex(0);
    if (state_model.selectedAvailabilityState() != QStringLiteral("permission-denied")) {
      return fail("existing unreadable originals should be classified as a permission failure");
    }
    (void)QFile::setPermissions(unreadable_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }

  {
    const auto preflight_root = runtime_root.filePath(QStringLiteral("import-preflight-source"));
    const auto preflight_backup_root = runtime_root.filePath(QStringLiteral("import-preflight-backup"));
    const auto raw_path = runtime_root.filePath(QStringLiteral("import-preflight-source/DCIM/PAIR_0001.CR3"));
    const auto jpeg_path = runtime_root.filePath(QStringLiteral("import-preflight-source/DCIM/PAIR_0001.jpg"));
    const auto video_path = runtime_root.filePath(QStringLiteral("import-preflight-source/DCIM/clip.mov"));
    const auto unsupported_path = runtime_root.filePath(QStringLiteral("import-preflight-source/DCIM/notes.txt"));
    const auto missing_path = runtime_root.filePath(QStringLiteral("import-preflight-source/DCIM/missing.jpg"));
    if (!QDir().mkpath(preflight_backup_root) ||
        !write_file(raw_path) || !write_file(jpeg_path) || !write_file(video_path) ||
        !write_file(unsupported_path)) {
      return fail("mixed import preflight fixtures should be created");
    }
    qputenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR", preflight_backup_root.toUtf8());

    photo_editor::desktop::CatalogAssetModel preflight_model;
    photo_editor::desktop::CatalogAssetItem existing_asset;
    existing_asset.asset_id = QStringLiteral("already-imported-raw");
    existing_asset.file_path = raw_path;
    existing_asset.location_path = raw_path;
    preflight_model.replaceAssetsForTesting({existing_asset});

    const auto managed_preflight = preflight_model.importPreflight(
        preflight_root,
        {raw_path, raw_path, jpeg_path, video_path, unsupported_path, missing_path},
        QStringLiteral("managed"),
        QStringLiteral("skip"));
    if (!managed_preflight.value(QStringLiteral("ok")).toBool() ||
        managed_preflight.value(QStringLiteral("schema")).toString() != QStringLiteral("import-preflight:v1") ||
        managed_preflight.value(QStringLiteral("importMode")).toString() != QStringLiteral("managed") ||
        managed_preflight.value(QStringLiteral("candidateCount")).toInt() != 6 ||
        managed_preflight.value(QStringLiteral("importableCount")).toInt() != 2 ||
        managed_preflight.value(QStringLiteral("photoCount")).toInt() != 2 ||
        managed_preflight.value(QStringLiteral("videoCount")).toInt() != 1 ||
        managed_preflight.value(QStringLiteral("rawCount")).toInt() != 1 ||
        managed_preflight.value(QStringLiteral("jpegCount")).toInt() != 1 ||
        managed_preflight.value(QStringLiteral("rawJpegPairCount")).toInt() != 1 ||
        managed_preflight.value(QStringLiteral("duplicateCount")).toInt() != 2 ||
        managed_preflight.value(QStringLiteral("unsupportedCount")).toInt() != 1 ||
        managed_preflight.value(QStringLiteral("unreadableCount")).toInt() != 1 ||
        managed_preflight.value(QStringLiteral("estimatedDiskBytes")).toLongLong() <= 0 ||
        !managed_preflight.value(QStringLiteral("backupEnabled")).toBool() ||
        QDir::cleanPath(managed_preflight.value(QStringLiteral("backupRoot")).toString()) !=
            QDir::cleanPath(preflight_backup_root) ||
        managed_preflight.value(QStringLiteral("copyVerification")).toString() != QStringLiteral("SHA-256") ||
        !managed_preflight.value(QStringLiteral("destinationPath")).toString().contains(QStringLiteral("managed")) ||
        !managed_preflight.value(QStringLiteral("outcomeText")).toString().contains(QStringLiteral("copied")) ||
        !managed_preflight.value(QStringLiteral("organizationText")).toString().contains(QStringLiteral("no renaming"))) {
      return fail("managed import preflight should explain mixed media, RAW/JPEG pairs, problems, duplicate handling, storage, and organization before queueing");
    }

    const auto organized_raw_preflight = preflight_model.importPreflight(
        preflight_root,
        {raw_path, jpeg_path, video_path},
        QStringLiteral("managed"),
        QStringLiteral("skip"),
        QStringLiteral("date-camera"),
        QStringLiteral("raw-only"));
    if (!organized_raw_preflight.value(QStringLiteral("ok")).toBool() ||
        organized_raw_preflight.value(QStringLiteral("organizationTemplate")).toString() !=
            QStringLiteral("date-camera") ||
        organized_raw_preflight.value(QStringLiteral("rawJpegPolicy")).toString() !=
            QStringLiteral("raw-only") ||
        organized_raw_preflight.value(QStringLiteral("policySkippedCount")).toInt() != 1 ||
        organized_raw_preflight.value(QStringLiteral("importableCount")).toInt() != 1 ||
        !organized_raw_preflight.value(QStringLiteral("organizationText")).toString().contains(
            QStringLiteral("capture date"))) {
      return fail("studio preflight should truthfully preview date/camera organization and RAW-only pair handling");
    }

    const auto referenced_preflight = preflight_model.importPreflight(
        preflight_root,
        {raw_path},
        QStringLiteral("referenced"),
        QStringLiteral("replace"));
    if (!referenced_preflight.value(QStringLiteral("ok")).toBool() ||
        referenced_preflight.value(QStringLiteral("importMode")).toString() != QStringLiteral("referenced") ||
        referenced_preflight.value(QStringLiteral("duplicatePolicy")).toString() != QStringLiteral("replace") ||
        referenced_preflight.value(QStringLiteral("importableCount")).toInt() != 1 ||
        referenced_preflight.value(QStringLiteral("primaryEstimatedDiskBytes")).toLongLong() != 0 ||
        referenced_preflight.value(QStringLiteral("backupEstimatedDiskBytes")).toLongLong() <= 0 ||
        QDir::cleanPath(referenced_preflight.value(QStringLiteral("destinationPath")).toString()) !=
            QDir::cleanPath(preflight_root) ||
        !referenced_preflight.value(QStringLiteral("outcomeText")).toString().contains(QStringLiteral("links"))) {
      return fail("referenced import preflight should state that originals stay in place while still accounting for an enabled backup copy");
    }

    preflight_model.importSelectionWithOptions(
        preflight_root,
        {raw_path, raw_path, jpeg_path, video_path, unsupported_path, missing_path},
        QString{},
        QStringLiteral("managed"),
        QStringLiteral("skip"));
    auto persisted_review_found = false;
    for (const auto& record : photo_editor::desktop::loadPersistedJobRecords()) {
      if (record.job_kind != QStringLiteral("catalog_import") ||
          QDir::cleanPath(record.source_path) != QDir::cleanPath(preflight_root)) {
        continue;
      }
      const auto persisted_preflight =
          record.metadata_json.value(QStringLiteral("importPreflight")).toObject();
      if (record.metadata_json.value(QStringLiteral("importMode")).toString() != QStringLiteral("managed") ||
          record.metadata_json.value(QStringLiteral("duplicatePolicy")).toString() != QStringLiteral("skip") ||
          persisted_preflight.value(QStringLiteral("duplicateCount")).toInt() != 2 ||
          persisted_preflight.value(QStringLiteral("unsupportedCount")).toInt() != 1 ||
          persisted_preflight.value(QStringLiteral("unreadableCount")).toInt() != 1 ||
          record.metadata_json.value(QStringLiteral("selectionCount")).toInt() != 2) {
        return fail("queued imports should persist the reviewed mode, duplicate policy, problem counts, and eligible selection for accurate completion summaries and retry");
      }
      persisted_review_found = true;
      if (!photo_editor::desktop::removePersistedJobRecord(record.job_id, nullptr)) {
        return fail("preflight persistence proof job should be removable");
      }
      break;
    }
    if (!persisted_review_found) {
      return fail("reviewed import preflight should be persisted with its queued job");
    }
    qunsetenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR");
  }

  {
    photo_editor::desktop::CatalogAssetModel timeline_picker_model;
    photo_editor::desktop::CatalogAssetItem video_item;
    video_item.asset_id = QStringLiteral("timeline-picker-video");
    video_item.file_path = runtime_root.filePath(QStringLiteral("picker-video.mp4"));
    video_item.media_type = QStringLiteral("video");
    video_item.duration_text = QStringLiteral("0:12");
    video_item.duration_seconds = 12.5;
    video_item.online = true;
    timeline_picker_model.replaceAssetsForTesting({video_item});
    const auto picker_assets = timeline_picker_model.timelinePickerAssets();
    if (picker_assets.size() != 1 ||
        std::abs(picker_assets.constFirst().toMap()
                     .value(QStringLiteral("durationSeconds"))
                     .toDouble() - 12.5) > 0.001) {
      return fail("timeline picker assets should preserve exact media duration for direct insert and overwrite");
    }
  }

  {
    const auto import_root = runtime_root.filePath(QStringLiteral("import-source"));
    const auto import_backup_root = runtime_root.filePath(QStringLiteral("import-backup"));
    const auto import_file_path = runtime_root.filePath(QStringLiteral("import-source/DCIM/IMG_0300.jpg"));
    if (!QDir().mkpath(import_backup_root) || !write_file(import_file_path)) {
      return fail("import source file should be created");
    }
    qputenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR", import_backup_root.toUtf8());

    photo_editor::desktop::JobRunner import_runner;
    photo_editor::desktop::CatalogAssetModel import_model;
    import_model.importFolderPath(import_root);
    if (!wait_for_import_completion(&import_model)) {
      return fail("import should eventually finish");
    }

    const auto jobs = photo_editor::desktop::loadPersistedJobRecords();
    auto import_job = photo_editor::desktop::PersistentJobRecord{};
    auto found_import_job = false;
    for (const auto& record : jobs) {
      if (record.job_kind != QStringLiteral("catalog_import")) {
        continue;
      }
      if (QDir::cleanPath(record.source_path) != QDir::cleanPath(import_root)) {
        continue;
      }
      import_job = record;
      found_import_job = true;
      break;
    }

    if (!found_import_job) {
      return fail("import attempts should be recorded in the shared job store");
    }
    if (import_job.status == QStringLiteral("Running") || import_job.status == QStringLiteral("Queued")) {
      return fail("persisted import jobs should be finalized by the shared job runner");
    }
    if (import_job.metadata_json.value(QStringLiteral("selectionCount")).toInt() != 0) {
      return fail("folder imports should persist a zero selection count");
    }
    if (import_job.metadata_json.value(QStringLiteral("importMode")).toString() != QStringLiteral("managed")) {
      return fail("import jobs should persist the managed import mode");
    }
    const auto import_result = import_job.metadata_json.value(QStringLiteral("importResult")).toObject();
    const auto completed_summary_valid =
        import_job.status == QStringLiteral("Completed") &&
        import_result.value(QStringLiteral("schema")).toString() == QStringLiteral("import-result:v1") &&
        import_result.value(QStringLiteral("importedCount")).toInt() == 1 &&
        import_result.value(QStringLiteral("backedUpCount")).toInt() == 1 &&
        import_result.value(QStringLiteral("verificationAlgorithm")).toString() == QStringLiteral("SHA-256") &&
        import_result.value(QStringLiteral("failureCount")).toInt() == 0 &&
        import_result.value(QStringLiteral("skippedCount")).toInt() == 0;
    if ((import_job.status == QStringLiteral("Completed") && !completed_summary_valid) ||
        (qEnvironmentVariableIsSet("PHOTO_EDITOR_DATABASE_URL") && !completed_summary_valid)) {
      std::cerr << "import status=" << import_job.status.toStdString()
                << " error=" << import_job.error_message.toStdString()
                << " metadata="
                << QJsonDocument(import_job.metadata_json).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return fail("completed imports should persist a structured success, skip, duplicate, and failure summary");
    }
    if (import_job.status != QStringLiteral("Completed") && import_job.error_message.trimmed().isEmpty()) {
      return fail("an unavailable local import service should produce an inspectable failed job");
    }
    const auto import_backup_file = QDir(import_backup_root)
                                        .filePath(QStringLiteral("drive-%1/DCIM/IMG_0300.jpg").arg(
                                            QString::fromLatin1(
                                                QCryptographicHash::hash(import_root.toUtf8(), QCryptographicHash::Sha1)
                                                    .toHex()
                                                    .left(12))));
    if (import_job.status == QStringLiteral("Completed") && !QFileInfo::exists(import_backup_file)) {
      return fail("a completed import with originals backup enabled should publish the verified second copy");
    }
    qunsetenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
