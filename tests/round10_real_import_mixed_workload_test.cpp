#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "edit_recipe_state.h"
#include "job_runner.h"
#include "job_store.h"
#include "observability.h"
#include "photo_export_task.h"
#include "photo_render_utils.h"
#include "source_image_decode.h"
#include "photo_editor/pipeline/output_rendering.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>

#include <QByteArrayView>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRect>
#include <QScopeGuard>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static bool interactive_render_running(const EditorPreviewController& controller) {
    return controller.interactive_render_watcher_.isRunning();
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }
  static QJsonObject render_profile(const EditorPreviewController& controller, bool full) {
    const auto& watcher = full ? controller.full_render_watcher_ : controller.interactive_render_watcher_;
    if (watcher.future().resultCount() == 0) return {};
    const auto r = watcher.result();
    return {{"generation", r.generation}, {"elapsedMs", r.total_elapsed_ms},
        {"decodeMs", r.decode_elapsed_ms}, {"editMs", r.edit_elapsed_ms},
        {"outputMs", r.output_stage_elapsed_ms},
        {"photographicOutputMs", r.photographic_output_stage_elapsed_ms},
        {"pipelineMs", r.adjustment_pipeline_elapsed_ms},
        {"linearMs", r.adjustment_linear_stage_elapsed_ms},
        {"legacyOutputMs", r.adjustment_output_elapsed_ms},
        {"fastGlobalMs", r.adjustment_fast_global_ms},
        {"toneMaskMs", r.adjustment_tone_mask_ms},
        {"histogramScopeMs", r.histogram_scope_elapsed_ms},
        {"backend", r.backend}, {"stageBackends", QJsonObject::fromVariantMap(r.render_stage_backends)},
        {"stageElapsedMs", QJsonObject::fromVariantMap(r.render_stage_elapsed_ms)},
        {"width", r.output_width}, {"height", r.output_height}};
  }
  static QJsonObject state_profile(const EditorPreviewController& controller) {
    const auto state = controller.currentState();
    return {{"photographicModel", state.photographic_rendering_model},
        {"workingProfile", state.working_color_profile}, {"previewColorSpace", state.preview_color_space},
        {"previewOutputMode", state.preview_output_mode}, {"monitorProfile", state.monitor_color_profile},
        {"previewIccSha256", state.preview_icc_profile_asset.payload_sha256},
        {"scopesVisible", controller.scopesVisible()}, {"scopeDomain", controller.scopeDomain()}};
  }
};

struct JobRunnerTestAccess {
  static bool active_import(
      const JobRunner& runner, QString* job_id, qint64* queue_wait_ms, qint64* active_ms) {
    for (auto iterator = runner.active_attempts_.cbegin(); iterator != runner.active_attempts_.cend(); ++iterator) {
      const auto& attempt = iterator.value();
      if (!attempt || attempt->job_kind != QStringLiteral("catalog_import") || !attempt->watcher ||
          !attempt->watcher->isRunning()) {
        continue;
      }
      if (job_id != nullptr) *job_id = attempt->job_id;
      if (queue_wait_ms != nullptr) *queue_wait_ms = attempt->queue_wait_ms;
      if (active_ms != nullptr) *active_ms = attempt->timer.elapsed();
      return true;
    }
    return false;
  }
};

}  // namespace photo_editor::desktop

namespace {

using namespace photo_editor::desktop;

constexpr int kSeedPhotoCount = 32;
constexpr int kImportPhotoCount = 1024;
constexpr int kWorkloadTimeoutMs = 180000;

class LivePreviewSink final : public EditorPreviewImageSink {
 public:
  QImage edited;

  void setImage(const QString& key, const QImage& image) override {
    if (key == QStringLiteral("edited")) edited = image;
  }

  void clearImage(const QString& key) override {
    if (key == QStringLiteral("edited")) edited = {};
  }
};

struct ExportResult final {
  bool success = false;
  QByteArray render_pixel_sha256;
  QByteArray output_pixel_sha256;
  QByteArray output_file_sha256;
  QString backend;
  QString fallback_reason;
  QString canonical_plan_error;
  qint64 render_plan_ms = 0;
  qint64 render_ms = 0;
  qint64 total_export_ms = 0;
  qint64 encode_and_publish_ms = 0;
  qint64 output_bytes = 0;
};

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return 1;
}

QJsonObject output_lut_cache_stats_json() {
  const auto stats = photo_editor::pipeline::aces2_output_lut_cache_stats();
  return {
      {QStringLiteral("requests"), static_cast<qint64>(stats.requests)},
      {QStringLiteral("hits"), static_cast<qint64>(stats.hits)},
      {QStringLiteral("misses"), static_cast<qint64>(stats.misses)},
      {QStringLiteral("builds"), static_cast<qint64>(stats.builds)},
      {QStringLiteral("evictions"), static_cast<qint64>(stats.evictions)},
      {QStringLiteral("entries"), static_cast<qint64>(stats.entries)},
      {QStringLiteral("bytes"), static_cast<qint64>(stats.bytes)},
      {QStringLiteral("maximumBytes"), static_cast<qint64>(stats.maximum_bytes)},
  };
}

QByteArray sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && !file.atEnd()) return {};
    hash.addData(chunk);
  }
  return hash.result().toHex();
}

QByteArray pixel_sha256(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) * 4;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return hash.result().toHex();
}

bool wait_until(const std::function<bool()>& ready, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}

QString drive_id_for_root(const QString& root) {
  const auto digest = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QStringLiteral("drive-%1").arg(QString::fromLatin1(digest.left(12)));
}

bool write_photo_sources(const QString& fixture, const QString& root, int count, bool enlarge,
                         QStringList* paths, QHash<QString, QByteArray>* source_hashes,
                         QByteArray* expected_pixels_sha256, QString* error) {
  if (paths == nullptr || source_hashes == nullptr || expected_pixels_sha256 == nullptr ||
      !QDir().mkpath(root)) {
    if (error != nullptr) *error = QStringLiteral("Could not create the photo source directory.");
    return false;
  }

  QString template_path = fixture;
  if (enlarge) {
    QImageReader reader(fixture);
    reader.setAutoTransform(true);
    const auto source = reader.read();
    if (source.isNull()) {
      if (error != nullptr) *error = QStringLiteral("Could not decode the bundled photo fixture.");
      return false;
    }
    const auto enlarged = source.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (enlarged.isNull()) {
      if (error != nullptr) *error = QStringLiteral("Could not prepare large thumbnail source pixels.");
      return false;
    }
    template_path = QFileInfo(root).dir().filePath(QStringLiteral("large-photo-template.jpg"));
    QImageWriter writer(template_path, "JPEG");
    writer.setQuality(94);
    if (!writer.write(enlarged)) {
      if (error != nullptr) *error = QStringLiteral("Could not write the derived photo fixture: %1").arg(writer.errorString());
      return false;
    }
  }

  QFile template_file(template_path);
  if (!template_file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) *error = QStringLiteral("Could not read the bundled photo template.");
    return false;
  }
  const auto template_bytes = template_file.readAll();
  const auto eoi = template_bytes.lastIndexOf(QByteArray::fromHex("ffd9"));
  if (eoi < 0 || template_bytes.size() < eoi + 2) {
    if (error != nullptr) *error = QStringLiteral("The bundled photo template is not a complete JPEG.");
    return false;
  }
  QImageReader template_reader(template_path);
  template_reader.setAutoTransform(true);
  *expected_pixels_sha256 = pixel_sha256(template_reader.read());
  if (expected_pixels_sha256->isEmpty()) {
    if (error != nullptr) *error = QStringLiteral("Could not decode the bundled photo template.");
    return false;
  }

  paths->clear();
  paths->reserve(count);
  source_hashes->clear();
  const auto manifest_comment_prefix = QByteArrayLiteral("Looksy-R10-Photo-");
  for (int index = 0; index < count; ++index) {
    const auto path = QDir(root).filePath(QStringLiteral("photo-%1.jpg").arg(index, 5, 10, QLatin1Char('0')));
    const auto comment = manifest_comment_prefix + QByteArray::number(index);
    const auto segment_length = comment.size() + 2;
    if (segment_length > std::numeric_limits<quint16>::max()) {
      if (error != nullptr) *error = QStringLiteral("JPEG comment exceeds the legal marker size.");
      return false;
    }
    QByteArray derived;
    derived.reserve(template_bytes.size() + comment.size() + 4);
    const auto eoi_offset = static_cast<qsizetype>(eoi);
    derived.append(template_bytes.constData(), eoi_offset);
    derived.append(static_cast<char>(0xff));
    derived.append(static_cast<char>(0xfe));
    derived.append(static_cast<char>((segment_length >> 8) & 0xff));
    derived.append(static_cast<char>(segment_length & 0xff));
    derived.append(comment);
    derived.append(template_bytes.constData() + eoi_offset, template_bytes.size() - eoi_offset);
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        output.write(derived) != derived.size()) {
      if (error != nullptr) *error = QStringLiteral("Could not write unique bundled photo %1.").arg(index);
      return false;
    }
    output.close();
    const auto relative_path = QFileInfo(path).fileName();
    const auto digest = sha256_file(path);
    if (digest.isEmpty()) {
      if (error != nullptr) *error = QStringLiteral("Could not hash bundled photo %1.").arg(index);
      return false;
    }
    source_hashes->insert(relative_path, digest);
    if (index == 0 || index == count - 1) {
      QImageReader reader(path);
      reader.setAutoTransform(true);
      if (pixel_sha256(reader.read()) != *expected_pixels_sha256) {
        if (error != nullptr) *error = QStringLiteral("A unique JPEG comment changed decoded photo pixels.");
        return false;
      }
    }
    paths->push_back(path);
  }
  std::sort(paths->begin(), paths->end());
  return true;
}

QVector<PersistentJobRecord> job_records(QString* error) {
  return loadPersistedJobRecords(error, false);
}

std::optional<PersistentJobRecord> import_record_for_root(const QString& root, QString* error) {
  const auto records = job_records(error);
  for (const auto& record : records) {
    if (record.job_kind == QStringLiteral("catalog_import") &&
        QDir::cleanPath(record.source_path) == QDir::cleanPath(root)) {
      return record;
    }
  }
  return std::nullopt;
}

QJsonObject catalog_snapshot(const QString& repository_root, QString* error) {
  QProcess process;
  process.setWorkingDirectory(repository_root);
  process.start(QDir(repository_root).filePath(QStringLiteral("rust/target/debug/catalog_snapshot")),
                {QStringLiteral("0"), QStringLiteral("--library")});
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) || process.exitCode() != 0) {
    if (error != nullptr) {
      *error = QStringLiteral("catalog_snapshot failed: %1 %2")
                   .arg(QString::fromUtf8(process.readAllStandardOutput()),
                        QString::fromUtf8(process.readAllStandardError()));
    }
    return {};
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    if (error != nullptr) *error = QStringLiteral("catalog_snapshot returned invalid JSON.");
    return {};
  }
  return document.object();
}

ExportResult render_and_export(const QString& source_path, const QString& output_path,
                               const QSize& source_size, const EditRecipeState& state) {
  ExportResult result;
  QImage rendered_pixels;
  QElapsedTimer export_timer;
  export_timer.start();
  auto control = std::make_shared<PhotoExportControl>();
  const auto export_result = run_photo_export([&] {
    RenderBackendTelemetry telemetry;
    QElapsedTimer plan_timer;
    plan_timer.start();
    const auto plan = RenderPlanStore::shared().admit(
        QStringLiteral("round10-real-import-edited-export"), source_path, state, source_size,
        photo_editor::pipeline::RenderPurpose::Export,
        photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
    result.canonical_plan_error = plan.error;
    if (!plan.admitted || !plan.plan.has_value()) return QImage{};
    result.render_plan_ms = plan_timer.elapsed();
    QElapsedTimer render_timer;
    render_timer.start();
    rendered_pixels = render_output_file(
        source_path, source_size, QRect(QPoint(0, 0), source_size), state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, nullptr, &telemetry,
        {}, photo_editor::pipeline::RemovalQuality::Final, RenderOutputPolicy::DisplayReferred,
        nullptr, &*plan.plan);
    result.render_ms = render_timer.elapsed();
    result.backend = telemetry.backend;
    result.fallback_reason = telemetry.fallback_reason;
    return rendered_pixels;
  }, output_path, QByteArrayLiteral("png"), control);
  result.total_export_ms = export_timer.elapsed();
  result.encode_and_publish_ms =
      std::max<qint64>(0, result.total_export_ms - result.render_plan_ms - result.render_ms);
  if (!export_result.success || rendered_pixels.isNull()) return result;
  result.render_pixel_sha256 = pixel_sha256(rendered_pixels);
  QImage encoded(output_path);
  result.output_pixel_sha256 = pixel_sha256(encoded);
  result.output_file_sha256 = sha256_file(output_path);
  result.output_bytes = QFileInfo(output_path).size();
  result.success = !result.render_pixel_sha256.isEmpty() &&
      result.render_pixel_sha256 == result.output_pixel_sha256 &&
      !result.output_file_sha256.isEmpty() && result.output_bytes > 0;
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QGuiApplication app(argc, argv);

#ifndef PHOTO_EDITOR_ROUND10_REAL_PHOTO_FIXTURE
  return fail(QStringLiteral("PHOTO_EDITOR_ROUND10_REAL_PHOTO_FIXTURE must point to the bundled photo fixture."));
#else
  const auto private_root = qEnvironmentVariable("PHOTO_EDITOR_ROUND10_PRIVATE_ROOT");
  const auto database_url = qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL");
  const auto repository_root = qEnvironmentVariable("PHOTO_EDITOR_REPOSITORY_ROOT");
  if (!private_root.startsWith(QStringLiteral("/private/tmp/looksy-round10-import-")) ||
      !QFileInfo(private_root).isDir() ||
      !database_url.contains(QStringLiteral("host=%1/socket").arg(private_root)) ||
      !QDir(private_root).exists(QStringLiteral("pg")) ||
      !QDir(private_root).exists(QStringLiteral("socket"))) {
    return fail(QStringLiteral("The real-import workload requires its wrapper-owned private /private/tmp Unix-socket catalog."));
  }

  const auto seed_root = QDir(private_root).filePath(QStringLiteral("seed-source"));
  const auto batch_root = QDir(private_root).filePath(QStringLiteral("import-source"));
  const auto managed_root = qEnvironmentVariable("PHOTO_EDITOR_MANAGED_MEDIA_DIR");
  const auto runtime_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR");
  if (managed_root.isEmpty() || runtime_root.isEmpty() || repository_root.isEmpty() ||
      !QDir().mkpath(managed_root) || !QDir().mkpath(runtime_root)) {
    return fail(QStringLiteral("The isolated app runtime and managed media directories are required."));
  }

  observability::initialize();
  QString source_error;
  QStringList seed_paths;
  QStringList import_paths;
  QHash<QString, QByteArray> seed_source_hashes;
  QHash<QString, QByteArray> import_source_hashes;
  QByteArray seed_source_pixels_hash;
  QByteArray import_source_pixels_hash;
  if (!write_photo_sources(QStringLiteral(PHOTO_EDITOR_ROUND10_REAL_PHOTO_FIXTURE),
                           seed_root, kSeedPhotoCount, true, &seed_paths, &seed_source_hashes,
                           &seed_source_pixels_hash, &source_error) ||
      !write_photo_sources(QStringLiteral(PHOTO_EDITOR_ROUND10_REAL_PHOTO_FIXTURE),
                           batch_root, kImportPhotoCount, false, &import_paths, &import_source_hashes,
                           &import_source_pixels_hash, &source_error)) {
    return fail(source_error);
  }
  const auto fixture_hash = sha256_file(QStringLiteral(PHOTO_EDITOR_ROUND10_REAL_PHOTO_FIXTURE));
  if (fixture_hash.isEmpty()) return fail(QStringLiteral("Could not hash the bundled source photo."));
  if (import_source_pixels_hash.isEmpty()) return fail(QStringLiteral("Could not verify decoded pixels for the import photo set."));
  QCryptographicHash source_manifest(QCryptographicHash::Sha256);
  QStringList source_manifest_paths = import_source_hashes.keys();
  std::sort(source_manifest_paths.begin(), source_manifest_paths.end());
  for (const auto& relative_path : source_manifest_paths) {
    source_manifest.addData(relative_path.toUtf8());
    source_manifest.addData("\n");
    source_manifest.addData(import_source_hashes.value(relative_path));
    source_manifest.addData("\n");
  }
  const auto import_source_manifest_sha256 = source_manifest.result().toHex();
  QSet<QByteArray> unique_import_source_hashes;
  for (const auto& digest : import_source_hashes) unique_import_source_hashes.insert(digest);
  if (import_paths.size() != kImportPhotoCount || import_source_hashes.size() != kImportPhotoCount ||
      seed_paths.size() != kSeedPhotoCount || seed_source_hashes.size() != kSeedPhotoCount ||
      unique_import_source_hashes.size() != kImportPhotoCount || seed_source_pixels_hash.isEmpty()) {
    return fail(QStringLiteral("The source photo manifest did not contain the exact bounded fixture set."));
  }

  JobRunner runner;
  QString recovery_error;
  bool recovery_finished = false;
  QObject::connect(&runner, &JobRunner::startupRecoveryFinished, &app,
                   [&](const QString& error) { recovery_error = error; recovery_finished = true; });
  runner.initializeFromStore();
  if (!wait_until([&] { return recovery_finished; }, 30000) || !recovery_error.isEmpty()) {
    return fail(QStringLiteral("JobRunner startup recovery failed: %1").arg(recovery_error));
  }

  CatalogAssetModel import_model;
  import_model.setInteractiveRenderingActive(true);
  import_model.refresh();
  if (!wait_until([&] { return !import_model.loading(); }, 30000) || import_model.assetCount() != 0) {
    return fail(QStringLiteral("The isolated catalog did not start empty."));
  }

  QElapsedTimer seed_import_timer;
  seed_import_timer.start();
  import_model.importSelectionWithOptions(seed_root, seed_paths, {},
      QStringLiteral("managed"), QStringLiteral("skip"), QStringLiteral("source"), QStringLiteral("both"));
  QString job_error;
  if (!wait_until([&] {
        return !import_model.importPreflightBusy() && !import_model.importing() &&
            !import_model.loading() && import_model.assetCount() == kSeedPhotoCount;
      }, kWorkloadTimeoutMs)) {
    const auto failed_record = import_record_for_root(seed_root, &job_error);
    return fail(QStringLiteral("Seed-photo import did not complete through JobRunner: %1 %2")
                    .arg(job_error, failed_record.has_value() ? failed_record->error_message : QString{}));
  }
  const auto seed_record = import_record_for_root(seed_root, &job_error);
  if (!job_error.isEmpty() || !seed_record.has_value() || seed_record->status != QStringLiteral("Completed")) {
    return fail(QStringLiteral("Seed-photo import did not persist a completed JobRunner record: %1")
                    .arg(job_error.isEmpty() && seed_record.has_value() ? seed_record->error_message : job_error));
  }
  if (seed_record->metadata_json.value(QStringLiteral("importResult")).toObject()
          .value(QStringLiteral("importedCount")).toInt() != kSeedPhotoCount) {
    return fail(QStringLiteral("The seed import did not commit every bundled-photo-derived source."));
  }
  const auto seed_import_ms = seed_import_timer.elapsed();
  import_model.refresh();
  if (!wait_until([&] { return !import_model.loading() && import_model.assetCount() == kSeedPhotoCount; }, 30000)) {
    return fail(QStringLiteral("The committed seed catalog snapshot did not contain every photo."));
  }

  CatalogAssetModel thumbnail_model;
  thumbnail_model.setInteractiveRenderingActive(true);
  thumbnail_model.refresh();
  if (!wait_until([&] { return !thumbnail_model.loading() && thumbnail_model.assetCount() == kSeedPhotoCount; }, 30000)) {
    return fail(QStringLiteral("The independent thumbnail model did not load the committed photo rows."));
  }
  thumbnail_model.setVisibleAssetRange(0, kSeedPhotoCount - 1);
  const auto seed_asset_id = thumbnail_model.data(
      thumbnail_model.index(0, 0), CatalogAssetModel::AssetIdRole).toString();
  const auto seed_file_path = thumbnail_model.data(
      thumbnail_model.index(0, 0), CatalogAssetModel::FilePathRole).toString();
  if (seed_asset_id.isEmpty() || !QFileInfo::exists(seed_file_path)) {
    return fail(QStringLiteral("The committed seed photo was not available to the production editor renderer."));
  }
  const auto seed_drive_root = QDir(managed_root).filePath(drive_id_for_root(seed_root));
  if (QDir::cleanPath(QFileInfo(seed_file_path).absolutePath()) != QDir::cleanPath(seed_drive_root)) {
    return fail(QStringLiteral("The render source was not read from the committed managed photo."));
  }

  QByteArray neutral_render_hash;
  QByteArray neutral_preview_hash;
  QJsonObject output_lut_cache_before_neutral_export;
  QJsonObject output_lut_cache_after_neutral_export;
  qint64 editor_warm_ms = 0;
  {
    const QSize source_size = source_image_size(seed_file_path);
    if (!source_size.isValid()) return fail(QStringLiteral("Could not inspect the committed render photo."));
    EditRecipeState neutral_state;
    const auto plan = RenderPlanStore::shared().admit(
        QStringLiteral("round10-real-import-neutral-oracle"), seed_file_path, neutral_state, source_size,
        photo_editor::pipeline::RenderPurpose::Export,
        photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
    if (!plan.admitted || !plan.plan.has_value()) {
      return fail(QStringLiteral("Neutral production render-plan admission failed: %1").arg(plan.error));
    }
    RenderBackendTelemetry telemetry;
    output_lut_cache_before_neutral_export = output_lut_cache_stats_json();
    const auto neutral_image = render_output_file(
        seed_file_path, source_size, QRect(QPoint(0, 0), source_size), neutral_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, nullptr, &telemetry,
        {}, photo_editor::pipeline::RemovalQuality::Final, RenderOutputPolicy::DisplayReferred,
        nullptr, &*plan.plan);
    output_lut_cache_after_neutral_export = output_lut_cache_stats_json();
    if (neutral_image.isNull()) {
      return fail(QStringLiteral("Neutral production render failed: backend=%1 fallback=%2 canonical=%3")
                      .arg(telemetry.backend, telemetry.fallback_reason, telemetry.canonical_plan_error));
    }
    neutral_render_hash = pixel_sha256(neutral_image);
  }

  LivePreviewSink preview_sink;
  EditorPreviewController preview_controller;
  preview_controller.setImageProvider(&preview_sink);
  preview_controller.setCatalogRecipePersistenceEnabled(false);
  preview_controller.setPreviewTargetEdge(2048);
  const auto output_lut_cache_before_editor_load = output_lut_cache_stats_json();
  QElapsedTimer editor_warm_timer;
  editor_warm_timer.start();
  preview_controller.loadAsset(seed_asset_id, seed_file_path, {}, {}, {}, {}, {});
  if (!wait_until([&] {
        return !preview_controller.processing() && !preview_controller.fullRenderRunning() &&
            !preview_sink.edited.isNull();
      }, 60000)) {
    return fail(QStringLiteral("The production interactive editor preview did not warm on the committed seed photo."));
  }
  editor_warm_ms = editor_warm_timer.elapsed();
  const auto output_lut_cache_after_editor_load = output_lut_cache_stats_json();
  neutral_preview_hash = pixel_sha256(preview_sink.edited);
  if (neutral_preview_hash.isEmpty()) return fail(QStringLiteral("The warmed editor preview had no pixels."));

  // Keep the visible seed range deferred until the real importer is in its
  // worker, then resume it while the changed-recipe export runs.
  if (!wait_until([&] { return thumbnail_model.activeThumbnailWorkerCount() == 0; }, 30000)) {
    return fail(QStringLiteral("Could not defer and drain the seed thumbnail queue."));
  }

  const auto output_lut_cache_before_mixed_workload = output_lut_cache_stats_json();
  QElapsedTimer import_request_timer;
  import_request_timer.start();
  qint64 preflight_completed_ms = -1;
  bool preflight_observed_busy = false;
  QObject::connect(&import_model, &CatalogAssetModel::importPreflightChanged, &app, [&] {
    if (import_model.importPreflightBusy()) {
      preflight_observed_busy = true;
    } else if (preflight_observed_busy && preflight_completed_ms < 0) {
      preflight_completed_ms = import_request_timer.elapsed();
    }
  });
  import_model.importSelectionWithOptions(batch_root, import_paths, {},
      QStringLiteral("managed"), QStringLiteral("skip"), QStringLiteral("source"), QStringLiteral("both"));
  const auto preflight_busy_at_admission = import_model.importPreflightBusy();
  if (!preflight_busy_at_admission) return fail(QStringLiteral("The real import did not enter asynchronous production preflight."));
  preflight_observed_busy = true;

  auto maximum_active_thumbnails = 0;
  auto maximum_pending_thumbnails = 0;
  int import_thumbnail_overlap_samples = 0;
  int import_render_export_overlap_samples = 0;
  int triple_overlap_samples = 0;
  int import_active_samples = 0;
  int export_active_samples = 0;
  qint64 import_worker_active_ms = 0;
  qint64 import_running_since_ms = -1;
  qint64 import_worker_completed_ms = -1;
  qint64 import_queue_wait_ms = -1;
  qint64 edit_to_preview_ms = -1;
  QString running_job_id;
  bool export_started = false;
  bool thumbnails_released = false;
  bool preview_edit_requested = false;
  bool preview_edit_completed = false;
  int preview_generation_before_edit = -1;
  int preview_generation_after_edit = -1;
  QElapsedTimer preview_edit_timer;
  int interactive_preview_overlap_samples = 0;
  bool import_worker_was_active = false;
  std::optional<QFuture<ExportResult>> export_future;
  QThreadPool export_pool;
  export_pool.setMaxThreadCount(1);

  const auto start_export = [&] {
    if (export_started) return;
    export_started = true;
    const QSize source_size = source_image_size(seed_file_path);
    EditRecipeState edited_state;
    edited_state.exposure = 0.75;
    const auto output_path = QDir(runtime_root).filePath(QStringLiteral("round10-import-overlap-edited.png"));
    export_future.emplace(QtConcurrent::run(&export_pool, [seed_file_path, output_path, source_size, edited_state] {
      return render_and_export(seed_file_path, output_path, source_size, edited_state);
    }));
  };

  const auto sample_workload = [&] {
    if (preflight_completed_ms < 0 && preflight_observed_busy && !import_model.importPreflightBusy()) {
      preflight_completed_ms = import_request_timer.elapsed();
    }
    QString active_id;
    qint64 queue_wait_ms = -1;
    qint64 active_ms = -1;
    const auto import_active = JobRunnerTestAccess::active_import(
        runner, &active_id, &queue_wait_ms, &active_ms);
    if (import_active) {
      import_worker_was_active = true;
      ++import_active_samples;
      if (import_running_since_ms < 0) {
        import_running_since_ms = import_request_timer.elapsed();
        import_queue_wait_ms = queue_wait_ms;
        running_job_id = active_id;
      }
      import_worker_active_ms = std::max(import_worker_active_ms, active_ms);
    } else if (import_worker_was_active && import_worker_completed_ms < 0) {
      import_worker_completed_ms = import_request_timer.elapsed();
    }
    if (import_active && !thumbnails_released) {
      thumbnails_released = true;
      thumbnail_model.setInteractiveRenderingActive(false);
    }
    const auto thumbnails_active = thumbnail_model.activeThumbnailWorkerCount();
    const auto thumbnails_pending = thumbnail_model.pendingThumbnailWorkerCount();
    maximum_active_thumbnails = std::max(maximum_active_thumbnails, thumbnails_active);
    maximum_pending_thumbnails = std::max(maximum_pending_thumbnails, thumbnails_pending);
    if (import_active && thumbnails_active > 0 && !export_started) {
      start_export();
      preview_edit_requested = true;
      preview_edit_timer.start();
      preview_generation_before_edit = EditorPreviewControllerTestAccess::render_generation(preview_controller);
      preview_controller.setExposure(0.75);
    }
    const auto export_active = export_future.has_value() && !export_future->isFinished();
    if (export_active) ++export_active_samples;
    if (import_active && thumbnails_active > 0) ++import_thumbnail_overlap_samples;
    if (import_active && export_active) ++import_render_export_overlap_samples;
    if (import_active && thumbnails_active > 0 && export_active) ++triple_overlap_samples;
    const auto interactive_preview_active =
        EditorPreviewControllerTestAccess::interactive_render_running(preview_controller);
    if (import_active && thumbnails_active > 0 && export_active && interactive_preview_active) {
      ++interactive_preview_overlap_samples;
    }
    if (preview_edit_requested && !preview_edit_completed &&
        preview_controller.exposure() == 0.75 && !preview_controller.processing() &&
        !preview_controller.fullRenderRunning() && !preview_sink.edited.isNull() &&
        pixel_sha256(preview_sink.edited) != neutral_preview_hash) {
      preview_edit_completed = true;
      edit_to_preview_ms = preview_edit_timer.elapsed();
      preview_generation_after_edit = EditorPreviewControllerTestAccess::render_generation(preview_controller);
    }
  };

  QTimer workload_sampler;
  workload_sampler.setInterval(1);
  QObject::connect(&workload_sampler, &QTimer::timeout, &app, sample_workload);
  workload_sampler.start();
  [[maybe_unused]] const auto cleanup = qScopeGuard([&] {
    workload_sampler.stop();
    thumbnail_model.setInteractiveRenderingActive(true);
    import_model.cancelImportPreflight();
    QString cleanup_error;
    const auto active_record = import_record_for_root(batch_root, &cleanup_error);
    if (active_record.has_value() &&
        (active_record->status == QStringLiteral("Queued") || active_record->status == QStringLiteral("Running"))) {
      (void)cancelPersistedJobRecord(active_record->job_id, &cleanup_error);
      static_cast<void>(wait_until([&] {
        return !JobRunnerTestAccess::active_import(runner, nullptr, nullptr, nullptr);
      }, 30000));
    }
    if (export_future.has_value()) export_future->waitForFinished();
    export_pool.waitForDone(30000);
    static_cast<void>(thumbnail_model.waitForInteractiveThumbnailWorkers(30000));
    static_cast<void>(wait_until([&] {
      return !preview_controller.processing() && !preview_controller.fullRenderRunning();
    }, 30000));
    observability::flush();
  });

  // Keep the event loop alive until the model, thumbnail workers and export
  // reach terminal state; read the persisted job store once afterward.
  const auto workload_drained = wait_until([&] {
    sample_workload();
    if (!export_future.has_value() || !export_future->isFinished()) return false;
    return !import_model.importPreflightBusy() && !import_model.importing() && !import_model.loading() &&
        import_model.assetCount() == kSeedPhotoCount + kImportPhotoCount &&
        preview_edit_completed &&
        thumbnail_model.activeThumbnailWorkerCount() == 0 &&
        thumbnail_model.pendingThumbnailWorkerCount() == 0;
  }, kWorkloadTimeoutMs);
  workload_sampler.stop();
  if (!job_error.isEmpty()) return fail(QStringLiteral("Reading persisted import records failed: %1").arg(job_error));
  if (!workload_drained) {
    return fail(QStringLiteral("Import, thumbnail and render/export work did not drain within %1 seconds.")
                    .arg(kWorkloadTimeoutMs / 1000));
  }
  const auto combined_workload_ms = import_request_timer.elapsed();
  const auto output_lut_cache_after_mixed_workload = output_lut_cache_stats_json();

  const auto final_record = import_record_for_root(batch_root, &job_error);
  if (!final_record.has_value() || final_record->status != QStringLiteral("Completed")) {
    return fail(QStringLiteral("The production catalog import did not complete: %1")
                    .arg(final_record.has_value() ? final_record->error_message : job_error));
  }
  const auto import_result = final_record->metadata_json.value(QStringLiteral("importResult")).toObject();
  if (import_result.value(QStringLiteral("importedCount")).toInt() != kImportPhotoCount ||
      import_result.value(QStringLiteral("failureCount")).toInt() != 0 ||
      import_result.value(QStringLiteral("verificationAlgorithm")).toString() != QStringLiteral("SHA-256")) {
    return fail(QStringLiteral("Production import receipt did not verify every managed photo."));
  }
  if (import_running_since_ms < 0 || import_worker_completed_ms < 0 || import_active_samples <= 0 ||
      import_queue_wait_ms < 0) {
    return fail(QStringLiteral("The workload did not observe the actual catalog_import worker running."));
  }
  if (preflight_completed_ms < 0) preflight_completed_ms = import_running_since_ms;
  if (!export_future.has_value() || !preview_edit_completed) {
    return fail(QStringLiteral("No changed-recipe production export or live editor preview completed."));
  }
  if (preview_generation_before_edit < 0 || preview_generation_after_edit <= preview_generation_before_edit) {
    return fail(QStringLiteral("The exposure edit did not publish a newer interactive preview generation."));
  }
  const auto export_result = export_future->result();
  if (!export_result.success || export_result.render_pixel_sha256 == neutral_render_hash) {
    return fail(QStringLiteral("Changed-recipe production rendering did not publish distinct pixels."));
  }
  if (triple_overlap_samples <= 0 || import_thumbnail_overlap_samples <= 0 ||
      import_render_export_overlap_samples <= 0 || interactive_preview_overlap_samples <= 0) {
    return fail(QStringLiteral("The workload did not observe committed import, production thumbnails, interactive edit preview and export work overlapping."));
  }

  import_model.refresh();
  if (!wait_until([&] { return !import_model.loading() && import_model.assetCount() == kSeedPhotoCount + kImportPhotoCount; }, 45000)) {
    return fail(QStringLiteral("The committed catalog snapshot did not contain the exact expected asset count."));
  }

  const auto snapshot = catalog_snapshot(repository_root, &job_error);
  if (!job_error.isEmpty()) return fail(job_error);
  const auto assets = snapshot.value(QStringLiteral("assets")).toArray();
  if (assets.size() != kSeedPhotoCount + kImportPhotoCount) {
    return fail(QStringLiteral("catalog_snapshot returned an unexpected committed row count."));
  }
  const auto batch_drive_id = drive_id_for_root(batch_root);
  const auto batch_drive_root = QDir(managed_root).filePath(batch_drive_id);
  QSet<QString> committed_relative_paths;
  QSet<QString> committed_asset_ids;
  for (const auto& value : assets) {
    const auto asset = value.toObject();
    const auto location = asset.value(QStringLiteral("location")).toObject();
    if (location.value(QStringLiteral("drive_id")).toString() != batch_drive_id) continue;
    const auto root_path = QDir::cleanPath(location.value(QStringLiteral("root_path")).toString());
    const auto relative_path = location.value(QStringLiteral("relative_path")).toString();
    const auto asset_id = asset.value(QStringLiteral("asset_id")).toString();
    if (root_path != QDir::cleanPath(batch_drive_root) || relative_path.isEmpty() || asset_id.isEmpty() ||
        committed_relative_paths.contains(relative_path) || committed_asset_ids.contains(asset_id)) {
      return fail(QStringLiteral("Committed import rows contained an unexpected path, root or duplicate identity."));
    }
    committed_relative_paths.insert(relative_path);
    committed_asset_ids.insert(asset_id);
  }
  if (committed_relative_paths.size() != kImportPhotoCount) {
    return fail(QStringLiteral("The exact committed import set does not match the selected source files."));
  }
  QCryptographicHash committed_set_hash(QCryptographicHash::Sha256);
  QStringList ordered_paths = committed_relative_paths.values();
  std::sort(ordered_paths.begin(), ordered_paths.end());
  for (const auto& relative_path : ordered_paths) {
    const auto expected_path = QFileInfo(QDir(batch_root).filePath(relative_path)).canonicalFilePath();
    const auto managed_path = QDir(batch_drive_root).filePath(relative_path);
    const auto expected_hash = import_source_hashes.value(relative_path);
    if (expected_path.isEmpty() || expected_hash.isEmpty() ||
        sha256_file(expected_path) != expected_hash || sha256_file(managed_path) != expected_hash) {
      return fail(QStringLiteral("Managed import content did not match its immutable bundled photo source."));
    }
    committed_set_hash.addData(relative_path.toUtf8());
    committed_set_hash.addData("\n");
    committed_set_hash.addData(sha256_file(managed_path));
    committed_set_hash.addData("\n");
  }

  if (!observability::flush()) return fail(QStringLiteral("Could not flush workload observability."));
  QJsonArray thumbnail_events;
  QFile desktop_log(observability::desktopLogFilePath());
  if (desktop_log.open(QIODevice::ReadOnly)) {
    while (!desktop_log.atEnd()) {
      const auto event = QJsonDocument::fromJson(desktop_log.readLine()).object();
      if (event.value(QStringLiteral("event")).toString() != QStringLiteral("thumbnail.completed")) continue;
      const auto fields = event.value(QStringLiteral("fields")).toObject();
      const auto file_path = fields.value(QStringLiteral("file_path")).toString();
      if (!file_path.startsWith(seed_drive_root)) continue;
      thumbnail_events.append(QJsonObject{
          {QStringLiteral("assetId"), fields.value(QStringLiteral("asset_id"))},
          {QStringLiteral("success"), fields.value(QStringLiteral("success"))},
          {QStringLiteral("queueWaitMs"), fields.value(QStringLiteral("queue_wait_ms"))},
          {QStringLiteral("poolWaitMs"), fields.value(QStringLiteral("pool_wait_ms"))},
          {QStringLiteral("elapsedMs"), fields.value(QStringLiteral("elapsed_ms"))},
          {QStringLiteral("deliveryWaitMs"), fields.value(QStringLiteral("delivery_wait_ms"))},
          {QStringLiteral("cancellationReason"), fields.value(QStringLiteral("cancellation_reason"))},
      });
    }
  }
  if (thumbnail_events.isEmpty()) return fail(QStringLiteral("No production thumbnail decode completion was recorded."));

  QJsonObject receipt{
      {QStringLiteral("schema"), QStringLiteral("round10-real-import-mixed-workload:v1")},
      {QStringLiteral("qualification"), QStringLiteral("real committed managed import via CatalogAssetModel + JobRunner + import_catalog; private disposable PostgreSQL")},
      {QStringLiteral("sourceFixture"), QStringLiteral(PHOTO_EDITOR_ROUND10_REAL_PHOTO_FIXTURE)},
      {QStringLiteral("sourceFixtureSha256"), QString::fromLatin1(fixture_hash)},
      {QStringLiteral("importSourcePixelsSha256"), QString::fromLatin1(import_source_pixels_hash)},
      {QStringLiteral("importSourceManifestSha256"), QString::fromLatin1(import_source_manifest_sha256)},
      {QStringLiteral("seedCommittedPhotos"), kSeedPhotoCount},
      {QStringLiteral("importSelectedPhotos"), kImportPhotoCount},
      {QStringLiteral("importCommittedRows"), committed_relative_paths.size()},
      {QStringLiteral("importCommittedAssetIds"), committed_asset_ids.size()},
      {QStringLiteral("importCommittedSetSha256"), QString::fromLatin1(committed_set_hash.result().toHex())},
      {QStringLiteral("importResult"), import_result},
      {QStringLiteral("importJobId"), final_record->job_id},
      {QStringLiteral("importWorkerObservedJobId"), running_job_id},
      {QStringLiteral("preflightMs"), preflight_completed_ms},
      {QStringLiteral("importQueueWaitMs"), import_queue_wait_ms},
      {QStringLiteral("importRequestToRunningMs"), import_running_since_ms},
      {QStringLiteral("importRequestToCompletionMs"), import_worker_completed_ms},
      {QStringLiteral("importWorkerElapsedMs"), std::max<qint64>(0, import_worker_completed_ms - import_running_since_ms)},
      {QStringLiteral("mixedWorkloadDrainMs"), combined_workload_ms},
      {QStringLiteral("observedImportAttemptElapsedMs"), import_worker_active_ms},
      {QStringLiteral("importActiveSamples"), import_active_samples},
      {QStringLiteral("importThumbnailOverlapSamples"), import_thumbnail_overlap_samples},
      {QStringLiteral("importRenderExportOverlapSamples"), import_render_export_overlap_samples},
      {QStringLiteral("exportActiveSamples"), export_active_samples},
      {QStringLiteral("interactivePreviewOverlapSamples"), interactive_preview_overlap_samples},
      {QStringLiteral("tripleOverlapSamples"), triple_overlap_samples},
      {QStringLiteral("maximumActiveThumbnailWorkers"), maximum_active_thumbnails},
      {QStringLiteral("maximumPendingThumbnailWorkers"), maximum_pending_thumbnails},
      {QStringLiteral("productionThumbnailCompletions"), thumbnail_events.size()},
      {QStringLiteral("thumbnailEvents"), thumbnail_events},
      {QStringLiteral("changedRecipe"), QJsonObject{{QStringLiteral("exposure"), 0.75}}},
      {QStringLiteral("interactivePreviewSurface"), QStringLiteral("EditorPreviewController image sink; headless pixels only")},
      {QStringLiteral("outputLutCacheBeforeNeutralExport"), output_lut_cache_before_neutral_export},
      {QStringLiteral("outputLutCacheAfterNeutralExport"), output_lut_cache_after_neutral_export},
      {QStringLiteral("outputLutCacheBeforeEditorLoad"), output_lut_cache_before_editor_load},
      {QStringLiteral("outputLutCacheAfterEditorLoad"), output_lut_cache_after_editor_load},
      {QStringLiteral("outputLutCacheBeforeMixedWorkload"), output_lut_cache_before_mixed_workload},
      {QStringLiteral("outputLutCacheAfterMixedWorkload"), output_lut_cache_after_mixed_workload},
      {QStringLiteral("editorWarmMs"), editor_warm_ms},
      {QStringLiteral("editToPreviewMs"), edit_to_preview_ms},
      {QStringLiteral("interactiveRenderProfile"), EditorPreviewControllerTestAccess::render_profile(preview_controller, false)},
      {QStringLiteral("fullRenderProfile"), EditorPreviewControllerTestAccess::render_profile(preview_controller, true)},
      {QStringLiteral("previewStateProfile"), EditorPreviewControllerTestAccess::state_profile(preview_controller)},
      {QStringLiteral("editToPreviewMetric"), QStringLiteral("Edit request to full-render drain and changed image-sink publication; not first-feedback latency.")},
      {QStringLiteral("previewGenerationBeforeEdit"), preview_generation_before_edit},
      {QStringLiteral("previewGenerationAfterEdit"), preview_generation_after_edit},
      {QStringLiteral("editorExposure"), preview_controller.exposure()},
      {QStringLiteral("editorRenderGeneration"), EditorPreviewControllerTestAccess::render_generation(preview_controller)},
      {QStringLiteral("interactivePreviewPixelSha256"), QString::fromLatin1(pixel_sha256(preview_sink.edited))},
      {QStringLiteral("interactivePreviewChangedFromNeutral"), pixel_sha256(preview_sink.edited) != neutral_preview_hash},
      {QStringLiteral("renderSourceAssetId"), seed_asset_id},
      {QStringLiteral("renderSourcePath"), seed_file_path},
      {QStringLiteral("neutralRenderPixelSha256"), QString::fromLatin1(neutral_render_hash)},
      {QStringLiteral("editedRenderPixelSha256"), QString::fromLatin1(export_result.render_pixel_sha256)},
      {QStringLiteral("encodedPixelSha256"), QString::fromLatin1(export_result.output_pixel_sha256)},
      {QStringLiteral("encodedFileSha256"), QString::fromLatin1(export_result.output_file_sha256)},
      {QStringLiteral("outputBytes"), export_result.output_bytes},
      {QStringLiteral("renderMs"), export_result.render_ms},
      {QStringLiteral("renderPlanMs"), export_result.render_plan_ms},
      {QStringLiteral("encodeAndAtomicPublishMs"), export_result.encode_and_publish_ms},
      {QStringLiteral("productionRendererBackend"), export_result.backend},
      {QStringLiteral("rendererFallbackReason"), export_result.fallback_reason},
      {QStringLiteral("canonicalPlanError"), export_result.canonical_plan_error},
      {QStringLiteral("seedImportMs"), seed_import_ms},
      {QStringLiteral("realCameraRawPhotoSources"), 0},
      {QStringLiteral("rawPhotoQualification"), QStringLiteral("not-run-no-local-real-raw-input")},
  };
  std::cout << QJsonDocument(receipt).toJson(QJsonDocument::Compact).toStdString() << '\n';
  return 0;
#endif
}
