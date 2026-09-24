#include <cstdlib>
#include <functional>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "export_job_controller.h"
#include "job_runner.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for(const std::function<bool()>& condition, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (condition()) {
      return true;
    }
    QThread::msleep(5);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  return condition();
}

QString asset_id_for(int index) {
  return QStringLiteral("representative-%1").arg(index + 1, 3, 10, QLatin1Char('0'));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("UxRepresentativeWorkflowTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("representative workflow should create its isolated runtime root");
  }
  const auto source_dir = runtime_root.filePath(QStringLiteral("mixed-shoot"));
  const auto export_dir = runtime_root.filePath(QStringLiteral("delivery"));
  const auto app_data_dir = runtime_root.filePath(QStringLiteral("app-data"));
  const auto cache_dir = runtime_root.filePath(QStringLiteral("cache"));
  if (!QDir().mkpath(source_dir) || !QDir().mkpath(export_dir) ||
      !QDir().mkpath(app_data_dir) || !QDir().mkpath(cache_dir)) {
    return fail("representative workflow should create source, delivery, and runtime folders");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data_dir.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_dir.toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArray("1"));
  qputenv(
      "PHOTO_EDITOR_AI_MODEL_DIR",
      runtime_root.filePath(QStringLiteral("intentionally-missing-models")).toUtf8());
  qputenv(
      "PHOTO_EDITOR_OLLAMA_BINARY",
      runtime_root.filePath(QStringLiteral("intentionally-missing-ollama")).toUtf8());
  qputenv("PHOTO_EDITOR_ENABLE_CLOUD_MODELS", QByteArray("0"));
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");

  QImage seed_image(64, 48, QImage::Format_RGB32);
  for (int y = 0; y < seed_image.height(); ++y) {
    for (int x = 0; x < seed_image.width(); ++x) {
      seed_image.setPixelColor(
          x,
          y,
          QColor(
              24 + (x * 170 / seed_image.width()),
              42 + (y * 150 / seed_image.height()),
              196 - (x * 90 / seed_image.width())));
    }
  }
  const auto jpeg_seed = QDir(source_dir).filePath(QStringLiteral("jpeg-seed.jpg"));
  const auto raw_seed = QDir(source_dir).filePath(QStringLiteral("raw-seed.png"));
  if (!seed_image.save(jpeg_seed, "JPG", 94) || !seed_image.save(raw_seed, "PNG")) {
    return fail("representative workflow should create decodable source fixtures");
  }

  constexpr int kAssetCount = 500;
  constexpr int kKeeperCount = 50;
  constexpr int kBatchCount = 20;
  constexpr int kRawCount = 225;
  QVector<photo_editor::desktop::CatalogAssetItem> assets;
  assets.reserve(kAssetCount);
  QStringList jpeg_asset_ids;
  jpeg_asset_ids.reserve(kAssetCount - kRawCount);
  for (int index = 0; index < kAssetCount; ++index) {
    const bool raw_asset = index >= kKeeperCount && index < kKeeperCount + kRawCount;
    const auto extension = raw_asset ? QStringLiteral("dng") : QStringLiteral("jpg");
    const auto file_name = QStringLiteral("SHOOT_%1.%2")
                               .arg(index + 1, 3, 10, QLatin1Char('0'))
                               .arg(extension);
    const auto file_path = QDir(source_dir).filePath(file_name);
    if (!QFile::copy(raw_asset ? raw_seed : jpeg_seed, file_path)) {
      return fail("representative workflow should create 500 mixed source files");
    }
    photo_editor::desktop::CatalogAssetItem asset;
    asset.asset_id = asset_id_for(index);
    asset.file_path = file_path;
    asset.location_path = file_path;
    asset.thumbnail_source = QUrl::fromLocalFile(file_path).toString();
    asset.adjustment_patch_json.clear();
    asset.media_type = QStringLiteral("photo");
    asset.camera_label = raw_asset ? QStringLiteral("Representative RAW")
                                   : QStringLiteral("Representative JPEG");
    asset.capture_date = QStringLiteral("2026-07-15");
    asset.import_mode = QStringLiteral("referenced");
    asset.online = true;
    asset.duplicate_group_key = QStringLiteral("burst-%1").arg(index / 25);
    asset.duplicate_group_size = 25;
    asset.duplicate_group_position = (index % 25) + 1;
    assets.push_back(asset);
    if (!raw_asset) {
      jpeg_asset_ids.push_back(asset.asset_id);
    }
  }
  if (assets.size() != kAssetCount || jpeg_asset_ids.size() != kAssetCount - kRawCount) {
    return fail("representative workflow should contain exactly 500 mixed RAW/JPEG catalog assets");
  }

  photo_editor::desktop::CatalogAssetModel catalog;
  catalog.replaceAssetsForTesting(assets);
  if (catalog.assetCount() != kAssetCount || catalog.totalAssetCount() != kAssetCount) {
    return fail("representative workflow should load all 500 assets into one catalog state");
  }
  for (int index = 0; index < kKeeperCount; ++index) {
    catalog.selectIndex(index);
    catalog.toggleSelectedPick();
    catalog.setSelectedRating(index % 5 + 1);
  }
  if (catalog.visiblePickedCount() != kKeeperCount || catalog.visibleRatedCount() != kKeeperCount) {
    return fail("representative workflow should cull exactly 50 rated keepers");
  }

  const auto first_asset = catalog.assetAt(0);
  const auto first_adjustments =
      photo_editor::desktop::
          parse_adjustment_patch_json(
              first_asset
                  .value(
                      QStringLiteral(
                          "adjustmentPatchJson"))
                  .toString());
  photo_editor::desktop::EditorPreviewController editor;
  editor.loadAsset(
      first_asset.value(QStringLiteral("assetId")).toString(),
      first_asset.value(QStringLiteral("filePath")).toString(),
      first_asset.value(QStringLiteral("thumbnailSource")).toString(),
      first_asset.value(QStringLiteral("cameraLabel")).toString(),
      first_asset.value(QStringLiteral("captureDate")).toString(),
      first_asset.value(QStringLiteral("lensModel")).toString(),
      first_adjustments.ok()
          ? photo_editor::desktop::
                serialize_edit_recipe_json(
                    *first_adjustments.state)
          : QString{});
  if (!wait_for([&editor]() { return editor.hasAsset(); }, 3000)) {
    return fail("representative workflow should open the first keeper for editing");
  }
  editor.setExposure(0.45);
  editor.setVibrance(18.0);
  const auto batch_patch =
      editor.currentAdjustmentPatchJson();
  QStringList batch_asset_ids;
  for (int index = 0; index < kBatchCount; ++index) {
    batch_asset_ids.push_back(asset_id_for(index));
  }
  const auto batch_applied =
      catalog.applyAdjustmentPatchJsonToAssetIds(
      batch_asset_ids,
      batch_patch,
      {QStringLiteral("light"), QStringLiteral("color")});
  if (batch_applied != kBatchCount || !catalog.canUndoBatchRecipe()) {
    return fail("representative workflow should selectively batch-edit 20 keepers with one undo step");
  }

  editor.addMask(QStringLiteral("brush"));
  editor.setActiveMaskExposure(0.35);
  editor.setActiveMaskFeather(0.28);
  if (!editor.hasActiveMask() || editor.maskCount() != 1 || !editor.canUndo()) {
    return fail("representative workflow should create one undoable masked local refinement");
  }
  const auto local_recipe =
      editor.currentAdjustmentPatchJson();
  const auto local_patch =
      editor.currentAdjustmentPatchJson();
  if (catalog.applyAdjustmentPatchJsonToAssetIds(
          {asset_id_for(0)},
          local_patch,
          {QStringLiteral("masks")}) != 1) {
    return fail("representative workflow should persist the local refinement on its keeper");
  }
  editor.undo();
  editor.redo();
  if (editor.currentAdjustmentPatchJson() != local_recipe) {
    return fail("representative workflow should restore the exact local edit through undo and redo");
  }

  QVariantList delivery_assets;
  delivery_assets.reserve(kBatchCount);
  for (int index = 0; index < kBatchCount; ++index) {
    const auto asset = catalog.assetAt(index);
    delivery_assets.push_back(QVariantMap{
        {QStringLiteral("assetId"), asset.value(QStringLiteral("assetId"))},
        {QStringLiteral("mediaType"), QStringLiteral("photo")},
        {QStringLiteral("filePath"), asset.value(QStringLiteral("filePath"))},
        {QStringLiteral("adjustmentPatchJson"),
         asset.value(
             QStringLiteral(
                 "adjustmentPatchJson"))},
        {QStringLiteral("displayLabel"),
         QFileInfo(asset.value(QStringLiteral("filePath")).toString()).completeBaseName()},
    });
  }

  photo_editor::desktop::JobRunner runner;
  photo_editor::desktop::ExportJobController exports;
  if (exports.queueBatchExport(
          delivery_assets, QStringLiteral("Web Proof"), export_dir) != kBatchCount) {
    return fail("representative workflow should queue all 20 keeper exports after preflight");
  }
  runner.initializeFromStore();
  if (!wait_for(
          [&exports]() {
            if (exports.jobs().size() != kBatchCount) {
              return false;
            }
            for (const auto& value : exports.jobs()) {
              if (value.toMap().value(QStringLiteral("status")).toString() !=
                  QStringLiteral("Completed")) {
                return false;
              }
            }
            return true;
          },
          30000)) {
    for (const auto& value : exports.jobs()) {
      const auto job = value.toMap();
      std::cerr
          << job.value(QStringLiteral("jobId")).toString().toStdString()
          << " status="
          << job.value(QStringLiteral("status")).toString().toStdString()
          << " progress="
          << job.value(QStringLiteral("progress")).toInt()
          << " error="
          << job.value(QStringLiteral("errorMessage")).toString().toStdString()
          << '\n';
    }
    return fail("representative workflow should complete all 20 keeper exports");
  }

  const auto delivered_files =
      QDir(export_dir).entryList({QStringLiteral("*.jpg")}, QDir::Files, QDir::Name);
  if (delivered_files.size() != kBatchCount) {
    return fail("representative workflow should deliver exactly 20 decoded JPEG artifacts");
  }
  QJsonArray output_proof;
  for (const auto& value : exports.jobs()) {
    const auto job = value.toMap();
    const auto output_path = job.value(QStringLiteral("outputPath")).toString();
    const auto metadata = job.value(QStringLiteral("metadata")).toMap();
    const auto summary = metadata.value(QStringLiteral("deliverySummary")).toMap();
    QImage delivered(output_path);
    if (delivered.isNull() || !summary.value(QStringLiteral("artifactVerified")).toBool()) {
      return fail("representative workflow should decode and verify every delivered artifact");
    }
    output_proof.append(QJsonObject{
        {QStringLiteral("assetId"), job.value(QStringLiteral("assetId")).toString()},
        {QStringLiteral("path"), output_path},
        {QStringLiteral("width"), delivered.width()},
        {QStringLiteral("height"), delivered.height()},
        {QStringLiteral("bytes"), QFileInfo(output_path).size()},
        {QStringLiteral("artifactVerified"), true},
    });
  }

  const auto proof_root = QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"));
  if (!proof_root.trimmed().isEmpty()) {
    const auto persistent_delivery =
        QDir(proof_root).filePath(QStringLiteral("representative-workflow-delivery"));
    if (!QDir().mkpath(persistent_delivery)) {
      return fail("representative workflow should create its persistent proof directory");
    }
    QJsonArray persistent_outputs;
    for (const auto& file_name : delivered_files) {
      const auto source = QDir(export_dir).filePath(file_name);
      const auto target = QDir(persistent_delivery).filePath(file_name);
      if ((QFile::exists(target) && !QFile::remove(target)) || !QFile::copy(source, target)) {
        return fail("representative workflow should preserve all 20 delivery artifacts");
      }
      persistent_outputs.append(QJsonObject{
          {QStringLiteral("path"), target},
          {QStringLiteral("bytes"), QFileInfo(target).size()},
      });
    }
    const QJsonObject manifest{
        {QStringLiteral("schema"), QStringLiteral("ux-representative-workflow:v1")},
        {QStringLiteral("assetCount"), kAssetCount},
        {QStringLiteral("jpegCount"), kAssetCount - kRawCount},
        {QStringLiteral("rawCatalogPathCount"), kRawCount},
        {QStringLiteral("rawFixtureKind"), QStringLiteral("dng-extension-catalog-fixture")},
        {QStringLiteral("rawDecodeExercised"), false},
        {QStringLiteral("keeperCount"), kKeeperCount},
        {QStringLiteral("batchEditCount"), batch_applied},
        {QStringLiteral("localMaskCount"), editor.maskCount()},
        {QStringLiteral("undoRedoExact"), true},
        {QStringLiteral("exportCount"), delivered_files.size()},
        {QStringLiteral("outputs"), persistent_outputs},
        {QStringLiteral("runtimeOutputVerification"), output_proof},
    };
    QFile manifest_file(
        QDir(proof_root).filePath(QStringLiteral("representative-workflow-manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("representative workflow should write its proof manifest");
    }
  }

  std::cout << "ux_representative_workflow asset_count=" << kAssetCount
            << " keeper_count=" << kKeeperCount
            << " batch_edit_count=" << batch_applied
            << " local_mask_count=" << editor.maskCount()
            << " export_count=" << delivered_files.size() << '\n';
  return EXIT_SUCCESS;
}
