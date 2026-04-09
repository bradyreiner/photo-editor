#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "job_runner.h"
#include "job_store.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
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

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CatalogAssetModelTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());

  {
    photo_editor::desktop::CatalogAssetModel model;
    model.replaceAssetsForTesting({
        make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg")),
        make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg")),
        make_asset(QStringLiteral("asset-3"), QStringLiteral("IMG_0003.jpg")),
    });
    model.selectIndex(0);

    if (model.selectedAssetCount() != 0) {
      return fail("opening a photo should not auto-select it for batch actions");
    }
    if (model.selectedRating() != 0 || model.selectedPicked() || model.selectedRejected()) {
      return fail("new selections should start with default culling state");
    }

    model.setSelectedRating(4);
    model.toggleSelectedPick();
    model.toggleSelectedReject();

    if (model.selectedRating() != 4) {
      return fail("selected rating should update");
    }
    if (model.selectedPicked()) {
      return fail("rejecting a selection should clear picked state");
    }
    if (!model.selectedRejected()) {
      return fail("selected asset should be marked rejected");
    }
    if (model.data(model.index(0, 0), photo_editor::desktop::CatalogAssetModel::RatingRole).toInt() != 4) {
      return fail("model role should expose the updated rating");
    }
    if (!model.data(model.index(0, 0), photo_editor::desktop::CatalogAssetModel::RejectedRole).toBool()) {
      return fail("model role should expose the rejected state");
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

    model.toggleSelectionIndex(0);
    model.toggleSelectionIndex(1);
    if (model.selectedAssetCount() != 2 || !model.assetSelected(QStringLiteral("asset-1")) ||
        !model.assetSelected(QStringLiteral("asset-2"))) {
      return fail("toggle selection should build a multi-photo batch selection");
    }

    photo_editor::desktop::EditRecipeState copied_state;
    copied_state.exposure = 1.15;
    copied_state.temperature = 18.0;
    copied_state.crop_preset = QStringLiteral("4x5");
    const auto copied_recipe = photo_editor::desktop::serialize_edit_recipe_json(copied_state);
    if (model.applyRecipeJsonToAssetIds(model.selectedAssetIds(), copied_recipe, {QStringLiteral("tone")}) != 2) {
      return fail("batch recipe apply should update every selected photo");
    }

    const auto asset_1_recipe = photo_editor::desktop::parse_edit_recipe_json(
        model.data(model.index(0, 0), photo_editor::desktop::CatalogAssetModel::RecipeJsonRole).toString());
    const auto asset_2_recipe = photo_editor::desktop::parse_edit_recipe_json(
        model.data(model.index(1, 0), photo_editor::desktop::CatalogAssetModel::RecipeJsonRole).toString());
    if (std::abs(asset_1_recipe.exposure - 1.15) > 1e-6 || std::abs(asset_2_recipe.exposure - 1.15) > 1e-6) {
      return fail("tone-only sync should copy tone settings into all selected recipe states");
    }
    if (std::abs(asset_1_recipe.temperature) > 1e-6 || std::abs(asset_2_recipe.temperature) > 1e-6) {
      return fail("tone-only sync should leave color settings untouched");
    }

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

    const auto persisted_recipe = photo_editor::desktop::parse_edit_recipe_json(reloaded_model.selectedRecipeJson());
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

    if (cleared_model.selectedRating() != 0 || cleared_model.selectedPicked() || cleared_model.selectedRejected()) {
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

    const auto initial_thumbnail =
        thumbnail_model.data(
            thumbnail_model.index(0, 0),
            photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
    const QImage initial_image(QUrl(initial_thumbnail).toLocalFile());
    if (initial_image.isNull()) {
      return fail("initial thumbnail image should be readable from disk");
    }

    photo_editor::desktop::EditRecipeState edited_state;
    edited_state.exposure = 1.0;
    const auto edited_recipe = photo_editor::desktop::serialize_edit_recipe_json(edited_state);
    if (thumbnail_model.applyRecipeJsonToAssetIds(
            {QStringLiteral("asset-recipe-thumb")},
            edited_recipe,
            {QStringLiteral("tone")}) != 1) {
      return fail("recipe apply should update the thumbnail test asset");
    }

    const auto edited_thumbnail = wait_for_thumbnail_change(&thumbnail_model, 0, initial_thumbnail);
    if (edited_thumbnail.isEmpty()) {
      return fail("thumbnail should be regenerated after applying recipe edits");
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
    const auto import_root = runtime_root.filePath(QStringLiteral("import-source"));
    const auto import_file_path = runtime_root.filePath(QStringLiteral("import-source/DCIM/IMG_0300.jpg"));
    if (!write_file(import_file_path)) {
      return fail("import source file should be created");
    }

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
    if (import_job.metadata_json.value(QStringLiteral("importMode")).toString() != QStringLiteral("referenced")) {
      return fail("import jobs should persist the referenced import mode");
    }
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
