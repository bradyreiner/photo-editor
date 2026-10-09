#include "agent_photo_output_task.h"
#include "agent_task_recipe_persistence.h"
#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "photo_layer_source.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>

namespace {

using namespace photo_editor::desktop;

int fail(const QString& message, const QString& retained_root = {}) {
  std::cerr << message.toStdString();
  if (!retained_root.isEmpty())
    std::cerr << "\nRetained private photo-output fixture: " << retained_root.toStdString();
  std::cerr << '\n';
  return EXIT_FAILURE;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 180000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!predicate() && elapsed.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QString sha256(const QByteArray& bytes) {
  if (bytes.isEmpty()) return {};
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash digest(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto block = file.read(1024 * 1024);
    if (block.isEmpty() && file.error() != QFileDevice::NoError) return {};
    digest.addData(block);
  }
  return QString::fromLatin1(digest.result().toHex());
}

QVariantMap failure(const QString& message) {
  return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
      file.write(bytes) == bytes.size() && file.flush();
}

QVariantMap layer_for_id(const EditorPreviewController& preview, const QString& id, int* index = nullptr) {
  const auto parsed = parse_adjustment_patch_json(preview.currentAssetBoundAdjustmentPatchJson());
  if (!parsed.ok() || !parsed.state.has_value()) return {};
  for (int position = 0; position < parsed.state->layers.size(); ++position) {
    const auto layer = parsed.state->layers.at(position).toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) {
      if (index) *index = position;
      return layer;
    }
  }
  return {};
}

QVariantMap output_resource_step(const QVariantMap& report, const QString& resource_id) {
  QVariantMap result;
  for (const auto& value : report.value(QStringLiteral("steps")).toList()) {
    const auto step = value.toMap();
    if (step.value(QStringLiteral("resourceId")).toString() == resource_id &&
        step.value(QStringLiteral("command")).toString().startsWith(QStringLiteral("photo.export_")))
      result = step;
  }
  return result;
}

bool image_has_photo_detail(const QImage& image) {
  if (image.isNull() || image.width() < 2 || image.height() < 2) return false;
  QSet<QRgb> colors;
  constexpr int grid = 12;
  for (int y = 0; y < grid; ++y) {
    for (int x = 0; x < grid; ++x) {
      const auto px = qBound(0, ((2 * x + 1) * image.width()) / (2 * grid), image.width() - 1);
      const auto py = qBound(0, ((2 * y + 1) * image.height()) / (2 * grid), image.height() - 1);
      colors.insert(image.pixel(px, py));
    }
  }
  return colors.size() >= 8;
}

bool sampled_pixels_close(const QImage& actual, const QImage& reference, int maximum_mean_delta) {
  if (actual.isNull() || reference.isNull() || actual.size() != reference.size()) return false;
  const auto actual_rgba = actual.convertToFormat(QImage::Format_RGBA8888);
  const auto reference_rgba = reference.convertToFormat(QImage::Format_RGBA8888);
  if (actual_rgba.isNull() || reference_rgba.isNull()) return false;
  constexpr int grid = 17;
  qint64 total_delta = 0;
  qint64 samples = 0;
  for (int y = 0; y < grid; ++y) {
    for (int x = 0; x < grid; ++x) {
      const auto px = qBound(0, ((2 * x + 1) * actual.width()) / (2 * grid), actual.width() - 1);
      const auto py = qBound(0, ((2 * y + 1) * actual.height()) / (2 * grid), actual.height() - 1);
      const auto a = actual_rgba.pixelColor(px, py);
      const auto b = reference_rgba.pixelColor(px, py);
      total_delta += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
          std::abs(a.blue() - b.blue());
      samples += 3;
    }
  }
  return samples > 0 && total_delta / samples <= maximum_mean_delta;
}

struct DrainWorkers final {
  ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(180000); }
};

}  // namespace

int main(int argc, char** argv) {
  // This contract is opt-in. It only touches an asset under an explicitly
  // marked private fixture root and a separately named test database URL.
  if (argc != 4) return 77;
  const QDir fixture_root(QString::fromLocal8Bit(argv[1]));
  const auto asset_id = QString::fromLocal8Bit(argv[2]).trimmed();
  const QFileInfo requested_source(QString::fromLocal8Bit(argv[3]));
  const auto database_url =
      qEnvironmentVariable("PHOTO_EDITOR_AGENT_PHOTO_OUTPUT_TEST_DATABASE_URL").trimmed();
  const QUrl database_authority(database_url);
  if (!fixture_root.isAbsolute() || asset_id.isEmpty() || !requested_source.isFile() ||
      requested_source.isSymLink() ||
      !QFileInfo::exists(fixture_root.filePath(QStringLiteral("agent-photo-output.marker"))))
    return 77;
  const auto canonical_root = fixture_root.canonicalPath();
  const auto source_path = requested_source.canonicalFilePath();
  const auto relative_source = QDir(canonical_root).relativeFilePath(source_path);
  if (canonical_root.isEmpty() || source_path.isEmpty() || relative_source.isEmpty() ||
      relative_source == QStringLiteral("..") || relative_source.startsWith(QStringLiteral("../")) ||
      QDir::isAbsolutePath(relative_source))
    return 77;
  if (!database_authority.isValid() ||
      !QStringList{QStringLiteral("postgres"), QStringLiteral("postgresql")}
          .contains(database_authority.scheme(), Qt::CaseInsensitive) ||
      !database_authority.path().contains(QStringLiteral("test"), Qt::CaseInsensitive))
    return 77;

  QTemporaryDir run_root(fixture_root.filePath(QStringLiteral("photo-output-native-XXXXXX")));
  if (!run_root.isValid()) return fail(QStringLiteral("Could not create a private output-owner run root."));
  run_root.setAutoRemove(false);
  const auto retained_root = run_root.path();
  const auto runtime_root = QDir(retained_root).filePath(QStringLiteral("runtime"));
  const auto runtime_cache = QDir(retained_root).filePath(QStringLiteral("cache"));
  const auto output_root = QDir(retained_root).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(runtime_root) || !QDir().mkpath(runtime_cache) || !QDir().mkpath(output_root))
    return fail(QStringLiteral("Could not create the isolated runtime and output folders."), retained_root);

  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime_cache.toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", database_url.toUtf8());
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentPhotoOutputNativeContract"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
      QDir(retained_root).filePath(QStringLiteral("preferences")));
  DrainWorkers drain;

  QString bootstrap_error;
  if (!AppBootstrap::ensureCatalogService(&bootstrap_error))
    return fail(QStringLiteral("Could not open the explicit isolated catalog: %1").arg(bootstrap_error), retained_root);

  CatalogAssetModel catalog;
  catalog.setEditAccessPolicy([] { return true; });
  catalog.refresh();
  if (!wait_until([&catalog] { return !catalog.loading(); }) || !catalog.errorMessage().isEmpty() ||
      !catalog.selectAssetById(asset_id, true))
    return fail(QStringLiteral("The private catalog asset is unavailable: %1").arg(catalog.errorMessage()), retained_root);
  const auto catalog_asset = catalog.assetById(asset_id);
  const auto catalog_source_path = QFileInfo(catalog_asset.value(QStringLiteral("filePath")).toString()).canonicalFilePath();
  if (catalog_asset.isEmpty() || catalog_source_path != source_path)
    return fail(QStringLiteral("The marked fixture asset does not match the supplied source path."), retained_root);
  const auto source_sha_before = sha256_file(source_path);
  if (source_sha_before.size() != 64)
    return fail(QStringLiteral("Could not hash the actual private source photo."), retained_root);

  EditorPreviewController preview;
  preview.setEditAccessPolicy([] { return true; });
  preview.loadAsset(asset_id, source_path, {}, {}, {}, {}, catalog.selectedAdjustmentPatchJson());
  if (!wait_until([&preview, &asset_id] {
        return preview.hasAsset() && preview.assetId() == asset_id && !preview.processing();
      }))
    return fail(QStringLiteral("The real photo preview did not load and settle."), retained_root);
  const auto native_before = preview.currentAssetBoundAdjustmentPatchJson().toUtf8();
  if (native_before.isEmpty())
    return fail(QStringLiteral("The actual native Preview recipe has no readable baseline."), retained_root);

  const auto task_id = QStringLiteral("photo-output-native-%1")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  AgentTaskCoordinator tasks;
  if (!tasks.begin(runtime_root, task_id).value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The task journal did not start in the private runtime."), retained_root);
  const auto approved_task = tasks.journal()->recordApprovedSnapshot({
      {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
      {QStringLiteral("prompt"), QStringLiteral("Export a flattened photo and the selected source layer to the approved private output folder.")},
      {QStringLiteral("targetIds"), QVariantList{asset_id}},
      {QStringLiteral("planVersion"), 1},
  });
  if (!approved_task.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The output task did not pin its approved brief and target."), retained_root);
  const auto catalog_resource = QStringLiteral("catalog-recipe:%1").arg(asset_id);
  auto catalog_adapter = catalog.agentTaskRecipeResourceAdapter(asset_id, task_id);
  const auto catalog_before = catalog_adapter.read();
  if (catalog_before.isEmpty() ||
      !tasks.enroll(catalog_resource, std::move(catalog_adapter)).value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The exact native Catalog recipe baseline could not be enrolled."), retained_root);
  preview.setAgentTaskJournal(tasks.journal());
  if (!preview.beginAgentTask(task_id).value(QStringLiteral("ok")).toBool() ||
      !tasks.enroll(asset_id, preview.agentTaskResourceAdapter(task_id))
           .value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The actual Preview recipe did not join the same task journal."), retained_root);

  AgentTaskRecipePersistence recipe_persistence(preview, catalog, tasks);
  const auto persist_handler = [&recipe_persistence](const QString& stage, const QVariantMap& context) {
    return recipe_persistence.handle(stage, context);
  };
  preview.setAgentRecipePersistenceHandler(persist_handler);
  catalog.setAgentRecipePersistenceHandler(persist_handler);

  const auto add_source = preview.runAgentTaskComposition(task_id, QVariantMap{
      {QStringLiteral("assetId"), asset_id},
      {QStringLiteral("operation"), QStringLiteral("add_source_layer")},
      {QStringLiteral("values"), QVariantMap{}},
  });
  const auto source_layer_id = add_source.value(QStringLiteral("createdLayerId")).toString();
  if (!add_source.value(QStringLiteral("ok")).toBool() || source_layer_id.isEmpty() ||
      !add_source.value(QStringLiteral("changed")).toBool())
    return fail(QStringLiteral("The real Preview did not create its stable native source layer."), retained_root);
  const auto source_layer = layer_for_id(preview, source_layer_id);
  if (source_layer.isEmpty() || source_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("source"))
    return fail(QStringLiteral("The native source layer is not present in the Preview recipe."), retained_root);
  if (!wait_until([&] {
        return !preview.recipeSavePending() && !recipe_persistence.pending() &&
            catalog.agentTaskRecipePersistenceSettled(asset_id);
      }))
    return fail(QStringLiteral("The native Preview/Catalog recipe save did not settle."), retained_root);
  if (!recipe_persistence.error().isEmpty() || !preview.recipeSaveError().isEmpty())
    return fail(QStringLiteral("The actual recipe persistence owner failed: %1 %2")
        .arg(recipe_persistence.error(), preview.recipeSaveError()), retained_root);

  const auto output_authorization = [&](const QString& path) {
    return sha256((task_id + QLatin1Char('\n') + asset_id + QLatin1Char('\n') + path).toUtf8());
  };
  const auto snapshot_for = [&](const QString& requested_task, const QString& requested_asset,
                           const QString& output_path, const QString& operation,
                           const QString& layer_id) -> QVariantMap {
    if (requested_task != task_id || requested_asset != asset_id)
      return failure(QStringLiteral("The output source no longer matches this approved task."));
    const auto approved_session = tasks.journal()->report().value(QStringLiteral("approvedSnapshot")).toMap();
    const auto approved_targets = approved_session.value(QStringLiteral("targetIds")).toList();
    if (approved_session.value(QStringLiteral("contract")) != QStringLiteral("agent-approved-session-v1") ||
        approved_session.value(QStringLiteral("prompt")).toString().trimmed().isEmpty() ||
        !std::any_of(approved_targets.cbegin(), approved_targets.cend(), [&](const QVariant& value) {
          return value.toString() == asset_id;
        }))
      return failure(QStringLiteral("The output task's exact approved brief or target is no longer active."));
    const auto current_asset = catalog.assetById(asset_id);
    const auto current_source = QFileInfo(current_asset.value(QStringLiteral("filePath")).toString()).canonicalFilePath();
    const auto source_hash = sha256_file(source_path);
    if (current_source != source_path || source_hash.isEmpty() || source_hash != source_sha_before ||
        preview.assetId() != asset_id)
      return failure(QStringLiteral("The imported source identity or bytes changed during export."));
    const auto native_recipe = preview.currentAssetBoundAdjustmentPatchJson().toUtf8();
    const auto catalog_recipe = catalog.agentTaskRecipeResourceAdapter(asset_id, task_id).read();
    if (native_recipe.isEmpty() || catalog_recipe.isEmpty())
      return failure(QStringLiteral("The exact Preview or Catalog recipe snapshot is unavailable."));
    QVariantMap layer_snapshot;
    if (operation == QStringLiteral("layer")) {
      layer_snapshot = layer_for_id(preview, layer_id);
      if (layer_snapshot.isEmpty())
        return failure(QStringLiteral("The requested layer no longer exists in the actual Preview."));
    }
    if (operation != QStringLiteral("flattened") && operation != QStringLiteral("layer"))
      return failure(QStringLiteral("The native output operation is not an approved photo export."));

    const QFileInfo target(output_path);
    if (target.absoluteFilePath() != output_path || target.fileName().isEmpty())
      return failure(QStringLiteral("The requested output identity is not canonical."));
    const auto canonical_parent = QFileInfo(target.absolutePath()).canonicalFilePath();
    if (canonical_parent.isEmpty() || QDir(canonical_parent).filePath(target.fileName()) != output_path)
      return failure(QStringLiteral("The approved output parent changed or could not be resolved."));
    QVariantMap destination{{QStringLiteral("path"), output_path},
        {QStringLiteral("parent"), canonical_parent},
        {QStringLiteral("authorization"), output_authorization(output_path)},
        {QStringLiteral("exists"), target.exists()}, {QStringLiteral("symlink"), target.isSymLink()}};
    if (target.exists() && target.isFile() && !target.isSymLink()) {
      const auto bytes = read_file(output_path);
      if (bytes.isEmpty()) return failure(QStringLiteral("The occupied output path could not be read safely."));
      destination.insert(QStringLiteral("sha256"), sha256(bytes));
      destination.insert(QStringLiteral("byteCount"), bytes.size());
    }
    return {{QStringLiteral("schema"), QStringLiteral("agent-photo-output-native-snapshot-v1")},
        {QStringLiteral("taskId"), task_id}, {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("approvedBriefSha256"), tasks.journal()->report().value(QStringLiteral("approvedSnapshotHash"))},
        {QStringLiteral("sourcePath"), source_path}, {QStringLiteral("sourceSha256"), source_hash},
        {QStringLiteral("nativeRecipeBase64"), QString::fromLatin1(native_recipe.toBase64())},
        {QStringLiteral("nativeRecipeSha256"), sha256(native_recipe)},
        {QStringLiteral("catalogRecipeBase64"), QString::fromLatin1(catalog_recipe.toBase64())},
        {QStringLiteral("catalogRecipeSha256"), sha256(catalog_recipe)},
        {QStringLiteral("operation"), operation}, {QStringLiteral("layerId"), layer_id},
        {QStringLiteral("layerSnapshot"), layer_snapshot},
        {QStringLiteral("destination"), destination}};
  };

  QVariantList admission_snapshots;
  QVariantList callback_approved_snapshots;
  QVariantList completion_snapshots;
  QStringList native_terminal_paths;
  QVector<QVariantMap> finished_receipts;
  int native_terminal_count = 0;
  auto owner = std::make_unique<AgentPhotoOutputTask>(tasks,
      [&](const QString& requested_task, const QString& requested_asset, const QString& output_path,
          const QString& operation, const QString& layer_id) {
        const auto snapshot = snapshot_for(requested_task, requested_asset, output_path, operation, layer_id);
        if (snapshot.isEmpty() || !snapshot.value(QStringLiteral("ok"), true).toBool()) return snapshot;
        admission_snapshots.append(snapshot);
        return QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("approvedSnapshot"), snapshot}};
      },
      [&](const QString& operation, const QString& layer_id, const QString& staging_path) {
        if (operation == QStringLiteral("flattened"))
          return preview.startFlattenedPhotoExport(staging_path);
        int index = -1;
        if (layer_for_id(preview, layer_id, &index).isEmpty()) return false;
        return preview.startPhotoLayerExport(index, staging_path);
      },
      [&preview] { return preview.cancelPhotoExport(); },
      [&preview] { return preview.photoExportBusy(); },
      [&](const QString& requested_task, const QString& requested_asset, const QString& output_path,
          const QString& operation, const QString& layer_id, const QVariantMap& approved_snapshot) {
        const auto snapshot = snapshot_for(requested_task, requested_asset, output_path, operation, layer_id);
        if (snapshot.isEmpty() || !snapshot.value(QStringLiteral("ok"), true).toBool()) return snapshot;
        callback_approved_snapshots.append(approved_snapshot);
        completion_snapshots.append(snapshot);
        return QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("currentSnapshot"), snapshot}};
      });
  owner->setFinishedHandler([&](const QVariantMap& result) { finished_receipts.append(result); });
  QObject::connect(&preview, &EditorPreviewController::photoExportFinished, owner.get(),
      [&](bool success, bool cancelled, const QString& path, const QString& error) {
        ++native_terminal_count;
        native_terminal_paths.append(path);
        static_cast<void>(owner->nativeExportFinished(success, cancelled, path, error));
      });

  const QByteArray sibling_bytes("user sibling survives exact output Revert\n");
  const auto sibling_path = QDir(output_root).filePath(QStringLiteral("user-note.txt"));
  if (!write_bytes(sibling_path, sibling_bytes))
    return fail(QStringLiteral("Could not create the independent output-folder sentinel."), retained_root);

  const auto run_successful_output = [&](const QString& name, const QString& operation,
      const QString& layer_id) -> QVariantMap {
    const auto requested_path = QDir(output_root).filePath(name);
    const QFileInfo parent_info(output_root);
    const auto target_path = QDir(parent_info.canonicalFilePath()).filePath(name);
    const auto previous_terminal_count = native_terminal_count;
    const auto started = owner->start(task_id, asset_id, requested_path, operation, layer_id);
    if (!started.value(QStringLiteral("ok")).toBool() ||
        !started.value(QStringLiteral("accepted")).toBool() ||
        !tasks.journal()->pending(owner->resourceId()))
      return failure(QStringLiteral("The actual native exporter was not accepted into the same pending task journal."));
    const auto resource_id = owner->resourceId();
    const auto step = output_resource_step(tasks.journal()->report(), resource_id);
    const auto arguments = step.value(QStringLiteral("arguments")).toMap();
    if (step.value(QStringLiteral("status")).toString() != QStringLiteral("accepted") ||
        arguments.value(QStringLiteral("approvedSnapshot")).toMap() != admission_snapshots.last().toMap() ||
        arguments.value(QStringLiteral("outputPath")).toString() != target_path)
      return failure(QStringLiteral("The accepted output step did not pin its exact native source and destination map."));
    const auto expected_stage = owner->stagingPath();
    if (!wait_until([&] { return !owner->busy() && !preview.photoExportBusy(); }))
      return failure(QStringLiteral("The actual Preview exporter or output receipt did not settle."));
    const auto result = owner->lastResult();
    if (!result.value(QStringLiteral("ok")).toBool() || result.value(QStringLiteral("pending")).toBool() ||
        result.value(QStringLiteral("sha256")).toString().size() != 64 ||
        result.value(QStringLiteral("byteCount")).toLongLong() <= 0 ||
        native_terminal_count != previous_terminal_count + 1 || native_terminal_paths.last() != expected_stage ||
        !QFileInfo(target_path).isFile() || finished_receipts.isEmpty() ||
        !finished_receipts.last().value(QStringLiteral("ok")).toBool() ||
        finished_receipts.last().value(QStringLiteral("pending")).toBool())
      return failure(QStringLiteral("The native terminal receipt did not authenticate a settled published output."));
    const auto terminal_step = output_resource_step(tasks.journal()->report(), resource_id);
    if (terminal_step.value(QStringLiteral("status")).toString() != QStringLiteral("completed") ||
        tasks.journal()->pending(resource_id))
      return failure(QStringLiteral("The journal did not settle the native output terminal receipt."));
    if (completion_snapshots.isEmpty() || callback_approved_snapshots.isEmpty() ||
        callback_approved_snapshots.last().toMap() != admission_snapshots.last().toMap() ||
        completion_snapshots.last().toMap() != admission_snapshots.last().toMap())
      return failure(QStringLiteral("The native source, recipe, layer or approved destination changed during rendering."));
    const auto actual_bytes = read_file(target_path);
    if (actual_bytes.isEmpty() || sha256(actual_bytes) != result.value(QStringLiteral("sha256")).toString() ||
        actual_bytes.size() != result.value(QStringLiteral("byteCount")).toLongLong())
      return failure(QStringLiteral("The published output bytes do not match the exact terminal photo receipt."));
    QImageReader reader(target_path);
    const auto pixels = reader.read();
    if (pixels.isNull())
      return failure(QStringLiteral("The task-owned output could not be decoded: %1").arg(reader.errorString()));
    if (!image_has_photo_detail(pixels))
      return failure(QStringLiteral("The decoded task-owned output lacks sampled natural-photo pixel detail."));
    return result;
  };

  const auto flattened = run_successful_output(QStringLiteral("flattened.png"),
      QStringLiteral("flattened"), {});
  if (!flattened.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("Flattened native output failed: %1").arg(flattened.value(QStringLiteral("error")).toString()), retained_root);
  const auto layer = run_successful_output(QStringLiteral("source-layer.png"),
      QStringLiteral("layer"), source_layer_id);
  if (!layer.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("Layer native output failed: %1").arg(layer.value(QStringLiteral("error")).toString()), retained_root);

  const auto source_layer_pixels = decode_photo_layer_source_image(source_path);
  QImageReader layer_reader(QDir(output_root).filePath(QStringLiteral("source-layer.png")));
  const auto exported_layer_pixels = layer_reader.read();
  if (source_layer_pixels.isNull() || exported_layer_pixels.isNull() ||
      source_layer_pixels.size() != exported_layer_pixels.size() ||
      !sampled_pixels_close(exported_layer_pixels, source_layer_pixels, 3))
    return fail(QStringLiteral("The exported native source layer did not preserve readable source-photo pixels."), retained_root);

  // Exercise a real asynchronous cancellation and a destination collision.
  // The native producer is never replaced by a synchronous/const writer.
  const auto cancel_target = QDir(output_root).filePath(QStringLiteral("cancelled.png"));
  const auto cancel_start = owner->start(task_id, asset_id, cancel_target, QStringLiteral("flattened"));
  if (!cancel_start.value(QStringLiteral("ok")).toBool() || !cancel_start.value(QStringLiteral("accepted")).toBool())
    return fail(QStringLiteral("The actual flattened exporter did not start for the Stop contract."), retained_root);
  const auto stop = owner->stop(task_id);
  if (!stop.value(QStringLiteral("ok")).toBool() || !stop.value(QStringLiteral("stopRequested")).toBool())
    return fail(QStringLiteral("Stop did not request cancellation from the actual Preview exporter."), retained_root);
  if (!wait_until([&] { return !owner->busy() && !preview.photoExportBusy(); }))
    return fail(QStringLiteral("The canceled native export did not report terminal quiescence."), retained_root);
  const auto cancelled_result = owner->lastResult();
  if (cancelled_result.value(QStringLiteral("ok")).toBool() ||
      cancelled_result.value(QStringLiteral("pending")).toBool() || QFileInfo::exists(cancel_target) ||
      finished_receipts.isEmpty() || finished_receipts.last().value(QStringLiteral("ok")).toBool() ||
      finished_receipts.last().value(QStringLiteral("pending")).toBool())
    return fail(QStringLiteral("The actual Stop path published or left an accepted output unresolved."), retained_root);

  const auto conflict_path = QDir(output_root).filePath(QStringLiteral("manual-race.png"));
  const auto conflict_start = owner->start(task_id, asset_id, conflict_path, QStringLiteral("flattened"));
  if (!conflict_start.value(QStringLiteral("ok")).toBool() || !conflict_start.value(QStringLiteral("accepted")).toBool())
    return fail(QStringLiteral("The actual native exporter did not start for the destination-race check."), retained_root);
  const QByteArray manual_bytes("manual destination wins over the in-flight task output\n");
  if (!write_bytes(conflict_path, manual_bytes))
    return fail(QStringLiteral("Could not write the isolated manual destination race fixture."), retained_root);
  if (!wait_until([&] {
        return native_terminal_count >= 4 && owner->lastResult().value(QStringLiteral("recoveryRequired")).toBool();
      }))
    return fail(QStringLiteral("The destination race did not retain its native stage and pending journal receipt."), retained_root);
  const auto unresolved = owner->lastResult();
  if (!unresolved.value(QStringLiteral("pending")).toBool() || !tasks.journal()->pending(owner->resourceId()) ||
      unresolved.value(QStringLiteral("recoveryPath")).toString().isEmpty() ||
      !QFileInfo::exists(unresolved.value(QStringLiteral("recoveryPath")).toString()) ||
      read_file(conflict_path) != manual_bytes || finished_receipts.isEmpty() ||
      !finished_receipts.last().value(QStringLiteral("recoveryRequired")).toBool() ||
      !finished_receipts.last().value(QStringLiteral("pending")).toBool())
    return fail(QStringLiteral("A manual destination collision was overwritten or lost its pending recovery bytes."), retained_root);
  const auto blocked_revert = tasks.revert();
  if (blocked_revert.value(QStringLiteral("ok")).toBool() || read_file(conflict_path) != manual_bytes ||
      !QFileInfo::exists(QDir(output_root).filePath(QStringLiteral("flattened.png"))))
    return fail(QStringLiteral("Revert did not preserve a conflicting manual output and the other owned outputs."), retained_root);
  if (!QFile::remove(conflict_path))
    return fail(QStringLiteral("Could not remove the test-created collision after verifying preservation."), retained_root);
  const auto retry = owner->retrySettlement(task_id);
  if (!retry.value(QStringLiteral("pending")).toBool() ||
      !wait_until([&] { return !owner->busy(); }))
    return fail(QStringLiteral("The accepted collision receipt did not settle after the exact manual file was removed."), retained_root);
  if (owner->lastResult().value(QStringLiteral("pending")).toBool() ||
      tasks.journal()->pending(owner->resourceId()) || QFileInfo::exists(conflict_path))
    return fail(QStringLiteral("The rejected destination receipt remains pending after exact absence was restored."), retained_root);

  if (read_file(sibling_path) != sibling_bytes)
    return fail(QStringLiteral("An unrelated output-folder file changed before Revert."), retained_root);
  const auto reviewed = tasks.finish(QStringLiteral("review"));
  if (!reviewed.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("The task could not reach Review with settled native receipts: %1")
        .arg(reviewed.value(QStringLiteral("error")).toString()), retained_root);
  const auto reverted = tasks.revert();
  if (!reverted.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("Whole-task Revert failed after all actual producers settled: %1")
        .arg(reverted.value(QStringLiteral("error")).toString()), retained_root);
  if (tasks.journal()->report().value(QStringLiteral("status")).toString() != QStringLiteral("reverted") ||
      preview.currentAssetBoundAdjustmentPatchJson().toUtf8() != native_before ||
      catalog.agentTaskRecipeResourceAdapter(asset_id, task_id).read() != catalog_before ||
      sha256_file(source_path) != source_sha_before ||
      QFileInfo::exists(QDir(output_root).filePath(QStringLiteral("flattened.png"))) ||
      QFileInfo::exists(QDir(output_root).filePath(QStringLiteral("source-layer.png"))) ||
      QFileInfo::exists(cancel_target) || QFileInfo::exists(conflict_path) ||
      read_file(sibling_path) != sibling_bytes ||
      !QFileInfo::exists(unresolved.value(QStringLiteral("recoveryPath")).toString()))
    return fail(QStringLiteral("Whole-task Revert did not remove only authenticated outputs and restore exact native/Catalog/source baselines."), retained_root);

  std::cout << "native_output_contract=passed"
            << " task_id=" << task_id.toStdString()
            << " source_sha256=" << source_sha_before.toStdString()
            << " flattened_sha256=" << flattened.value(QStringLiteral("sha256")).toString().toStdString()
            << " layer_sha256=" << layer.value(QStringLiteral("sha256")).toString().toStdString()
            << " admission_snapshots=" << admission_snapshots.size()
            << " approved_completion_bindings=" << callback_approved_snapshots.size()
            << " completion_snapshots=" << completion_snapshots.size()
            << " native_terminal_receipts=" << native_terminal_count
            << " whole_revert=exact"
            << " local_model_proven=false full_app_ui_proven=false" << std::endl;
  std::cout << "Retained private output fixture: " << retained_root.toStdString() << std::endl;
  return EXIT_SUCCESS;
}
