#include "catalog_asset_model.h"

#include "app_bootstrap.h"
#include "catalog_helper_payload.h"
#include "catalog_recipe_projection.h"
#include "linked_image_source.h"
#include "render_plan_store.h"
#include "desktop_recipe_persistence_service.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "observability.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "timeline_asset_store.h"
#include "worker_queue_registry.h"
#include "catalog_metadata_io.h"
#include "photo_editor/pipeline/cpu_work_scheduler.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

#include <QCoreApplication>
#include <QColorSpace>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPainter>
#include <QPointer>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QThreadPool>
#include <QTemporaryFile>
#include <QThread>
#include <QTimeZone>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>
#include "blank_canvas_document.h"

namespace photo_editor::desktop {

struct ImportFileFact final {
  QString path;
  QString pair_key;
  qint64 bytes = 0;
  bool within_root = false;
  bool is_file = false;
  bool supported = false;
  bool readable = false;
  bool raw = false;
  bool jpeg = false;
  bool video = false;
  bool audio = false;
};

struct ImportPreflightScan final {
  QString root;
  QStringList paths;
  QVector<ImportFileFact> files;
  QElapsedTimer age;
  bool root_available = false;
};

struct ImportPreflightRequest final {
  QString root;
  QStringList paths;
  QString mode;
  QString duplicates;
  QString organization;
  QString raw_jpeg;
  QString library_root;
  QString backup_root;
  QString project;
  QString prepared_task_id;
  QVector<CatalogAssetItem> assets;
  std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
  std::shared_ptr<std::atomic<int>> scanned = std::make_shared<std::atomic<int>>(0);
  std::shared_ptr<const ImportPreflightScan> scan;
  quint64 generation = 0;
  bool admission = false;
};

struct CatalogAssetModel::PreparedBlankCanvasOperation final {
  QString task_id;
  QString document_id;
  QString import_job_id;
  QString source_path;
  QString source_sha256;
  QString status = QStringLiteral("generating");
  QString terminal_import_status;
  QString asset_id;
  QString error;
  std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
  bool import_started = false;
};

namespace {

constexpr int kThumbnailEdge = 768;
constexpr int kThumbnailDecodeCeiling = 1280;
constexpr int kMaxConcurrentThumbnailTasks = 4;
constexpr int kMaxConcurrentMediaDurationTasks = 2;
constexpr int kVisibleThumbnailPrefetchRows = 18;
constexpr qint64 kImportSourceRefreshCacheWindowMs = 30000;
constexpr int kCatalogStateStartTimeoutMs = 5000;
constexpr int kCatalogStateFinishTimeoutMs = 15000;

QThreadPool& catalog_state_thread_pool() {
  static QThreadPool pool;
  static const bool configured = []() {
    pool.setMaxThreadCount(2);
    pool.setExpiryTimeout(30000);
    return true;
  }();
  Q_UNUSED(configured)
  return pool;
}

QThreadPool& import_job_poll_thread_pool() {
  static QThreadPool pool;
  static const bool configured = []() {
    pool.setMaxThreadCount(1);
    pool.setExpiryTimeout(30000);
    return true;
  }();
  Q_UNUSED(configured)
  return pool;
}

bool run_catalog_state_process(
    const QString& program,
    const QStringList& arguments,
    const QString& working_directory,
    const QProcessEnvironment& environment,
    const std::atomic_bool* cancellation_requested,
    QString* error_message) {
  QProcess process;
  process.setWorkingDirectory(working_directory);
  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(program, arguments);

  QElapsedTimer timer;
  timer.start();
  auto started = process.state() == QProcess::Running;
  while (process.state() == QProcess::Starting &&
         timer.elapsed() < kCatalogStateStartTimeoutMs) {
    if (cancellation_requested != nullptr &&
        cancellation_requested->load(std::memory_order_relaxed)) {
      process.kill();
      process.waitForFinished(500);
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Catalog state update canceled.");
      }
      return false;
    }
    started = process.waitForStarted(25) || process.state() == QProcess::Running;
  }
  if (process.state() == QProcess::Starting) {
    process.kill();
    process.waitForFinished(500);
  }
  started = started || process.state() == QProcess::Running;
  if (!started) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to start catalog state helper.");
    }
    return false;
  }

  timer.restart();
  while (process.state() != QProcess::NotRunning &&
         timer.elapsed() < kCatalogStateFinishTimeoutMs) {
    if (cancellation_requested != nullptr &&
        cancellation_requested->load(std::memory_order_relaxed)) {
      process.terminate();
      if (!process.waitForFinished(500)) {
        process.kill();
        process.waitForFinished(500);
      }
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Catalog state update canceled.");
      }
      return false;
    }
    process.waitForFinished(25);
  }

  if (process.state() != QProcess::NotRunning) {
    process.terminate();
    if (!process.waitForFinished(500)) {
      process.kill();
      process.waitForFinished(500);
    }
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_message != nullptr) {
      const auto output =
          QString::fromUtf8(process.readAllStandardOutput()).trimmed();
      *error_message = output.isEmpty()
                           ? QStringLiteral("Catalog state update failed.")
                           : output;
    }
    return false;
  }
  return true;
}

QString sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(chunk);
  }
  return QString::fromLatin1(hash.result().toHex());
}

bool run_catalog_state_json_command(
    const QString& command,
    const QJsonObject& request,
    QJsonObject* response,
    QString* error_message) {
  QTemporaryFile request_file(QDir::tempPath() + QStringLiteral("/photo-editor-agent-catalog-XXXXXX.json"));
  if (!request_file.open()) {
    if (error_message != nullptr) *error_message = QStringLiteral("Could not stage the catalog receipt.");
    return false;
  }
  const auto request_bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
  if (request_file.write(request_bytes) != request_bytes.size() || !request_file.flush()) {
    if (error_message != nullptr) *error_message = request_file.errorString();
    return false;
  }
  const auto request_path = request_file.fileName();
  const auto result_path = request_path + QStringLiteral(".result.json");
  QFile::remove(result_path);
  const auto cleanup = qScopeGuard([&] { QFile::remove(result_path); });
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    if (error_message != nullptr) *error_message = environment_error;
    return false;
  }
  const auto arguments = AppBootstrap::helperArguments(
      QStringLiteral("catalog_state"), {command, request_path, result_path});
  if (!run_catalog_state_process(
          AppBootstrap::helperProgram(QStringLiteral("catalog_state")), arguments,
          QDir::currentPath(), environment, nullptr, error_message)) return false;
  if (response == nullptr) return true;
  QFile result_file(result_path);
  if (!result_file.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) *error_message = QStringLiteral("The catalog helper did not return a receipt.");
    return false;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(result_file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    if (error_message != nullptr) *error_message = QStringLiteral("The catalog helper returned an invalid receipt.");
    return false;
  }
  *response = document.object();
  return true;
}

QString agent_task_recipe_receipt_key(const QString& task_id, const QString& asset_id) {
  return task_id + QChar(0x1f) + asset_id;
}

QString agent_task_recipe_terminal_key(
    const QString& task_id, const QString& asset_id, const QString& revision_id) {
  return task_id + QChar(0x1f) + asset_id + QChar(0x1f) + revision_id;
}

QJsonObject normalized_agent_recipe_fields(const QVariantMap& exact_recipes, bool* valid) {
  if (valid != nullptr) *valid = false;
  if (exact_recipes.size() != 2 || !exact_recipes.contains(QStringLiteral("legacyRecipe")) ||
      !exact_recipes.contains(QStringLiteral("canonicalRecipe"))) return {};
  const auto fields = QJsonObject::fromVariantMap(exact_recipes);
  const auto legacy = fields.value(QStringLiteral("legacyRecipe"));
  const auto canonical = fields.value(QStringLiteral("canonicalRecipe"));
  if ((!legacy.isNull() && !legacy.isObject()) ||
      (!canonical.isNull() && !canonical.isObject())) return {};
  if (canonical.isObject()) {
    const auto object = canonical.toObject();
    if (object.size() != 3 ||
        !object.value(QStringLiteral("recipe_payload")).isString() ||
        object.value(QStringLiteral("recipe_payload")).toString().trimmed().isEmpty() ||
        !object.value(QStringLiteral("cache_key")).isString() ||
        object.value(QStringLiteral("cache_key")).toString().trimmed().isEmpty() ||
        !object.value(QStringLiteral("provenance_payload")).isString() ||
        object.value(QStringLiteral("provenance_payload")).toString().trimmed().isEmpty()) return {};
  }
  if (valid != nullptr) *valid = true;
  return {{QStringLiteral("legacy_recipe"), legacy},
          {QStringLiteral("canonical_recipe"), canonical}};
}

bool agent_recipe_snapshot_matches_receipt(const QJsonObject& snapshot, const QJsonObject& receipt) {
  const auto recipes = receipt.value(QStringLiteral("recipes")).toObject();
  const auto actual_canonical = snapshot.value(QStringLiteral("canonical_recipe"));
  const auto expected_canonical = recipes.value(QStringLiteral("canonical_recipe"));
  bool canonical_matches = actual_canonical.isNull() && expected_canonical.isNull();
  if (actual_canonical.isObject() && expected_canonical.isObject()) {
    const auto actual = actual_canonical.toObject();
    const auto expected = expected_canonical.toObject();
    const auto actual_provenance_text = actual.value(QStringLiteral("provenance_payload")).toString();
    const auto expected_provenance_text = expected.value(QStringLiteral("provenance_payload")).toString();
    QJsonParseError actual_parse;
    QJsonParseError expected_parse;
    const auto actual_provenance = QJsonDocument::fromJson(
        actual_provenance_text.toUtf8(), &actual_parse);
    const auto expected_provenance = QJsonDocument::fromJson(
        expected_provenance_text.toUtf8(), &expected_parse);
    canonical_matches = actual.size() == 3 && expected.size() == 3 &&
        actual.value(QStringLiteral("recipe_payload")) == expected.value(QStringLiteral("recipe_payload")) &&
        actual.value(QStringLiteral("cache_key")) == expected.value(QStringLiteral("cache_key")) &&
        actual_parse.error == QJsonParseError::NoError && actual_provenance.isObject() &&
        expected_parse.error == QJsonParseError::NoError && expected_provenance.isObject() &&
        actual_provenance.object() == expected_provenance.object();
  }
  return snapshot.value(QStringLiteral("asset_id")) == receipt.value(QStringLiteral("assetId")) &&
      snapshot.value(QStringLiteral("source_path")) == receipt.value(QStringLiteral("sourcePath")) &&
      snapshot.value(QStringLiteral("source_sha256")) == receipt.value(QStringLiteral("sourceSha256")) &&
      snapshot.value(QStringLiteral("catalog_row_state")) == receipt.value(QStringLiteral("catalogRowState")) &&
      snapshot.value(QStringLiteral("legacy_recipe")) == recipes.value(QStringLiteral("legacy_recipe")) &&
      canonical_matches;
}

const QStringList& supported_video_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("m4v"),
      QStringLiteral("mov"),
      QStringLiteral("mp4"),
      QStringLiteral("mpeg"),
      QStringLiteral("mpg"),
      QStringLiteral("webm"),
  };
  return kExtensions;
}

const QStringList& supported_audio_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("aac"),
      QStringLiteral("aif"),
      QStringLiteral("aiff"),
      QStringLiteral("flac"),
      QStringLiteral("m4a"),
      QStringLiteral("mp3"),
      QStringLiteral("ogg"),
      QStringLiteral("opus"),
      QStringLiteral("wav"),
  };
  return kExtensions;
}

const QStringList& supported_import_extensions() {
  static const QStringList kExtensions = {
      QStringLiteral("3fr"),
      QStringLiteral("arw"),
      QStringLiteral("avif"),
      QStringLiteral("bmp"),
      QStringLiteral("cr2"),
      QStringLiteral("cr3"),
      QStringLiteral("dng"),
      QStringLiteral("erf"),
      QStringLiteral("gif"),
      QStringLiteral("heic"),
      QStringLiteral("heif"),
      QStringLiteral("iiq"),
      QStringLiteral("jpeg"),
      QStringLiteral("jpg"),
      QStringLiteral("kdc"),
      QStringLiteral("mef"),
      QStringLiteral("mos"),
      QStringLiteral("nef"),
      QStringLiteral("nrw"),
      QStringLiteral("orf"),
      QStringLiteral("pef"),
      QStringLiteral("png"),
      QStringLiteral("raf"),
      QStringLiteral("raw"),
      QStringLiteral("rw2"),
      QStringLiteral("sr2"),
      QStringLiteral("srf"),
      QStringLiteral("tif"),
      QStringLiteral("tiff"),
      QStringLiteral("m4v"),
      QStringLiteral("mov"),
      QStringLiteral("mp4"),
      QStringLiteral("mpeg"),
      QStringLiteral("mpg"),
      QStringLiteral("webp"),
      QStringLiteral("webm"),
      QStringLiteral("aac"),
      QStringLiteral("aif"),
      QStringLiteral("aiff"),
      QStringLiteral("flac"),
      QStringLiteral("m4a"),
      QStringLiteral("mp3"),
      QStringLiteral("ogg"),
      QStringLiteral("opus"),
      QStringLiteral("wav"),
      QStringLiteral("x3f"),
  };
  return kExtensions;
}

QStringList supported_import_name_filters() {
  QStringList filters;
  filters.reserve(supported_import_extensions().size() * 2);
  for (const auto& extension : supported_import_extensions()) {
    const auto lowercase_filter = QStringLiteral("*.%1").arg(extension);
    filters.push_back(lowercase_filter);

    const auto uppercase_extension = extension.toUpper();
    if (uppercase_extension != extension) {
      const auto uppercase_filter = QStringLiteral("*.%1").arg(uppercase_extension);
      if (!filters.contains(uppercase_filter)) {
        filters.push_back(uppercase_filter);
      }
    }
  }
  return filters;
}

QString normalize_local_path(const QString& path) {
  const auto trimmed = path.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

Qt::CaseSensitivity local_path_case_sensitivity() {
#ifdef Q_OS_WIN
  return Qt::CaseInsensitive;
#else
  return Qt::CaseSensitive;
#endif
}

bool local_path_is_within_root(const QString& path, const QString& root) {
  const auto normalized_path = QDir::fromNativeSeparators(QDir::cleanPath(path));
  const auto normalized_root = QDir::fromNativeSeparators(QDir::cleanPath(root));
  if (normalized_path.isEmpty() || normalized_root.isEmpty()) return false;
  const auto sensitivity = local_path_case_sensitivity();
  if (normalized_path.compare(normalized_root, sensitivity) == 0) return true;
  const auto prefix = normalized_root.endsWith(QLatin1Char('/'))
                          ? normalized_root
                          : normalized_root + QLatin1Char('/');
  return normalized_path.startsWith(prefix, sensitivity);
}

QString stable_prepared_catalog_source_path(const QString& path) {
  const auto normalized = normalize_local_path(path);
  if (normalized.isEmpty()) return {};

  QFileInfo target(normalized);
  if (target.exists()) {
    const auto canonical = target.canonicalFilePath();
    return canonical.isEmpty() ? normalized : QDir::cleanPath(canonical);
  }

  // The prepared row's absence is journaled before the worker creates its
  // source. Resolve the nearest existing ancestor now so an alias such as
  // /var/folders and its later canonical path /private/var/folders produce
  // the same identity both before and after creation.
  QStringList missing_components;
  while (!target.exists()) {
    const auto component = target.fileName();
    if (!component.isEmpty()) missing_components.prepend(component);
    const auto current_path = target.absoluteFilePath();
    const auto parent_path = target.dir().absolutePath();
    if (parent_path.isEmpty() || parent_path == current_path) return normalized;
    target = QFileInfo(parent_path);
  }

  auto stable_path = target.canonicalFilePath();
  if (stable_path.isEmpty()) return normalized;
  for (const auto& component : missing_components)
    stable_path = QDir(stable_path).filePath(component);
  return QDir::cleanPath(stable_path);
}

bool has_supported_import_extension(const QString& file_path) {
  const auto suffix = QFileInfo(file_path).suffix().toLower();
  return supported_import_extensions().contains(suffix);
}

QString source_label_for_path(const QString& path, const QString& fallback) {
  const QFileInfo info(path);
  if (!info.fileName().isEmpty()) {
    return info.fileName();
  }
  return fallback;
}

QVariantList build_import_sources() {
  QVariantList sources;
  QSet<QString> seen_paths;

  const auto append_source =
      [&sources, &seen_paths](
          const QString& label,
          const QString& path,
          const QString& kind,
          const QString& subtitle) {
        const auto normalized_path = normalize_local_path(path);
        if (normalized_path.isEmpty() || seen_paths.contains(normalized_path)) {
          return;
        }

        seen_paths.insert(normalized_path);

        QVariantMap source;
        source.insert(QStringLiteral("label"), label);
        source.insert(QStringLiteral("path"), normalized_path);
        source.insert(QStringLiteral("url"), QUrl::fromLocalFile(normalized_path).toString());
        source.insert(QStringLiteral("kind"), kind);
        source.insert(QStringLiteral("subtitle"), subtitle);
        sources.push_back(source);
      };

  append_source(QStringLiteral("Home"), QDir::homePath(), QStringLiteral("home"), QStringLiteral("User home"));

  const auto desktop_locations = QStandardPaths::standardLocations(QStandardPaths::DesktopLocation);
  if (!desktop_locations.isEmpty()) {
    append_source(
        QStringLiteral("Desktop"),
        desktop_locations.first(),
        QStringLiteral("desktop"),
        QStringLiteral("Quick access"));
  }

  const auto picture_locations = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
  if (!picture_locations.isEmpty()) {
    append_source(
        QStringLiteral("Pictures"),
        picture_locations.first(),
        QStringLiteral("pictures"),
        QStringLiteral("Photo library"));
  }

  const auto download_locations = QStandardPaths::standardLocations(QStandardPaths::DownloadLocation);
  if (!download_locations.isEmpty()) {
    append_source(
        QStringLiteral("Downloads"),
        download_locations.first(),
        QStringLiteral("downloads"),
        QStringLiteral("Recent files"));
  }

  for (const auto& storage : QStorageInfo::mountedVolumes()) {
    if (!storage.isValid() || !storage.isReady()) {
      continue;
    }

    const auto root_path = normalize_local_path(storage.rootPath());
    if (root_path.isEmpty()) {
      continue;
    }

    auto label = storage.displayName().trimmed();
    if (label.isEmpty()) {
      label = source_label_for_path(root_path, root_path == QStringLiteral("/") ? QStringLiteral("Filesystem") : root_path);
    }

    const auto kind =
        root_path.startsWith(QStringLiteral("/Volumes/")) ? QStringLiteral("volume") : QStringLiteral("drive");
    auto subtitle = QString::fromUtf8(storage.fileSystemType()).trimmed();
    if (subtitle.isEmpty()) {
      subtitle = root_path == QStringLiteral("/") ? QStringLiteral("System root") : QStringLiteral("Mounted storage");
    }

    append_source(label, root_path, kind, subtitle);
  }

  append_source(
      QStringLiteral("Filesystem"),
      QStringLiteral("/"),
      QStringLiteral("system"),
      QStringLiteral("Browse all mounted paths"));

  return sources;
}

QString format_date(const QDate& date, const QString& fallback) {
  if (!date.isValid()) {
    return fallback;
  }

  return QLocale().toString(date, QStringLiteral("MMM d, yyyy"));
}

QString format_datetime(const QDateTime& datetime, const QString& fallback) {
  if (!datetime.isValid()) {
    return fallback;
  }

  return QLocale().toString(datetime, QStringLiteral("MMM d, yyyy  h:mm AP"));
}

QDateTime parse_job_timestamp_utc(const QString& timestamp) {
  const auto trimmed = timestamp.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  auto parsed = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(trimmed, Qt::ISODate);
  }
  if (!parsed.isValid()) {
    return {};
  }

  return parsed.toUTC();
}

bool is_import_job_active(const PersistentJobRecord& record) {
  return record.job_kind == QStringLiteral("catalog_import") &&
         (record.status == QStringLiteral("Queued") ||
          record.status == QStringLiteral("Running") ||
          record.status == QStringLiteral("Pausing") ||
          record.status == QStringLiteral("Canceling"));
}

bool is_import_job_finished(const PersistentJobRecord& record) {
  return record.job_kind == QStringLiteral("catalog_import") &&
         (record.status == QStringLiteral("Completed") ||
          record.status == QStringLiteral("Failed") ||
          record.status == QStringLiteral("Canceled"));
}

QDate parse_capture_date(const QJsonObject& asset) {
  const auto year = asset.value("capture_year").toInt();
  const auto month = asset.value("capture_month").toInt();
  const auto day = asset.value("capture_day").toInt();

  const QDate capture_date(year, month, day);
  return capture_date.isValid() ? capture_date : QDate{};
}

QString format_capture_date(const QJsonObject& asset, QDate* capture_date_value) {
  const auto capture_date = parse_capture_date(asset);
  if (capture_date_value != nullptr) {
    *capture_date_value = capture_date;
  }

  return format_date(capture_date, QStringLiteral("Unknown capture date"));
}

QString format_iso_label(const QJsonObject& asset) {
  const auto iso = asset.value("iso").toInt();
  if (iso <= 0) {
    return {};
  }

  return QStringLiteral("ISO %1").arg(iso);
}

QString format_aperture_label(const QJsonObject& asset) {
  const auto aperture = asset.value("aperture").toDouble();
  if (aperture <= 0.0) {
    return {};
  }

  return QStringLiteral("f/%1").arg(QString::number(aperture, 'g', 3));
}

QDateTime parse_imported_at(const QJsonObject& asset) {
  const auto imported_at = asset.value("imported_at").toString().trimmed();
  if (imported_at.isEmpty()) {
    return {};
  }

  auto parsed = QDateTime::fromString(imported_at, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(imported_at, Qt::ISODate);
  }
  if (!parsed.isValid()) {
    return {};
  }

  if (parsed.timeSpec() == Qt::UTC || parsed.timeSpec() == Qt::OffsetFromUTC) {
    parsed = parsed.toLocalTime();
  }

  return parsed;
}

QString format_camera_label(const QJsonObject& asset) {
  const auto make = asset.value("camera_make").toString().trimmed();
  const auto model = asset.value("camera_model").toString().trimmed();

  if (make.isEmpty() && model.isEmpty()) {
    return QStringLiteral("Unknown camera");
  }

  if (make.isEmpty()) {
    return model;
  }

  if (model.isEmpty()) {
    return make;
  }

  return QStringLiteral("%1 %2").arg(make, model);
}

QString format_location_path(const QJsonObject& asset) {
  const auto location = asset.value("location").toObject();
  const auto root_path = location.value("root_path").toString();
  const auto relative_path = location.value("relative_path").toString();

  if (root_path.isEmpty()) {
    return relative_path;
  }

  if (relative_path.isEmpty()) {
    return root_path;
  }

  return QStringLiteral("%1/%2").arg(root_path, relative_path);
}

QString filesystem_path(const QJsonObject& asset) {
  const auto location = asset.value("location").toObject();
  const auto root_path = location.value("root_path").toString();
  const auto relative_path = location.value("relative_path").toString();

  if (root_path.isEmpty()) {
    return relative_path;
  }

  return QDir(root_path).filePath(relative_path);
}

bool preview_supported(const QString& file_path) {
  const QFileInfo file_info(file_path);
  const auto suffix = file_info.suffix().toLower().toUtf8();
  return QImageReader::supportedImageFormats().contains(suffix) || raw_image_supported(file_path) ||
         native_image_supported(file_path);
}

QString media_type_for_path(const QString& file_path) {
  const auto suffix = QFileInfo(file_path).suffix().toLower();
  if (supported_video_extensions().contains(suffix)) {
    return QStringLiteral("video");
  }
  if (supported_audio_extensions().contains(suffix)) {
    return QStringLiteral("audio");
  }
  return QStringLiteral("photo");
}

QSet<QString> board_owned_canvas_sources(const QString& app_data_root, const QString& boards_override) {
  QStringList data_roots{app_data_root};
  const QFileInfo root_info(app_data_root);
  // Older direct launches and run-native used adjacent application-data roots.
  if (root_info.fileName() == QStringLiteral("PhotoEditor")) {
    const auto nested = QDir(app_data_root).filePath(QStringLiteral("PhotoEditor"));
    if (QFileInfo(nested).isDir()) data_roots.append(nested);
    if (QFileInfo(root_info.absolutePath()).fileName() == QStringLiteral("PhotoEditor"))
      data_roots.append(root_info.absolutePath());
  }
  QStringList board_roots;
  QStringList managed_roots;
  for (const auto& root : data_roots) {
    board_roots.append(QDir(root).filePath(QStringLiteral("carousels")));
    managed_roots.append(normalize_local_path(QDir(root).filePath(QStringLiteral("canvas-documents"))) + QLatin1Char('/'));
    managed_roots.append(normalize_local_path(QDir(root).filePath(QStringLiteral("carousels/sources"))) + QLatin1Char('/'));
  }
  if (!boards_override.isEmpty()) {
    board_roots.append(boards_override);
    managed_roots.append(normalize_local_path(QDir(boards_override).filePath(QStringLiteral("sources"))) + QLatin1Char('/'));
  }
  QSet<QString> result;
  const auto collect_sources = [&](const auto& self, const QJsonValue& value) -> void {
    if (value.isArray()) {
      for (const auto& entry : value.toArray()) self(self, entry);
    } else if (value.isObject()) {
      const auto object = value.toObject();
      for (auto entry = object.begin(); entry != object.end(); ++entry) {
        if (entry.key() == QStringLiteral("filePath") || entry.key() == QStringLiteral("file_path")) {
          const auto path = normalize_local_path(entry.value().toString());
          if (std::any_of(managed_roots.cbegin(), managed_roots.cend(),
                          [&](const QString& root) { return path.startsWith(root); })) result.insert(path);
        } else if (entry.value().isObject() || entry.value().isArray()) {
          self(self, entry.value());
        }
      }
    }
  };
  for (const auto& root : board_roots) {
    QFile projection(QDir(root).filePath(QStringLiteral("gallery-sources.json")));
    if (projection.open(QIODevice::ReadOnly))
      collect_sources(collect_sources, QJsonDocument::fromJson(projection.readAll()).object());
    QFile index(QDir(root).filePath(QStringLiteral("index.json")));
    if (!index.open(QIODevice::ReadOnly)) continue;
    const auto entries = QJsonDocument::fromJson(index.readAll()).object().value(QStringLiteral("documents")).toArray();
    for (const auto& entry : entries) {
      const auto id = entry.toObject().value(QStringLiteral("documentId")).toString();
      if (QUuid(id).isNull()) continue;
      QFile document(QDir(root).filePath(QStringLiteral("documents/%1.json").arg(id)));
      if (document.open(QIODevice::ReadOnly)) collect_sources(collect_sources, QJsonDocument::fromJson(document.readAll()).object());
    }
  }
  return result;
}

QString thumbnail_cache_root() {
  const auto cache_root = AppBootstrap::cacheLocation();
  const auto base = cache_root.isEmpty() ? QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache")) : cache_root;
  return QDir(base).filePath(QStringLiteral("thumbnails"));
}

QString thumbnail_cache_path_for_key(const QString& key) {
  const auto hash =
      QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QDir(thumbnail_cache_root()).filePath(QStringLiteral("%1.jpg").arg(QString::fromLatin1(hash)));
}

struct ThumbnailCacheStats final {
  qint64 bytes = 0;
  int entries = 0;
  qint64 elapsed_ms = 0;
};

ThumbnailCacheStats sample_thumbnail_cache_stats() {
  QElapsedTimer timer;
  timer.start();

  ThumbnailCacheStats stats;
  const QDir cache_dir(thumbnail_cache_root());
  if (!cache_dir.exists()) {
    stats.elapsed_ms = timer.elapsed();
    return stats;
  }

  QDirIterator iterator(
      cache_dir.absolutePath(),
      QStringList{QStringLiteral("*.jpg")},
      QDir::Files,
      QDirIterator::NoIteratorFlags);
  while (iterator.hasNext()) {
    iterator.next();
    const QFileInfo info = iterator.fileInfo();
    if (!info.exists()) {
      continue;
    }
    stats.bytes += qMax<qint64>(0, info.size());
    ++stats.entries;
  }
  stats.elapsed_ms = timer.elapsed();
  return stats;
}

QString versioned_local_file_url(const QString& file_path) {
  QFileInfo file_info(file_path);
  if (!file_info.exists()) {
    return {};
  }

  auto url = QUrl::fromLocalFile(file_info.absoluteFilePath());
  QUrlQuery query;
  query.addQueryItem(
      QStringLiteral("v"),
      QStringLiteral("%1-%2")
          .arg(file_info.lastModified().toUTC().toMSecsSinceEpoch())
          .arg(file_info.size()));
  url.setQuery(query);
  return url.toString();
}

QString thumbnail_adjustment_identity(
    const QString& adjustment_payload) {
  const auto canonical_patch =
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
  return QString::fromLatin1(
      QCryptographicHash::hash(
          identity_bytes,
          QCryptographicHash::Sha256)
          .toHex());
}

struct CatalogThumbnailInputs final {
  QHash<QString, QString> revisions;
  bool reusable = true;
};

void collect_thumbnail_file_dependencies(const QJsonValue& value, QHash<QString, QString>* revisions) {
  if (value.isArray()) {
    for (const auto& child : value.toArray()) collect_thumbnail_file_dependencies(child, revisions);
  } else if (value.isObject()) {
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
      if (it.value().isString() && (it.key().endsWith(QStringLiteral("path"), Qt::CaseInsensitive) ||
                                  it.key().endsWith(QStringLiteral("uri"), Qt::CaseInsensitive))) {
        const auto text = it.value().toString();
        const QUrl url(text);
        const auto path = url.isLocalFile() ? url.toLocalFile() : text;
        if (!path.isEmpty() && (QFileInfo(path).isAbsolute() || url.scheme().isEmpty())) {
          const auto absolute_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
          revisions->insert(absolute_path, linked_image_file_revision(absolute_path));
        }
      } else {
        collect_thumbnail_file_dependencies(it.value(), revisions);
      }
    }
  }
}

CatalogThumbnailInputs catalog_thumbnail_inputs(const QString& path, const QString& recipe) {
  CatalogThumbnailInputs inputs;
  inputs.revisions.insert(path, linked_image_file_revision(path));
  if (!recipe.trimmed().isEmpty()) {
    const auto decoded = parse_adjustment_payload_or_legacy_recipe(recipe);
    if (!decoded.ok()) inputs.reusable = false;
    else {
      const auto& state = *decoded.state;
      // Registry mappings have no revision notification; render these requests
      // again instead of treating matching recipe bytes as current provenance.
      inputs.reusable = !state.input_icc_profile_asset.configured() &&
          !state.preview_icc_profile_asset.configured() && !state.proof_icc_profile_asset.configured() &&
          !state.export_icc_profile_asset.configured() && !state.camera_dcp_profile_asset.configured();
    }
    collect_thumbnail_file_dependencies(QJsonDocument::fromJson(recipe.toUtf8()).object(), &inputs.revisions);
  }
  return inputs;
}

bool thumbnail_revisions_current(const QHash<QString, QString>& revisions) {
  if (revisions.isEmpty()) return false;
  for (auto it = revisions.cbegin(); it != revisions.cend(); ++it)
    if (linked_image_file_revision(it.key()) != it.value()) return false;
  return true;
}

QHash<QString, QString> validated_thumbnail_reuse_tokens(
    const QVector<CatalogAssetItem>& items, const std::shared_ptr<std::atomic_bool>& cancellation = {}) {
  QHash<QString, QString> tokens;
  for (const auto& item : items) {
    if (cancellation && cancellation->load(std::memory_order_relaxed)) return {};
    const QUrl output_url(item.thumbnail_source);
    const QFileInfo output(output_url.toLocalFile());
    if (item.search_preview_current && output_url.isLocalFile() && output.isFile() && output.isReadable() &&
        !item.thumbnail_provenance_token.isEmpty() && thumbnail_revisions_current(item.thumbnail_dependency_revisions))
      tokens.insert(item.asset_id, item.thumbnail_provenance_token);
  }
  return tokens;
}

QString thumbnail_dependency_key(const QHash<QString, QString>& revisions) {
  QJsonObject object;
  for (auto it = revisions.cbegin(); it != revisions.cend(); ++it) object.insert(it.key(), it.value());
  return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex());
}

QString video_thumbnail_cache_key(const QString& path) {
  return QStringLiteral("video-v3:%1:%2:%3").arg(path, linked_image_file_revision(path)).arg(kThumbnailEdge);
}

QString photo_thumbnail_cache_key(
    const QString& file_path,
    const QString& adjustment_payload,
    const CatalogThumbnailInputs& inputs) {
  return QStringLiteral("photo-v7-display-text-color-policy:%1:%2:%3:%4:%5")
      .arg(file_path, thumbnail_adjustment_identity(adjustment_payload), thumbnail_dependency_key(inputs.revisions))
      .arg(kThumbnailEdge).arg(kThumbnailDecodeCeiling);
}

QString thumbnail_adjustment_hash(
    const QString& adjustment_payload) {
  return QStringLiteral("sha256:%1")
      .arg(
          thumbnail_adjustment_identity(
              adjustment_payload));
}

bool cached_thumbnail_is_readable(const QString& cache_path) {
  QImageReader reader(cache_path);
  if (!reader.canRead()) {
    return false;
  }
  const auto size = reader.size();
  return size.isValid() && size.width() > 0 && size.height() > 0;
}

QImage raw_thumbnail_error_placeholder(const QFileInfo& source_info, const QSize& source_size) {
  Q_UNUSED(source_info);
  const QSize canvas_size = source_size.isValid()
                                ? source_size.scaled(kThumbnailEdge, kThumbnailEdge, Qt::KeepAspectRatio)
                                : QSize(360, 240);
  QImage image(canvas_size.expandedTo(QSize(280, 180)), QImage::Format_RGB32);
  image.fill(QColor(QStringLiteral("#10161A")));

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(QStringLiteral("#182227")));
  painter.drawRoundedRect(image.rect().adjusted(6, 6, -6, -6), 10, 10);
  painter.setBrush(QColor(QStringLiteral("#27E0C0")));
  painter.drawRoundedRect(QRectF(18.0, 18.0, 74.0, 28.0), 6.0, 6.0);
  painter.setBrush(QColor(QStringLiteral("#10161A")));
  painter.drawRect(QRectF(31.0, 27.0, 8.0, 10.0));
  painter.drawRect(QRectF(45.0, 27.0, 8.0, 10.0));
  painter.drawRect(QRectF(59.0, 27.0, 8.0, 10.0));
  painter.drawRect(QRectF(73.0, 27.0, 8.0, 10.0));

  painter.setBrush(QColor(QStringLiteral("#2B3A40")));
  const auto detail_rect = QRectF(18.0, 62.0, std::max(120, image.width() - 36), 10.0);
  painter.drawRoundedRect(detail_rect, 4.0, 4.0);
  painter.drawRoundedRect(detail_rect.translated(0.0, 20.0), 4.0, 4.0);
  painter.setBrush(QColor(QStringLiteral("#27E0C0")));
  painter.drawRoundedRect(QRectF(18.0, image.height() - 28.0, image.width() - 36.0, 4.0), 2.0, 2.0);
  painter.end();
  return image;
}

RawDecodeSettings thumbnail_raw_decode_settings_for_state(const RenderEditState& state) {
  return RawDecodeSettings{
      .input_transform_enabled = state.raw_input_transform_enabled,
      .input_color_profile = state.input_color_profile,
      .demosaic_quality = state.raw_demosaic_quality,
      .exposure = state.raw_exposure,
      .hot_dead_pixel_cleanup_amount = state.raw_hot_dead_pixel_cleanup,
      .false_color_suppression_amount = state.raw_false_color_suppression,
      .highlight_recovery_enabled = state.raw_highlight_recovery,
      .white_balance_override_enabled = state.raw_white_balance_override,
      .white_balance_temperature = state.raw_white_balance_temperature,
      .white_balance_tint = state.raw_white_balance_tint,
  };
}

QString cached_video_poster(const QString& file_path) {
  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto cache_path = thumbnail_cache_path_for_key(video_thumbnail_cache_key(file_path));
  const QFileInfo cache_info(cache_path);
  if (!cache_info.exists() || cache_info.lastModified() < source_info.lastModified()) {
    return {};
  }

  return versioned_local_file_url(cache_path);
}

QThreadPool& media_duration_thread_pool() {
  static QThreadPool pool;
  static const bool configured = []() {
    pool.setMaxThreadCount(kMaxConcurrentMediaDurationTasks);
    pool.setExpiryTimeout(30000);
    return true;
  }();
  Q_UNUSED(configured)
  return pool;
}

// Called only on a worker. Both filesystem access and the external process can
// block on removable/network storage, so even a cache hit stays off the UI thread.
double probe_video_duration_seconds(
    const QString& file_path,
    const QString& cache_root,
    const std::shared_ptr<std::atomic_bool>& cancellation) {
  const auto cancelled = [&]() { return cancellation->load(std::memory_order_relaxed); };
  if (cancelled()) return 0.0;
  const QFileInfo source(file_path);
  if (!source.isFile()) return 0.0;
  const auto revision = QJsonObject{
      {QStringLiteral("path"), source.canonicalFilePath()},
      {QStringLiteral("size"), QString::number(source.size())},
      {QStringLiteral("modified"), QString::number(source.lastModified().toMSecsSinceEpoch())},
      {QStringLiteral("created"), QString::number(source.birthTime().toMSecsSinceEpoch())}};
  const auto cache_key = QString::fromLatin1(QCryptographicHash::hash(
      source.absoluteFilePath().toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto cache_path = QDir(cache_root).filePath(cache_key + QStringLiteral(".json"));
  QFile cache(cache_path);
  if (cache.open(QIODevice::ReadOnly) && cache.size() <= 4096) {
    const auto record = QJsonDocument::fromJson(cache.readAll()).object();
    const auto seconds = record.value(QStringLiteral("seconds")).toDouble();
    if (record.value(QStringLiteral("revision")).toObject() == revision &&
        std::isfinite(seconds) && seconds > 0.0 && seconds < std::numeric_limits<int>::max()) {
      return seconds;
    }
  }
  cache.close();
  if (cancelled()) return 0.0;
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffprobe.isEmpty()) {
    return 0.0;
  }

  QProcess process;
  process.start(
      ffprobe,
      {
          QStringLiteral("-v"),
          QStringLiteral("error"),
          QStringLiteral("-show_entries"),
          QStringLiteral("format=duration"),
          QStringLiteral("-of"),
          QStringLiteral("default=noprint_wrappers=1:nokey=1"),
          file_path,
      });
  QElapsedTimer timer;
  timer.start();
  while (!process.waitForFinished(50) && process.state() != QProcess::NotRunning) {
    if (cancelled() || timer.elapsed() >= 5000) {
      process.kill();
      process.waitForFinished(1000);
      return 0.0;
    }
  }
  if (cancelled() || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return 0.0;
  }

  bool okay = false;
  const auto seconds = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toDouble(&okay);
  const QFileInfo current(file_path);
  if (!okay || !std::isfinite(seconds) || seconds <= 0.0 ||
      seconds >= std::numeric_limits<int>::max() || !current.isFile() ||
      current.canonicalFilePath() != source.canonicalFilePath() ||
      current.size() != source.size() || current.lastModified() != source.lastModified() ||
      current.birthTime() != source.birthTime()) {
    return 0.0;
  }
  if (!cancelled() && QDir().mkpath(cache_root)) {
    QSaveFile output(cache_path);
    if (output.open(QIODevice::WriteOnly)) {
      const auto bytes = QJsonDocument(QJsonObject{
          {QStringLiteral("revision"), revision}, {QStringLiteral("seconds"), seconds}})
                             .toJson(QJsonDocument::Compact);
      if (output.write(bytes) == bytes.size()) output.commit();
    }
  }
  return seconds;
}

QString format_duration_text(double seconds) {
  if (seconds <= 0.0) {
    return {};
  }

  const auto rounded = qMax(0, static_cast<int>(std::round(seconds)));
  const auto hours = rounded / 3600;
  const auto minutes = (rounded % 3600) / 60;
  const auto remaining_seconds = rounded % 60;

  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remaining_seconds, 2, 10, QLatin1Char('0'));
  }

  return QStringLiteral("%1:%2")
      .arg(minutes)
      .arg(remaining_seconds, 2, 10, QLatin1Char('0'));
}

QSize scaled_size_for_edge(const QSize& source_size, int max_edge) {
  if (!source_size.isValid() || max_edge <= 0) {
    return {};
  }
  return source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio);
}

QString ensure_thumbnail(
    const QString& file_path,
    const QString& adjustment_payload,
    const std::atomic_bool* cancellation_requested = nullptr) {
  const auto canceled = [cancellation_requested]() {
    return cancellation_requested != nullptr &&
           cancellation_requested->load(std::memory_order_relaxed);
  };
  if (canceled()) {
    return {};
  }
  if (!preview_supported(file_path)) {
    return {};
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto inputs = catalog_thumbnail_inputs(file_path, adjustment_payload);
  const auto cache_path = thumbnail_cache_path_for_key(
      photo_thumbnail_cache_key(file_path, adjustment_payload, inputs));
  const QFileInfo cache_info(cache_path);

  if (inputs.reusable && cache_info.exists() && cache_info.lastModified() >= source_info.lastModified()) {
    if (cached_thumbnail_is_readable(cache_path)) {
      return versioned_local_file_url(cache_path);
    }
    QFile::remove(cache_path);
  }

  if (canceled()) {
    return {};
  }

  QDir().mkpath(thumbnail_cache_root());

  const auto decoded_adjustments =
      parse_adjustment_payload_or_legacy_recipe(
          adjustment_payload);
  const auto state =
      decoded_adjustments.ok()
          ? *decoded_adjustments.state
          : EditRecipeState{};
  const auto decode_edge = std::min(kThumbnailDecodeCeiling, kThumbnailEdge + 256);
  const auto raw_supported = raw_image_supported(file_path);
  auto image = QImage{};
  auto decoded_metadata = photo_editor::pipeline::DecodedImageMetadata{};
  auto raw_placeholder = false;
  // Do not use the camera-rendered JPEG embedded in a RAW as the editor's
  // fallback. Its manufacturer picture style and tone curve do not match the
  // application's RAW pipeline, which caused an immediate color jump when the
  // color-managed preview arrived. The bounded RAW decode below is cacheable
  // and follows the same recipe as the editor render.
  if (image.isNull()) {
    if (!decode_source_image(
            file_path,
            &image,
            decode_edge,
            state,
            &decoded_metadata)) {
      if (raw_supported) {
        auto draft_state = state;
        draft_state.raw_demosaic_quality = QStringLiteral("Preview");
        if (!decode_raw_image(file_path, &image, decode_edge, thumbnail_raw_decode_settings_for_state(draft_state))) {
          observability::logInfo(
              photoEditorCatalogLog(),
              QStringLiteral("thumbnail.raw_decode_failed"),
              {
                  {QStringLiteral("file_path"), file_path},
                  {QStringLiteral("fallback"), QStringLiteral("error_placeholder")},
              });
          image = raw_thumbnail_error_placeholder(source_info, raw_image_size(file_path));
          raw_placeholder = true;
        }
      } else {
        return {};
      }
    }
  }

  if (image.isNull() || canceled()) {
    return {};
  }

  if (!raw_placeholder) {
    const auto source_identity =
        QStringLiteral(
            "catalog-thumbnail:%1:%2:%3")
            .arg(
                source_info.absoluteFilePath())
            .arg(source_info.size())
            .arg(
                source_info.lastModified()
                    .toUTC()
                    .toMSecsSinceEpoch());
    const auto admission =
        RenderPlanStore::shared().admit(
            source_identity,
            file_path,
            state,
            image.size(),
            photo_editor::pipeline::
                RenderPurpose::Thumbnail,
            photo_editor::pipeline::
                DisplayRenderingTarget::
                    SrgbSimulation,
            std::nullopt,
            &decoded_metadata,
            thumbnail_adjustment_hash(
                adjustment_payload));
    const auto canonical_required =
        normalized_photographic_rendering_model_name(
            state.photographic_rendering_model) ==
        QStringLiteral("aces2-ocio-v1");
    if (canonical_required &&
        !admission.admitted) {
      observability::logInfo(
          photoEditorCatalogLog(),
          QStringLiteral(
              "thumbnail.canonical_plan_failed"),
          {
              {QStringLiteral("file_path"),
               file_path},
              {QStringLiteral("error"),
               admission.error},
          });
      return {};
    }
    image = render_output_image(
        image,
        state,
        QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32,
        &decoded_metadata,
        nullptr,
        RenderMaskOptions{
            .rebuild_editable_masks_at_output_resolution = true},
        photo_editor::pipeline::RemovalQuality::Final,
        RenderOutputPolicy::DisplayReferred,
        admission.admitted
            ? &*admission.plan
            : nullptr,
        photo_editor::pipeline::CancellationToken{
            .cancellation_requested = cancellation_requested});
    if (image.isNull() || canceled()) {
      return {};
    }
    image = apply_crop(image, state);
    if (image.isNull()) {
      return {};
    }
  }

  const auto target_size = scaled_size_for_edge(image.size(), kThumbnailEdge);

  if (target_size.isValid() &&
      (image.width() > target_size.width() || image.height() > target_size.height())) {
    image = image.scaled(target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }

  if (canceled()) {
    return {};
  }

  QSaveFile cache_file(cache_path);
  if (!cache_file.open(QIODevice::WriteOnly)) {
    return {};
  }
  {
    QImageWriter writer(&cache_file, "jpg");
    writer.setQuality(90);
    writer.setOptimizedWrite(true);
    if (!writer.write(image)) {
      return {};
    }
  }
  if (!cache_file.commit()) {
    return {};
  }

  return versioned_local_file_url(cache_path);
}

QString ensure_video_poster(const QString& file_path) {
  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return {};
  }

  const auto cache_path = thumbnail_cache_path_for_key(video_thumbnail_cache_key(file_path));
  const QFileInfo cache_info(cache_path);
  if (cache_info.exists() && cache_info.lastModified() >= source_info.lastModified()) {
    return versioned_local_file_url(cache_path);
  }

  QDir().mkpath(thumbnail_cache_root());

  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"),
          QStringLiteral("-ss"),
          QStringLiteral("0.250"),
          QStringLiteral("-i"),
          file_path,
          QStringLiteral("-frames:v"),
          QStringLiteral("1"),
          QStringLiteral("-vf"),
          QStringLiteral("scale=%1:%1:force_original_aspect_ratio=decrease").arg(kThumbnailEdge),
          cache_path,
      });
  if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }

  return QFileInfo::exists(cache_path) ? versioned_local_file_url(cache_path) : QString{};
}

QString thumbnail_source_for_path(
    const QString& file_path,
    const QString& adjustment_payload = {},
    const std::atomic_bool* cancellation_requested = nullptr) {
  if (cancellation_requested != nullptr &&
      cancellation_requested->load(std::memory_order_relaxed)) {
    return {};
  }
  const auto media_type = media_type_for_path(file_path);
  if (media_type == QStringLiteral("video")) {
    return ensure_video_poster(file_path);
  }
  if (media_type == QStringLiteral("audio")) {
    return {};
  }
  return ensure_thumbnail(
      file_path,
      adjustment_payload,
      cancellation_requested);
}

void clear_thumbnail_source_for_item(CatalogAssetItem* item) {
  if (item == nullptr || item->media_type == QStringLiteral("timeline")) {
    return;
  }

  item->thumbnail_source.clear();
  item->search_preview_current = false;
  item->thumbnail_provenance_token.clear();
  item->thumbnail_dependency_revisions.clear();
  item->thumbnail_stale_detected = false;
  item->thumbnail_stale_mismatch = false;
  item->thumbnail_stale_detection_reason.clear();
  item->thumbnail_stale_previous_recipe_hash.clear();
  item->thumbnail_stale_current_recipe_hash.clear();
  item->thumbnail_stale_detection_signature.clear();
}

void copy_thumbnail_runtime_fields(
    const CatalogAssetItem& source,
    CatalogAssetItem* target) {
  if (target == nullptr) {
    return;
  }
  target->search_preview_current = source.search_preview_current;
  target->thumbnail_provenance_token = source.thumbnail_provenance_token;
  target->thumbnail_dependency_revisions = source.thumbnail_dependency_revisions;
  target->thumbnail_source = source.thumbnail_source;
  target->thumbnail_stale_detected = source.thumbnail_stale_detected;
  target->thumbnail_stale_mismatch = source.thumbnail_stale_mismatch;
  target->thumbnail_stale_detection_reason = source.thumbnail_stale_detection_reason;
  target->thumbnail_stale_previous_recipe_hash = source.thumbnail_stale_previous_recipe_hash;
  target->thumbnail_stale_current_recipe_hash = source.thumbnail_stale_current_recipe_hash;
  target->thumbnail_stale_detection_signature = source.thumbnail_stale_detection_signature;
}

QDateTime parse_iso_datetime(const QString& value) {
  auto parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(value, Qt::ISODate);
  }
  if (parsed.isValid() && (parsed.timeSpec() == Qt::UTC || parsed.timeSpec() == Qt::OffsetFromUTC)) {
    parsed = parsed.toLocalTime();
  }
  return parsed;
}

QVector<CatalogAssetItem> load_timeline_asset_items() {
  QVector<CatalogAssetItem> items;
  const auto records = loadTimelineAssetRecords();
  items.reserve(records.size());

  for (const auto& record : records) {
    if (record.timeline_id.isEmpty()) {
      continue;
    }

    CatalogAssetItem item;
    item.asset_id = record.asset_id.isEmpty() ? QStringLiteral("timeline:%1").arg(record.timeline_id) : record.asset_id;
    item.camera_label = record.name.isEmpty() ? QStringLiteral("Untitled timeline") : record.name;
    item.capture_date = QStringLiteral("Timeline");
    item.lens_model = QStringLiteral("Saved timeline");
    item.location_path = record.document_path;
    item.file_path = record.render_path;
    item.drive_id = QStringLiteral("timeline-library");
    item.import_mode = QStringLiteral("timeline");
    item.imported_at_value = parse_iso_datetime(record.updated_at);
    item.imported_at = format_datetime(item.imported_at_value, QStringLiteral("Unknown save time"));
    item.project_summary = QStringLiteral("No projects");
    item.media_type = QStringLiteral("timeline");
    item.duration_text = format_duration_text(record.duration_seconds);
    item.duration_seconds = record.duration_seconds;
    item.timeline_document_path = record.document_path;
    item.online = QFileInfo(record.document_path).exists() && QFileInfo(record.render_path).exists();

    if (!record.poster_path.isEmpty() && QFileInfo::exists(record.poster_path)) {
      item.thumbnail_source = versioned_local_file_url(record.poster_path);
    } else if (!record.render_path.isEmpty()) {
      item.thumbnail_source = cached_video_poster(record.render_path);
    }

    items.push_back(item);
  }

  return items;
}

QString derive_drive_id(const QString& folder_path) {
  const auto digest = QCryptographicHash::hash(folder_path.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QStringLiteral("drive-%1").arg(QString::fromLatin1(digest.left(12)));
}

QString derive_drive_name(const QString& folder_path) {
  const QFileInfo info(folder_path);
  if (!info.fileName().isEmpty()) {
    return info.fileName();
  }

  return folder_path;
}

bool is_video_import_path(const QString& file_path) {
  return supported_video_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

bool is_audio_import_path(const QString& file_path) {
  return supported_audio_extensions().contains(QFileInfo(file_path).suffix().toLower());
}

bool is_raw_import_path(const QString& file_path) {
  static const QSet<QString> kRawExtensions = {
      QStringLiteral("3fr"), QStringLiteral("arw"), QStringLiteral("cr2"),
      QStringLiteral("cr3"), QStringLiteral("dng"), QStringLiteral("erf"),
      QStringLiteral("iiq"), QStringLiteral("kdc"), QStringLiteral("mef"),
      QStringLiteral("mos"), QStringLiteral("nef"), QStringLiteral("nrw"),
      QStringLiteral("orf"), QStringLiteral("pef"), QStringLiteral("raf"),
      QStringLiteral("raw"), QStringLiteral("rw2"), QStringLiteral("sr2"),
      QStringLiteral("srf"), QStringLiteral("x3f"),
  };
  return kRawExtensions.contains(QFileInfo(file_path).suffix().toLower());
}

bool is_jpeg_import_path(const QString& file_path) {
  const auto extension = QFileInfo(file_path).suffix().toLower();
  return extension == QStringLiteral("jpg") || extension == QStringLiteral("jpeg");
}

QString formatted_import_size(qint64 bytes) {
  return QLocale().formattedDataSize(qMax<qint64>(0, bytes), 1, QLocale::DataSizeTraditionalFormat);
}

QVariantMap import_issue(
    const QString& code,
    const QString& severity,
    const QString& title,
    const QString& detail) {
  return QVariantMap{
      {QStringLiteral("code"), code},
      {QStringLiteral("severity"), severity},
      {QStringLiteral("title"), title},
      {QStringLiteral("detail"), detail},
  };
}

QString project_summary_for_names(const QStringList& project_names) {
  if (project_names.isEmpty()) {
    return QStringLiteral("No projects");
  }

  if (project_names.size() == 1) {
    return project_names.first();
  }

  return QStringLiteral("%1  +%2").arg(project_names.first()).arg(project_names.size() - 1);
}

QString burst_stem_for_path(const QString& file_path) {
  const auto base_name = QFileInfo(file_path).completeBaseName().toLower();
  if (base_name.isEmpty()) {
    return {};
  }

  static const QRegularExpression kTrailingDigits(QStringLiteral("([_-]?\\d+)$"));
  auto stem = base_name;
  stem.remove(kTrailingDigits);
  stem = stem.trimmed();
  if (stem.length() < 3) {
    stem = base_name;
  }
  return stem;
}

QString intelligence_date_key_for_item(const CatalogAssetItem& item) {
  if (item.capture_date_value.isValid()) {
    return item.capture_date_value.toString(Qt::ISODate);
  }
  if (item.imported_at_value.isValid()) {
    return item.imported_at_value.date().toString(Qt::ISODate);
  }
  return {};
}

QString duplicate_stem_for_path(const QString& file_path) {
  auto stem = QFileInfo(file_path).completeBaseName().toLower().trimmed();
  if (stem.isEmpty()) {
    return {};
  }

  static const QRegularExpression kCopyIndex(QStringLiteral("\\s*\\(\\d+\\)$"));
  static const QRegularExpression kVariantTokens(
      QStringLiteral("([\\s._-]+(?:copy|edited?|final|export|retouch|v\\d+))+$"));
  stem.remove(kCopyIndex);
  stem.remove(kVariantTokens);
  stem = stem.trimmed();
  return stem.isEmpty() ? QFileInfo(file_path).completeBaseName().toLower() : stem;
}

QString burst_group_key_for_item(const CatalogAssetItem& item) {
  if (item.media_type != QStringLiteral("photo")) {
    return {};
  }

  QString date_key;
  if (item.capture_date_value.isValid()) {
    date_key = item.capture_date_value.toString(Qt::ISODate);
  } else if (item.imported_at_value.isValid()) {
    date_key = item.imported_at_value.date().toString(Qt::ISODate);
  }

  const auto burst_stem = burst_stem_for_path(item.file_path);
  if (date_key.isEmpty() || burst_stem.isEmpty()) {
    return {};
  }

  return QStringLiteral("%1|%2|%3")
      .arg(item.camera_label.trimmed().toLower(), date_key, burst_stem);
}

QString duplicate_group_key_for_item(const CatalogAssetItem& item) {
  if (item.media_type != QStringLiteral("photo")) {
    return {};
  }

  const auto date_key = intelligence_date_key_for_item(item);
  const auto duplicate_stem = duplicate_stem_for_path(item.file_path);
  if (date_key.isEmpty() || duplicate_stem.isEmpty()) {
    return {};
  }

  return QStringLiteral("%1|%2|%3")
      .arg(item.camera_label.trimmed().toLower(), date_key, duplicate_stem);
}

QString sync_group_key_for_item(const CatalogAssetItem& item) {
  if (item.media_type != QStringLiteral("photo")) {
    return {};
  }

  const auto date_key = intelligence_date_key_for_item(item);
  if (date_key.isEmpty()) {
    return {};
  }

  const auto lens_key = item.lens_model.trimmed().isEmpty() ? QStringLiteral("unknown-lens")
                                                            : item.lens_model.trimmed().toLower();
  return QStringLiteral("%1|%2|%3")
      .arg(item.camera_label.trimmed().toLower(), lens_key, date_key);
}

QStringList normalized_path_segments(const QString& path) {
  return QDir::cleanPath(path).split(QChar('/'), Qt::SkipEmptyParts);
}

QString cull_status_for_state(const CullingStateRecord& state) {
  if (state.rejected) {
    return QStringLiteral("rejected");
  }
  if (state.picked) {
    return QStringLiteral("picked");
  }
  return QStringLiteral("unmarked");
}

CullingStateRecord normalized_culling_state(CullingStateRecord state) {
  state.rating = std::clamp(state.rating, 0, 5);
  state.color_label = state.color_label.trimmed().toLower();
  const QSet<QString> allowed_color_labels = {
      QStringLiteral("red"),
      QStringLiteral("yellow"),
      QStringLiteral("green"),
      QStringLiteral("blue"),
      QStringLiteral("purple"),
  };
  if (!allowed_color_labels.contains(state.color_label)) {
    state.color_label.clear();
  }
  if (state.rejected) {
    state.picked = false;
  }
  return state;
}

QJsonObject culling_state_json(const CullingStateRecord& state) {
  return {{QStringLiteral("rating"), state.rating}, {QStringLiteral("picked"), state.picked},
      {QStringLiteral("rejected"), state.rejected}, {QStringLiteral("color_label"), state.color_label}};
}

bool culling_state_is_default(const CullingStateRecord& state) {
  return state.rating == 0 && !state.picked && !state.rejected && state.color_label.isEmpty();
}

bool culling_states_equal(const CullingStateRecord& first, const CullingStateRecord& second) {
  return first.rating == second.rating && first.picked == second.picked && first.rejected == second.rejected &&
         first.color_label == second.color_label;
}

void apply_culling_state_to_item(CatalogAssetItem* item, const CullingStateRecord& state) {
  if (item == nullptr) {
    return;
  }

  item->rating = state.rating;
  item->picked = state.picked;
  item->rejected = state.rejected;
  item->color_label = state.color_label;
  item->cull_status = cull_status_for_state(state);
}

void apply_relinked_path_to_item(CatalogAssetItem* item, const QString& file_path) {
  if (item == nullptr || item->media_type == QStringLiteral("timeline")) {
    return;
  }

  const auto normalized_path = normalize_local_path(file_path);
  if (normalized_path.isEmpty()) {
    return;
  }

  item->file_path = normalized_path;
  item->location_path = normalized_path;
  item->media_type = media_type_for_path(normalized_path);
  clear_thumbnail_source_for_item(item);

  const auto parent_path = QFileInfo(normalized_path).absolutePath();
  if (!parent_path.isEmpty()) {
    item->drive_id = derive_drive_id(parent_path);
  }

  item->duration_seconds = 0.0;
  item->duration_text.clear();
}

bool refresh_online_state_for_item(CatalogAssetItem* item) {
  if (item == nullptr) {
    return false;
  }

  auto next_online = false;
  if (item->media_type == QStringLiteral("timeline")) {
    next_online =
        QFileInfo(item->timeline_document_path).exists() && QFileInfo(item->file_path).exists();
  } else {
    const QFileInfo source_info(item->file_path);
    next_online = source_info.exists() && source_info.isFile() && source_info.isReadable();
  }

  const auto changed = item->online != next_online;
  item->online = next_online;
  return changed;
}

int relink_candidate_score(const CatalogAssetItem& item, const QString& candidate_path) {
  const QFileInfo original_info(item.file_path);
  const QFileInfo candidate_info(candidate_path);
  if (candidate_info.fileName().compare(original_info.fileName(), Qt::CaseInsensitive) != 0) {
    return std::numeric_limits<int>::min();
  }

  auto score = 100;
  if (candidate_info.suffix().compare(original_info.suffix(), Qt::CaseInsensitive) == 0) {
    score += 20;
  }
  if (candidate_info.completeBaseName().compare(original_info.completeBaseName(), Qt::CaseInsensitive) == 0) {
    score += 15;
  }
  if (media_type_for_path(candidate_path) == item.media_type) {
    score += 10;
  }

  const auto original_segments = normalized_path_segments(item.file_path);
  const auto candidate_segments = normalized_path_segments(candidate_path);
  const auto segment_count = std::min(original_segments.size(), candidate_segments.size());
  for (auto offset = 1; offset <= segment_count; ++offset) {
    if (original_segments.at(original_segments.size() - offset)
            .compare(candidate_segments.at(candidate_segments.size() - offset), Qt::CaseInsensitive)
        != 0) {
      break;
    }
    score += 10;
  }

  return score;
}

QString best_relink_candidate(
    const CatalogAssetItem& item,
    const QHash<QString, QStringList>& candidates_by_name) {
  const auto key = QFileInfo(item.file_path).fileName().toLower();
  if (key.isEmpty()) {
    return {};
  }

  const auto candidates = candidates_by_name.value(key);
  if (candidates.isEmpty()) {
    return {};
  }

  auto best_score = std::numeric_limits<int>::min();
  QString best_candidate;
  for (const auto& candidate : candidates) {
    const auto score = relink_candidate_score(item, candidate);
    if (score > best_score ||
        (score == best_score && !candidate.isEmpty() &&
         (best_candidate.isEmpty() || candidate.length() < best_candidate.length()))) {
      best_score = score;
      best_candidate = candidate;
    }
  }

  return best_candidate;
}

CatalogLibraryState parse_library_state(
    const QJsonObject& root, const std::shared_ptr<std::atomic_bool>& cancellation) {
  CatalogLibraryState result;
  const auto has_projects = root.contains(QStringLiteral("projects"));
  const auto has_review_states = root.contains(QStringLiteral("review_states"));
  const auto has_relinked_paths = root.contains(QStringLiteral("relinked_paths"));
  result.present = has_projects || has_review_states || has_relinked_paths || root.contains(QStringLiteral("asset_metadata"));
  const auto metadata = root.value(QStringLiteral("asset_metadata")).toObject();
  for (auto it = metadata.begin(); it != metadata.end(); ++it) {
    if (it.value().isObject()) result.asset_metadata.insert(it.key(), it.value().toObject().toVariantMap());
  }
  if (!result.present) return result;

  const auto project_array = root.value(QStringLiteral("projects")).toArray();
  for (const auto& project_value : project_array) {
    if (cancellation->load(std::memory_order_relaxed)) return {};
    if (!project_value.isObject()) {
      continue;
    }

    const auto project_object = project_value.toObject();
    ProjectRecord project;
    project.name = project_object.value(QStringLiteral("name")).toString().simplified();
    project.created_at = project_object.value(QStringLiteral("created_at")).toString().trimmed();
    if (project.created_at.isEmpty()) {
      project.created_at = project_object.value(QStringLiteral("createdAt")).toString().trimmed();
    }
    const auto asset_ids = project_object.value(QStringLiteral("asset_ids")).toArray();
    for (const auto& asset_id_value : asset_ids) {
      if (cancellation->load(std::memory_order_relaxed)) return {};
      const auto asset_id = asset_id_value.toString().trimmed();
      if (!asset_id.isEmpty()) {
        project.asset_ids.insert(asset_id);
      }
    }

    if (!project.name.isEmpty()) {
      result.projects.push_back(std::move(project));
    }
  }

  const auto culling_object = root.value(QStringLiteral("review_states")).toObject();
  for (auto iterator = culling_object.begin(); iterator != culling_object.end(); ++iterator) {
    if (cancellation->load(std::memory_order_relaxed)) return {};
    const auto asset_id = iterator.key().trimmed();
    if (asset_id.isEmpty() || !iterator.value().isObject()) {
      continue;
    }

    const auto value = iterator.value().toObject();
    const auto state = normalized_culling_state(
        {
            .rating = value.value(QStringLiteral("rating")).toInt(),
            .picked = value.value(QStringLiteral("picked")).toBool(),
            .rejected = value.value(QStringLiteral("rejected")).toBool(),
            .color_label = value.value(QStringLiteral("color_label")).toString(),
        });
    if (!culling_state_is_default(state)) {
      result.review_states.insert(asset_id, state);
    }
  }

  const auto relinked_paths_object = root.value(QStringLiteral("relinked_paths")).toObject();
  for (auto iterator = relinked_paths_object.begin(); iterator != relinked_paths_object.end(); ++iterator) {
    if (cancellation->load(std::memory_order_relaxed)) return {};
    const auto asset_id = iterator.key().trimmed();
    const auto file_path = normalize_local_path(iterator.value().toString());
    if (asset_id.isEmpty() || file_path.isEmpty()) {
      continue;
    }
    result.relinked_paths.insert(asset_id, file_path);
  }
  return result;
}

QVector<CatalogAssetItem> parse_assets(
    const QJsonArray& asset_array, const QString& recipe_asset_root,
    const std::shared_ptr<std::atomic_bool>& cancellation) {
  QVector<CatalogAssetItem> items;
  if (cancellation->load(std::memory_order_relaxed)) return items;
  items.resize(asset_array.size());
  // Detach once before workers write disjoint elements. Calling non-const
  // QVector accessors concurrently could otherwise race implicit sharing.
  auto* prepared = items.data();

  const auto prepare_asset = [&](qsizetype index) {
    if (cancellation->load(std::memory_order_relaxed)) return;
    const auto asset = asset_array.at(index).toObject();
    const auto location = asset.value("location").toObject();

    CatalogAssetItem item;
    item.asset_id = asset.value("asset_id").toString();
    item.capture_date = format_capture_date(asset, &item.capture_date_value);
    item.capture_timestamp = QDateTime::fromString(asset.value("capture_timestamp").toString(), Qt::ISODateWithMs);
    item.camera_label = format_camera_label(asset);
    item.lens_model = asset.value("lens_model").toString().trimmed();
    item.iso_label = format_iso_label(asset);
    item.shutter_speed = asset.value("shutter_speed").toString().trimmed();
    item.aperture_label = format_aperture_label(asset);
    item.location_path = format_location_path(asset);
    item.file_path = filesystem_path(asset);
    item.media_type = media_type_for_path(item.file_path);
    if (item.media_type == QStringLiteral("video")) {
      item.camera_label = QFileInfo(item.file_path).completeBaseName();
      if (item.lens_model.isEmpty()) {
        item.lens_model = QStringLiteral("Video clip");
      }
    } else if (item.media_type == QStringLiteral("audio")) {
      item.camera_label = QFileInfo(item.file_path).completeBaseName();
      if (item.lens_model.isEmpty()) {
        item.lens_model = QStringLiteral("Audio clip");
      }
    }
    const auto recipe_projection =
        project_catalog_adjustments_for_desktop(
            asset.value(
                QStringLiteral("edit_recipe")),
            asset.value(
                QStringLiteral(
                    "canonical_edit_recipe_v13")),
            recipe_asset_root);
    if (recipe_projection.ok()) {
      item.adjustment_patch_json =
          recipe_projection.adjustment_patch_json;
    }
    item.imported_at_value = parse_imported_at(asset);
    item.imported_at = format_datetime(item.imported_at_value, QStringLiteral("Unknown import time"));
    item.drive_id = location.value("drive_id").toString();
    item.import_mode = asset.value("import_mode").toString();
    item.project_summary = QStringLiteral("No projects");
    item.online = location.value("online").toBool();
    prepared[index] = std::move(item);
  };
  if (asset_array.size() >= 64 && asset_array.size() <= std::numeric_limits<int>::max()) {
    // Reuse the bounded process-wide pool. Its synchronous join protects the
    // captured JSON/storage on cancellation and exceptions; nested callers run
    // inline. Completion order cannot change catalog ordering.
    // Bound each admission so queued interactive image work can run between
    // batches instead of waiting behind an entire large catalog.
    // The shared scheduler groups at least eight items per task: 32 records
    // therefore admit at most four tasks, leaving capacity for foreground work.
    constexpr qsizetype kPreparationBatchSize = 32;
    for (qsizetype first = 0; first < asset_array.size();) {
      if (cancellation->load(std::memory_order_relaxed)) return {};
      const auto count = std::min(kPreparationBatchSize, asset_array.size() - first);
      photo_editor::pipeline::run_cpu_parallel_for(static_cast<int>(count),
          [&](int offset) { prepare_asset(first + offset); });
      first += count;
    }
  } else {
    for (qsizetype index = 0; index < asset_array.size(); ++index) prepare_asset(index);
  }
  if (cancellation->load(std::memory_order_relaxed)) return {};
  return items;
}

}  // namespace

CatalogAssetModel::CatalogAssetModel(QObject* parent)
    : QAbstractListModel(parent),
      import_job_observation_started_at_(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)) {
  connect(&import_preflight_watcher_, &QFutureWatcher<QVariantMap>::finished,
          this, &CatalogAssetModel::handleImportPreflightFinished);
  connect(&import_job_poll_watcher_, &QFutureWatcher<QVector<PersistentJobRecord>>::finished,
          this, &CatalogAssetModel::handleImportJobPollFinished);
  import_preflight_progress_timer_.setInterval(100);
  connect(&import_preflight_progress_timer_, &QTimer::timeout, this, [this] {
    if (!active_import_preflight_ || !import_preflight_requested_) return;
    const auto count = active_import_preflight_->scanned->load(std::memory_order_relaxed);
    if (count == import_preflight_scanned_count_) return;
    import_preflight_scanned_count_ = count;
    emit importPreflightChanged();
  });
  loadProjects();
  loadProjectChanges();
  loadCullingChanges();
  initializeMetadataPersistence();
  initializeDuplicateAnalysis();
  rebuildImportSources();

  snapshot_process_.setProcessChannelMode(QProcess::SeparateChannels);
  connect(
      &snapshot_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleSnapshotFinished);
  connect(&snapshot_parse_watcher_, &QFutureWatcher<CatalogSnapshotResult>::finished,
          this, &CatalogAssetModel::handleSnapshotParsed);
  connect(&snapshot_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    // FailedToStart has no finished signal. Crashes are handled by finished.
    if (error != QProcess::FailedToStart || !snapshot_fetch_active_) return;
    snapshot_fetch_active_ = false;
    if (snapshot_fetch_generation_ != snapshot_request_generation_) {
      startSnapshotFetch();
      return;
    }
    setErrorMessage(snapshot_process_.errorString());
    setStatus(QStringLiteral("Catalog unavailable"));
    setLoading(false);
  });

  import_process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(
      &import_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleImportFinished);

  culling_persistence_process_.setProcessChannelMode(QProcess::SeparateChannels);
  culling_save_timer_.setSingleShot(true);
  culling_timeout_timer_.setSingleShot(true);
  connect(&culling_save_timer_, &QTimer::timeout, this, &CatalogAssetModel::startNextCullingPersistence);
  connect(&culling_timeout_timer_, &QTimer::timeout, this, [this]() { culling_persistence_process_.kill(); });
  connect(&culling_persistence_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) handleCullingPersistenceFinished(-1, QProcess::CrashExit);
  });
  // Drain diagnostics without retaining an unbounded child stderr buffer.
  connect(&culling_persistence_process_, &QProcess::readyReadStandardError, this,
          [this]() { (void)culling_persistence_process_.readAllStandardError(); });
  if (!pending_culling_persistence_.isEmpty()) culling_save_timer_.start(0);
  connect(
      &culling_persistence_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleCullingPersistenceFinished);
  projects_save_timer_.setSingleShot(true);
  projects_save_timer_.setInterval(25);
  connect(
      &projects_save_timer_,
      &QTimer::timeout,
      this,
      [this]() { saveProjects(); });

  recipe_persistence_process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(&recipe_persistence_process_, &QProcess::started, this, [this] {
    if (active_adjustment_patch_persistence_task_id_.isEmpty()) return;
    const auto accepted = agent_recipe_persistence_handler_
        ? agent_recipe_persistence_handler_(
              QStringLiteral("accepted"), active_adjustment_patch_persistence_context_)
        : QVariantMap{{QStringLiteral("ok"), false}};
    active_adjustment_patch_persistence_owner_accepted_ =
        accepted.value(QStringLiteral("ok")).toBool();
    if (!active_adjustment_patch_persistence_owner_accepted_) {
      failed_adjustment_patch_persistence_asset_ids_.insert(
          active_adjustment_patch_persistence_asset_id_);
      recipe_persistence_process_.kill();
    }
    emit agentTaskRecipePersistenceChanged(active_adjustment_patch_persistence_asset_id_);
  });
  connect(
      &recipe_persistence_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleRecipePersistenceFinished);
  connect(&recipe_persistence_process_, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) return;
    handleRecipePersistenceFinished(-1, QProcess::CrashExit);
  });

  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &CatalogAssetModel::pollActiveImportJob);
  connect(
      &timeline_assets_watcher_,
      &QFutureWatcher<QVector<CatalogAssetItem>>::finished,
      this,
      &CatalogAssetModel::handleTimelineAssetsLoaded);
  connect(&document_source_exclusions_watcher_, &QFutureWatcher<QSet<QString>>::finished, this, [this]() {
    const auto next = document_source_exclusions_watcher_.result();
    if (next != document_source_exclusions_) {
      document_source_exclusions_ = next;
      if (media_collection_filter_ == QStringLiteral("images")) applyFilters(selectedAssetId());
    }
    if (document_source_exclusions_refresh_pending_) {
      document_source_exclusions_refresh_pending_ = false;
      refreshDocumentSourceExclusions();
    }
  });

  thumbnail_watchers_.reserve(kMaxConcurrentThumbnailTasks);
  for (int index = 0; index < kMaxConcurrentThumbnailTasks; ++index) {
    auto watcher = std::make_unique<QFutureWatcher<ThumbnailTaskResult>>(this);
    connect(watcher.get(), &QFutureWatcher<ThumbnailTaskResult>::finished, this, &CatalogAssetModel::handleThumbnailFinished);
    thumbnail_watchers_.push_back(std::move(watcher));
  }
  for (int index = 0; index < kMaxConcurrentMediaDurationTasks; ++index) {
    auto watcher = std::make_unique<QFutureWatcher<CatalogMediaDurationResult>>(this);
    connect(watcher.get(), &QFutureWatcher<CatalogMediaDurationResult>::finished,
            this, &CatalogAssetModel::handleMediaDurationFinished);
    media_duration_watchers_.push_back(std::move(watcher));
  }
  connect(this, &CatalogAssetModel::selectedAssetChanged,
          this, &CatalogAssetModel::prioritizeMediaDurationTasks);
  media_duration_notification_timer_.setSingleShot(true);
  media_duration_notification_timer_.setInterval(100);
  connect(&media_duration_notification_timer_, &QTimer::timeout,
          this, &CatalogAssetModel::mediaDurationsChanged);
  connect(&delete_watcher_, &QFutureWatcher<DeleteAssetsTaskResult>::finished, this, &CatalogAssetModel::handleDeleteFinished);
  connect(&project_change_watcher_, &QFutureWatcher<QVariantMap>::finished, this, [this]() {
    const auto name = active_project_change_;
    const auto result = project_change_watcher_.result();
    active_project_change_.clear();
    if (result.value(QStringLiteral("success")).toBool()) {
      failed_project_changes_.remove(name);
      setStatus(QStringLiteral("Saved project '%1'.").arg(name));
    } else {
      failed_project_changes_.insert(name);
      setStatus(QStringLiteral("Project '%1' is saved locally. Refresh the catalog to retry syncing.").arg(name));
      setErrorMessage(result.value(QStringLiteral("error")).toString());
    }
    startNextProjectChange();
  });
  QTimer::singleShot(0, this, &CatalogAssetModel::startNextProjectChange);
}

CatalogAssetModel::~CatalogAssetModel() {
  cancelDuplicateAnalysis();
  duplicate_analysis_watcher_.waitForFinished();
  if (prepared_blank_canvas_operation_)
    prepared_blank_canvas_operation_->cancelled->store(true, std::memory_order_relaxed);
  if (prepared_blank_canvas_watcher_ != nullptr) prepared_blank_canvas_watcher_->waitForFinished();
  if (project_change_cancellation_) {
    project_change_cancellation_->store(true, std::memory_order_relaxed);
  }
  if (active_import_preflight_) active_import_preflight_->cancelled->store(true, std::memory_order_relaxed);
  if (pending_import_preflight_) pending_import_preflight_->cancelled->store(true, std::memory_order_relaxed);

  if (snapshot_parse_cancellation_) {
    snapshot_parse_cancellation_->store(true, std::memory_order_relaxed);
  }
  if (media_duration_cancellation_) {
    media_duration_cancellation_->store(true, std::memory_order_relaxed);
  }
  ++delete_request_generation_;
  if (delete_cancellation_ != nullptr) {
    delete_cancellation_->store(true, std::memory_order_relaxed);
  }
  const auto stop_process = [this](QProcess& process) {
    QObject::disconnect(&process, nullptr, this, nullptr);
    if (process.state() == QProcess::NotRunning) {
      return;
    }
    process.terminate();
    if (!process.waitForFinished(500)) {
      process.kill();
      process.waitForFinished(500);
    }
  };
  stop_process(snapshot_process_);
  stop_process(metadata_process_);
  metadata_file_watcher_.waitForFinished();
  stop_process(import_process_);
  if (projects_save_timer_.isActive()) {
    projects_save_timer_.stop();
    saveProjects();
  }
  culling_save_timer_.stop();
  culling_timeout_timer_.stop();
  if (culling_outbox_dirty_) (void)saveCullingChanges(pending_culling_persistence_);
  stop_process(culling_persistence_process_);

  pending_adjustment_patch_persistence_.clear();
  stop_process(recipe_persistence_process_);
}

int CatalogAssetModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(assets_.size());
}

QVariant CatalogAssetModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= assets_.size()) {
    return {};
  }

  const auto& item = assets_.at(index.row());

  switch (role) {
    case AssetIdRole:
      return item.asset_id;
    case CaptureDateRole:
      return item.capture_date;
    case CameraLabelRole:
      return item.camera_label;
    case LensModelRole:
      return item.lens_model;
    case LocationPathRole:
      return item.location_path;
    case FilePathRole:
      return item.file_path;
    case ThumbnailSourceRole:
      return item.thumbnail_source;
    case ThumbnailStaleDetectedRole:
      return item.thumbnail_stale_detected;
    case ThumbnailStaleMismatchRole:
      return item.thumbnail_stale_mismatch;
    case ThumbnailStaleDetectionReasonRole:
      return item.thumbnail_stale_detection_reason;
    case ThumbnailStalePreviousRecipeHashRole:
      return item.thumbnail_stale_previous_recipe_hash;
    case ThumbnailStaleCurrentRecipeHashRole:
      return item.thumbnail_stale_current_recipe_hash;
    case ThumbnailStaleDetectionSignatureRole:
      return item.thumbnail_stale_detection_signature;
    case AdjustmentPatchJsonRole:
      return adjustmentPatchProjection(
          item.adjustment_patch_json);
    case DriveIdRole:
      return item.drive_id;
    case ImportModeRole:
      return item.import_mode;
    case ImportedAtRole:
      return item.imported_at;
    case ProjectNamesRole:
      return item.project_names;
    case ProjectSummaryRole:
      return item.project_summary;
    case MediaTypeRole:
      return item.media_type;
    case DurationTextRole:
      return item.duration_text;
    case DurationSecondsRole:
      return item.duration_seconds;
    case TimelineDocumentPathRole:
      return item.timeline_document_path;
    case RatingRole:
      return item.rating;
    case PickedRole:
      return item.picked;
    case RejectedRole:
      return item.rejected;
    case ColorLabelRole:
      return item.color_label;
    case CullStatusRole:
      return item.cull_status;
    case BurstKeyRole:
      return item.burst_key;
    case BurstSizeRole:
      return item.burst_size;
    case BurstPositionRole:
      return item.burst_position;
    case DuplicateGroupKeyRole:
      return item.duplicate_group_key;
    case DuplicateGroupSizeRole:
      return item.duplicate_group_size;
    case DuplicateGroupPositionRole:
      return item.duplicate_group_position;
    case BestShotSuggestedRole:
      return item.best_shot_suggested;
    case SyncGroupKeyRole:
      return item.sync_group_key;
    case SyncGroupSizeRole:
      return item.sync_group_size;
    case OnlineRole:
      return item.online;
    case VirtualCopyRole:
      return item.virtual_copy;
    case SourceAssetIdRole:
      return item.source_asset_id;
    case VariantNameRole:
      return item.variant_name;
    case VariantIndexRole:
      return item.variant_index;
    default:
      return {};
  }
}

QHash<int, QByteArray> CatalogAssetModel::roleNames() const {
  return {
      {AssetIdRole, "assetId"},
      {CaptureDateRole, "captureDate"},
      {CameraLabelRole, "cameraLabel"},
      {LensModelRole, "lensModel"},
      {LocationPathRole, "locationPath"},
      {FilePathRole, "filePath"},
      {ThumbnailSourceRole, "thumbnailSource"},
      {ThumbnailStaleDetectedRole, "thumbnailStaleDetected"},
      {ThumbnailStaleMismatchRole, "thumbnailStaleMismatch"},
      {ThumbnailStaleDetectionReasonRole, "thumbnailStaleDetectionReason"},
      {ThumbnailStalePreviousRecipeHashRole, "thumbnailStalePreviousRecipeHash"},
      {ThumbnailStaleCurrentRecipeHashRole, "thumbnailStaleCurrentRecipeHash"},
      {ThumbnailStaleDetectionSignatureRole, "thumbnailStaleDetectionSignature"},
      {AdjustmentPatchJsonRole, "adjustmentPatchJson"},
      {DriveIdRole, "driveId"},
      {ImportModeRole, "importMode"},
      {ImportedAtRole, "importedAt"},
      {ProjectNamesRole, "projectNames"},
      {ProjectSummaryRole, "projectSummary"},
      {MediaTypeRole, "mediaType"},
      {DurationTextRole, "durationText"},
      {DurationSecondsRole, "durationSeconds"},
      {TimelineDocumentPathRole, "timelineDocumentPath"},
      {RatingRole, "rating"},
      {PickedRole, "picked"},
      {RejectedRole, "rejected"},
      {ColorLabelRole, "colorLabel"},
      {CullStatusRole, "cullStatus"},
      {BurstKeyRole, "burstKey"},
      {BurstSizeRole, "burstSize"},
      {BurstPositionRole, "burstPosition"},
      {DuplicateGroupKeyRole, "duplicateGroupKey"},
      {DuplicateGroupSizeRole, "duplicateGroupSize"},
      {DuplicateGroupPositionRole, "duplicateGroupPosition"},
      {BestShotSuggestedRole, "bestShotSuggested"},
      {SyncGroupKeyRole, "syncGroupKey"},
      {SyncGroupSizeRole, "syncGroupSize"},
      {OnlineRole, "online"},
      {VirtualCopyRole, "virtualCopy"},
      {SourceAssetIdRole, "sourceAssetId"},
      {VariantNameRole, "variantName"},
      {VariantIndexRole, "variantIndex"},
  };
}

QString CatalogAssetModel::status() const { return status_; }

QString CatalogAssetModel::errorMessage() const { return error_message_; }

bool CatalogAssetModel::loading() const { return loading_; }

bool CatalogAssetModel::importing() const { return importing_; }

bool CatalogAssetModel::deleting() const { return deleting_; }

int CatalogAssetModel::deleteProgress() const { return delete_progress_; }

int CatalogAssetModel::deleteTargetCount() const { return delete_target_count_; }

bool CatalogAssetModel::busy() const {
  return loading_ || importing_ || deleting_ || import_preparing_ || blank_canvas_generating_ ||
      (prepared_blank_canvas_operation_ &&
       preparedBlankCanvasBusy(prepared_blank_canvas_operation_->task_id));
}

int CatalogAssetModel::assetCount() const { return static_cast<int>(assets_.size()); }

int CatalogAssetModel::totalAssetCount() const { return static_cast<int>(all_assets_.size()); }

int CatalogAssetModel::driveCount() const { return drive_count_; }

QStringList CatalogAssetModel::selectedAssetIds() const {
  QStringList asset_ids;
  asset_ids.reserve(selected_asset_ids_.size());
  for (const auto& item : assets_) {
    if (selected_asset_ids_.contains(item.asset_id)) {
      asset_ids.push_back(item.asset_id);
    }
  }
  return asset_ids;
}

int CatalogAssetModel::selectedAssetCount() const { return static_cast<int>(selectedAssetIds().size()); }

bool CatalogAssetModel::hasMultiSelection() const { return selectedAssetCount() > 1; }

QVariantList CatalogAssetModel::selectedSurveyAssets() const {
  return surveyAssets(selected_index_, 6);
}

int CatalogAssetModel::selectedSurveyAssetCount() const {
  return surveyAssetCount(selected_index_, 6);
}

int CatalogAssetModel::visiblePickedCount() const { return visible_picked_count_; }

int CatalogAssetModel::visibleRejectedCount() const { return visible_rejected_count_; }

int CatalogAssetModel::visibleUnmarkedCount() const { return visible_unmarked_count_; }

int CatalogAssetModel::visibleRatedCount() const { return visible_rated_count_; }

int CatalogAssetModel::visibleUnratedCount() const { return visible_unrated_count_; }

int CatalogAssetModel::visibleOnlineCount() const { return visible_online_count_; }

int CatalogAssetModel::visibleOfflineCount() const { return visible_offline_count_; }

QVariantList CatalogAssetModel::importSources() const { return import_sources_; }

QStringList CatalogAssetModel::importFileNameFilters() const { return supported_import_name_filters(); }

int CatalogAssetModel::selectedIndex() const { return selected_index_; }

QString CatalogAssetModel::selectedAssetId() const {
  if (const auto* item = selectedItem()) {
    return item->asset_id;
  }
  return {};
}

QString CatalogAssetModel::selectedCameraLabel() const {
  if (const auto* item = selectedItem()) {
    return item->camera_label;
  }
  return {};
}

QString CatalogAssetModel::selectedCaptureDate() const {
  if (const auto* item = selectedItem()) {
    return item->capture_date;
  }
  return {};
}

QString CatalogAssetModel::selectedImportedAt() const {
  if (const auto* item = selectedItem()) {
    return item->imported_at;
  }
  return {};
}

QString CatalogAssetModel::selectedLensModel() const {
  if (const auto* item = selectedItem()) {
    return item->lens_model;
  }
  return {};
}

QString CatalogAssetModel::selectedIso() const {
  if (const auto* item = selectedItem()) {
    return item->iso_label;
  }
  return {};
}

QString CatalogAssetModel::selectedShutterSpeed() const {
  if (const auto* item = selectedItem()) {
    return item->shutter_speed;
  }
  return {};
}

QString CatalogAssetModel::selectedAperture() const {
  if (const auto* item = selectedItem()) {
    return item->aperture_label;
  }
  return {};
}

QString CatalogAssetModel::selectedFilePath() const {
  if (const auto* item = selectedItem()) {
    return item->file_path;
  }
  return {};
}

QString CatalogAssetModel::selectedLocationPath() const {
  if (const auto* item = selectedItem()) {
    return item->location_path;
  }
  return {};
}

QString CatalogAssetModel::selectedThumbnailSource() const {
  if (const auto* item = selectedItem()) {
    return item->thumbnail_source;
  }
  return {};
}

QString CatalogAssetModel::selectedAdjustmentPatchJson() const {
  if (const auto* item = selectedItem()) {
    return adjustmentPatchProjection(
        item->adjustment_patch_json);
  }
  return {};
}

QString CatalogAssetModel::selectedProjectSummary() const {
  if (const auto* item = selectedItem()) {
    return item->project_summary;
  }
  return QStringLiteral("No projects");
}

QString CatalogAssetModel::selectedMediaType() const {
  if (const auto* item = selectedItem()) {
    return item->media_type;
  }
  return {};
}

QString CatalogAssetModel::selectedDurationText() const {
  if (const auto* item = selectedItem()) {
    return item->duration_text;
  }
  return {};
}

QString CatalogAssetModel::selectedTimelineDocumentPath() const {
  if (const auto* item = selectedItem()) {
    return item->timeline_document_path;
  }
  return {};
}

int CatalogAssetModel::selectedRating() const {
  if (const auto* item = selectedItem()) {
    return item->rating;
  }
  return 0;
}

bool CatalogAssetModel::selectedPicked() const {
  if (const auto* item = selectedItem()) {
    return item->picked;
  }
  return false;
}

bool CatalogAssetModel::selectedRejected() const {
  if (const auto* item = selectedItem()) {
    return item->rejected;
  }
  return false;
}

QString CatalogAssetModel::selectedColorLabel() const {
  if (const auto* item = selectedItem()) {
    return item->color_label;
  }
  return {};
}

bool CatalogAssetModel::canUndoCulling() const { return !culling_undo_stack_.isEmpty(); }

bool CatalogAssetModel::canRedoCulling() const { return !culling_redo_stack_.isEmpty(); }

QString CatalogAssetModel::cullingUndoLabel() const {
  return canUndoCulling() ? culling_undo_stack_.constLast().label : QString();
}

QString CatalogAssetModel::cullingRedoLabel() const {
  return canRedoCulling() ? culling_redo_stack_.constLast().label : QString();
}

bool CatalogAssetModel::canUndoBatchRecipe() const { return !batch_recipe_undo_stack_.isEmpty(); }

bool CatalogAssetModel::canRedoBatchRecipe() const { return !batch_recipe_redo_stack_.isEmpty(); }

QString CatalogAssetModel::batchRecipeUndoLabel() const {
  return canUndoBatchRecipe() ? batch_recipe_undo_stack_.constLast().label : QString();
}

QString CatalogAssetModel::batchRecipeRedoLabel() const {
  return canRedoBatchRecipe() ? batch_recipe_redo_stack_.constLast().label : QString();
}

QVariantList CatalogAssetModel::batchRecipeHistoryEntries() const {
  QVariantList entries;
  const auto current_index = batchRecipeHistoryCurrentIndex();
  entries.append(QVariantMap{
      {QStringLiteral("index"), 0},
      {QStringLiteral("label"), QStringLiteral("Before batch edits")},
      {QStringLiteral("detail"), QStringLiteral("Original recipes for the affected photos")},
      {QStringLiteral("scope"), QStringLiteral("Batch")},
      {QStringLiteral("state"), current_index == 0 ? QStringLiteral("current") : QStringLiteral("applied")},
      {QStringLiteral("current"), current_index == 0},
      {QStringLiteral("restorable"), true},
  });

  QVector<BatchRecipeHistoryEntry> chronological_entries = batch_recipe_undo_stack_;
  chronological_entries.reserve(batch_recipe_undo_stack_.size() + batch_recipe_redo_stack_.size());
  for (auto index = batch_recipe_redo_stack_.size(); index > 0; --index) {
    chronological_entries.append(batch_recipe_redo_stack_.at(index - 1));
  }
  for (auto index = 0; index < chronological_entries.size(); ++index) {
    const auto& entry = chronological_entries.at(index);
    const auto history_index = index + 1;
    const auto affected_count =
        entry.after_adjustment_patch_jsons.size();
    entries.append(QVariantMap{
        {QStringLiteral("index"), history_index},
        {QStringLiteral("label"), entry.label},
        {QStringLiteral("detail"),
         QStringLiteral("%1 %2 changed together")
             .arg(affected_count)
             .arg(affected_count == 1 ? QStringLiteral("photo") : QStringLiteral("photos"))},
        {QStringLiteral("scope"), QStringLiteral("Batch")},
        {QStringLiteral("state"), history_index <= current_index ? QStringLiteral("applied")
                                                                  : QStringLiteral("undone")},
        {QStringLiteral("current"), history_index == current_index},
        {QStringLiteral("restorable"), true},
    });
  }
  return entries;
}

int CatalogAssetModel::batchRecipeHistoryCurrentIndex() const {
  return static_cast<int>(batch_recipe_undo_stack_.size());
}

QString CatalogAssetModel::selectedCullStatus() const {
  if (const auto* item = selectedItem()) {
    return item->cull_status;
  }
  return QStringLiteral("unmarked");
}

int CatalogAssetModel::selectedBurstSize() const {
  if (const auto* item = selectedItem()) {
    return item->burst_size;
  }
  return 1;
}

int CatalogAssetModel::selectedBurstPosition() const {
  if (const auto* item = selectedItem()) {
    return item->burst_position;
  }
  return 1;
}

int CatalogAssetModel::selectedDuplicateGroupSize() const {
  if (const auto* item = selectedItem()) {
    return item->duplicate_group_size;
  }
  return 1;
}

bool CatalogAssetModel::selectedBestShotSuggested() const {
  if (const auto* item = selectedItem()) {
    return item->best_shot_suggested;
  }
  return false;
}

int CatalogAssetModel::selectedSyncGroupSize() const {
  if (const auto* item = selectedItem()) {
    return item->sync_group_size;
  }
  return 1;
}

bool CatalogAssetModel::selectedOnline() const {
  if (const auto* item = selectedItem()) {
    return item->online;
  }
  return false;
}

QString CatalogAssetModel::selectedAvailabilityState() const {
  const auto* item = selectedItem();
  if (item == nullptr) {
    return QStringLiteral("none");
  }
  if (item->online) {
    return QStringLiteral("online");
  }
  if (permanently_missing_asset_ids_.contains(item->asset_id)) {
    return QStringLiteral("permanently-missing");
  }

  const auto source_path = normalize_local_path(item->file_path);
  if (source_path.isEmpty()) {
    return QStringLiteral("permanently-missing");
  }
  const QFileInfo source_info(source_path);
  if (source_info.exists() && !source_info.isReadable()) {
    return QStringLiteral("permission-denied");
  }
  if (source_path.startsWith(QStringLiteral("/Volumes/"))) {
    const auto path_segments = source_path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (path_segments.size() >= 2) {
      const auto volume_root = QStringLiteral("/%1/%2").arg(path_segments.at(0), path_segments.at(1));
      if (!QFileInfo::exists(volume_root)) {
        return QStringLiteral("drive-offline");
      }
    }
  }

  // On macOS removable and network volumes are rooted under /Volumes. A
  // missing ordinary local path is a moved file, even when intermediate
  // directories no longer exist (QStorageInfo cannot classify that reliably).
  return QStringLiteral("moved-file");
}

QString CatalogAssetModel::selectedAvailabilityTitle() const {
  const auto state = selectedAvailabilityState();
  if (state == QStringLiteral("online")) {
    return QStringLiteral("Original available");
  }
  if (state == QStringLiteral("drive-offline")) {
    return QStringLiteral("Source drive is offline");
  }
  if (state == QStringLiteral("permission-denied")) {
    return QStringLiteral("Permission needed");
  }
  if (state == QStringLiteral("permanently-missing")) {
    return QStringLiteral("Marked permanently missing");
  }
  if (state == QStringLiteral("moved-file")) {
    return QStringLiteral("Original was moved");
  }
  return QStringLiteral("No media selected");
}

QString CatalogAssetModel::selectedAvailabilityDetail() const {
  const auto state = selectedAvailabilityState();
  if (state == QStringLiteral("online")) {
    return QStringLiteral("The original can be previewed and exported.");
  }
  if (state == QStringLiteral("drive-offline")) {
    return QStringLiteral("Reconnect the drive, recheck availability, or relink the moved folder. Catalog details and cached previews remain available.");
  }
  if (state == QStringLiteral("permission-denied")) {
    return QStringLiteral("The file exists, but Looksy cannot read it. Restore file or folder permission, then recheck.");
  }
  if (state == QStringLiteral("permanently-missing")) {
    return QStringLiteral("The catalog entry, edit recipe, ratings, and cached preview are preserved. Relink remains available if the original is found later.");
  }
  if (state == QStringLiteral("moved-file")) {
    return QStringLiteral("Choose the new folder to relink this original, or relink all offline media in one pass.");
  }
  return {};
}

bool CatalogAssetModel::selectedVirtualCopy() const {
  if (const auto* item = selectedItem()) {
    return item->virtual_copy;
  }
  return false;
}

QString CatalogAssetModel::selectedSourceAssetId() const {
  if (const auto* item = selectedItem()) {
    return item->source_asset_id;
  }
  return {};
}

QString CatalogAssetModel::selectedVariantName() const {
  if (const auto* item = selectedItem()) {
    return item->variant_name;
  }
  return {};
}

int CatalogAssetModel::selectedVariantIndex() const {
  if (const auto* item = selectedItem()) {
    return item->variant_index;
  }
  return 0;
}

QVariantMap CatalogAssetModel::assetMap(const CatalogAssetItem& item, bool include_adjustment_patch) const {
  QVariantMap map;
  map.insert(QStringLiteral("assetId"), item.asset_id);
  map.insert(QStringLiteral("captureDate"), item.capture_date);
  map.insert(QStringLiteral("captureTimestamp"), item.capture_timestamp.isValid() ? item.capture_timestamp.toString(Qt::ISODateWithMs) : QString{});
  map.insert(QStringLiteral("cameraLabel"), item.camera_label);
  map.insert(QStringLiteral("lensModel"), item.lens_model);
  map.insert(QStringLiteral("iso"), item.iso_label);
  map.insert(QStringLiteral("shutterSpeed"), item.shutter_speed);
  map.insert(QStringLiteral("aperture"), item.aperture_label);
  map.insert(QStringLiteral("locationPath"), item.location_path);
  map.insert(QStringLiteral("filePath"), item.file_path);
  map.insert(QStringLiteral("thumbnailSource"), item.thumbnail_source);
  map.insert(QStringLiteral("searchPreviewCurrent"),
             item.search_preview_current && !item.thumbnail_source.isEmpty() &&
             !transient_adjustment_asset_ids_.contains(item.asset_id));
  map.insert(QStringLiteral("thumbnailStaleDetected"), item.thumbnail_stale_detected);
  map.insert(QStringLiteral("thumbnailStaleMismatch"), item.thumbnail_stale_mismatch);
  map.insert(QStringLiteral("thumbnailStaleDetectionReason"), item.thumbnail_stale_detection_reason);
  map.insert(QStringLiteral("thumbnailStalePreviousRecipeHash"), item.thumbnail_stale_previous_recipe_hash);
  map.insert(QStringLiteral("thumbnailStaleCurrentRecipeHash"), item.thumbnail_stale_current_recipe_hash);
  map.insert(QStringLiteral("thumbnailStaleDetectionSignature"), item.thumbnail_stale_detection_signature);
  if (include_adjustment_patch) {
    map.insert(
        QStringLiteral("adjustmentPatchJson"),
        adjustmentPatchProjection(
            item.adjustment_patch_json));
  }
  map.insert(QStringLiteral("driveId"), item.drive_id);
  map.insert(QStringLiteral("importMode"), item.import_mode);
  map.insert(QStringLiteral("importedAt"), item.imported_at);
  map.insert(
      QStringLiteral("importedAtValue"),
      item.imported_at_value.isValid()
          ? item.imported_at_value.toUTC().toString(Qt::ISODateWithMs)
          : QString{});
  map.insert(QStringLiteral("projectNames"), item.project_names);
  map.insert(QStringLiteral("projectSummary"), item.project_summary);
  map.insert(QStringLiteral("mediaType"), item.media_type);
  map.insert(QStringLiteral("durationText"), item.duration_text);
  map.insert(QStringLiteral("durationSeconds"), item.duration_seconds);
  map.insert(QStringLiteral("timelineDocumentPath"), item.timeline_document_path);
  map.insert(QStringLiteral("rating"), item.rating);
  map.insert(QStringLiteral("picked"), item.picked);
  map.insert(QStringLiteral("rejected"), item.rejected);
  map.insert(QStringLiteral("colorLabel"), item.color_label);
  map.insert(QStringLiteral("cullStatus"), item.cull_status);
  map.insert(QStringLiteral("burstKey"), item.burst_key);
  map.insert(QStringLiteral("burstSize"), item.burst_size);
  map.insert(QStringLiteral("burstPosition"), item.burst_position);
  map.insert(QStringLiteral("burstTimeBased"), item.burst_time_based);
  map.insert(QStringLiteral("duplicateGroupKey"), item.duplicate_group_key);
  map.insert(QStringLiteral("duplicateGroupSize"), item.duplicate_group_size);
  map.insert(QStringLiteral("duplicateGroupPosition"), item.duplicate_group_position);
  map.insert(QStringLiteral("bestShotSuggested"), item.best_shot_suggested);
  map.insert(QStringLiteral("duplicateConfirmed"), confirmed_duplicate_groups_.contains(item.asset_id));
  map.insert(QStringLiteral("metadata"), asset_metadata_.value(metadataAssetId(item)));
  map.insert(QStringLiteral("syncGroupKey"), item.sync_group_key);
  map.insert(QStringLiteral("syncGroupSize"), item.sync_group_size);
  map.insert(QStringLiteral("online"), item.online);
  map.insert(QStringLiteral("virtualCopy"), item.virtual_copy);
  map.insert(QStringLiteral("sourceAssetId"), item.source_asset_id);
  map.insert(QStringLiteral("variantName"), item.variant_name);
  map.insert(QStringLiteral("variantIndex"), item.variant_index);
  return map;
}

QString CatalogAssetModel::adjustmentPatchProjection(
    const QString& adjustment_payload) const {
  if (adjustment_payload.trimmed().isEmpty()) {
    return {};
  }
  const auto canonical_patch =
      canonical_asset_bound_adjustment_patch_json(
          adjustment_payload);
  if (canonical_patch.isEmpty()) {
    return {};
  }
  const auto key = QString::fromLatin1(
      QCryptographicHash::hash(
          canonical_patch.toUtf8(),
          QCryptographicHash::Sha1)
          .toHex());
  const auto cached =
      adjustment_patch_projection_cache_.constFind(key);
  if (cached !=
      adjustment_patch_projection_cache_.cend()) {
    return cached.value();
  }
  if (adjustment_patch_projection_cache_.size() >= 256) {
    adjustment_patch_projection_cache_.clear();
  }
  adjustment_patch_projection_cache_.insert(
      key,
      canonical_patch);
  return canonical_patch;
}

void CatalogAssetModel::refreshVisibleCullingStats() {
  auto next_visible_picked_count = 0;
  auto next_visible_rejected_count = 0;
  auto next_visible_unmarked_count = 0;
  auto next_visible_rated_count = 0;
  auto next_visible_unrated_count = 0;
  auto next_visible_online_count = 0;
  auto next_visible_offline_count = 0;

  for (const auto& item : assets_) {
    if (item.picked) {
      ++next_visible_picked_count;
    }
    if (item.rejected) {
      ++next_visible_rejected_count;
    }
    if (!item.picked && !item.rejected) {
      ++next_visible_unmarked_count;
    }
    if (item.rating > 0) {
      ++next_visible_rated_count;
    } else {
      ++next_visible_unrated_count;
    }
    if (item.online) {
      ++next_visible_online_count;
    } else {
      ++next_visible_offline_count;
    }
  }

  if (visible_picked_count_ == next_visible_picked_count && visible_rejected_count_ == next_visible_rejected_count &&
      visible_unmarked_count_ == next_visible_unmarked_count && visible_rated_count_ == next_visible_rated_count &&
      visible_unrated_count_ == next_visible_unrated_count && visible_online_count_ == next_visible_online_count &&
      visible_offline_count_ == next_visible_offline_count) {
    return;
  }

  visible_picked_count_ = next_visible_picked_count;
  visible_rejected_count_ = next_visible_rejected_count;
  visible_unmarked_count_ = next_visible_unmarked_count;
  visible_rated_count_ = next_visible_rated_count;
  visible_unrated_count_ = next_visible_unrated_count;
  visible_online_count_ = next_visible_online_count;
  visible_offline_count_ = next_visible_offline_count;
  emit cullingStatsChanged();
}

QStringList CatalogAssetModel::projectNames() const {
  QStringList names;
  names.reserve(projects_.size());
  for (const auto& project : projects_) {
    names.push_back(project.name);
  }
  return names;
}

int CatalogAssetModel::projectStatsRevision() const { return project_stats_revision_; }

QStringList CatalogAssetModel::captureYears() const { return capture_years_; }

QStringList CatalogAssetModel::cameraLabels() const { return camera_labels_; }

QString CatalogAssetModel::projectFilter() const { return project_filter_; }

QString CatalogAssetModel::importDateFilter() const { return import_date_filter_; }

QString CatalogAssetModel::importSessionFromFilter() const { return import_session_from_filter_; }

QString CatalogAssetModel::importSessionToFilter() const { return import_session_to_filter_; }

QString CatalogAssetModel::captureYearFilter() const { return capture_year_filter_; }

QString CatalogAssetModel::cameraFilter() const { return camera_filter_; }

QString CatalogAssetModel::captureDateFromFilter() const { return capture_date_from_filter_; }

QString CatalogAssetModel::captureDateToFilter() const { return capture_date_to_filter_; }

QString CatalogAssetModel::mediaTypeFilter() const { return media_type_filter_; }
QString CatalogAssetModel::mediaCollectionFilter() const { return media_collection_filter_; }

QString CatalogAssetModel::reviewFilter() const { return review_filter_; }

QString CatalogAssetModel::availabilityFilter() const { return availability_filter_; }

int CatalogAssetModel::minimumRatingFilter() const { return minimum_rating_filter_; }
QString CatalogAssetModel::searchFilter() const { return search_filter_; }

QVariantList CatalogAssetModel::mediaPickerAssets(
    const QString& project_name, const QStringList& media_types) const {
  QVariantList items;
  const auto normalized_project_name = normalizedProjectName(project_name);
  items.reserve(all_assets_.size());
  for (const auto& item : all_assets_) {
    if (item.media_type == QStringLiteral("timeline") ||
        (!media_types.isEmpty() && !media_types.contains(item.media_type))) {
      continue;
    }
    if (!normalized_project_name.isEmpty() && normalized_project_name != QStringLiteral("all") &&
        normalized_project_name != QStringLiteral("unassigned") &&
        !item.project_names.contains(normalized_project_name)) {
      continue;
    }
    // Keep dialog rows small; full metadata remains available through assetMap.
    // Avoid projecting adjustment recipes for every unselected Library asset.
    items.push_back(QVariantMap{
        {QStringLiteral("assetId"), item.asset_id},
        {QStringLiteral("cameraLabel"), item.camera_label},
        {QStringLiteral("thumbnailSource"), item.thumbnail_source},
        {QStringLiteral("mediaType"), item.media_type},
        {QStringLiteral("durationText"), item.duration_text},
        {QStringLiteral("durationSeconds"), item.duration_seconds},
        {QStringLiteral("filePath"), item.file_path},
        {QStringLiteral("timelineDocumentPath"), item.timeline_document_path},
        {QStringLiteral("online"), item.online}});
  }
  return items;
}

QVariantList CatalogAssetModel::timelinePickerAssets(const QString& project_name) const {
  QVariantList items;
  const auto normalized_project_name = normalizedProjectName(project_name);
  items.reserve(all_assets_.size());

  for (const auto& item : all_assets_) {
    if (item.media_type == QStringLiteral("timeline")) {
      continue;
    }
    if (!normalized_project_name.isEmpty() && normalized_project_name != QStringLiteral("all") &&
        normalized_project_name != QStringLiteral("unassigned") &&
        !item.project_names.contains(normalized_project_name)) {
      continue;
    }
    items.push_back(assetMap(item));
  }

  return items;
}

QVariantMap CatalogAssetModel::galleryAssetAt(int index) const {
  if (index < 0 || index >= assets_.size()) return {};
  const auto& item = assets_.at(index);
  return QVariantMap{{QStringLiteral("assetId"), item.asset_id},
      {QStringLiteral("thumbnailSource"), item.thumbnail_source},
      {QStringLiteral("mediaType"), item.media_type},
      {QStringLiteral("durationText"), item.duration_text},
      {QStringLiteral("rating"), item.rating},
      {QStringLiteral("picked"), item.picked},
      {QStringLiteral("rejected"), item.rejected}};
}

QVariantList CatalogAssetModel::galleryAssets() const {
  // Gallery layout never needs recipe projection, source metadata or history.
  QVariantList rows;
  rows.reserve(assets_.size());
  for (int index = 0; index < assets_.size(); ++index)
    rows.push_back(galleryAssetAt(index));
  return rows;
}

QVariantMap CatalogAssetModel::assetAt(int index) const {
  if (index < 0 || index >= assets_.size()) {
    return {};
  }

  return assetMap(assets_.at(index));
}

QVariantMap CatalogAssetModel::assetById(const QString& asset_id) const {
  const auto normalized_asset_id = asset_id.trimmed();
  const auto iterator = all_asset_index_by_id_.constFind(normalized_asset_id);
  if (normalized_asset_id.isEmpty() ||
      iterator == all_asset_index_by_id_.cend()) {
    return {};
  }
  const auto index = iterator.value();
  if (index < 0 || index >= all_assets_.size()) {
    return {};
  }
  return assetMap(all_assets_.at(index));
}

QVariantList CatalogAssetModel::surveyAssets(int selected_index, int maximum_count) const {
  QVariantList items;
  if (assets_.isEmpty() || maximum_count <= 0) {
    return items;
  }

  const auto asset_count = static_cast<int>(assets_.size());
  if (selected_index < 0 || selected_index >= asset_count) {
    selected_index = std::clamp(selected_index, 0, asset_count - 1);
  }

  const auto append_asset = [this, &items, maximum_count, asset_count](int index) {
    if (items.size() >= maximum_count || index < 0 || index >= asset_count) {
      return;
    }

    // Survey displays cached previews and metadata; opening a photo fetches
    // its recipe separately. Never prepare raster edit data during selection.
    auto map = assetMap(assets_.at(index), false);
    map.insert(QStringLiteral("visibleIndex"), index);
    items.push_back(map);
  };

  const auto& current = assets_.at(selected_index);
  if (!current.burst_key.isEmpty() && current.burst_size > 1) {
    for (auto index = 0; index < asset_count; ++index) {
      if (assets_.at(index).burst_key == current.burst_key) {
        append_asset(index);
      }
    }
    return items;
  }

  if (!current.duplicate_group_key.isEmpty() && current.duplicate_group_size > 1) {
    for (auto index = 0; index < asset_count; ++index) {
      if (assets_.at(index).duplicate_group_key == current.duplicate_group_key) {
        append_asset(index);
      }
    }
    return items;
  }

  const auto half_window = std::max(0, maximum_count / 2);
  auto start = std::max(0, selected_index - half_window);
  if (start + maximum_count > asset_count) {
    start = std::max(0, asset_count - maximum_count);
  }
  const auto end = std::min(asset_count, start + maximum_count);
  for (auto index = start; index < end; ++index) {
    append_asset(index);
  }
  return items;
}

QVariantMap CatalogAssetModel::surveyAssetAt(int selected_index, int position, int maximum_count) const {
  const auto items = surveyAssets(selected_index, maximum_count);
  if (position < 0 || position >= items.size()) {
    return {};
  }
  return items.at(position).toMap();
}

int CatalogAssetModel::surveyAssetCount(int selected_index, int maximum_count) const {
  return static_cast<int>(surveyAssets(selected_index, maximum_count).size());
}

bool CatalogAssetModel::assetSelected(const QString& asset_id) const {
  return selected_asset_ids_.contains(asset_id);
}

void CatalogAssetModel::beginCatalogStartup() {
  setErrorMessage({});
  setStatus(QStringLiteral("Connecting to catalog"));
  setLoading(true);
}

void CatalogAssetModel::reportCatalogStartupFailure(const QString& error_message) {
  // Keep the current assets and catalog authority intact on connection failure.
  setErrorMessage(error_message);
  setStatus(QStringLiteral("Catalog unavailable"));
  setLoading(false);
}

void CatalogAssetModel::refresh() {
  if (!pending_culling_persistence_.isEmpty() && culling_outbox_readable_) {
    failed_culling_persistence_.clear();
    culling_persistence_paused_ = false;
    for (auto it = pending_culling_persistence_.cbegin(); it != pending_culling_persistence_.cend(); ++it)
      if (!pending_culling_persistence_order_.contains(it.key())) pending_culling_persistence_order_.append(it.key());
    culling_save_timer_.start(0);
  }
  if (!project_refresh_after_save_ && !failed_project_changes_.isEmpty() && active_project_change_.isEmpty()) {
    for (const auto& name : failed_project_changes_) attempted_project_change_revisions_.remove(name);
    failed_project_changes_.clear();
    startNextProjectChange();
  }
  if (!snapshot_fetch_active_ && !snapshot_parse_active_) {
    snapshot_changed_review_ids_.clear();
    snapshot_changed_relink_ids_.clear();
    snapshot_deleted_asset_ids_.clear();
  }
  ++snapshot_request_generation_;
  if (snapshot_parse_cancellation_) {
    snapshot_parse_cancellation_->store(true, std::memory_order_relaxed);
  }
  setLoading(true);
  if (!timeline_assets_watcher_.isRunning()) {
    timeline_assets_loaded_ = false;
  }
  setErrorMessage({});
  setStatus(QStringLiteral("Loading catalog snapshot"));

  // Serialize fetch/parse stages and coalesce refreshes into the latest generation.
  // In particular, do not block the event loop waiting for a killed helper.
  if (snapshot_fetch_active_) {
    snapshot_process_.kill();
    return;
  }
  if (!snapshot_parse_active_) startSnapshotFetch();
}

void CatalogAssetModel::startSnapshotFetch() {
  snapshot_fetch_generation_ = snapshot_request_generation_;
  snapshot_process_.setWorkingDirectory(QDir::currentPath());
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    setLoading(false);
    setErrorMessage(environment_error);
    failPendingBlankCanvas(environment_error);
    setStatus(QStringLiteral("Catalog unavailable"));
    return;
  }
  snapshot_process_.setProcessEnvironment(environment);
  snapshot_fetch_active_ = true;
  snapshot_process_.start(snapshotProgram(), snapshotArguments());
}

void CatalogAssetModel::refreshImportSources(bool force) {
  const auto now = QDateTime::currentMSecsSinceEpoch();
  if (!force &&
      !import_sources_.isEmpty() &&
      import_sources_last_rebuilt_msecs_ > 0 &&
      now - import_sources_last_rebuilt_msecs_ < kImportSourceRefreshCacheWindowMs) {
    return;
  }
  rebuildImportSources();
}

void CatalogAssetModel::clearFilters() {
  const auto preferred_asset_id = selectedAssetId();
  auto changed = false;

  const auto reset_string = [&changed](QString* value, const QString& next) {
    if (*value == next) {
      return;
    }
    *value = next;
    changed = true;
  };

  reset_string(&project_filter_, QStringLiteral("all"));
  reset_string(&import_date_filter_, QStringLiteral("all"));
  reset_string(&import_session_from_filter_, {});
  reset_string(&import_session_to_filter_, {});
  reset_string(&capture_year_filter_, QStringLiteral("all"));
  reset_string(&camera_filter_, QStringLiteral("all"));
  reset_string(&capture_date_from_filter_, {});
  reset_string(&capture_date_to_filter_, {});
  reset_string(&media_type_filter_, QStringLiteral("all"));
  reset_string(&review_filter_, QStringLiteral("all"));
  reset_string(&availability_filter_, QStringLiteral("all"));
  reset_string(&search_filter_, {});
  if (visual_search_active_) {
    visual_search_active_ = false;
    visual_search_ranks_.clear();
    changed = true;
  }
  if (minimum_rating_filter_ != 0) {
    minimum_rating_filter_ = 0;
    changed = true;
  }

  if (!changed) {
    return;
  }

  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setImportSessionFilter(const QString& from, const QString& to) {
  const auto normalized_from = from.trimmed();
  const auto normalized_to = to.trimmed();
  if ((!normalized_from.isEmpty() && !parse_iso_datetime(normalized_from).isValid()) ||
      (!normalized_to.isEmpty() && !parse_iso_datetime(normalized_to).isValid())) {
    return;
  }
  if (!normalized_from.isEmpty() && !normalized_to.isEmpty() &&
      parse_iso_datetime(normalized_from) > parse_iso_datetime(normalized_to)) {
    return;
  }
  if (import_session_from_filter_ == normalized_from && import_session_to_filter_ == normalized_to) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  import_session_from_filter_ = normalized_from;
  import_session_to_filter_ = normalized_to;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::importFolder() {
  if (busy()) {
    return;
  }

  const auto folder_path = QFileDialog::getExistingDirectory(
      nullptr,
      QStringLiteral("Import Media Folder"),
      QDir::homePath(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (folder_path.isEmpty()) {
    return;
  }

  startImport(normalize_local_path(folder_path), {}, {});
}

void CatalogAssetModel::importFolderPath(const QString& folder_path) {
  const auto normalized_path = folder_path.trimmed();
  if (normalized_path.isEmpty()) {
    return;
  }

  startImport(normalize_local_path(normalized_path), {}, {});
}

void CatalogAssetModel::startImport(const QString& folder_path) {
  startImport(folder_path, {}, {});
}

namespace {

QThreadPool& import_preflight_thread_pool() {
  static QThreadPool pool;
  static const bool configured = [] {
    pool.setMaxThreadCount(1);
    pool.setExpiryTimeout(30000);
    return true;
  }();
  Q_UNUSED(configured)
  return pool;
}

bool same_import_scan(const ImportPreflightScan& scan, const ImportPreflightRequest& request) {
  return scan.root == request.root && scan.paths == request.paths;
}

std::shared_ptr<const ImportPreflightScan> scan_import_files(const ImportPreflightRequest& request) {
  auto scan = std::make_shared<ImportPreflightScan>();
  scan->root = request.root;
  scan->paths = request.paths;
  const auto cancelled = [&] { return request.cancelled->load(std::memory_order_relaxed); };
  if (cancelled()) return {};
  scan->root_available = !request.root.isEmpty() && QFileInfo::exists(request.root);
  const auto append = [&](const QString& path) {
    ImportFileFact fact;
    fact.path = normalize_local_path(path);
    const QFileInfo info(fact.path);
    fact.within_root = local_path_is_within_root(fact.path, request.root);
    fact.is_file = info.isFile();
    fact.supported = has_supported_import_extension(fact.path);
    if (fact.is_file && fact.supported) {
      fact.pair_key = QDir::cleanPath(info.absolutePath()) + QLatin1Char('/') + info.completeBaseName().toLower();
      fact.raw = is_raw_import_path(fact.path);
      fact.jpeg = is_jpeg_import_path(fact.path);
      fact.video = is_video_import_path(fact.path);
      fact.audio = is_audio_import_path(fact.path);
      if (fact.within_root) {
        QFile file(fact.path);
        fact.readable = info.isReadable() && file.open(QIODevice::ReadOnly);
        fact.bytes = qMax<qint64>(0, info.size());
      }
    }
    scan->files.push_back(std::move(fact));
    request.scanned->fetch_add(1, std::memory_order_relaxed);
  };
  if (scan->root_available) {
    if (request.paths.isEmpty()) {
      QDirIterator iterator(request.root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
      while (!cancelled() && iterator.hasNext()) append(iterator.next());
    } else {
      for (const auto& path : request.paths) {
        if (cancelled()) return {};
        append(path);
      }
    }
  }
  if (cancelled()) return {};
  scan->age.start();
  return scan;
}

QVariantMap evaluate_import_preflight(const ImportPreflightRequest& request, const ImportPreflightScan& scan) {
  const auto normalized_root = request.root;
  const auto normalized_mode = request.mode.trimmed().compare(QStringLiteral("referenced"), Qt::CaseInsensitive) == 0
                                   ? QStringLiteral("referenced")
                                   : QStringLiteral("managed");
  const auto normalized_duplicate_policy =
      request.duplicates.trimmed().compare(QStringLiteral("replace"), Qt::CaseInsensitive) == 0
          ? QStringLiteral("replace")
          : QStringLiteral("skip");
  const auto normalized_organization_template =
      normalized_mode == QStringLiteral("managed") &&
              request.organization.trimmed().compare(QStringLiteral("date-camera"), Qt::CaseInsensitive) == 0
          ? QStringLiteral("date-camera")
          : QStringLiteral("source");
  const auto requested_raw_jpeg_policy = request.raw_jpeg.trimmed().toLower();
  const auto normalized_raw_jpeg_policy =
      requested_raw_jpeg_policy == QStringLiteral("raw-only") ||
              requested_raw_jpeg_policy == QStringLiteral("jpeg-only")
          ? requested_raw_jpeg_policy
          : QStringLiteral("both");

  QVariantMap result;
  result.insert(QStringLiteral("schema"), QStringLiteral("import-preflight:v1"));
  result.insert(QStringLiteral("sourcePath"), normalized_root);
  result.insert(QStringLiteral("importMode"), normalized_mode);
  result.insert(QStringLiteral("duplicatePolicy"), normalized_duplicate_policy);
  result.insert(QStringLiteral("organizationTemplate"), normalized_organization_template);
  result.insert(QStringLiteral("rawJpegPolicy"), normalized_raw_jpeg_policy);

  QVariantList issues;
  if (!scan.root_available) {
    issues.push_back(import_issue(
        QStringLiteral("source-unavailable"),
        QStringLiteral("error"),
        QStringLiteral("Source unavailable"),
        QStringLiteral("Choose a connected source before importing.")));
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("issues"), issues);
    result.insert(QStringLiteral("eligiblePaths"), QStringList{});
    return result;
  }

  QSet<QString> catalog_paths;
  for (const auto& asset : request.assets) {
    if (request.cancelled->load(std::memory_order_relaxed)) return {};
    if (!asset.file_path.trimmed().isEmpty()) {
      catalog_paths.insert(normalize_local_path(asset.file_path));
    }
  }

  QSet<QString> seen_paths;
  QSet<QString> raw_pair_keys;
  QSet<QString> jpeg_pair_keys;
  for (const auto& fact : scan.files) {
    if (request.cancelled->load(std::memory_order_relaxed)) return {};
    if (!fact.is_file || !fact.supported) continue;
    if (fact.raw) raw_pair_keys.insert(fact.pair_key);
    else if (fact.jpeg) jpeg_pair_keys.insert(fact.pair_key);
  }
  QStringList eligible_paths;
  auto duplicate_count = 0;
  auto unsupported_count = 0;
  auto unreadable_count = 0;
  auto photo_count = 0;
  auto video_count = 0;
  auto audio_count = 0;
  auto raw_count = 0;
  auto jpeg_count = 0;
  auto policy_skipped_count = 0;
  qint64 selected_bytes = 0;
  qint64 eligible_bytes = 0;
  const auto photo_library_root = request.library_root;
  const auto managed_drive_root = QDir(photo_library_root)
                                      .filePath(derive_drive_id(normalized_root));
  const auto originals_backup_root = request.backup_root;
  const auto originals_backup_drive_root = originals_backup_root.isEmpty()
                                               ? QString{}
                                               : QDir(originals_backup_root)
                                                     .filePath(derive_drive_id(normalized_root));

  for (const auto& fact : scan.files) {
    if (request.cancelled->load(std::memory_order_relaxed)) return {};
    const auto& normalized_path = fact.path;
    if (normalized_path.isEmpty() || !fact.within_root || !fact.is_file) {
      ++unreadable_count;
      continue;
    }
    if (seen_paths.contains(normalized_path)) {
      ++duplicate_count;
      continue;
    }
    seen_paths.insert(normalized_path);

    if (!fact.supported) {
      ++unsupported_count;
      continue;
    }

    if (!fact.readable) {
      ++unreadable_count;
      continue;
    }
    const auto file_bytes = fact.bytes;
    selected_bytes += file_bytes;
    if (fact.video) {
      ++video_count;
    } else if (fact.audio) {
      ++audio_count;
    } else {
      ++photo_count;
    }

    const auto& pair_key = fact.pair_key;
    const auto raw_path = fact.raw;
    const auto jpeg_path = fact.jpeg;
    if (raw_path) {
      ++raw_count;
    } else if (jpeg_path) {
      ++jpeg_count;
    }

    if ((normalized_raw_jpeg_policy == QStringLiteral("raw-only") && jpeg_path &&
         raw_pair_keys.contains(pair_key)) ||
        (normalized_raw_jpeg_policy == QStringLiteral("jpeg-only") && raw_path &&
         jpeg_pair_keys.contains(pair_key))) {
      ++policy_skipped_count;
      continue;
    }

    auto is_duplicate = catalog_paths.contains(normalized_path);
    if (normalized_mode == QStringLiteral("managed")) {
      const auto relative_path = QDir(normalized_root).relativeFilePath(normalized_path);
      is_duplicate = is_duplicate || QFileInfo::exists(QDir(managed_drive_root).filePath(relative_path));
    }
    if (is_duplicate) {
      ++duplicate_count;
      if (normalized_duplicate_policy == QStringLiteral("skip")) {
        continue;
      }
    }
    eligible_paths.push_back(normalized_path);
    eligible_bytes += file_bytes;
  }

  auto raw_jpeg_pair_count = 0;
  for (const auto& key : raw_pair_keys) {
    if (jpeg_pair_keys.contains(key)) {
      ++raw_jpeg_pair_count;
    }
  }

  if (duplicate_count > 0) {
    issues.push_back(import_issue(
        QStringLiteral("duplicates-found"),
        QStringLiteral("warning"),
        QStringLiteral("Duplicates found"),
        QStringLiteral("%1 duplicate %2 will be %3.")
            .arg(duplicate_count)
            .arg(duplicate_count == 1 ? QStringLiteral("item") : QStringLiteral("items"))
            .arg(normalized_duplicate_policy == QStringLiteral("skip")
                     ? QStringLiteral("skipped")
                     : QStringLiteral("replaced"))));
  }
  if (unsupported_count > 0) {
    issues.push_back(import_issue(
        QStringLiteral("unsupported-files"),
        QStringLiteral("warning"),
        QStringLiteral("Unsupported files"),
        QStringLiteral("%1 unsupported %2 will be skipped before the job starts.")
            .arg(unsupported_count)
            .arg(unsupported_count == 1 ? QStringLiteral("file") : QStringLiteral("files"))));
  }
  if (unreadable_count > 0) {
    issues.push_back(import_issue(
        QStringLiteral("unreadable-files"),
        QStringLiteral("warning"),
        QStringLiteral("Unreadable files"),
        QStringLiteral("%1 missing or unreadable %2 will be skipped before the job starts.")
            .arg(unreadable_count)
            .arg(unreadable_count == 1 ? QStringLiteral("file") : QStringLiteral("files"))));
  }
  if (policy_skipped_count > 0) {
    issues.push_back(import_issue(
        QStringLiteral("raw-jpeg-policy"),
        QStringLiteral("warning"),
        QStringLiteral("RAW + JPEG policy"),
        QStringLiteral("%1 paired %2 will be skipped by the selected policy.")
            .arg(policy_skipped_count)
            .arg(policy_skipped_count == 1 ? QStringLiteral("file") : QStringLiteral("files"))));
  }
  if (eligible_paths.isEmpty()) {
    issues.push_back(import_issue(
        QStringLiteral("nothing-to-import"),
        QStringLiteral("error"),
        QStringLiteral("Nothing to import"),
        QStringLiteral("Change the selection or duplicate handling before continuing.")));
  }

  if (request.cancelled->load(std::memory_order_relaxed)) return {};
  auto storage_ready = true;
  const auto validate_destination = [&](const QString& path, const QString& title) {
    if (path.isEmpty()) {
      return;
    }
    const QFileInfo info(path);
    auto writable = info.exists() && info.isDir() && info.isWritable();
    if (!info.exists()) {
      auto ancestor = info.absoluteDir();
      while (!ancestor.exists() && ancestor.cdUp()) {
      }
      const QFileInfo ancestor_info(ancestor.absolutePath());
      writable = ancestor_info.exists() && ancestor_info.isDir() && ancestor_info.isWritable();
    }
    if (!writable) {
      storage_ready = false;
      issues.push_back(import_issue(
          QStringLiteral("storage-unavailable"),
          QStringLiteral("error"),
          title,
          QStringLiteral("Connect or choose a writable destination before importing: %1").arg(path)));
    }
  };
  if (normalized_mode == QStringLiteral("managed")) {
    validate_destination(photo_library_root, QStringLiteral("Photo Library unavailable"));
  }
  validate_destination(originals_backup_root, QStringLiteral("Originals backup unavailable"));

  const auto primary_estimated_disk_bytes =
      normalized_mode == QStringLiteral("managed") ? eligible_bytes : 0;
  const auto backup_estimated_disk_bytes =
      originals_backup_root.isEmpty() ? 0 : eligible_bytes;
  const auto estimated_disk_bytes =
      primary_estimated_disk_bytes + backup_estimated_disk_bytes;
  const auto validate_capacity = [&](const QString& path, qint64 required_bytes, const QString& title) {
    if (path.isEmpty() || required_bytes <= 0 || !storage_ready) {
      return;
    }
    const QStorageInfo storage(path);
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() < required_bytes) {
      storage_ready = false;
      issues.push_back(import_issue(
          QStringLiteral("storage-space"),
          QStringLiteral("error"),
          title,
          QStringLiteral("%1 is required, but only %2 is available at %3.")
              .arg(formatted_import_size(required_bytes),
                   formatted_import_size(storage.bytesAvailable()),
                   path)));
    }
  };
  validate_capacity(photo_library_root, primary_estimated_disk_bytes, QStringLiteral("Photo Library is full"));
  validate_capacity(originals_backup_root, backup_estimated_disk_bytes, QStringLiteral("Originals backup is full"));

  result.insert(QStringLiteral("ok"), !eligible_paths.isEmpty() && storage_ready);
  result.insert(QStringLiteral("issues"), issues);
  result.insert(QStringLiteral("candidateCount"), scan.files.size());
  result.insert(QStringLiteral("supportedCount"), photo_count + video_count + audio_count);
  result.insert(QStringLiteral("importableCount"), eligible_paths.size());
  result.insert(QStringLiteral("photoCount"), photo_count);
  result.insert(QStringLiteral("videoCount"), video_count);
  result.insert(QStringLiteral("audioCount"), audio_count);
  result.insert(QStringLiteral("rawCount"), raw_count);
  result.insert(QStringLiteral("jpegCount"), jpeg_count);
  result.insert(QStringLiteral("rawJpegPairCount"), raw_jpeg_pair_count);
  result.insert(QStringLiteral("duplicateCount"), duplicate_count);
  result.insert(QStringLiteral("unsupportedCount"), unsupported_count);
  result.insert(QStringLiteral("unreadableCount"), unreadable_count);
  result.insert(QStringLiteral("policySkippedCount"), policy_skipped_count);
  result.insert(QStringLiteral("selectedBytes"), selected_bytes);
  result.insert(QStringLiteral("selectedSizeText"), formatted_import_size(selected_bytes));
  result.insert(QStringLiteral("primaryEstimatedDiskBytes"), primary_estimated_disk_bytes);
  result.insert(
      QStringLiteral("primaryEstimatedDiskText"),
      formatted_import_size(primary_estimated_disk_bytes));
  result.insert(QStringLiteral("backupEstimatedDiskBytes"), backup_estimated_disk_bytes);
  result.insert(
      QStringLiteral("backupEstimatedDiskText"),
      formatted_import_size(backup_estimated_disk_bytes));
  result.insert(QStringLiteral("estimatedDiskBytes"), estimated_disk_bytes);
  result.insert(QStringLiteral("estimatedDiskText"), formatted_import_size(estimated_disk_bytes));
  result.insert(QStringLiteral("photoLibraryRoot"), photo_library_root);
  result.insert(QStringLiteral("backupEnabled"), !originals_backup_root.isEmpty());
  result.insert(QStringLiteral("backupRoot"), originals_backup_root);
  result.insert(QStringLiteral("backupDestinationPath"), originals_backup_drive_root);
  result.insert(QStringLiteral("copyVerification"), QStringLiteral("SHA-256"));
  result.insert(
      QStringLiteral("destinationPath"),
      normalized_mode == QStringLiteral("managed") ? managed_drive_root : normalized_root);
  result.insert(
      QStringLiteral("outcomeText"),
      normalized_mode == QStringLiteral("managed")
          ? QStringLiteral("Managed copy: originals are copied into Looksy storage; source files stay untouched.")
          : QStringLiteral("Leave in place: Looksy links to these originals; moving or disconnecting them makes them offline."));
  result.insert(
      QStringLiteral("organizationText"),
      normalized_organization_template == QStringLiteral("date-camera")
          ? QStringLiteral("Organize copies into capture date and camera folders; preserve filenames.")
          : QStringLiteral("Preserve source folders and filenames; no renaming."));
  result.insert(QStringLiteral("eligiblePaths"), eligible_paths);
  return result;
}

}  // namespace

QVariantMap CatalogAssetModel::importPreflight(
    const QString& source_root_path, const QStringList& file_paths,
    const QString& import_mode, const QString& duplicate_policy,
    const QString& organization_template, const QString& raw_jpeg_policy) const {
  // Synchronous compatibility API for non-interactive callers. The import UI
  // and admission path use requestImportPreflight and the same worker helpers.
  ImportPreflightRequest request;
  request.root = normalize_local_path(source_root_path);
  request.paths = file_paths;
  request.mode = import_mode;
  request.duplicates = duplicate_policy;
  request.organization = organization_template;
  request.raw_jpeg = raw_jpeg_policy;
  request.library_root = AppBootstrap::photoLibraryLocation();
  request.backup_root = AppBootstrap::originalsBackupLocation();
  request.assets = all_assets_;
  const auto scan = scan_import_files(request);
  return scan ? evaluate_import_preflight(request, *scan) : QVariantMap{};
}

void CatalogAssetModel::requestImportPreflight(
    const QString& source_root_path, const QStringList& file_paths,
    const QString& import_mode, const QString& duplicate_policy,
    const QString& organization_template, const QString& raw_jpeg_policy) {
  if (busy()) return;
  queueImportPreflight(source_root_path, file_paths, import_mode, duplicate_policy,
                       organization_template, raw_jpeg_policy, {}, false);
}

void CatalogAssetModel::cancelImportPreflight() {
  // Closing the review must not cancel a separately confirmed import.
  if (import_preparing_) return;
  ++import_preflight_generation_;
  if (active_import_preflight_) active_import_preflight_->cancelled->store(true, std::memory_order_relaxed);
  pending_import_preflight_.reset();
  import_preflight_scan_.reset();
  import_preflight_result_.clear();
  import_preflight_requested_ = false;
  import_preflight_scanned_count_ = 0;
  import_preflight_progress_timer_.stop();
  emit importPreflightChanged();
}

void CatalogAssetModel::queueImportPreflight(
    const QString& source_root_path, const QStringList& file_paths,
    const QString& import_mode, const QString& duplicate_policy,
    const QString& organization_template, const QString& raw_jpeg_policy,
    const QString& project_name, bool admission, const QString& prepared_task_id) {
  auto request = std::make_shared<ImportPreflightRequest>();
  request->root = normalize_local_path(source_root_path);
  request->paths = file_paths;
  request->mode = import_mode;
  request->duplicates = duplicate_policy;
  request->organization = organization_template;
  request->raw_jpeg = raw_jpeg_policy;
  request->project = project_name;
  request->prepared_task_id = prepared_task_id;
  request->library_root = AppBootstrap::photoLibraryLocation();
  request->backup_root = AppBootstrap::originalsBackupLocation();
  request->assets = all_assets_;  // Implicitly shared; catalog iteration stays on the worker.
  request->generation = ++import_preflight_generation_;
  request->admission = admission;
  // Let a matching in-flight scan finish when only policies change. Its report
  // is stale, but the immutable file facts can serve the latest pending request.
  if (active_import_preflight_ &&
      (admission || active_import_preflight_->root != request->root ||
       active_import_preflight_->paths != request->paths ||
       active_import_preflight_->library_root != request->library_root ||
       active_import_preflight_->backup_root != request->backup_root)) {
    active_import_preflight_->cancelled->store(true, std::memory_order_relaxed);
  }
  pending_import_preflight_ = std::move(request);
  import_preflight_result_.clear();
  import_preflight_requested_ = true;
  import_preflight_scanned_count_ = 0;
  if (admission) {
    import_preparing_ = true;
    setErrorMessage({});
    setStatus(QStringLiteral("Checking import files"));
    emit busyChanged();
  }
  import_preflight_progress_timer_.start();
  emit importPreflightChanged();
  // Coalesce several policy setters in one QML event before scheduling work.
  QTimer::singleShot(0, this, &CatalogAssetModel::startNextImportPreflight);
}

void CatalogAssetModel::startNextImportPreflight() {
  if (active_import_preflight_ || !pending_import_preflight_) return;
  active_import_preflight_ = std::move(pending_import_preflight_);
  const auto request = active_import_preflight_;
  const auto cached = import_preflight_scan_;
  import_preflight_watcher_.setFuture(QtConcurrent::run(&importPreflightThreadPool(), [request, cached] {
    // Confirming always reads current source facts. Reviews may reuse one recent
    // scan across policy changes, but never across roots or selections.
    request->scan = !request->admission && cached && same_import_scan(*cached, *request) &&
            cached->age.isValid() && cached->age.elapsed() < 30000
        ? cached : scan_import_files(*request);
    if (!request->scan || request->cancelled->load(std::memory_order_relaxed)) return QVariantMap{};
    return evaluate_import_preflight(*request, *request->scan);
  }));
}

QThreadPool& CatalogAssetModel::importPreflightThreadPool() {
  return import_preflight_thread_pool();
}

void CatalogAssetModel::handleImportPreflightFinished() {
  const auto request = std::move(active_import_preflight_);
  if (!request) return;
  QVariantMap result;
  try {
    result = import_preflight_watcher_.result();
  } catch (...) {
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("issues"), QVariantList{import_issue(
        QStringLiteral("scan-failed"), QStringLiteral("error"),
        QStringLiteral("Import scan failed"), QStringLiteral("Try scanning the source again."))});
  }
  const bool current = request->generation == import_preflight_generation_ && import_preflight_requested_;
  const auto prepared_operation = prepared_blank_canvas_operation_;
  const auto is_prepared_request = !request->prepared_task_id.isEmpty() && prepared_operation &&
      prepared_operation->task_id == request->prepared_task_id;
  if (is_prepared_request && request->cancelled->load(std::memory_order_relaxed)) {
    if (current) {
      import_preflight_requested_ = false;
      import_preflight_scanned_count_ = request->scanned->load(std::memory_order_relaxed);
      import_preflight_progress_timer_.stop();
      import_preparing_ = false;
    }
    finishPreparedBlankCanvas(QStringLiteral("canceled"), QStringLiteral("Canvas import was canceled before it started."));
    emit busyChanged();
    emit importPreflightChanged();
    startNextImportPreflight();
    return;
  }
  if (!request->cancelled->load(std::memory_order_relaxed)) {
    if (request->scan && (current || pending_import_preflight_)) import_preflight_scan_ = request->scan;
    if (current) {
      import_preflight_result_ = result;
      // QML needs counts and issues, not a second copy of every eligible path.
      import_preflight_result_.remove(QStringLiteral("eligiblePaths"));
      import_preflight_requested_ = false;
      import_preflight_scanned_count_ = request->scanned->load(std::memory_order_relaxed);
      import_preflight_progress_timer_.stop();
      if (request->admission) {
        import_preparing_ = false;
        const auto prepared = is_prepared_request && prepared_operation->status == QStringLiteral("preflight");
        const auto other_work_busy = loading_ || importing_ || deleting_ || import_preparing_ || blank_canvas_generating_ ||
            (prepared_blank_canvas_operation_ && preparedBlankCanvasBusy(prepared_blank_canvas_operation_->task_id) && !prepared);
        if (!result.value(QStringLiteral("ok")).toBool() || other_work_busy ||
            request->library_root != AppBootstrap::photoLibraryLocation() ||
            request->backup_root != AppBootstrap::originalsBackupLocation()) {
          const auto error = QStringLiteral("Import could not be queued. Review the source and try again.");
          setErrorMessage(error);
          if (prepared) finishPreparedBlankCanvas(QStringLiteral("failed"), error);
          else {
            failPendingBlankCanvas(error_message_);
            setStatus(QStringLiteral("Import needs attention"));
          }
        } else {
          startImport(request->root, result.value(QStringLiteral("eligiblePaths")).toStringList(),
                      request->project, result.value(QStringLiteral("importMode")).toString(),
                      result.value(QStringLiteral("duplicatePolicy")).toString(), result,
                      prepared ? request->prepared_task_id : QString{});
        }
        emit busyChanged();
      }
      emit importPreflightChanged();
    }
  }
  startNextImportPreflight();
}

bool CatalogAssetModel::createBlankCanvas(const QVariantMap& settings) {
  if (busy() || blankCanvasBusy()) return false;
  setErrorMessage({});
  blank_canvas_generating_ = true;
  blank_canvas_import_finished_ = false;
  emit blankCanvasBusyChanged();
  emit busyChanged();
  setStatus(QStringLiteral("Creating canvas"));
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    blank_canvas_generating_ = false;
    pending_blank_canvas_path_ = result.value(QStringLiteral("path")).toString();
    emit busyChanged();
    emit blankCanvasBusyChanged();
    if (pending_blank_canvas_path_.isEmpty()) {
      const auto error = result.value(QStringLiteral("error")).toString();
      setErrorMessage(error);
      setStatus(QStringLiteral("Canvas could not be created"));
      emit blankCanvasFailed(error);
      return;
    }
    importSelectionWithOptions(QFileInfo(pending_blank_canvas_path_).absolutePath(),
                              {pending_blank_canvas_path_}, {}, QStringLiteral("referenced"));
  });
  const auto root = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("canvas-documents"));
  watcher->setFuture(QtConcurrent::run([root, settings] { return create_blank_canvas_source(root, settings); }));
  return true;
}

bool CatalogAssetModel::blankCanvasBusy() const {
  if (blank_canvas_generating_ || !pending_blank_canvas_path_.isEmpty()) return true;
  return prepared_blank_canvas_operation_ &&
      preparedBlankCanvasBusy(prepared_blank_canvas_operation_->task_id);
}

bool CatalogAssetModel::startPreparedBlankCanvas(
    const QString& task_id, const QString& document_id, const QString& import_job_id,
    const QVariantMap& settings) {
  const QRegularExpression valid_task_id(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
  const QUuid document_uuid(document_id);
  const QUuid import_uuid(import_job_id);
  if (!valid_task_id.match(task_id).hasMatch() || document_uuid.isNull() || import_uuid.isNull() ||
      document_uuid.toString(QUuid::WithoutBraces) != document_id.toLower() ||
      import_uuid.toString(QUuid::WithoutBraces) != import_job_id.toLower() ||
      busy() || blankCanvasBusy() ||
      (prepared_blank_canvas_operation_ &&
       prepared_blank_canvas_operation_->task_id == task_id)) {
    setErrorMessage(QStringLiteral("A prepared canvas cannot start while another import or canvas task is active."));
    return false;
  }
  for (const auto& record : loadPersistedJobRecords()) {
    if (is_import_job_active(record)) {
      setErrorMessage(QStringLiteral("Wait for the current import to settle before starting a prepared canvas."));
      return false;
    }
  }

  const auto root = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("canvas-documents"));
  const auto planned_path = planned_blank_canvas_source_path(root, settings, document_id);
  if (planned_path.isEmpty() || QFileInfo::exists(planned_path) ||
      QFileInfo::exists(QFileInfo(planned_path).absolutePath())) {
    setErrorMessage(QStringLiteral("The prepared canvas destination is already in use or invalid."));
    return false;
  }

  auto operation = std::make_shared<PreparedBlankCanvasOperation>();
  operation->task_id = task_id;
  operation->document_id = document_id;
  operation->import_job_id = import_job_id;
  operation->source_path = normalize_local_path(planned_path);
  prepared_blank_canvas_operation_ = operation;
  setErrorMessage({});
  setStatus(QStringLiteral("Creating prepared canvas source"));
  emit preparedBlankCanvasChanged(task_id);
  emit busyChanged();
  emit blankCanvasBusyChanged();

  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  prepared_blank_canvas_watcher_ = watcher;
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
          [this, watcher, operation] {
    const auto result = watcher->result();
    watcher->deleteLater();
    if (prepared_blank_canvas_watcher_ == watcher) prepared_blank_canvas_watcher_ = nullptr;
    if (prepared_blank_canvas_operation_ != operation) return;
    const auto result_path = normalize_local_path(result.value(QStringLiteral("path")).toString());
    if (!result_path.isEmpty() && result_path == operation->source_path) {
      operation->source_sha256 = sha256_file(result_path);
      if (operation->source_sha256.isEmpty()) {
        finishPreparedBlankCanvas(QStringLiteral("failed"), QStringLiteral("The created canvas source could not be hashed."));
        return;
      }
      operation->status = QStringLiteral("source-created");
      setStatus(QStringLiteral("Prepared canvas source is ready for catalog admission"));
      emit preparedBlankCanvasSourceCreated(operation->task_id, operation->document_id,
                                            operation->source_path, operation->source_sha256);
      emit preparedBlankCanvasChanged(operation->task_id);
      if (operation->cancelled->load(std::memory_order_relaxed)) {
        finishPreparedBlankCanvas(QStringLiteral("canceled"), QStringLiteral("Canvas creation was canceled before import."));
      }
      return;
    }
    const auto error = result.value(QStringLiteral("error")).toString().trimmed();
    finishPreparedBlankCanvas(
        operation->cancelled->load(std::memory_order_relaxed) ? QStringLiteral("canceled") : QStringLiteral("failed"),
        error.isEmpty() ? QStringLiteral("Prepared canvas creation did not produce the planned source file.") : error);
  });
  const auto cancellation = operation->cancelled;
  watcher->setFuture(QtConcurrent::run([root, settings, document_id, cancellation] {
    return create_blank_canvas_source(root, settings, document_id,
        [cancellation] { return cancellation->load(std::memory_order_relaxed); });
  }));
  return true;
}

bool CatalogAssetModel::acceptPreparedBlankCanvasSource(
    const QString& task_id, const QString& source_sha256) {
  const auto operation = prepared_blank_canvas_operation_;
  if (!operation || operation->task_id != task_id || operation->status != QStringLiteral("source-created") ||
      operation->cancelled->load(std::memory_order_relaxed) ||
      operation->source_sha256 != source_sha256 || sha256_file(operation->source_path) != operation->source_sha256) {
    setErrorMessage(QStringLiteral("The prepared canvas source receipt is stale or does not match the file."));
    return false;
  }
  operation->status = QStringLiteral("preflight");
  setStatus(QStringLiteral("Checking prepared canvas import"));
  queueImportPreflight(QFileInfo(operation->source_path).absolutePath(), {operation->source_path},
                       QStringLiteral("referenced"), QStringLiteral("skip"),
                       QStringLiteral("source"), QStringLiteral("both"), {}, true, task_id);
  return true;
}

bool CatalogAssetModel::cancelPreparedBlankCanvas(const QString& task_id) {
  const auto operation = prepared_blank_canvas_operation_;
  if (!operation || operation->task_id != task_id) return false;
  if (!preparedBlankCanvasBusy(task_id)) return true;
  operation->cancelled->store(true, std::memory_order_relaxed);
  if (operation->import_started) {
    const auto records = loadPersistedJobRecords();
    const auto record = std::find_if(records.cbegin(), records.cend(), [&](const auto& value) {
      return value.job_id == operation->import_job_id && value.job_kind == QStringLiteral("catalog_import") &&
          value.metadata_json.value(QStringLiteral("preparedTaskId")).toString() == task_id;
    });
    if (record == records.cend()) {
      setErrorMessage(QStringLiteral("The prepared import job identity could not be verified; it was left untouched."));
      return false;
    }
    if (is_import_job_active(*record)) {
      QString error;
      if (!cancelPersistedJobRecord(operation->import_job_id, &error) &&
          !error.contains(QStringLiteral("already in progress"), Qt::CaseInsensitive)) {
        setErrorMessage(error.isEmpty() ? QStringLiteral("The prepared import could not be canceled safely.") : error);
        return false;
      }
    }
  } else if (active_import_preflight_ && active_import_preflight_->prepared_task_id == task_id) {
    active_import_preflight_->cancelled->store(true, std::memory_order_relaxed);
  } else if (pending_import_preflight_ && pending_import_preflight_->prepared_task_id == task_id) {
    pending_import_preflight_->cancelled->store(true, std::memory_order_relaxed);
  } else if (operation->status == QStringLiteral("source-created")) {
    finishPreparedBlankCanvas(QStringLiteral("canceled"), QStringLiteral("Canvas import was canceled before it started."));
    return true;
  }
  operation->status = operation->status == QStringLiteral("generating")
      ? QStringLiteral("cancel-requested") : operation->status;
  setStatus(QStringLiteral("Stopping prepared canvas import safely"));
  emit preparedBlankCanvasChanged(task_id);
  return true;
}

bool CatalogAssetModel::preparedBlankCanvasBusy(const QString& task_id) const {
  const auto operation = prepared_blank_canvas_operation_;
  if (!operation || operation->task_id != task_id) return false;
  return !QStringList{QStringLiteral("completed"), QStringLiteral("failed"), QStringLiteral("canceled"),
                      QStringLiteral("completed-after-failure"), QStringLiteral("completed-after-cancel")}
      .contains(operation->status);
}

QVariantMap CatalogAssetModel::preparedBlankCanvasStatus(const QString& task_id) const {
  const auto operation = prepared_blank_canvas_operation_;
  if (!operation || operation->task_id != task_id) return {{QStringLiteral("ok"), false}};
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("taskId"), operation->task_id},
          {QStringLiteral("documentId"), operation->document_id},
          {QStringLiteral("importJobId"), operation->import_job_id},
          {QStringLiteral("sourcePath"), operation->source_path},
          {QStringLiteral("sourceSha256"), operation->source_sha256},
          {QStringLiteral("status"), operation->status},
          {QStringLiteral("assetId"), operation->asset_id},
          {QStringLiteral("error"), operation->error},
          {QStringLiteral("producerBusy"), preparedBlankCanvasBusy(task_id)}};
}

void CatalogAssetModel::finishPreparedBlankCanvas(const QString& status, const QString& error) {
  const auto operation = prepared_blank_canvas_operation_;
  if (!operation || !preparedBlankCanvasBusy(operation->task_id)) return;
  operation->status = status;
  operation->error = error;
  if (!error.isEmpty()) setErrorMessage(error);
  setStatus(status == QStringLiteral("completed") || status == QStringLiteral("completed-after-failure") ||
                    status == QStringLiteral("completed-after-cancel")
                ? QStringLiteral("Prepared canvas import completed")
                : QStringLiteral("Prepared canvas import %1").arg(status));
  emit preparedBlankCanvasFinished(operation->task_id, operation->import_job_id, status,
                                    operation->asset_id, operation->source_path,
                                    operation->source_sha256, error);
  emit preparedBlankCanvasChanged(operation->task_id);
  emit busyChanged();
  emit blankCanvasBusyChanged();
}

void CatalogAssetModel::importSelection(
    const QString& source_root_path,
    const QStringList& file_paths,
    const QString& project_name) {
  importSelectionWithOptions(
      source_root_path,
      file_paths,
      project_name,
      QStringLiteral("managed"),
      QStringLiteral("skip"));
}

void CatalogAssetModel::importSelectionWithOptions(
    const QString& source_root_path,
    const QStringList& file_paths,
    const QString& project_name,
    const QString& import_mode,
    const QString& duplicate_policy,
    const QString& organization_template,
    const QString& raw_jpeg_policy) {
  if (busy()) {
    return;
  }

  queueImportPreflight(source_root_path, file_paths, import_mode, duplicate_policy,
                       organization_template, raw_jpeg_policy, project_name, true);
}

void CatalogAssetModel::startImport(
    const QString& folder_path,
    const QStringList& selected_file_paths,
    const QString& project_name,
    const QString& import_mode,
    const QString& duplicate_policy,
    const QVariantMap& reviewed_preflight,
    const QString& prepared_task_id) {
  const auto prepared_operation = prepared_blank_canvas_operation_;
  const auto prepared = !prepared_task_id.isEmpty() && prepared_operation &&
      prepared_operation->task_id == prepared_task_id &&
      prepared_operation->status == QStringLiteral("preflight") &&
      !prepared_operation->cancelled->load(std::memory_order_relaxed);
  if (prepared_task_id.isEmpty() ? busy()
      : (!prepared || loading_ || importing_ || deleting_ || import_preparing_ || blank_canvas_generating_)) return;
  if (prepared && (selected_file_paths.size() != 1 ||
      normalize_local_path(selected_file_paths.front()) != prepared_operation->source_path ||
      sha256_file(prepared_operation->source_path) != prepared_operation->source_sha256)) {
    finishPreparedBlankCanvas(QStringLiteral("failed"), QStringLiteral("The prepared source changed before catalog import."));
    return;
  }

  if (reviewed_preflight.isEmpty()) {
    if (prepared) {
      finishPreparedBlankCanvas(QStringLiteral("failed"), QStringLiteral("Prepared canvas import requires a completed preflight."));
      return;
    }
    importSelectionWithOptions(folder_path, selected_file_paths, project_name,
                               import_mode, duplicate_policy);
    return;
  }
  const auto normalized_path = normalize_local_path(folder_path);
  const auto& import_preflight = reviewed_preflight;
  if (!import_preflight.value(QStringLiteral("ok")).toBool()) {
    setErrorMessage(QStringLiteral("Import preflight found no files that can be imported."));
    if (prepared) {
      finishPreparedBlankCanvas(QStringLiteral("failed"), error_message_);
    } else {
      failPendingBlankCanvas(error_message_);
      setStatus(QStringLiteral("Import needs attention"));
    }
    return;
  }

  if (!import_selection_manifest_path_.isEmpty()) {
    QFile::remove(import_selection_manifest_path_);
    import_selection_manifest_path_.clear();
  }

  if (!selected_file_paths.isEmpty()) {
    auto temp_root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (temp_root.isEmpty()) {
      temp_root = QDir::tempPath();
    }

    const auto manifest_path = QDir(temp_root).filePath(
        QStringLiteral("photo-editor-import-%1.txt")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    QFile manifest_file(manifest_path);
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      setErrorMessage(QStringLiteral("Unable to stage the selected photos for import."));
      if (prepared) {
        finishPreparedBlankCanvas(QStringLiteral("failed"), error_message_);
      } else {
        failPendingBlankCanvas(error_message_);
        setStatus(QStringLiteral("Import failed"));
      }
      return;
    }

    for (const auto& file_path : selected_file_paths) {
      manifest_file.write(file_path.toUtf8());
      manifest_file.write("\n");
    }
    manifest_file.close();
    import_selection_manifest_path_ = manifest_path;
  }

  const auto normalized_project_name = normalizedProjectName(project_name);
  pending_import_project_name_ = normalized_project_name;
  pending_import_file_paths_ =
      prepared || !pending_blank_canvas_path_.isEmpty() || (normalized_project_name.isEmpty() &&
          import_preset_adjustment_patch_json_.isEmpty())
          ? QStringList{}
          : selected_file_paths;
  if (!beginImportJob(
      normalized_path,
      normalized_project_name,
      selected_file_paths,
      static_cast<int>(selected_file_paths.size()),
      import_mode,
      QJsonObject::fromVariantMap(import_preflight),
      prepared ? prepared_task_id : QString{},
      prepared ? prepared_operation->import_job_id : QString{},
      prepared ? prepared_operation->document_id : QString{},
      prepared ? prepared_operation->source_sha256 : QString())) {
    if (prepared) finishPreparedBlankCanvas(QStringLiteral("failed"), error_message_.isEmpty()
        ? QStringLiteral("The prepared import job could not be queued.") : error_message_);
    return;
  }

  setImporting(true);
  setErrorMessage({});
  setStatus(selected_file_paths.isEmpty()
                ? QStringLiteral("Queued source import")
                : QStringLiteral("Queued selected photo import"));
  if (prepared) {
    prepared_operation->import_started = true;
    prepared_operation->status = QStringLiteral("import-accepted");
    emit preparedBlankCanvasImportAccepted(prepared_task_id, prepared_operation->import_job_id,
                                           prepared_operation->source_path,
                                           prepared_operation->source_sha256);
    emit preparedBlankCanvasChanged(prepared_task_id);
  }
}

void CatalogAssetModel::selectIndex(int index) {
  setSelectedIndexInternal(index);
  selection_anchor_asset_id_ = selectedAssetId();
}

void CatalogAssetModel::selectIndexWithModifiers(int index, int modifiers, bool selection_mode) {
  if (index < 0 || index >= assets_.size()) return;
  const auto flags = Qt::KeyboardModifiers(modifiers);
  const auto extend = flags.testFlag(Qt::ShiftModifier);
  const auto toggle = flags.testFlag(Qt::ControlModifier) || flags.testFlag(Qt::MetaModifier);
  const auto& target_id = assets_.at(index).asset_id;
  QSet<QString> next_selection;
  if (extend) {
    // Store the anchor by identity so filtering/reordering cannot silently move
    // it to another photo. Repeated Shift-clicks keep the original anchor.
    const auto anchor_index = visible_asset_index_by_id_.value(selection_anchor_asset_id_, -1);
    const auto anchor = anchor_index >= 0 ? static_cast<int>(anchor_index)
        : (selected_index_ >= 0 && selected_index_ < assets_.size() ? selected_index_ : index);
    selection_anchor_asset_id_ = assets_.at(anchor).asset_id;
    if (toggle) next_selection = selected_asset_ids_;
    next_selection.reserve(next_selection.size() + std::abs(index - anchor) + 1);
    for (auto row = std::min(anchor, index); row <= std::max(anchor, index); ++row)
      next_selection.insert(assets_.at(row).asset_id);
  } else if (toggle || selection_mode) {
    next_selection = selected_asset_ids_;
    // A normal active photo is the first member when starting a modifier-based
    // multi-selection. Explicit checkbox mode starts from its checked set.
    if (toggle && next_selection.isEmpty() && selected_index_ >= 0 && selected_index_ < assets_.size())
      next_selection.insert(assets_.at(selected_index_).asset_id);
    if (!next_selection.remove(target_id)) next_selection.insert(target_id);
    selection_anchor_asset_id_ = target_id;
  } else {
    next_selection.insert(target_id);
    selection_anchor_asset_id_ = target_id;
  }
  const auto changed = next_selection != selected_asset_ids_;
  selected_asset_ids_ = std::move(next_selection);
  setSelectedIndexInternal(index);
  // One notification for the whole range, including off-screen photos.
  if (changed) emit selectionChanged();
}

bool CatalogAssetModel::selectAssetById(const QString& asset_id, bool refresh_if_selected) {
  const auto normalized_asset_id = asset_id.trimmed();
  if (normalized_asset_id.isEmpty()) {
    return false;
  }
  const auto iterator = visible_asset_index_by_id_.constFind(normalized_asset_id);
  if (iterator == visible_asset_index_by_id_.cend()) {
    return false;
  }
  const auto index = static_cast<int>(iterator.value());
  if (index < 0 || index >= assets_.size()) {
    return false;
  }
  if (selected_index_ == index && refresh_if_selected) {
    emit selectedAssetChanged();
    return true;
  }
  setSelectedIndexInternal(index);
  return selectedAssetId() == normalized_asset_id;
}

QString CatalogAssetModel::assetIdForFilePath(const QString& file_path) const {
  const auto normalized = normalize_local_path(file_path);
  if (normalized.isEmpty()) return {};
  for (const auto& asset : all_assets_) {
    if (!asset.virtual_copy && normalize_local_path(asset.file_path) == normalized) return asset.asset_id;
  }
  return {};
}

bool CatalogAssetModel::prepareRenderedPhotoForEditing(const QString& asset_id) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return {};
  const auto iterator = all_asset_index_by_id_.constFind(asset_id);
  if (iterator == all_asset_index_by_id_.cend()) return false;
  const auto& item = all_assets_.at(iterator.value());
  if (item.media_type != QStringLiteral("photo") || item.virtual_copy) return false;
  const auto parsed = parse_adjustment_payload_or_legacy_recipe(item.adjustment_patch_json);
  if (!item.adjustment_patch_json.trimmed().isEmpty() && !parsed.ok()) return false;
  auto state = parsed.ok() ? *parsed.state : EditRecipeState{};
  // Scene exports already contain a display rendering. Persist the canonical
  // colorimetric output intent, avoiding a second ACES tone rendering. Preserve
  // the import preset and every other recipe field; do not change RAW defaults.
  state.render_intent = QStringLiteral("Relative Colorimetric");
  const auto patch = serialize_adjustment_patch_json(
      state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  return patch == item.adjustment_patch_json ||
      applyRecipeStateToAssetIds({asset_id}, state, all_adjustment_groups(), false) == 1;
}

void CatalogAssetModel::selectAll() {
  if (assets_.isEmpty()) {
    return;
  }

  QSet<QString> next_selected_asset_ids;
  next_selected_asset_ids.reserve(assets_.size());
  for (const auto& item : assets_) {
    next_selected_asset_ids.insert(item.asset_id);
  }

  const auto selection_set_changed = selected_asset_ids_ != next_selected_asset_ids;
  selected_asset_ids_ = next_selected_asset_ids;
  if (selected_index_ < 0 || selected_index_ >= assets_.size()) {
    setSelectedIndexInternal(0);
  }

  if (selection_set_changed) {
    emit selectionChanged();
  }
}

void CatalogAssetModel::selectRelative(int offset) {
  if (offset == 0 || assets_.isEmpty()) {
    return;
  }

  const auto base_index = selected_index_ >= 0 ? selected_index_ : 0;
  const auto max_index = static_cast<int>(assets_.size()) - 1;
  setSelectedIndexInternal(std::clamp(base_index + offset, 0, max_index));
}

QString CatalogAssetModel::pickFolder(const QString& title) const {
  const auto folder_path = QFileDialog::getExistingDirectory(
      nullptr,
      title.isEmpty() ? QStringLiteral("Browse Media Folder") : title,
      QDir::homePath(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (folder_path.isEmpty()) {
    return {};
  }

  return QUrl::fromLocalFile(folder_path).toString();
}

QStringList CatalogAssetModel::pickFiles(const QString& title) const {
  const auto media_filters = supported_import_name_filters().join(QLatin1Char(' '));
  const auto file_paths = QFileDialog::getOpenFileNames(
      nullptr,
      title.isEmpty() ? QStringLiteral("Choose Media Files") : title,
      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
      QStringLiteral("Supported media (%1);;All files (*)").arg(media_filters));

  QStringList normalized_paths;
  normalized_paths.reserve(file_paths.size());
  for (const auto& file_path : file_paths) {
    const auto normalized_path = normalize_local_path(file_path);
    if (!normalized_path.isEmpty() && has_supported_import_extension(normalized_path)) {
      normalized_paths.push_back(normalized_path);
    }
  }
  return normalized_paths;
}

QString CatalogAssetModel::localFileUrlForPath(const QString& file_path) const {
  const auto normalized_path = normalize_local_path(file_path);
  return normalized_path.isEmpty() ? QString{} : QUrl::fromLocalFile(normalized_path).toString();
}

QString CatalogAssetModel::commonDirectoryForFiles(const QStringList& file_paths) const {
  if (file_paths.isEmpty()) return {};

  auto common_directory = normalize_local_path(QFileInfo(file_paths.front()).absolutePath());
  if (common_directory.isEmpty()) return {};
  for (qsizetype index = 1; index < file_paths.size(); ++index) {
    const auto directory = normalize_local_path(QFileInfo(file_paths.at(index)).absolutePath());
    if (directory.isEmpty()) return {};
    while (!local_path_is_within_root(directory, common_directory)) {
      QDir parent(common_directory);
      if (!parent.cdUp()) return {};
      const auto parent_path = normalize_local_path(parent.absolutePath());
      if (parent_path.isEmpty() ||
          parent_path.compare(common_directory, local_path_case_sensitivity()) == 0) {
        return {};
      }
      common_directory = parent_path;
    }
  }
  return common_directory;
}

QString CatalogAssetModel::localPathForUrl(const QString& url) const {
  const auto parsed = QUrl(url);
  if (!parsed.isValid()) {
    return normalize_local_path(url);
  }

  return normalize_local_path(parsed.isLocalFile() ? parsed.toLocalFile() : url);
}

bool CatalogAssetModel::localPathIsWithinRoot(const QString& path, const QString& root) const {
  return local_path_is_within_root(normalize_local_path(path), normalize_local_path(root));
}

QString CatalogAssetModel::parentFolderUrl(const QString& url) const {
  const auto local_path = localPathForUrl(url);
  if (local_path.isEmpty()) {
    return {};
  }

  const QFileInfo info(local_path);
  const auto parent_path = info.dir().absolutePath();
  if (parent_path.isEmpty() || parent_path == local_path) {
    return QUrl::fromLocalFile(local_path).toString();
  }

  return QUrl::fromLocalFile(parent_path).toString();
}

void CatalogAssetModel::createProject(const QString& name) {
  const auto trimmed_name = normalizedProjectName(name);
  if (trimmed_name.isEmpty()) return;
  if (findProject(trimmed_name) != nullptr) {
    setStatus(QStringLiteral("Project '%1' already exists.").arg(trimmed_name));
    return;
  }
  changeProjectMemberships(trimmed_name, {}, true, true);
}

void CatalogAssetModel::createProjectWithSelectedAssets(const QString& name) {
  // Capture the selection before a project filter can change it.
  changeProjectMemberships(name, selectedAssetIds(), true, true);
}

void CatalogAssetModel::openProject(const QString& project_name) {
  const auto trimmed_name = normalizedProjectName(project_name);
  if (trimmed_name.isEmpty()) {
    return;
  }

  if (findProject(trimmed_name) == nullptr) {
    setStatus(QStringLiteral("Project '%1' is unavailable.").arg(trimmed_name));
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  auto filters_changed = false;

  if (project_filter_ != trimmed_name) {
    project_filter_ = trimmed_name;
    filters_changed = true;
  }
  if (import_date_filter_ != QStringLiteral("all")) {
    import_date_filter_ = QStringLiteral("all");
    filters_changed = true;
  }
  if (!import_session_from_filter_.isEmpty() || !import_session_to_filter_.isEmpty()) {
    import_session_from_filter_.clear();
    import_session_to_filter_.clear();
    filters_changed = true;
  }
  if (capture_year_filter_ != QStringLiteral("all")) {
    capture_year_filter_ = QStringLiteral("all");
    filters_changed = true;
  }
  if (camera_filter_ != QStringLiteral("all")) {
    camera_filter_ = QStringLiteral("all");
    filters_changed = true;
  }
  if (!capture_date_from_filter_.isEmpty() || !capture_date_to_filter_.isEmpty()) {
    capture_date_from_filter_.clear();
    capture_date_to_filter_.clear();
    filters_changed = true;
  }
  if (media_type_filter_ != QStringLiteral("all")) {
    media_type_filter_ = QStringLiteral("all");
    filters_changed = true;
  }
  if (filters_changed) {
    emit filtersChanged();
  }

  applyFilters(preferred_asset_id);
  setStatus(QStringLiteral("Opened project '%1'.").arg(trimmed_name));
}

void CatalogAssetModel::addAssetToProject(const QString& asset_id, const QString& project_name) {
  if (!asset_id.trimmed().isEmpty()) changeProjectMemberships(project_name, {asset_id}, true);
}

bool CatalogAssetModel::addAssetToProjectAsync(
    const QString& asset_id,
    const QString& project_name) {
  if (asset_id.trimmed().isEmpty() || normalizedProjectName(project_name).isEmpty()) return false;
  const auto changed = changeProjectMemberships(project_name, {asset_id}, true);
  if (changed) {
    // Timeline creation uses this entry point to expose a newly cataloged asset.
    if (active_project_change_.isEmpty()) refresh();
    else project_refresh_after_save_ = true;
  }
  return changed;
}

void CatalogAssetModel::addSelectedAssetsToProject(const QString& project_name) {
  const auto ids = selectedAssetIds();
  if (!ids.isEmpty()) changeProjectMemberships(project_name, ids, true);
}

void CatalogAssetModel::removeAssetFromProject(const QString& asset_id, const QString& project_name) {
  if (!asset_id.trimmed().isEmpty()) changeProjectMemberships(project_name, {asset_id}, false);
}

void CatalogAssetModel::removeSelectedAssetsFromProject(const QString& project_name) {
  const auto ids = selectedAssetIds();
  if (!ids.isEmpty()) changeProjectMemberships(project_name, ids, false);
}

bool CatalogAssetModel::assetInProject(const QString& asset_id, const QString& project_name) const {
  const auto trimmed_name = normalizedProjectName(project_name);
  if (asset_id.isEmpty() || trimmed_name.isEmpty()) {
    return false;
  }

  const auto* project = findProject(trimmed_name);
  return project != nullptr && project->asset_ids.contains(asset_id);
}

int CatalogAssetModel::assetCountForProject(const QString& project_name) const {
  const auto normalized_name = normalizedProjectName(project_name);

  if (normalized_name == QStringLiteral("all")) {
    return static_cast<int>(all_assets_.size());
  }

  int count = 0;
  if (normalized_name == QStringLiteral("unassigned")) {
    for (const auto& item : all_assets_) {
      if (item.project_names.isEmpty()) {
        ++count;
      }
    }
    return count;
  }

  for (const auto& item : all_assets_) {
    if (item.project_names.contains(normalized_name)) {
      ++count;
    }
  }

  return count;
}

QVariantList CatalogAssetModel::visibleAssets() const {
  QVariantList items;
  items.reserve(assets_.size());
  for (const auto& item : assets_) {
    items.push_back(assetMap(item));
  }
  return items;
}

QVariantList CatalogAssetModel::assetsForProject(const QString& project_name) const {
  QVariantList items;
  const auto normalized_name = normalizedProjectName(project_name);
  items.reserve(all_assets_.size());

  for (const auto& item : all_assets_) {
    if (normalized_name == QStringLiteral("all")) {
      items.push_back(assetMap(item));
      continue;
    }
    if (normalized_name == QStringLiteral("unassigned")) {
      if (item.project_names.isEmpty()) {
        items.push_back(assetMap(item));
      }
      continue;
    }
    if (item.project_names.contains(normalized_name)) {
      items.push_back(assetMap(item));
    }
  }

  return items;
}

void CatalogAssetModel::setSelectedRating(int rating) {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  const auto before = cullingStateForAsset(asset_id);
  auto state = before;
  state.rating = rating;
  state = normalized_culling_state(state);
  setAssetCullingState(asset_id, state);
  recordCullingChange(asset_id, before, state, QStringLiteral("rating"));

  if (state.rating > 0) {
    setStatus(QStringLiteral("Rated selection %1 %2.")
                  .arg(state.rating)
                  .arg(state.rating == 1 ? QStringLiteral("star") : QStringLiteral("stars")));
  } else {
    setStatus(QStringLiteral("Cleared selection rating."));
  }
}

void CatalogAssetModel::toggleSelectedPick() {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  const auto before = cullingStateForAsset(asset_id);
  auto state = before;
  state.picked = !state.picked;
  if (state.picked) {
    state.rejected = false;
  }
  state = normalized_culling_state(state);
  setAssetCullingState(asset_id, state);
  recordCullingChange(asset_id, before, state, QStringLiteral("pick"));
  setStatus(state.picked ? QStringLiteral("Marked selection as picked.")
                         : QStringLiteral("Cleared picked state."));
}

void CatalogAssetModel::toggleSelectedReject() {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  const auto before = cullingStateForAsset(asset_id);
  auto state = before;
  state.rejected = !state.rejected;
  if (state.rejected) {
    state.picked = false;
  }
  state = normalized_culling_state(state);
  setAssetCullingState(asset_id, state);
  recordCullingChange(asset_id, before, state, QStringLiteral("reject"));
  setStatus(state.rejected ? QStringLiteral("Marked selection as rejected.")
                           : QStringLiteral("Cleared rejected state."));
}

void CatalogAssetModel::setSelectedColorLabel(const QString& color_label) {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  const auto before = cullingStateForAsset(asset_id);
  auto state = before;
  const auto requested_label = normalized_culling_state({.color_label = color_label}).color_label;
  state.color_label = state.color_label == requested_label ? QString() : requested_label;
  state = normalized_culling_state(state);
  setAssetCullingState(asset_id, state);
  recordCullingChange(asset_id, before, state, QStringLiteral("color label"));
  setStatus(state.color_label.isEmpty()
                ? QStringLiteral("Cleared selection color label.")
                : QStringLiteral("Labeled selection %1.").arg(state.color_label));
}

void CatalogAssetModel::clearSelectedCullState() {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  const auto before = cullingStateForAsset(asset_id);
  const CullingStateRecord cleared_state;
  setAssetCullingState(asset_id, cleared_state);
  recordCullingChange(asset_id, before, cleared_state, QStringLiteral("clear review"));
  setStatus(QStringLiteral("Cleared culling state for the selection."));
}

void CatalogAssetModel::undoCulling() {
  if (culling_undo_stack_.isEmpty()) {
    return;
  }

  const auto entry = culling_undo_stack_.takeLast();
  setAssetCullingState(entry.asset_id, entry.before);
  culling_redo_stack_.push_back(entry);
  selectAssetById(entry.asset_id);
  setStatus(QStringLiteral("Undid %1.").arg(entry.label));
  emit cullingHistoryChanged();
}

void CatalogAssetModel::redoCulling() {
  if (culling_redo_stack_.isEmpty()) {
    return;
  }

  const auto entry = culling_redo_stack_.takeLast();
  setAssetCullingState(entry.asset_id, entry.after);
  culling_undo_stack_.push_back(entry);
  selectAssetById(entry.asset_id);
  setStatus(QStringLiteral("Redid %1.").arg(entry.label));
  emit cullingHistoryChanged();
}

void CatalogAssetModel::toggleSelectionIndex(int index) {
  if (index < 0 || index >= assets_.size()) {
    return;
  }

  const auto asset_id = assets_.at(index).asset_id;
  auto next_selected_asset_ids = selected_asset_ids_;

  if (next_selected_asset_ids.contains(asset_id)) {
    next_selected_asset_ids.remove(asset_id);
  } else {
    next_selected_asset_ids.insert(asset_id);
  }

  if (next_selected_asset_ids == selected_asset_ids_) {
    return;
  }

  selected_asset_ids_ = next_selected_asset_ids;
  emit selectionChanged();
}

void CatalogAssetModel::clearSelection() {
  if (selected_asset_ids_.isEmpty()) {
    return;
  }

  selected_asset_ids_.clear();
  emit selectionChanged();
}

void CatalogAssetModel::clearSecondarySelection() {
  QString primary_asset_id;
  if (selected_index_ >= 0 && selected_index_ < assets_.size()) {
    const auto current_asset_id = assets_.at(selected_index_).asset_id;
    if (selected_asset_ids_.contains(current_asset_id)) {
      primary_asset_id = current_asset_id;
    }
  }

  if (primary_asset_id.isEmpty()) {
    for (const auto& item : assets_) {
      if (selected_asset_ids_.contains(item.asset_id)) {
        primary_asset_id = item.asset_id;
        break;
      }
    }
  }

  QSet<QString> next_selected_asset_ids;
  if (!primary_asset_id.isEmpty()) {
    next_selected_asset_ids.insert(primary_asset_id);
  }

  if (next_selected_asset_ids == selected_asset_ids_) {
    return;
  }

  selected_asset_ids_ = next_selected_asset_ids;
  emit selectionChanged();
}

void CatalogAssetModel::deleteSelectedAssets() {
  if (deleting_) {
    return;
  }

  const auto selected_ids = selectedAssetIds();
  if (selected_ids.isEmpty()) {
    return;
  }

  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    setErrorMessage(environment_error);
    setStatus(QStringLiteral("Catalog unavailable"));
    return;
  }
  const auto program = stateProgram();
  const auto working_directory = QDir::currentPath();
  QVector<QStringList> commands;
  commands.reserve(selected_ids.size());
  for (const auto& asset_id : selected_ids) {
    commands.push_back(
        stateArguments({QStringLiteral("delete-asset"), asset_id}));
  }

  setDeleting(true);
  setDeleteTargetCount(static_cast<int>(selected_ids.size()));
  setDeleteProgress(0);
  setErrorMessage({});
  setStatus(QStringLiteral("Removing %1 selected %2 from the gallery")
                .arg(selected_ids.size())
                .arg(selected_ids.size() == 1 ? QStringLiteral("asset") : QStringLiteral("assets")));

  delete_cancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto cancellation = delete_cancellation_;
  const auto request_generation = ++delete_request_generation_;
  const auto target_ids = selected_ids;
  QPointer<CatalogAssetModel> that(this);
  delete_watcher_.setFuture(QtConcurrent::run([
                                                that,
                                                target_ids,
                                                commands,
                                                program,
                                                working_directory,
                                                environment,
                                                cancellation,
                                                request_generation
                                              ]() {
    DeleteAssetsTaskResult result;
    result.total_count = static_cast<int>(target_ids.size());
    result.request_generation = request_generation;

    auto completed_count = 0;
    for (auto index = 0; index < target_ids.size(); ++index) {
      const auto& asset_id = target_ids.at(index);
      if (asset_id.isEmpty()) {
        continue;
      }
      if (cancellation->load(std::memory_order_relaxed)) {
        result.canceled = true;
        result.error_message = QStringLiteral("Catalog deletion canceled.");
        break;
      }

      QString error_message;
      if (!run_catalog_state_process(
              program,
              commands.at(index),
              working_directory,
              environment,
              cancellation.get(),
              &error_message)) {
        result.canceled = cancellation->load(std::memory_order_relaxed);
        result.error_message = error_message.isEmpty()
                                   ? QStringLiteral("Unable to remove the selection from the gallery.")
                                   : error_message;
        break;
      }

      result.deleted_asset_ids.push_back(asset_id);
      ++completed_count;
      if (that != nullptr) {
        QMetaObject::invokeMethod(
            that,
            [that, completed_count]() {
              if (that != nullptr) {
                that->setDeleteProgress(completed_count);
              }
            },
            Qt::QueuedConnection);
      }
    }

    return result;
  }));
}

int CatalogAssetModel::applyAdjustmentPatchJsonToAssetIds(
    const QStringList& asset_ids,
    const QString& patch_json,
    const QStringList& adjustment_groups) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return {};

  // Main.qml hands this method the editor's compact, asset-bound serialization
  // when it is leaving the current photo. An exact match for one ordinary
  // photo is already a complete no-op; avoid decoding and merging potentially
  // large paint/mask payloads just to serialize the same recipe again.
  if (asset_ids.size() == 1 && adjustment_groups.isEmpty() &&
      patch_json.startsWith(QStringLiteral("{\"adjustmentState\":")) &&
      patch_json.endsWith(QStringLiteral(
          ",\"format\":\"photo-editor-adjustment-patch\",\"groups\":[\"raw\",\"tone\",\"color\",\"crop\",\"masks\",\"presets\",\"layers\",\"history\",\"detail\"],\"scope\":\"asset-bound\",\"version\":1}"))) {
    const auto asset_id = asset_ids.constFirst().trimmed();
    const auto index_iterator = all_asset_index_by_id_.constFind(asset_id);
    if (!asset_id.isEmpty() &&
        index_iterator != all_asset_index_by_id_.cend() &&
        !transient_adjustment_asset_ids_.contains(asset_id)) {
      const auto asset_index = index_iterator.value();
      if (asset_index >= 0 && asset_index < all_assets_.size()) {
        const auto& item = all_assets_.at(asset_index);
        if (item.media_type == QStringLiteral("photo") &&
            item.adjustment_patch_json == patch_json) {
          // A compact envelope can still contain legacy inner fields. Only
          // bypass normalization after the existing projection path has
          // produced this exact canonical recipe. Verify bytes as well as the
          // cache key so a digest collision cannot authorize a different patch.
          const auto key = QString::fromLatin1(QCryptographicHash::hash(
              patch_json.toUtf8(), QCryptographicHash::Sha1).toHex());
          const auto cached = adjustment_patch_projection_cache_.constFind(key);
          if (cached != adjustment_patch_projection_cache_.cend() &&
              cached.value() == patch_json) return 0;
        }
      }
    }
  }

  const auto patch =
      parse_adjustment_patch_json(patch_json);
  if (!patch.ok()) {
    return 0;
  }
  return applyRecipeStateToAssetIds(
      asset_ids,
      *patch.state,
      adjustment_groups.isEmpty()
          ? patch.groups
      : adjustment_groups);
}

int CatalogAssetModel::applyAgentAdjustmentPatchJsonToAssetIds(
    const QString& task_id,
    const QStringList& asset_ids,
    const QString& patch_json,
    const QStringList& adjustment_groups) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return 0;
  static const QRegularExpression valid_task_id(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
  if (!valid_task_id.match(task_id).hasMatch() || !agent_recipe_persistence_handler_) return 0;
  const auto patch = parse_adjustment_patch_json(patch_json);
  if (!patch.ok()) return 0;
  return applyRecipeStateToAssetIds(
      asset_ids,
      *patch.state,
      adjustment_groups.isEmpty() ? patch.groups : adjustment_groups,
      false,
      false,
      task_id);
}

int CatalogAssetModel::pasteAdjustmentPatchJsonToAssetIds(
    const QStringList& asset_ids, const QString& patch_json) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return {};
  const auto patch = parse_adjustment_patch_json(patch_json);
  if (!patch.ok() || patch.scope != AdjustmentPatchScope::SourceAgnostic) return 0;
  return applyRecipeStateToAssetIds(asset_ids, *patch.state, patch.groups, false, true);
}

int CatalogAssetModel::applyTransientAdjustmentPatchJsonToAssetIds(
    const QStringList& asset_ids,
    const QString& patch_json,
    const QStringList& adjustment_groups) {
  const auto patch = parse_adjustment_patch_json(patch_json);
  if (!patch.ok()) {
    return 0;
  }
  return applyRecipeStateToAssetIds(
      asset_ids,
      *patch.state,
      adjustment_groups.isEmpty() ? patch.groups : adjustment_groups,
      true);
}

int CatalogAssetModel::applyRecipeStateToAssetIds(
    const QStringList& asset_ids,
    const EditRecipeState& source_state,
    const QStringList& adjustment_groups,
    bool transient_preview,
    bool clipboard_paste,
    const QString& task_id) {
  QSet<QString> target_asset_ids;
  target_asset_ids.reserve(asset_ids.size());
  for (const auto& asset_id : asset_ids) {
    const auto trimmed_asset_id = asset_id.trimmed();
    if (!trimmed_asset_id.isEmpty()) {
      target_asset_ids.insert(trimmed_asset_id);
    }
  }

  if (target_asset_ids.isEmpty()) {
    return 0;
  }

  QHash<QString, QString>
      updated_adjustment_patch_jsons;
  QHash<QString, QString>
      previous_adjustment_patch_jsons;
  QHash<QString, QString> updated_thumbnail_sources;
  auto updated_count = 0;

  for (const auto& asset_id : target_asset_ids) {
    const auto index_iterator = all_asset_index_by_id_.constFind(asset_id);
    if (index_iterator == all_asset_index_by_id_.cend()) {
      continue;
    }
    const auto asset_index = index_iterator.value();
    if (asset_index < 0 || asset_index >= all_assets_.size()) {
      continue;
    }
    auto& item = all_assets_[asset_index];
    if (item.media_type != QStringLiteral("photo")) {
      continue;
    }

    const auto current_adjustments =
        parse_adjustment_payload_or_legacy_recipe(
            item.adjustment_patch_json);
    const auto current_state =
        current_adjustments.ok()
            ? *current_adjustments.state
            : EditRecipeState{};
    auto merged_state =
        merge_edit_recipe_state_by_groups(
            current_state,
            source_state,
            adjustment_groups);
    if (clipboard_paste) {
      // Reusable edits carry processing choices, never another photo's input
      // profile or the source workstation's monitor/proof/export bindings.
      merged_state.input_color_profile = current_state.input_color_profile;
      merged_state.camera_dcp_profile_asset = current_state.camera_dcp_profile_asset;
      merged_state.input_icc_profile_asset = current_state.input_icc_profile_asset;
      merged_state.preview_icc_profile_asset = current_state.preview_icc_profile_asset;
      merged_state.proof_icc_profile_asset = current_state.proof_icc_profile_asset;
      merged_state.export_icc_profile_asset = current_state.export_icc_profile_asset;
      merged_state.monitor_color_profile = current_state.monitor_color_profile;
    }
    const auto next_adjustment_patch_json =
        serialize_adjustment_patch_json(
            merged_state,
            all_adjustment_groups(),
            AdjustmentPatchScope::AssetBound);
    const auto promoting_transient_preview =
        !transient_preview &&
        transient_adjustment_asset_ids_.contains(item.asset_id);
    if (next_adjustment_patch_json == item.adjustment_patch_json &&
        !promoting_transient_preview) {
      continue;
    }

    previous_adjustment_patch_jsons.insert(
        item.asset_id,
        item.adjustment_patch_json);
    item.adjustment_patch_json =
        next_adjustment_patch_json;
    if (!transient_preview) {
      clear_thumbnail_source_for_item(&item);
    }
    updated_adjustment_patch_jsons.insert(
        item.asset_id,
        next_adjustment_patch_json);
    updated_thumbnail_sources.insert(item.asset_id, item.thumbnail_source);
    if (transient_preview) {
      transient_adjustment_asset_ids_.insert(item.asset_id);
    } else {
      transient_adjustment_asset_ids_.remove(item.asset_id);
      adjustment_patch_overrides_.insert(
          item.asset_id,
          next_adjustment_patch_json);
      queueAdjustmentPatchPersistence(
          item.asset_id,
          next_adjustment_patch_json,
          task_id);
    }
    if (clipboard_paste) {
      emit photoRecipeChanged(item.asset_id, item.file_path,
          previous_adjustment_patch_jsons.value(item.asset_id), next_adjustment_patch_json, false);
    }
    ++updated_count;
  }

  if (updated_count == 0) {
    return 0;
  }

  emit searchInputsChanged(updated_adjustment_patch_jsons.keys());
  for (auto iterator =
           updated_adjustment_patch_jsons.cbegin();
       iterator !=
       updated_adjustment_patch_jsons.cend();
       ++iterator) {
    const auto visible_index_iterator = visible_asset_index_by_id_.constFind(iterator.key());
    if (visible_index_iterator == visible_asset_index_by_id_.cend()) {
      continue;
    }
    const auto index = visible_index_iterator.value();
    if (index < 0 || index >= assets_.size()) {
      continue;
    }
    auto& item = assets_[index];
    if (item.asset_id != iterator.key()) {
      continue;
    }

    item.adjustment_patch_json =
        iterator.value();
    const auto all_index_iterator = all_asset_index_by_id_.constFind(item.asset_id);
    if (all_index_iterator != all_asset_index_by_id_.cend() &&
        all_index_iterator.value() >= 0 &&
        all_index_iterator.value() < all_assets_.size()) {
      copy_thumbnail_runtime_fields(all_assets_.at(all_index_iterator.value()), &item);
    } else {
      item.thumbnail_source = updated_thumbnail_sources.value(item.asset_id);
    }
    const auto model_index = static_cast<int>(index);
    emit dataChanged(
        this->index(model_index, 0),
        this->index(model_index, 0),
        {AdjustmentPatchJsonRole,
         ThumbnailSourceRole,
         ThumbnailStaleDetectedRole,
         ThumbnailStaleMismatchRole,
         ThumbnailStaleDetectionReasonRole,
         ThumbnailStalePreviousRecipeHashRole,
         ThumbnailStaleCurrentRecipeHashRole,
         ThumbnailStaleDetectionSignatureRole});
  }

  if (!transient_preview) {
    saveProjects();
  }

  if (!transient_preview && (clipboard_paste || updated_adjustment_patch_jsons.size() > 1)) {
    batch_recipe_undo_stack_.push_back(
        {.before_adjustment_patch_jsons =
             previous_adjustment_patch_jsons,
         .after_adjustment_patch_jsons =
             updated_adjustment_patch_jsons,
         .label =
             (clipboard_paste ? QStringLiteral("Pasted edits to %1 photo(s)")
                              : QStringLiteral("Synced adjustments across %1 photos"))
                 .arg(
                     updated_adjustment_patch_jsons
                         .size())});
    constexpr qsizetype maximum_batch_history_entries = 100;
    if (batch_recipe_undo_stack_.size() > maximum_batch_history_entries) {
      batch_recipe_undo_stack_.removeFirst();
    }
    batch_recipe_redo_stack_.clear();
    emit batchRecipeHistoryChanged();
  }

  if (target_asset_ids.contains(selectedAssetId())) {
    emit selectedAssetChanged();
  }

  if (!transient_preview) {
    queueThumbnailRefreshes();
  }

  if (!transient_preview) {
    setStatus(
        QStringLiteral("Applied adjustments to %1 %2.")
            .arg(updated_count)
            .arg(updated_count == 1 ? QStringLiteral("photo")
                                    : QStringLiteral("photos")));
  }
  return updated_count;
}

void CatalogAssetModel::undoBatchRecipe() {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return;
  if (batch_recipe_undo_stack_.isEmpty()) {
    return;
  }

  const auto entry = batch_recipe_undo_stack_.takeLast();
  applyExactAdjustmentPatches(
      entry.before_adjustment_patch_jsons);
  batch_recipe_redo_stack_.push_back(entry);
  setStatus(QStringLiteral("Undid %1.").arg(entry.label));
  emit batchRecipeHistoryChanged();
}

void CatalogAssetModel::redoBatchRecipe() {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return;
  if (batch_recipe_redo_stack_.isEmpty()) {
    return;
  }

  const auto entry = batch_recipe_redo_stack_.takeLast();
  applyExactAdjustmentPatches(
      entry.after_adjustment_patch_jsons);
  batch_recipe_undo_stack_.push_back(entry);
  setStatus(QStringLiteral("Redid %1.").arg(entry.label));
  emit batchRecipeHistoryChanged();
}

void CatalogAssetModel::restoreBatchRecipeHistoryIndex(int index) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return;
  const auto maximum_index =
      static_cast<int>(batch_recipe_undo_stack_.size() + batch_recipe_redo_stack_.size());
  const auto target_index = std::clamp(index, 0, maximum_index);
  while (batchRecipeHistoryCurrentIndex() > target_index && canUndoBatchRecipe()) {
    undoBatchRecipe();
  }
  while (batchRecipeHistoryCurrentIndex() < target_index && canRedoBatchRecipe()) {
    redoBatchRecipe();
  }
}

int CatalogAssetModel::applyExactAdjustmentPatches(
    const QHash<QString, QString>&
        adjustment_patch_jsons) {
  auto updated_count = 0;
  for (auto iterator =
           adjustment_patch_jsons.cbegin();
       iterator !=
       adjustment_patch_jsons.cend();
       ++iterator) {
    const auto all_index_iterator = all_asset_index_by_id_.constFind(iterator.key());
    if (all_index_iterator == all_asset_index_by_id_.cend()) {
      continue;
    }
    const auto all_index = all_index_iterator.value();
    if (all_index < 0 || all_index >= all_assets_.size()) {
      continue;
    }

    auto& all_item = all_assets_[all_index];
    if (all_item.adjustment_patch_json ==
        iterator.value()) {
      continue;
    }
    const auto previous_patch = all_item.adjustment_patch_json;
    all_item.adjustment_patch_json =
        iterator.value();
    clear_thumbnail_source_for_item(&all_item);
    if (iterator.value().trimmed().isEmpty()) {
      adjustment_patch_overrides_.remove(
          iterator.key());
    } else {
      adjustment_patch_overrides_.insert(
          iterator.key(),
          iterator.value());
    }
    queueAdjustmentPatchPersistence(
        iterator.key(),
        iterator.value());

    const auto visible_iterator = visible_asset_index_by_id_.constFind(iterator.key());
    if (visible_iterator != visible_asset_index_by_id_.cend()) {
      const auto visible_index = visible_iterator.value();
      if (visible_index >= 0 && visible_index < assets_.size()) {
        auto& visible_item = assets_[visible_index];
        visible_item.adjustment_patch_json =
            iterator.value();
        copy_thumbnail_runtime_fields(all_item, &visible_item);
        emit dataChanged(
            index(static_cast<int>(visible_index), 0),
            index(static_cast<int>(visible_index), 0),
            {AdjustmentPatchJsonRole,
             ThumbnailSourceRole,
             ThumbnailStaleDetectedRole,
             ThumbnailStaleMismatchRole,
             ThumbnailStaleDetectionReasonRole,
             ThumbnailStalePreviousRecipeHashRole,
             ThumbnailStaleCurrentRecipeHashRole,
             ThumbnailStaleDetectionSignatureRole});
      }
    }
    emit photoRecipeChanged(all_item.asset_id, all_item.file_path,
        previous_patch, iterator.value(), true);
    ++updated_count;
  }

  if (updated_count > 0) {
    emit searchInputsChanged(adjustment_patch_jsons.keys());
    saveProjects();
    queueThumbnailRefreshes();
    emit selectedAssetChanged();
  }
  return updated_count;
}

void CatalogAssetModel::setImportPresetAdjustmentPatchJson(
    const QString& preset_patch_json,
    const QStringList& adjustment_groups) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return;
  const auto decoded =
      parse_adjustment_patch_json(
          preset_patch_json);
  if (!decoded.ok() ||
      decoded.scope !=
          AdjustmentPatchScope::SourceAgnostic) {
    import_preset_adjustment_patch_json_.clear();
    import_preset_adjustment_groups_.clear();
    setStatus(
        QStringLiteral(
            "Import preset must be a valid source-agnostic adjustment patch."));
    return;
  }
  import_preset_adjustment_patch_json_ =
      serialize_adjustment_patch_json(
          *decoded.state,
          decoded.groups,
          AdjustmentPatchScope::SourceAgnostic);
  import_preset_adjustment_groups_ =
      adjustment_groups.isEmpty()
          ? decoded.groups
          : adjustment_groups;
}

void CatalogAssetModel::clearImportPresetAdjustmentPatchJson() {
  import_preset_adjustment_patch_json_.clear();
  import_preset_adjustment_groups_.clear();
}

QString CatalogAssetModel::createVirtualCopyForAsset(
    const QString& asset_id,
    const QString& variant_name) {
  const auto trimmed_asset_id = asset_id.trimmed();
  if (trimmed_asset_id.isEmpty()) {
    return {};
  }

  const auto source_index_iterator = all_asset_index_by_id_.constFind(trimmed_asset_id);
  const auto source_index = source_index_iterator == all_asset_index_by_id_.cend()
                                ? qsizetype{-1}
                                : source_index_iterator.value();
  const auto* source_item =
      source_index >= 0 && source_index < all_assets_.size() ? &all_assets_.at(source_index) : nullptr;
  if (source_item != nullptr && source_item->media_type != QStringLiteral("photo")) {
    source_item = nullptr;
  }
  if (source_item == nullptr) {
    return {};
  }

  const auto root_asset_id = source_item->virtual_copy && !source_item->source_asset_id.trimmed().isEmpty()
                                 ? source_item->source_asset_id.trimmed()
                                 : source_item->asset_id;
  auto next_variant_index = 1;
  for (const auto& item : all_assets_) {
    if (item.source_asset_id == root_asset_id) {
      next_variant_index = std::max(next_variant_index, item.variant_index + 1);
    }
  }

  auto copy = *source_item;
  copy.asset_id = QStringLiteral("%1::variant-%2").arg(root_asset_id).arg(next_variant_index);
  while (std::any_of(
      all_assets_.cbegin(),
      all_assets_.cend(),
      [&copy](const CatalogAssetItem& item) { return item.asset_id == copy.asset_id; })) {
    ++next_variant_index;
    copy.asset_id = QStringLiteral("%1::variant-%2").arg(root_asset_id).arg(next_variant_index);
  }
  copy.virtual_copy = true;
  copy.source_asset_id = root_asset_id;
  copy.variant_index = next_variant_index;
  copy.variant_name = variant_name.trimmed().isEmpty()
                          ? QStringLiteral("Virtual Copy %1").arg(next_variant_index)
                          : variant_name.trimmed();
  copy.camera_label = QStringLiteral("%1 (%2)").arg(source_item->camera_label, copy.variant_name);
  clear_thumbnail_source_for_item(&copy);
  copy.burst_key.clear();
  copy.burst_size = 1;
  copy.burst_position = 1;
  copy.duplicate_group_key.clear();
  copy.duplicate_group_size = 1;
  copy.duplicate_group_position = 1;
  copy.best_shot_suggested = false;
  copy.sync_group_key.clear();
  copy.sync_group_size = 1;

  virtual_copies_.push_back(copy);
  adjustment_patch_overrides_.insert(
      copy.asset_id,
      copy.adjustment_patch_json);
  saveProjects();
  updateAssets([&]() {
    QVector<CatalogAssetItem> base_items;
    base_items.reserve(all_assets_.size());
    for (const auto& item : all_assets_) {
      if (!item.virtual_copy && item.media_type != QStringLiteral("timeline")) {
        base_items.push_back(item);
      }
    }
    return base_items;
  }());
  selectIndex([&]() {
    const auto copy_index_iterator = visible_asset_index_by_id_.constFind(copy.asset_id);
    if (copy_index_iterator != visible_asset_index_by_id_.cend()) {
      return static_cast<int>(copy_index_iterator.value());
    }
    return selected_index_;
  }());
  setStatus(QStringLiteral("Created virtual copy '%1'.").arg(copy.variant_name));
  return copy.asset_id;
}

QString CatalogAssetModel::createVirtualCopyForSelectedAsset(const QString& variant_name) {
  return createVirtualCopyForAsset(selectedAssetId(), variant_name);
}

QString CatalogAssetModel::createCropVariantForAsset(
    const QString& asset_id,
    const QString& variant_name,
    const QString& crop_preset,
    double crop_x,
    double crop_y,
    double crop_width,
    double crop_height) {
  const auto normalized_preset = crop_preset.trimmed();
  const auto has_rect = crop_width > 0.0 && crop_height > 0.0;
  if (normalized_preset.isEmpty() && !has_rect) {
    return {};
  }

  const auto copy_id = createVirtualCopyForAsset(asset_id, variant_name);
  if (copy_id.isEmpty()) {
    return {};
  }

  auto copy_adjustment_patch_json = QString();
  for (const auto& item : all_assets_) {
    if (item.asset_id == copy_id) {
      copy_adjustment_patch_json =
          item.adjustment_patch_json;
      break;
    }
  }
  const auto decoded_adjustments =
      parse_adjustment_payload_or_legacy_recipe(
          copy_adjustment_patch_json);
  auto crop_state =
      decoded_adjustments.ok()
          ? *decoded_adjustments.state
          : EditRecipeState{};
  crop_state.crop_preset = normalized_preset.isEmpty() ? QStringLiteral("Original") : normalized_preset;
  crop_state.crop_zoom = 1.0;
  if (has_rect) {
    const auto width = qBound(0.01, crop_width, 1.0);
    const auto height = qBound(0.01, crop_height, 1.0);
    crop_state.crop_rect_x = qBound(0.0, crop_x, 1.0 - width);
    crop_state.crop_rect_y = qBound(0.0, crop_y, 1.0 - height);
    crop_state.crop_rect_width = width;
    crop_state.crop_rect_height = height;
  } else {
    crop_state.crop_rect_x = 0.0;
    crop_state.crop_rect_y = 0.0;
    crop_state.crop_rect_width = 0.0;
    crop_state.crop_rect_height = 0.0;
  }

  if (applyAdjustmentPatchJsonToAssetIds(
          {copy_id},
          serialize_adjustment_patch_json(
              crop_state,
              all_adjustment_groups(),
              AdjustmentPatchScope::AssetBound)) ==
      1) {
    setStatus(QStringLiteral("Created crop variant '%1'.")
                  .arg(variant_name.trimmed().isEmpty() ? copy_id : variant_name.trimmed()));
  }
  return copy_id;
}

QString CatalogAssetModel::createCropVariantForSelectedAsset(
    const QString& variant_name,
    const QString& crop_preset,
    double crop_x,
    double crop_y,
    double crop_width,
    double crop_height) {
  return createCropVariantForAsset(
      selectedAssetId(), variant_name, crop_preset, crop_x, crop_y, crop_width, crop_height);
}

QStringList CatalogAssetModel::recipeVariantAssetIds(const QString& asset_id) const {
  const auto trimmed_asset_id = asset_id.trimmed();
  if (trimmed_asset_id.isEmpty()) {
    return {};
  }

  QString root_asset_id;
  for (const auto& item : all_assets_) {
    if (item.asset_id == trimmed_asset_id) {
      root_asset_id = item.virtual_copy && !item.source_asset_id.trimmed().isEmpty()
                          ? item.source_asset_id.trimmed()
                          : item.asset_id;
      break;
    }
  }
  if (root_asset_id.isEmpty()) {
    return {};
  }

  QVector<CatalogAssetItem> variants;
  for (const auto& item : all_assets_) {
    if (item.source_asset_id == root_asset_id) {
      variants.push_back(item);
    }
  }
  std::sort(
      variants.begin(),
      variants.end(),
      [](const CatalogAssetItem& left, const CatalogAssetItem& right) {
        return left.variant_index < right.variant_index;
      });

  QStringList asset_ids;
  asset_ids.reserve(variants.size());
  for (const auto& variant : variants) {
    asset_ids.push_back(variant.asset_id);
  }
  return asset_ids;
}

void CatalogAssetModel::refreshOnlineState() {
  auto changed = false;
  for (auto& item : all_assets_) {
    changed = refresh_online_state_for_item(&item) || changed;
  }

  if (!changed) {
    setStatus(QStringLiteral("Rechecked source availability. No changes detected."));
    return;
  }

  applyFilters(selectedAssetId());
  setStatus(QStringLiteral("Rechecked source availability."));
  restartMediaDurationRefresh();
}

int CatalogAssetModel::relinkSelectedAssetToFolder(const QString& folder_url) {
  return relinkAssetsToFolder(folder_url, true);
}

int CatalogAssetModel::relinkOfflineAssetsToFolder(const QString& folder_url) {
  return relinkAssetsToFolder(folder_url, false);
}

bool CatalogAssetModel::markSelectedAssetPermanentlyMissing(bool confirmed) {
  const auto asset_id = selectedAssetId().trimmed();
  if (asset_id.isEmpty() || selectedOnline()) {
    setStatus(QStringLiteral("Select an offline original before marking it permanently missing."));
    return false;
  }
  if (!confirmed) {
    setStatus(QStringLiteral("Confirmation is required. Relinking remains the recommended recovery action."));
    return false;
  }

  permanently_missing_asset_ids_.insert(asset_id);
  saveProjects();
  emit selectedAssetChanged();
  setStatus(QStringLiteral("Marked the original permanently missing without deleting its catalog entry or edits."));
  return true;
}

void CatalogAssetModel::setVisibleAssetRange(int first_index, int last_index) {
  if (first_index < 0 || last_index < first_index || assets_.isEmpty()) {
    first_index = -1;
    last_index = -1;
  } else {
    first_index = std::clamp(first_index, 0, static_cast<int>(assets_.size()) - 1);
    last_index = std::clamp(last_index, first_index, static_cast<int>(assets_.size()) - 1);
  }

  if (visible_thumbnail_first_index_ == first_index && visible_thumbnail_last_index_ == last_index) {
    return;
  }

  visible_thumbnail_first_index_ = first_index;
  visible_thumbnail_last_index_ = last_index;
  prioritizeMediaDurationTasks();
  queueThumbnailRefreshes();
}

void CatalogAssetModel::replaceAssetsForTesting(const QVector<CatalogAssetItem>& items) {
  const auto was_suppressed = timeline_asset_loading_suppressed_for_testing_;
  timeline_asset_loading_suppressed_for_testing_ = true;
  updateAssets(items, validated_thumbnail_reuse_tokens(all_assets_));
  timeline_asset_loading_suppressed_for_testing_ = was_suppressed;
}

void CatalogAssetModel::replaceTimelineAssetsForTesting(const QVector<CatalogAssetItem>& items) {
  const auto was_suppressed = timeline_asset_loading_suppressed_for_testing_;
  timeline_asset_loading_suppressed_for_testing_ = true;
  timeline_assets_ = items;
  timeline_assets_loaded_ = true;
  reconcileTimelineAssets();
  timeline_asset_loading_suppressed_for_testing_ = was_suppressed;
}

bool CatalogAssetModel::applyThumbnailResultForTesting(
    const QString& asset_id,
    const QString& file_path,
    const QString& adjustment_patch_json,
    const QString& thumbnail_source) {
  return applyThumbnailResult(ThumbnailTaskResult{
      .asset_id = asset_id,
      .file_path = file_path,
      .adjustment_patch_json =
          adjustment_patch_json,
      .thumbnail_source = thumbnail_source,
  });
}

void CatalogAssetModel::clearVirtualCopiesForTesting() {
  QVector<CatalogAssetItem> base_assets;
  base_assets.reserve(all_assets_.size());
  for (const auto& item : all_assets_) {
    if (!item.virtual_copy) {
      base_assets.push_back(item);
    }
  }
  virtual_copies_.clear();
  replaceAssetsForTesting(base_assets);
}

void CatalogAssetModel::setProjectFilter(const QString& project_filter) {
  auto next_filter = normalizedProjectName(project_filter);
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (project_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  project_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setImportDateFilter(const QString& import_date_filter) {
  auto next_filter = import_date_filter.trimmed().toLower();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (import_date_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  import_date_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setCaptureYearFilter(const QString& capture_year_filter) {
  auto next_filter = capture_year_filter.trimmed().toLower();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (capture_year_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  capture_year_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setCameraFilter(const QString& camera_filter) {
  auto next_filter = camera_filter.trimmed();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }
  if (camera_filter_ == next_filter) {
    return;
  }
  const auto preferred_asset_id = selectedAssetId();
  camera_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setCaptureDateFromFilter(const QString& capture_date_from_filter) {
  const auto trimmed = capture_date_from_filter.trimmed();
  if (!trimmed.isEmpty() && !QDate::fromString(trimmed, Qt::ISODate).isValid()) {
    return;
  }
  if (capture_date_from_filter_ == trimmed) {
    return;
  }
  const auto preferred_asset_id = selectedAssetId();
  capture_date_from_filter_ = trimmed;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setCaptureDateToFilter(const QString& capture_date_to_filter) {
  const auto trimmed = capture_date_to_filter.trimmed();
  if (!trimmed.isEmpty() && !QDate::fromString(trimmed, Qt::ISODate).isValid()) {
    return;
  }
  if (capture_date_to_filter_ == trimmed) {
    return;
  }
  const auto preferred_asset_id = selectedAssetId();
  capture_date_to_filter_ = trimmed;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setMediaTypeFilter(const QString& media_type_filter) {
  auto next_filter = media_type_filter.trimmed().toLower();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (media_type_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  media_type_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setMediaCollectionFilter(const QString& media_collection_filter) {
  const auto next_filter = media_collection_filter.trimmed().toLower();
  if ((next_filter != QStringLiteral("all") && next_filter != QStringLiteral("images")) ||
      media_collection_filter_ == next_filter) {
    return;
  }
  const auto preferred_asset_id = selectedAssetId();
  media_collection_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
  if (media_collection_filter_ == QStringLiteral("images")) refreshDocumentSourceExclusions();
}

void CatalogAssetModel::refreshDocumentSourceExclusions() {
  if (document_source_exclusions_watcher_.isRunning()) {
    document_source_exclusions_refresh_pending_ = true;
    return;
  }
  const auto root = AppBootstrap::appDataLocation();
  const auto boards_override = qEnvironmentVariable("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH").trimmed();
  document_source_exclusions_watcher_.setFuture(QtConcurrent::run([root, boards_override]() {
    return board_owned_canvas_sources(root, boards_override);
  }));
}

void CatalogAssetModel::setReviewFilter(const QString& review_filter) {
  auto next_filter = review_filter.trimmed().toLower();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (next_filter != QStringLiteral("all") && next_filter != QStringLiteral("picked") &&
      next_filter != QStringLiteral("rejected") && next_filter != QStringLiteral("unmarked") &&
      next_filter != QStringLiteral("rated") && next_filter != QStringLiteral("unrated")) {
    next_filter = QStringLiteral("all");
  }

  if (review_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  review_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setAvailabilityFilter(const QString& availability_filter) {
  auto next_filter = availability_filter.trimmed().toLower();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (next_filter != QStringLiteral("all") && next_filter != QStringLiteral("online") &&
      next_filter != QStringLiteral("offline")) {
    next_filter = QStringLiteral("all");
  }

  if (availability_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  availability_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setMinimumRatingFilter(int minimum_rating_filter) {
  const auto next_filter = std::clamp(minimum_rating_filter, 0, 5);
  if (minimum_rating_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  minimum_rating_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setSearchFilter(const QString& search_filter) {
  const auto next_filter = search_filter.trimmed();
  if (search_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  search_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setVisualSearchResults(const QStringList& ranked_asset_ids) {
  const auto preferred = selectedAssetId();
  visual_search_ranks_.clear();
  for (const auto& id : ranked_asset_ids) {
    if (!visual_search_ranks_.contains(id)) {
      visual_search_ranks_.insert(id, static_cast<int>(visual_search_ranks_.size()));
    }
  }
  visual_search_active_ = true;  // Empty results must produce an empty view, not all photos.
  emit filtersChanged();
  applyFilters(preferred);
}

void CatalogAssetModel::clearVisualSearchResults() {
  if (!visual_search_active_) return;
  const auto preferred = selectedAssetId();
  visual_search_active_ = false;
  visual_search_ranks_.clear();
  emit filtersChanged();
  applyFilters(preferred);
}

void CatalogAssetModel::handleSnapshotFinished(int exit_code, QProcess::ExitStatus exit_status) {
  snapshot_fetch_active_ = false;
  auto bytes = snapshot_process_.readAllStandardOutput();
  const auto error_bytes = snapshot_process_.readAllStandardError();
  if (snapshot_fetch_generation_ != snapshot_request_generation_) {
    startSnapshotFetch();
    return;
  }
  if (exit_status != QProcess::NormalExit || exit_code != 0) {
    pending_import_file_paths_.clear();
    pending_import_project_name_.clear();
    // A failed streaming helper can leave a large partial JSON document on
    // stdout. Show bounded diagnostics only, never recipe data from that prefix.
    const auto diagnostic = QString::fromUtf8(error_bytes.left(4096)).trimmed();
    setErrorMessage(diagnostic.isEmpty()
        ? QStringLiteral("Catalog snapshot helper failed (exit %1).").arg(exit_code)
        : diagnostic);
    setStatus(QStringLiteral("Catalog unavailable"));
    failPendingBlankCanvas(error_message_);
    setLoading(false);
    return;
  }

  snapshot_parse_cancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto cancellation = snapshot_parse_cancellation_;
  const auto generation = snapshot_fetch_generation_;
  const auto recipe_asset_root = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("recipe-assets"));
  const auto previous_thumbnails = all_assets_; // Implicitly shared, immutable worker snapshot.
  snapshot_parse_active_ = true;
  snapshot_parse_watcher_.setFuture(QtConcurrent::run(&catalog_state_thread_pool(),
      [bytes = std::move(bytes), generation, cancellation, recipe_asset_root, previous_thumbnails]() {
        CatalogSnapshotResult result;
        result.generation = generation;
        result.payload_bytes = bytes.size();
        QElapsedTimer preparation_timer;
        preparation_timer.start();
        try {
          if (cancellation->load(std::memory_order_relaxed)) return result;
          const auto payload = QJsonDocument::fromJson(bytes);
          if (!payload.isObject()) {
            result.error = QStringLiteral("Catalog snapshot output was not valid JSON.");
            return result;
          }
          const auto root = payload.object();
          result.assets = parse_assets(root.value(QStringLiteral("assets")).toArray(),
                                       recipe_asset_root, cancellation);
          if (cancellation->load(std::memory_order_relaxed)) {
            result.assets.clear();
            return result;
          }
          result.reusable_thumbnail_tokens = validated_thumbnail_reuse_tokens(previous_thumbnails, cancellation);
          result.library_state = parse_library_state(root, cancellation);
          result.drive_count = static_cast<int>(root.value(QStringLiteral("drives")).toArray().size());
        } catch (const std::exception& error) {
          result.assets.clear();
          result.error = QStringLiteral("Catalog snapshot preparation failed: %1").arg(QString::fromUtf8(error.what()));
        } catch (...) {
          result.assets.clear();
          result.error = QStringLiteral("Catalog snapshot preparation failed.");
        }
        result.preparation_ms = preparation_timer.elapsed();
        return result;
      }));
}

void CatalogAssetModel::handleSnapshotParsed() {
  snapshot_parse_active_ = false;
  auto result = snapshot_parse_watcher_.future().takeResult();
  if (result.generation != snapshot_request_generation_) {
    startSnapshotFetch();
    return;
  }
  const auto loading_guard = qScopeGuard([this, generation = result.generation]() {
    if (generation == snapshot_request_generation_) setLoading(false);
  });
  if (!result.error.isEmpty()) {
    pending_import_file_paths_.clear();
    pending_import_project_name_.clear();
    setErrorMessage(result.error);
    failPendingBlankCanvas(result.error);
    setStatus(QStringLiteral("Catalog unavailable"));
    return;
  }

  QElapsedTimer publication_timer;
  publication_timer.start();
  const auto publication_count = result.assets.size();
  const auto publication_guard = qScopeGuard([&]() {
    observability::logInfo(photoEditorCatalogLog(), QStringLiteral("catalog.snapshot.publish"), {
        {QStringLiteral("generation"), static_cast<qint64>(result.generation)},
        {QStringLiteral("asset_count"), static_cast<qint64>(publication_count)},
        {QStringLiteral("payload_bytes"), result.payload_bytes},
        {QStringLiteral("preparation_ms"), result.preparation_ms},
        {QStringLiteral("publication_ms"), publication_timer.elapsed()},
    });
  });

  // Preserve edits made while fetch/projection ran, even if their persistence
  // already completed. The next snapshot can otherwise contain older values.
  QHash<QString, CullingStateRecord> changed_reviews;
  QHash<QString, QString> changed_relinks;
  for (const auto& id : snapshot_changed_review_ids_) changed_reviews.insert(id, cullingStateForAsset(id));
  for (const auto& id : snapshot_changed_relink_ids_) changed_relinks.insert(id, relinked_file_paths_.value(id));
  loadPersistedLibraryState(std::move(result.library_state));
  for (auto it = changed_reviews.cbegin(); it != changed_reviews.cend(); ++it) {
    if (culling_state_is_default(it.value())) culling_states_.remove(it.key());
    else culling_states_.insert(it.key(), it.value());
  }
  for (auto it = changed_relinks.cbegin(); it != changed_relinks.cend(); ++it) {
    relinked_file_paths_.insert(it.key(), it.value());
  }
  if (!snapshot_deleted_asset_ids_.isEmpty()) {
    result.assets.removeIf([this](const CatalogAssetItem& item) { return snapshot_deleted_asset_ids_.contains(item.asset_id); });
    for (const auto& id : snapshot_deleted_asset_ids_) {
      culling_states_.remove(id);
      relinked_file_paths_.remove(id);
    }
  }
  updateAssets(std::move(result.assets), result.reusable_thumbnail_tokens);
  if (result.generation != snapshot_request_generation_) return;
  rebuildImportSources();
  setDriveCount(result.drive_count);
  setStatus(QStringLiteral("Loaded %1 assets across %2 drives.").arg(all_assets_.size()).arg(drive_count_));
  finalizePendingImportProjectAssignment();
}

void CatalogAssetModel::handleImportFinished(int exit_code, QProcess::ExitStatus exit_status) {
  Q_UNUSED(exit_code);
  Q_UNUSED(exit_status);
}

void CatalogAssetModel::pollActiveImportJob() {
  if (import_job_poll_in_flight_) {
    import_job_poll_dirty_ = true;
    return;
  }
  import_job_poll_in_flight_ = true;
  import_job_poll_dirty_ = false;
  import_job_poll_watcher_.setFuture(QtConcurrent::run(&import_job_poll_thread_pool(), [] {
    return loadPersistedJobRecords(nullptr, false);
  }));
}

void CatalogAssetModel::handleImportJobPollFinished() {
  import_job_poll_in_flight_ = false;
  QVector<PersistentJobRecord> records;
  try {
    records = import_job_poll_watcher_.result();
  } catch (...) {
    // Keep the last visible state if an asynchronous store read fails.
  }
  const auto rerun = import_job_poll_dirty_;
  import_job_poll_dirty_ = false;
  consumeImportedJobRecords(records);
  if (rerun && !import_job_poll_in_flight_) pollActiveImportJob();
}

void CatalogAssetModel::consumeImportedJobRecords(
    const QVector<PersistentJobRecord>& records) {
  if (active_import_job_.job_id.isEmpty()) {
    PersistentJobRecord latest_active_import_job;
    auto latest_active_updated_at = QDateTime{};
    auto has_active_import_job = false;
    auto observed_finished_import_job = false;
    const auto observation_started_at = parse_job_timestamp_utc(import_job_observation_started_at_);

    for (const auto& record : records) {
      if (is_import_job_active(record)) {
        const auto updated_at = parse_job_timestamp_utc(record.updated_at);
        if (!has_active_import_job || !latest_active_updated_at.isValid() ||
            (updated_at.isValid() && updated_at > latest_active_updated_at)) {
          latest_active_import_job = record;
          latest_active_updated_at = updated_at;
        }
        has_active_import_job = true;
        continue;
      }

      if (!is_import_job_finished(record) || observed_finished_import_job_ids_.contains(record.job_id)) {
        continue;
      }

      const auto updated_at = parse_job_timestamp_utc(record.updated_at);
      if (observation_started_at.isValid() && updated_at.isValid() && updated_at < observation_started_at) {
        continue;
      }

      observed_finished_import_job = true;
      observed_finished_import_job_ids_.insert(record.job_id);
    }

    if (has_active_import_job) {
      setImporting(true);
      setStatus(
          latest_active_import_job.status == QStringLiteral("Queued")
              ? QStringLiteral("Queued source import")
              : QStringLiteral("Importing source into catalog"));
      return;
    }

    setImporting(false);
    if (observed_finished_import_job) {
      refresh();
    }
    return;
  }

  const auto iterator = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == active_import_job_.job_id; });
  if (iterator == records.cend()) {
    return;
  }

  const auto job = *iterator;
  if (job.status == QStringLiteral("Queued")) {
    setImporting(true);
    setStatus(QStringLiteral("Queued source import"));
    return;
  }

  if (job.status == QStringLiteral("Running")) {
    setImporting(true);
    setStatus(QStringLiteral("Importing source into catalog"));
    return;
  }

  setImporting(false);

  if (!import_selection_manifest_path_.isEmpty()) {
    QFile::remove(import_selection_manifest_path_);
    import_selection_manifest_path_.clear();
  }

  if (job.status == QStringLiteral("Failed")) {
    pending_import_file_paths_.clear();
    pending_import_project_name_.clear();
    const auto failure_message = job.error_message.trimmed().isEmpty() ? QStringLiteral("Import failed.") : job.error_message.trimmed();
    setErrorMessage(failure_message);
    failPendingBlankCanvas(failure_message);
    active_import_job_ = PersistentJobRecord{};
    setStatus(QStringLiteral("Import failed"));
    if (prepared_blank_canvas_operation_ &&
        prepared_blank_canvas_operation_->import_job_id == job.job_id) {
      prepared_blank_canvas_operation_->terminal_import_status = job.status;
      prepared_blank_canvas_operation_->status = QStringLiteral("awaiting-catalog");
      prepared_blank_canvas_operation_->error = failure_message;
      emit preparedBlankCanvasChanged(prepared_blank_canvas_operation_->task_id);
      refresh();
    }
    return;
  }

  if (job.status != QStringLiteral("Completed") && job.status != QStringLiteral("Canceled")) {
    return;
  }

  if (prepared_blank_canvas_operation_ &&
      prepared_blank_canvas_operation_->import_job_id == job.job_id) {
    prepared_blank_canvas_operation_->terminal_import_status = job.status;
    prepared_blank_canvas_operation_->status = QStringLiteral("awaiting-catalog");
    if (job.status == QStringLiteral("Canceled")) {
      prepared_blank_canvas_operation_->error = job.error_message.trimmed().isEmpty()
          ? QStringLiteral("The prepared canvas import was canceled.") : job.error_message.trimmed();
    }
    active_import_job_ = PersistentJobRecord{};
    setStatus(job.status == QStringLiteral("Completed")
        ? QStringLiteral("Checking prepared canvas catalog receipt")
        : QStringLiteral("Checking canceled canvas import"));
    emit preparedBlankCanvasChanged(prepared_blank_canvas_operation_->task_id);
    refresh();
    return;
  }

  active_import_job_ = PersistentJobRecord{};
  if (!pending_blank_canvas_path_.isEmpty()) blank_canvas_import_finished_ = true;
  const auto success_message = job.metadata_json.value(QStringLiteral("resultMessage")).toString().trimmed();
  setStatus(success_message.isEmpty() ? QStringLiteral("Import completed.") : success_message);
  refresh();
}

void CatalogAssetModel::handleDeleteFinished() {
  const auto result = delete_watcher_.result();
  if (result.request_generation != delete_request_generation_) {
    return;
  }
  delete_cancellation_.reset();
  setDeleting(false);
  setDeleteTargetCount(0);
  setDeleteProgress(0);

  if (!result.deleted_asset_ids.isEmpty()) {
    removeDeletedAssets(result.deleted_asset_ids);
  }

  if (!result.error_message.isEmpty()) {
    setErrorMessage(result.error_message);
    if (!result.deleted_asset_ids.isEmpty()) {
      setStatus(QStringLiteral("Removed %1 %2 before the delete stopped.")
                    .arg(result.deleted_asset_ids.size())
                    .arg(result.deleted_asset_ids.size() == 1 ? QStringLiteral("asset") : QStringLiteral("assets")));
    } else {
      setStatus(result.error_message);
    }
    return;
  }

  setStatus(QStringLiteral("Removed %1 %2 from the gallery.")
                .arg(result.deleted_asset_ids.size())
                .arg(result.deleted_asset_ids.size() == 1 ? QStringLiteral("asset") : QStringLiteral("assets")));
}

void CatalogAssetModel::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }

  status_ = status;
  emit statusChanged();
}

void CatalogAssetModel::failPendingBlankCanvas(const QString& message) {
  if (!pending_blank_canvas_path_.isEmpty()) {
    pending_blank_canvas_path_.clear();
    emit blankCanvasBusyChanged();
    emit blankCanvasFailed(message);
  }
}

void CatalogAssetModel::setErrorMessage(const QString& message) {
  if (error_message_ == message) {
    return;
  }

  error_message_ = message;
  emit errorMessageChanged();
}

void CatalogAssetModel::setLoading(bool loading) {
  if (loading_ == loading) {
    return;
  }

  loading_ = loading;
  emit loadingChanged();
  emit busyChanged();
}

void CatalogAssetModel::setImporting(bool importing) {
  if (importing_ == importing) {
    return;
  }

  importing_ = importing;
  emit importingChanged();
  emit busyChanged();
}

void CatalogAssetModel::setDeleting(bool deleting) {
  if (deleting_ == deleting) {
    return;
  }

  deleting_ = deleting;
  emit deletingChanged();
  emit busyChanged();
}

void CatalogAssetModel::setDeleteProgress(int delete_progress) {
  if (delete_progress_ == delete_progress) {
    return;
  }

  delete_progress_ = delete_progress;
  emit deleteProgressChanged();
}

void CatalogAssetModel::setDeleteTargetCount(int delete_target_count) {
  if (delete_target_count_ == delete_target_count) {
    return;
  }

  delete_target_count_ = delete_target_count;
  emit deleteProgressChanged();
}

void CatalogAssetModel::removeDeletedAssets(const QStringList& asset_ids) {
  if (!asset_ids.isEmpty()) emit searchInputsChanged(asset_ids);
  QSet<QString> deleted_asset_ids;
  deleted_asset_ids.reserve(asset_ids.size());
  for (const auto& asset_id : asset_ids) {
    if (!asset_id.isEmpty()) {
      deleted_asset_ids.insert(asset_id);
    }
  }

  if (deleted_asset_ids.isEmpty()) {
    return;
  }

  for (const auto& id : deleted_asset_ids) {
    if (pending_culling_persistence_.remove(id) > 0) culling_outbox_dirty_ = true;
    pending_culling_persistence_order_.removeAll(id);
    failed_culling_persistence_.remove(id);
  }
  if (culling_outbox_dirty_ && !culling_save_timer_.isActive()) culling_save_timer_.start(0);

  auto next_project_changes = pending_project_changes_;
  QStringList changed_projects;
  for (auto it = pending_project_changes_.begin(); it != pending_project_changes_.end(); ++it) {
    auto members = it.value().toObject();
    bool changed = false;
    for (const auto& id : deleted_asset_ids) {
      if (members.value(id).toBool()) {
        members.insert(id, false);
        changed = true;
      }
    }
    if (changed) {
      next_project_changes.insert(it.key(), members);
      changed_projects.push_back(it.key());
    }
  }
  if (!changed_projects.isEmpty() && saveProjectChanges(next_project_changes)) {
    pending_project_changes_ = next_project_changes;
    for (const auto& name : changed_projects) project_change_revisions_.insert(name, ++next_project_change_revision_);
    startNextProjectChange();
  }

  const auto previous_total_count = all_assets_.size();
  all_assets_.erase(
      std::remove_if(
          all_assets_.begin(),
          all_assets_.end(),
          [&deleted_asset_ids](const CatalogAssetItem& item) {
            return deleted_asset_ids.contains(item.asset_id);
          }),
      all_assets_.end());

  for (auto& project : projects_) {
    for (const auto& asset_id : deleted_asset_ids) {
      project.asset_ids.remove(asset_id);
    }
  }

  for (const auto& asset_id : deleted_asset_ids) {
    if (snapshot_fetch_active_ || snapshot_parse_active_) snapshot_deleted_asset_ids_.insert(asset_id);
    culling_states_.remove(asset_id);
    adjustment_patch_overrides_.remove(
        asset_id);
    relinked_file_paths_.remove(asset_id);
    queued_thumbnail_asset_ids_.remove(asset_id);
    pending_thumbnail_asset_ids_.removeAll(asset_id);
    thumbnail_queued_at_msecs_.remove(asset_id);
  }
  virtual_copies_.erase(
      std::remove_if(
          virtual_copies_.begin(),
          virtual_copies_.end(),
          [&deleted_asset_ids](const CatalogAssetItem& item) {
            return deleted_asset_ids.contains(item.asset_id) ||
                   deleted_asset_ids.contains(item.source_asset_id);
          }),
      virtual_copies_.end());

  rebuildProjectMetadata();
  rebuildCaptureYears();
  rebuildBurstMetadata();
  rebuildIntelligenceMetadata();
  rebuildAllAssetIndexMap();
  bumpProjectStatsRevision();
  saveProjects();
  applyFilters();

  if (previous_total_count != all_assets_.size()) {
    emit totalAssetCountChanged();
  }
}

bool CatalogAssetModel::beginImportJob(
    const QString& folder_path,
    const QString& project_name,
    const QStringList& selected_file_paths,
    int selection_count,
    const QString& import_mode,
    const QJsonObject& import_preflight,
    const QString& prepared_task_id,
    const QString& prepared_job_id,
    const QString& prepared_document_id,
    const QString& prepared_source_sha256) {
  active_import_job_ = PersistentJobRecord{};
  active_import_job_.job_id = prepared_job_id.isEmpty()
      ? QUuid::createUuid().toString(QUuid::WithoutBraces) : prepared_job_id;
  active_import_job_.job_kind = QStringLiteral("catalog_import");
  active_import_job_.display_label = QFileInfo(folder_path).fileName().trimmed().isEmpty()
                                         ? folder_path
                                         : QFileInfo(folder_path).fileName().trimmed();
  active_import_job_.source_path = folder_path;
  active_import_job_.preset_name = import_mode;
  active_import_job_.status = QStringLiteral("Queued");
  active_import_job_.progress = 0;
  active_import_job_.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  active_import_job_.updated_at = active_import_job_.created_at;
  QJsonArray selected_paths;
  for (const auto& file_path : selected_file_paths) {
    selected_paths.push_back(file_path);
  }
  active_import_job_.metadata_json = QJsonObject{
      {QStringLiteral("projectName"), project_name},
      {QStringLiteral("selectionCount"), selection_count},
      {QStringLiteral("importMode"), import_mode},
      {QStringLiteral("selectedPaths"), selected_paths},
      {QStringLiteral("importPreflight"), import_preflight},
      {QStringLiteral("duplicatePolicy"), import_preflight.value(QStringLiteral("duplicatePolicy"))},
  };
  if (!prepared_task_id.isEmpty()) {
    active_import_job_.metadata_json.insert(QStringLiteral("preparedTaskId"), prepared_task_id);
    active_import_job_.metadata_json.insert(QStringLiteral("preparedDocumentId"), prepared_document_id);
    active_import_job_.metadata_json.insert(QStringLiteral("preparedSourceSha256"), prepared_source_sha256);
  }
  QString error;
  if (!upsertPersistedJobRecord(active_import_job_, &error)) {
    active_import_job_ = PersistentJobRecord{};
    setErrorMessage(error.isEmpty() ? QStringLiteral("Could not queue the import job.") : error);
    failPendingBlankCanvas(error_message_);
    setStatus(QStringLiteral("Import failed"));
    return false;
  }
  return true;
}

void CatalogAssetModel::finishImportJob(bool success, const QString& message) {
  if (active_import_job_.job_id.isEmpty()) {
    return;
  }

  active_import_job_.status = success ? QStringLiteral("Completed") : QStringLiteral("Failed");
  active_import_job_.progress = success ? 100 : 0;
  active_import_job_.error_message = success ? QString{} : message;
  active_import_job_.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  (void)upsertPersistedJobRecord(active_import_job_, nullptr);
  active_import_job_ = PersistentJobRecord{};
}

void CatalogAssetModel::setDriveCount(int drive_count) {
  if (drive_count_ == drive_count) {
    return;
  }

  drive_count_ = drive_count;
  emit driveCountChanged();
}

void CatalogAssetModel::rebuildImportSources() {
  const auto next_sources = build_import_sources();
  import_sources_last_rebuilt_msecs_ = QDateTime::currentMSecsSinceEpoch();
  if (import_sources_ == next_sources) {
    return;
  }

  import_sources_ = next_sources;
  emit importSourcesChanged();
}

bool CatalogAssetModel::runCatalogStateCommand(
    const QStringList& command_arguments,
    QString* error_message) const {
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    if (error_message != nullptr) {
      *error_message = environment_error;
    }
    return false;
  }

  return run_catalog_state_process(
      stateProgram(),
      stateArguments(command_arguments),
      QDir::currentPath(),
      environment,
      nullptr,
      error_message);
}

QByteArray CatalogAssetModel::preparedBlankCanvasCatalogSnapshot(
    const QString& source_path, const QString& task_id, const QString& import_job_id) const {
  if (source_path.trimmed().isEmpty() || task_id.trimmed().isEmpty() || import_job_id.trimmed().isEmpty()) return {};
  QJsonObject row;
  QString error;
  if (!run_catalog_state_json_command(
          QStringLiteral("agent-catalog-row-snapshot"),
          {{QStringLiteral("sourcePath"), stable_prepared_catalog_source_path(source_path)}}, &row, &error)) return {};
  if (row.value(QStringLiteral("schema")).toString() != QStringLiteral("agent-catalog-row:v1") ||
      row.value(QStringLiteral("source_path")).toString().isEmpty()) return {};
  const auto operation = prepared_blank_canvas_operation_;
  if (operation && operation->task_id == task_id && operation->import_job_id == import_job_id &&
      row.value(QStringLiteral("state")).toString() == QStringLiteral("present") &&
      row.value(QStringLiteral("source_sha256")).toString() != operation->source_sha256) return {};
  row.insert(QStringLiteral("taskId"), task_id);
  row.insert(QStringLiteral("importJobId"), import_job_id);
  row.remove(QStringLiteral("checkpointHash"));
  const auto hash = QCryptographicHash::hash(
      QJsonDocument(row).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
  row.insert(QStringLiteral("checkpointHash"), QString::fromLatin1(hash));
  return QJsonDocument(row).toJson(QJsonDocument::Compact);
}

AgentTaskJournal::ResourceAdapter CatalogAssetModel::preparedBlankCanvasCatalogResourceAdapter(
    const QString& source_path, const QString& task_id, const QString& import_job_id) {
  const auto guarded_model = QPointer<CatalogAssetModel>(this);
  const auto last_read = std::make_shared<QByteArray>();
  AgentTaskJournal::ResourceAdapter adapter;
  adapter.read = [guarded_model, last_read, source_path, task_id, import_job_id] {
    if (!guarded_model) return QByteArray{};
    *last_read = guarded_model->preparedBlankCanvasCatalogSnapshot(source_path, task_id, import_job_id);
    return *last_read;
  };
  adapter.restore = [guarded_model, last_read, source_path, task_id, import_job_id](const QByteArray& before) {
    return guarded_model && guarded_model->restorePreparedBlankCanvasCatalogSnapshot(
        source_path, *last_read, before, task_id, import_job_id, nullptr);
  };
  adapter.cancel = [guarded_model, task_id] {
    if (guarded_model) (void)guarded_model->cancelPreparedBlankCanvas(task_id);
  };
  return adapter;
}

bool CatalogAssetModel::restorePreparedBlankCanvasCatalogSnapshot(
    const QString& source_path, const QByteArray& expected_bytes, const QByteArray& before_bytes,
    const QString& task_id, const QString& import_job_id, QString* error_message) {
  return restorePreparedBlankCanvasCatalogSnapshotExpected(
      source_path, expected_bytes, before_bytes, task_id, import_job_id, error_message);
}

bool CatalogAssetModel::restorePreparedBlankCanvasCatalogSnapshotExpected(
    const QString& source_path, const QByteArray& expected_bytes, const QByteArray& before_bytes,
    const QString& task_id, const QString& import_job_id, QString* error_message) {
  const auto fail = [error_message](const QString& error) {
    if (error_message != nullptr) *error_message = error;
    return false;
  };
  QJsonParseError expected_parse;
  QJsonParseError before_parse;
  const auto expected_document = QJsonDocument::fromJson(expected_bytes, &expected_parse);
  const auto before_document = QJsonDocument::fromJson(before_bytes, &before_parse);
  if (expected_parse.error != QJsonParseError::NoError || !expected_document.isObject() ||
      before_parse.error != QJsonParseError::NoError || !before_document.isObject())
    return fail(QStringLiteral("The catalog row checkpoint is invalid."));
  auto expected = expected_document.object();
  const auto before = before_document.object();
  const auto identity_matches = [&](const QJsonObject& value) {
    return value.value(QStringLiteral("schema")).toString() == QStringLiteral("agent-catalog-row:v1") &&
        value.value(QStringLiteral("taskId")).toString() == task_id &&
        value.value(QStringLiteral("importJobId")).toString() == import_job_id &&
        stable_prepared_catalog_source_path(value.value(QStringLiteral("source_path")).toString()) ==
            stable_prepared_catalog_source_path(source_path);
  };
  if (!identity_matches(expected) || !identity_matches(before) ||
      before.value(QStringLiteral("state")).toString() != QStringLiteral("absent"))
    return fail(QStringLiteral("The catalog row baseline does not match this prepared canvas."));
  const auto current_bytes = preparedBlankCanvasCatalogSnapshot(source_path, task_id, import_job_id);
  if (current_bytes != expected_bytes) {
    if (expected.value(QStringLiteral("state")).toString() == QStringLiteral("absent") &&
        current_bytes == before_bytes) return true;
    return fail(QStringLiteral("The catalog row changed after its last checkpoint; it was preserved."));
  }
  if (expected.value(QStringLiteral("state")).toString() == QStringLiteral("absent")) return true;
  const auto operation = prepared_blank_canvas_operation_;
  if (preparedBlankCanvasBusy(task_id) || !operation || operation->import_job_id != import_job_id ||
      operation->source_path != normalize_local_path(source_path))
    return fail(QStringLiteral("The prepared import producer must settle before catalog recovery."));

  const auto asset_id = expected.value(QStringLiteral("asset_id")).toString();
  if (asset_id.isEmpty() || expected.value(QStringLiteral("source_sha256")).toString().isEmpty())
    return fail(QStringLiteral("The catalog row lacks its source receipt."));
  expected.remove(QStringLiteral("taskId"));
  expected.remove(QStringLiteral("importJobId"));
  expected.remove(QStringLiteral("checkpointHash"));
  QString error;
  if (!run_catalog_state_json_command(
          QStringLiteral("remove-agent-catalog-row"), expected, nullptr, &error))
    return fail(error.isEmpty() ? QStringLiteral("The catalog row no longer matches its owned receipt.") : error);
  removeDeletedAssets({asset_id});
  refresh();
  return true;
}

QByteArray CatalogAssetModel::recipeResourceSnapshot(const QString& asset_id) const {
  const auto index = all_asset_index_by_id_.constFind(asset_id);
  if (index == all_asset_index_by_id_.cend() || index.value() < 0 || index.value() >= all_assets_.size()) return {};
  const auto& item = all_assets_.at(index.value());
  if (item.file_path.trimmed().isEmpty()) return {};
  QJsonObject snapshot;
  QString error;
  const auto ok = run_catalog_state_json_command(
      QStringLiteral("agent-asset-recipe-snapshot"),
      {{QStringLiteral("assetId"), asset_id},
       {QStringLiteral("sourcePath"), normalize_local_path(item.file_path)}},
      &snapshot, &error);
  if (!ok || snapshot.value(QStringLiteral("schema")).toString() != QStringLiteral("agent-asset-recipe:v1") ||
      snapshot.value(QStringLiteral("state")).toString() != QStringLiteral("present") ||
      snapshot.value(QStringLiteral("asset_id")).toString() != asset_id) return {};
  return QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
}

bool CatalogAssetModel::restoreRecipeResourceSnapshot(
    const QString& asset_id, const QByteArray& expected_bytes, const QByteArray& before_bytes,
    const QString& task_id, QString* error_message) {
  const auto fail = [error_message](const QString& error) {
    if (error_message != nullptr) *error_message = error;
    return false;
  };
  if (!agentTaskRecipePersistenceSettled(asset_id))
    return fail(QStringLiteral("The asset's catalog recipe producer did not settle; its recipe was preserved."));
  if (failed_adjustment_patch_persistence_asset_ids_.contains(asset_id))
    return fail(QStringLiteral("The asset's last catalog recipe save failed; its recipe was preserved."));
  const auto current_bytes = recipeResourceSnapshot(asset_id);
  if (current_bytes.isEmpty())
    return fail(QStringLiteral("The asset recipe could not be read after its producer settled; it was preserved."));
  QJsonParseError expected_parse;
  QJsonParseError before_parse;
  QJsonParseError current_parse;
  const auto expected_document = QJsonDocument::fromJson(expected_bytes, &expected_parse);
  const auto before_document = QJsonDocument::fromJson(before_bytes, &before_parse);
  const auto current_document = QJsonDocument::fromJson(current_bytes, &current_parse);
  if (expected_parse.error != QJsonParseError::NoError || !expected_document.isObject() ||
      before_parse.error != QJsonParseError::NoError || !before_document.isObject() ||
      current_parse.error != QJsonParseError::NoError || !current_document.isObject())
    return fail(QStringLiteral("The asset recipe baseline is invalid."));
  const auto expected = expected_document.object();
  const auto before = before_document.object();
  const auto current = current_document.object();
  if (expected.value(QStringLiteral("schema")).toString() != QStringLiteral("agent-asset-recipe:v1") ||
      current.value(QStringLiteral("schema")).toString() != QStringLiteral("agent-asset-recipe:v1") ||
      before.value(QStringLiteral("schema")).toString() != QStringLiteral("agent-asset-recipe:v1") ||
      current.value(QStringLiteral("state")).toString() != QStringLiteral("present") ||
      expected.value(QStringLiteral("state")).toString() != QStringLiteral("present") ||
      before.value(QStringLiteral("state")).toString() != QStringLiteral("present") ||
      current.value(QStringLiteral("asset_id")).toString() != asset_id ||
      expected.value(QStringLiteral("asset_id")).toString() != asset_id ||
      before.value(QStringLiteral("asset_id")).toString() != asset_id ||
      current.value(QStringLiteral("source_path")) != before.value(QStringLiteral("source_path")) ||
      current.value(QStringLiteral("source_sha256")) != before.value(QStringLiteral("source_sha256")) ||
      expected.value(QStringLiteral("source_path")) != before.value(QStringLiteral("source_path")) ||
      expected.value(QStringLiteral("source_sha256")) != before.value(QStringLiteral("source_sha256")) ||
      current.value(QStringLiteral("catalog_row_state")) != before.value(QStringLiteral("catalog_row_state")) ||
      expected.value(QStringLiteral("catalog_row_state")) != before.value(QStringLiteral("catalog_row_state")))
    return fail(QStringLiteral("The asset recipe baseline belongs to a different source."));

  auto current_is_authorized = current_bytes == expected_bytes;
  const auto receipt_key = agent_task_recipe_receipt_key(task_id, asset_id);
  const auto restore_receipt = task_recipe_restore_receipts_.value(receipt_key);
  if (!current_is_authorized && !task_id.isEmpty() && !restore_receipt.isEmpty())
    current_is_authorized = agent_recipe_snapshot_matches_receipt(current, restore_receipt);
  if (!current_is_authorized)
    return fail(QStringLiteral("The asset recipe changed after its last task receipt; it was preserved."));

  QString error;
  if (!run_catalog_state_json_command(
          QStringLiteral("restore-agent-asset-recipe"),
          {{QStringLiteral("expected"), current}, {QStringLiteral("before"), before}}, nullptr, &error))
    return fail(error.isEmpty() ? QStringLiteral("The catalog recipe checkpoint changed; it was preserved.") : error);
  if (!task_id.isEmpty()) task_recipe_restore_receipts_.remove(receipt_key);
  adjustment_patch_overrides_.remove(asset_id);
  refresh();
  return true;
}

AgentTaskJournal::ResourceAdapter CatalogAssetModel::agentTaskRecipeResourceAdapter(
    const QString& asset_id, const QString& task_id) {
  const auto guarded_model = QPointer<CatalogAssetModel>(this);
  const auto last_read = std::make_shared<QByteArray>();
  AgentTaskJournal::ResourceAdapter adapter;
  adapter.read = [guarded_model, last_read, asset_id] {
    if (!guarded_model) return QByteArray{};
    *last_read = guarded_model->recipeResourceSnapshot(asset_id);
    return *last_read;
  };
  adapter.restore = [guarded_model, last_read, asset_id, task_id](const QByteArray& before) {
    return guarded_model && guarded_model->restoreRecipeResourceSnapshot(
        asset_id, *last_read, before, task_id, nullptr);
  };
  return adapter;
}

bool CatalogAssetModel::registerAgentTaskRecipeCheckpoint(
    const QString& task_id, const QString& asset_id, const QByteArray& expected_current,
    const QVariantMap& exact_recipes, const QString& preparing_revision_id) {
  const QRegularExpression valid_task_id(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
  bool recipes_valid = false;
  const auto recipes = normalized_agent_recipe_fields(exact_recipes, &recipes_valid);
  if (!valid_task_id.match(task_id).hasMatch() || asset_id.trimmed().isEmpty() ||
      expected_current.isEmpty() || !recipes_valid) return false;
  const auto key = agent_task_recipe_receipt_key(task_id, asset_id);
  if (task_recipe_checkpoint_requests_.contains(key)) return false;
  bool matching_prelaunch_revision = false;
  if (!preparing_revision_id.isEmpty()) {
    const auto& context = active_adjustment_patch_persistence_context_;
    QJsonParseError legacy_parse;
    const auto legacy_document = QJsonDocument::fromJson(
        context.value(QStringLiteral("legacyRecipeJson")).toString().toUtf8(), &legacy_parse);
    QJsonValue canonical_recipe(QJsonValue::Null);
    if (!context.value(QStringLiteral("legacyOnly")).toBool()) {
      canonical_recipe = QJsonObject{
          {QStringLiteral("recipe_payload"), context.value(QStringLiteral("canonicalRecipePayload")).toString()},
          {QStringLiteral("cache_key"), context.value(QStringLiteral("canonicalCacheKey")).toString()},
          {QStringLiteral("provenance_payload"), context.value(QStringLiteral("canonicalProvenancePayload")).toString()},
      };
    }
    const auto prepared_fields = legacy_parse.error == QJsonParseError::NoError && legacy_document.isObject()
        ? QJsonObject{
              {QStringLiteral("legacy_recipe"), legacy_document.object()},
              {QStringLiteral("canonical_recipe"), canonical_recipe},
          }
        : QJsonObject{};
    matching_prelaunch_revision =
        active_adjustment_patch_persistence_task_id_ == task_id &&
        active_adjustment_patch_persistence_asset_id_ == asset_id &&
        active_adjustment_patch_persistence_revision_id_ == preparing_revision_id &&
        context.value(QStringLiteral("sourceOwner")).toString() == QStringLiteral("catalog") &&
        !context.value(QStringLiteral("payload")).toByteArray().isEmpty() &&
        recipe_persistence_process_.state() == QProcess::NotRunning &&
        !prepared_fields.isEmpty() && prepared_fields == recipes;
  }
  if ((!matching_prelaunch_revision && !agentTaskRecipePersistenceSettled(asset_id)) ||
      failed_adjustment_patch_persistence_asset_ids_.contains(asset_id)) return false;
  const auto current_bytes = recipeResourceSnapshot(asset_id);
  if (current_bytes.isEmpty() || current_bytes != expected_current) return false;
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(current_bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject()) return false;
  const auto current = document.object();
  task_recipe_checkpoint_requests_.insert(key, {
      {QStringLiteral("taskId"), task_id},
      {QStringLiteral("assetId"), asset_id},
      {QStringLiteral("revisionId"), preparing_revision_id},
      {QStringLiteral("expectedCurrentBytes"), QString::fromLatin1(expected_current.toBase64())},
      {QStringLiteral("payloadSha256"), preparing_revision_id.isEmpty()
          ? QString{}
          : QString::fromLatin1(QCryptographicHash::hash(
                active_adjustment_patch_persistence_context_.value(QStringLiteral("payload")).toByteArray(),
                QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("sourcePath"), current.value(QStringLiteral("source_path"))},
      {QStringLiteral("sourceSha256"), current.value(QStringLiteral("source_sha256"))},
      {QStringLiteral("catalogRowState"), current.value(QStringLiteral("catalog_row_state"))},
      {QStringLiteral("recipes"), recipes},
  });
  return true;
}

bool CatalogAssetModel::cancelAgentTaskRecipeCheckpoint(
    const QString& task_id, const QString& asset_id, const QByteArray& expected_current,
    const QString& terminal_revision_id) {
  const auto key = agent_task_recipe_receipt_key(task_id, asset_id);
  const auto request = task_recipe_checkpoint_requests_.value(key);
  if (request.isEmpty() || expected_current.isEmpty() ||
      QByteArray::fromBase64(request.value(QStringLiteral("expectedCurrentBytes")).toString().toLatin1()) !=
          expected_current) return false;
  if (terminal_revision_id.isEmpty()) {
    if (!agentTaskRecipePersistenceSettled(asset_id)) return false;
  } else {
    const auto terminal_key = agent_task_recipe_terminal_key(task_id, asset_id, terminal_revision_id);
    const auto terminal = task_recipe_terminal_revisions_.value(terminal_key);
    if (request.value(QStringLiteral("revisionId")).toString() != terminal_revision_id ||
        terminal.value(QStringLiteral("helperSucceeded")).toBool() ||
        terminal.value(QStringLiteral("taskId")).toString() != task_id ||
        terminal.value(QStringLiteral("assetId")).toString() != asset_id ||
        terminal.value(QStringLiteral("revisionId")).toString() != terminal_revision_id ||
        terminal.value(QStringLiteral("payloadSha256")).toString() !=
            request.value(QStringLiteral("payloadSha256")).toString() ||
        recipe_persistence_process_.state() != QProcess::NotRunning ||
        !active_adjustment_patch_persistence_asset_id_.isEmpty()) return false;
  }
  // Release an unapplied prelaunch receipt only while the exact catalog state
  // remains untouched. A changed row/source/recipe may be an uncertain write,
  // so keep its ownership authority for recovery instead of discarding it.
  if (recipeResourceSnapshot(asset_id) != expected_current) return false;
  task_recipe_checkpoint_requests_.remove(key);
  if (!terminal_revision_id.isEmpty())
    task_recipe_terminal_revisions_.remove(
        agent_task_recipe_terminal_key(task_id, asset_id, terminal_revision_id));
  return true;
}

QByteArray CatalogAssetModel::completeAgentTaskRecipeCheckpoint(
    const QString& task_id, const QString& asset_id, const QString& terminal_revision_id) {
  const auto key = agent_task_recipe_receipt_key(task_id, asset_id);
  const auto request = task_recipe_checkpoint_requests_.value(key);
  if (request.isEmpty()) return {};
  if (terminal_revision_id.isEmpty()) {
    if (!agentTaskRecipePersistenceSettled(asset_id) ||
        failed_adjustment_patch_persistence_asset_ids_.contains(asset_id)) return {};
  } else {
    const auto terminal_key = agent_task_recipe_terminal_key(task_id, asset_id, terminal_revision_id);
    const auto terminal = task_recipe_terminal_revisions_.value(terminal_key);
    if (request.value(QStringLiteral("revisionId")).toString() != terminal_revision_id ||
        !terminal.value(QStringLiteral("helperSucceeded")).toBool() ||
        !terminal.value(QStringLiteral("ownerAccepted")).toBool() ||
        terminal.value(QStringLiteral("taskId")).toString() != task_id ||
        terminal.value(QStringLiteral("assetId")).toString() != asset_id ||
        terminal.value(QStringLiteral("revisionId")).toString() != terminal_revision_id ||
        terminal.value(QStringLiteral("payloadSha256")).toString() !=
            request.value(QStringLiteral("payloadSha256")).toString() ||
        recipe_persistence_process_.state() != QProcess::NotRunning ||
        !active_adjustment_patch_persistence_asset_id_.isEmpty()) return {};
  }
  const auto current_bytes = recipeResourceSnapshot(asset_id);
  if (current_bytes.isEmpty()) return {};
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(current_bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject() ||
      !agent_recipe_snapshot_matches_receipt(document.object(), request)) return {};
  task_recipe_checkpoint_requests_.remove(key);
  if (!terminal_revision_id.isEmpty())
    task_recipe_terminal_revisions_.remove(
        agent_task_recipe_terminal_key(task_id, asset_id, terminal_revision_id));
  return current_bytes;
}

bool CatalogAssetModel::registerAgentTaskRecipeRestoreReceipt(
    const QString& task_id, const QString& asset_id, const QByteArray& expected_current,
    const QVariantMap& exact_recipes) {
  const QRegularExpression valid_task_id(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
  bool recipes_valid = false;
  const auto recipes = normalized_agent_recipe_fields(exact_recipes, &recipes_valid);
  if (!valid_task_id.match(task_id).hasMatch() || asset_id.trimmed().isEmpty() ||
      expected_current.isEmpty() || !recipes_valid) return false;
  const auto key = agent_task_recipe_receipt_key(task_id, asset_id);
  if (task_recipe_checkpoint_requests_.contains(key) || task_recipe_restore_receipts_.contains(key) ||
      !agentTaskRecipePersistenceSettled(asset_id) ||
      failed_adjustment_patch_persistence_asset_ids_.contains(asset_id)) return false;
  const auto current_bytes = recipeResourceSnapshot(asset_id);
  if (current_bytes.isEmpty() || current_bytes != expected_current) return false;
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(current_bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject()) return false;
  const auto current = document.object();
  task_recipe_restore_receipts_.insert(key, {
      {QStringLiteral("taskId"), task_id},
      {QStringLiteral("assetId"), asset_id},
      {QStringLiteral("sourcePath"), current.value(QStringLiteral("source_path"))},
      {QStringLiteral("sourceSha256"), current.value(QStringLiteral("source_sha256"))},
      {QStringLiteral("catalogRowState"), current.value(QStringLiteral("catalog_row_state"))},
      {QStringLiteral("recipes"), recipes},
  });
  return true;
}

bool CatalogAssetModel::agentTaskRecipePersistenceSettled(const QString& asset_id) const {
  if (asset_id.trimmed().isEmpty()) return false;
  if (active_adjustment_patch_persistence_asset_id_ == asset_id) return false;
  return std::none_of(
      pending_adjustment_patch_persistence_.cbegin(),
      pending_adjustment_patch_persistence_.cend(),
      [&asset_id](const AdjustmentPatchPersistenceRevision& revision) {
        return revision.asset_id == asset_id;
      });
}

bool CatalogAssetModel::retryAgentAdjustmentPatchPersistence(
    const QString& task_id, const QString& asset_id) {
  if (asset_id.trimmed().isEmpty() || pending_adjustment_patch_persistence_.isEmpty() ||
      pending_adjustment_patch_persistence_.constFirst().asset_id != asset_id) return false;
  return resumeAgentAdjustmentPatchPersistence(task_id);
}

bool CatalogAssetModel::resumeAgentAdjustmentPatchPersistence(const QString& task_id) {
  if (task_id.trimmed().isEmpty() || recipe_persistence_process_.state() != QProcess::NotRunning)
    return false;
  if (!adjustment_patch_persistence_waiting_task_id_.isEmpty()) {
    if (adjustment_patch_persistence_waiting_task_id_ != task_id) return false;
    adjustment_patch_persistence_waiting_task_id_.clear();
  } else if (adjustment_patch_persistence_queue_blocked_) {
    if (pending_adjustment_patch_persistence_.isEmpty() ||
        pending_adjustment_patch_persistence_.constFirst().task_id != task_id) return false;
  } else {
    return false;
  }
  adjustment_patch_persistence_queue_blocked_ = false;
  if (!pending_adjustment_patch_persistence_.isEmpty())
    failed_adjustment_patch_persistence_asset_ids_.remove(
        pending_adjustment_patch_persistence_.constFirst().asset_id);
  startNextAdjustmentPatchPersistence();
  return true;
}

void CatalogAssetModel::loadPersistedLibraryState(CatalogLibraryState state) {
  if (!state.present) return;
  projects_ = std::move(state.projects);
  culling_states_ = std::move(state.review_states);
  overlayCullingChanges();
  relinked_file_paths_ = std::move(state.relinked_paths);
  for (const auto& id : changed_metadata_during_snapshot_) {
    if (asset_metadata_.contains(id)) state.asset_metadata.insert(id, asset_metadata_.value(id));
  }
  asset_metadata_ = std::move(state.asset_metadata);
  for (auto it = pending_metadata_.cbegin(); it != pending_metadata_.cend(); ++it) asset_metadata_.insert(it.key(), it.value());
  changed_metadata_during_snapshot_.clear();
  overlayProjectChanges(true);
}

void CatalogAssetModel::updateAssets(QVector<CatalogAssetItem> items,
                                     const QHash<QString, QString>& reusable_thumbnail_tokens) {
  confirmed_duplicate_groups_.clear();
  emit duplicateAnalysisChanged();
  emit searchInputsChanged({});
  const auto preferred_asset_id = selectedAssetId();
  const auto previous_total_count = all_assets_.size();
  const auto previous_assets = std::move(all_assets_);
  int retained_thumbnails = 0;
  int invalidated_thumbnails = 0;

  all_assets_ = std::move(items);
  QSet<QString> imported_asset_ids;
  imported_asset_ids.reserve(all_assets_.size());
  for (auto& item : all_assets_) {
    item.virtual_copy = false;
    item.source_asset_id = item.asset_id;
    item.variant_name = QStringLiteral("Original");
    item.variant_index = 0;
    imported_asset_ids.insert(item.asset_id);
  }
  for (const auto& virtual_copy : virtual_copies_) {
    if (!imported_asset_ids.contains(virtual_copy.source_asset_id)) {
      continue;
    }
    all_assets_.push_back(virtual_copy);
  }
  loadTimelineAssets();
  for (auto& item : all_assets_) {
    apply_culling_state_to_item(&item, cullingStateForAsset(item.asset_id));
    const auto adjustment_patch_override =
        adjustment_patch_overrides_.value(
            item.asset_id);
    if (!adjustment_patch_override
             .trimmed()
             .isEmpty()) {
      item.adjustment_patch_json =
          adjustment_patch_override;
    }
    const auto relinked_path = relinked_file_paths_.value(item.asset_id).trimmed();
    if (!relinked_path.isEmpty()) {
      apply_relinked_path_to_item(&item, relinked_path);
      refresh_online_state_for_item(&item);
    }
    if (item.virtual_copy && item.source_asset_id.trimmed().isEmpty()) {
      item.source_asset_id = item.asset_id;
    }
    if (item.variant_name.trimmed().isEmpty()) {
      item.variant_name = item.virtual_copy ? QStringLiteral("Virtual Copy") : QStringLiteral("Original");
    }
    if (item.media_type == QStringLiteral("timeline")) continue;
    const auto previous_index = all_asset_index_by_id_.value(item.asset_id, -1);
    const auto* previous = previous_index >= 0 && previous_index < previous_assets.size()
        ? &previous_assets.at(previous_index) : nullptr;
    const auto same_inputs = previous && previous->file_path == item.file_path &&
        previous->media_type == item.media_type && previous->adjustment_patch_json == item.adjustment_patch_json;
    if (same_inputs && item.online &&
        !previous->thumbnail_provenance_token.isEmpty() &&
        reusable_thumbnail_tokens.value(item.asset_id) == previous->thumbnail_provenance_token) {
      copy_thumbnail_runtime_fields(*previous, &item);
      ++retained_thumbnails;
    } else if (!item.online) {
      if (item.thumbnail_source.isEmpty() && previous && previous->file_path == item.file_path)
        copy_thumbnail_runtime_fields(*previous, &item);
      // An offline fallback can remain visible but is not a current search preview.
      item.search_preview_current = false;
      item.thumbnail_provenance_token.clear();
    } else {
      if (previous && !previous->thumbnail_source.isEmpty()) ++invalidated_thumbnails;
      clear_thumbnail_source_for_item(&item);
    }
  }
  observability::logInfo(photoEditorCatalogLog(), QStringLiteral("catalog.thumbnail.reuse"), {
      {QStringLiteral("retained"), retained_thumbnails}, {QStringLiteral("invalidated"), invalidated_thumbnails},
      {QStringLiteral("validated_candidates"), reusable_thumbnail_tokens.size()}});
  rebuildProjectMetadata();
  rebuildBurstMetadata();
  rebuildIntelligenceMetadata();
  rebuildAllAssetIndexMap();
  bumpProjectStatsRevision();
  rebuildCaptureYears();

  if (previous_total_count != all_assets_.size()) {
    emit totalAssetCountChanged();
  }

  applyFilters(preferred_asset_id);
  if (!pending_blank_canvas_path_.isEmpty()) {
    const auto canvas_id = assetIdForFilePath(pending_blank_canvas_path_);
    if (!canvas_id.isEmpty()) {
      pending_blank_canvas_path_.clear();
      clearFilters();
      selectAssetById(canvas_id);
      setStatus(QStringLiteral("Canvas ready"));
      emit blankCanvasBusyChanged();
      emit blankCanvasCreated(canvas_id);
    } else if (blank_canvas_import_finished_) {
      const auto error = QStringLiteral("The canvas source was saved, but the imported document could not be found. Refresh the library and try again.");
      setErrorMessage(error);
      failPendingBlankCanvas(error);
    }
  }
  resolvePreparedBlankCanvasAfterRefresh();
  restartMediaDurationRefresh();
  if (!timeline_asset_loading_suppressed_for_testing_) {
    startTimelineAssetsLoad();
  }
}

void CatalogAssetModel::resolvePreparedBlankCanvasAfterRefresh() {
  const auto operation = prepared_blank_canvas_operation_;
  if (!operation || operation->status != QStringLiteral("awaiting-catalog")) return;
  operation->asset_id = assetIdForFilePath(operation->source_path);
  if (!operation->asset_id.isEmpty()) {
    if (sha256_file(operation->source_path) != operation->source_sha256) {
      operation->asset_id.clear();
      finishPreparedBlankCanvas(QStringLiteral("failed"),
          QStringLiteral("The prepared source changed after import; the catalog row and file were preserved."));
      return;
    }
    const auto status = operation->terminal_import_status == QStringLiteral("Completed")
        ? QStringLiteral("completed")
        : operation->terminal_import_status == QStringLiteral("Canceled")
            ? QStringLiteral("completed-after-cancel") : QStringLiteral("completed-after-failure");
    const auto error = status == QStringLiteral("completed") ? QString{} : operation->error;
    finishPreparedBlankCanvas(status, error);
    return;
  }
  const auto error = operation->error.isEmpty()
      ? QStringLiteral("The importer finished, but the prepared canvas row was not found in the catalog.")
      : operation->error;
  const auto status = operation->terminal_import_status == QStringLiteral("Canceled")
      ? QStringLiteral("canceled") : QStringLiteral("failed");
  finishPreparedBlankCanvas(status, error);
}

void CatalogAssetModel::restartMediaDurationRefresh() {
  if (media_duration_cancellation_) {
    media_duration_cancellation_->store(true, std::memory_order_relaxed);
  }
  media_duration_cancellation_ = std::make_shared<std::atomic_bool>(false);
  ++media_duration_generation_;
  pending_media_durations_.clear();
  pending_media_duration_index_by_path_.clear();
  next_media_duration_ = 0;
  QSet<QString> seen_asset_ids;
  const auto append = [&](const CatalogAssetItem& item) {
    if (!item.online || item.file_path.isEmpty() || item.duration_seconds > 0.0 ||
        seen_asset_ids.contains(item.asset_id) ||
        (item.media_type != QStringLiteral("video") && item.media_type != QStringLiteral("audio"))) return;
    seen_asset_ids.insert(item.asset_id);
    const auto existing = pending_media_duration_index_by_path_.constFind(item.file_path);
    if (existing != pending_media_duration_index_by_path_.cend()) {
      pending_media_durations_[existing.value()].asset_ids.push_back(item.asset_id);
      return;
    }
    pending_media_duration_index_by_path_.insert(item.file_path, pending_media_durations_.size());
    pending_media_durations_.push_back({item.file_path, {item.asset_id}, media_duration_generation_, 0.0});
  };
  if (const auto* selected = selectedItem()) append(*selected);
  for (const auto& item : assets_) append(item);
  for (const auto& item : all_assets_) append(item);
  startNextMediaDurationTask();
}

void CatalogAssetModel::prioritizeMediaDurationTasks() {
  if (next_media_duration_ >= pending_media_durations_.size()) return;
  auto front = next_media_duration_;
  const auto promote = [&](const CatalogAssetItem& item) {
    if (!item.online || item.duration_seconds > 0.0) return;
    const auto found = pending_media_duration_index_by_path_.constFind(item.file_path);
    if (found == pending_media_duration_index_by_path_.cend()) return;
    const auto queued_index = found.value();
    // A shared path may already have been promoted by the selected asset or
    // another visible row. Active/completed requests are absent from the index.
    if (queued_index < front) return;
    if (queued_index != front) {
      pending_media_durations_.swapItemsAt(queued_index, front);
      pending_media_duration_index_by_path_.insert(pending_media_durations_.at(queued_index).file_path, queued_index);
      pending_media_duration_index_by_path_.insert(pending_media_durations_.at(front).file_path, front);
    }
    ++front;
  };
  if (const auto* selected = selectedItem()) promote(*selected);
  if (visible_thumbnail_first_index_ >= 0 &&
      visible_thumbnail_last_index_ >= visible_thumbnail_first_index_) {
    const auto end = std::min(static_cast<qsizetype>(visible_thumbnail_last_index_) + 1, assets_.size());
    for (qsizetype row = visible_thumbnail_first_index_; row < end; ++row) promote(assets_.at(row));
  }
}

void CatalogAssetModel::startNextMediaDurationTask() {
  prioritizeMediaDurationTasks();
  for (const auto& watcher : media_duration_watchers_) {
    // Keep a completed future occupied until its queued finished signal has
    // consumed the result; isRunning alone would allow overwriting that result.
    if (active_media_duration_watchers_.contains(watcher.get()) ||
        next_media_duration_ >= pending_media_durations_.size()) continue;
    auto request = std::move(pending_media_durations_[next_media_duration_++]);
    pending_media_duration_index_by_path_.remove(request.file_path);
    const auto cancellation = media_duration_cancellation_;
    active_media_duration_watchers_.insert(watcher.get());
    watcher->setFuture(QtConcurrent::run(&media_duration_thread_pool(), [request, cancellation]() mutable {
      const auto cache_root = QDir(AppBootstrap::cacheLocation()).filePath(QStringLiteral("media-duration-v1"));
      request.seconds = probe_video_duration_seconds(request.file_path, cache_root, cancellation);
      return request;
    }));
  }
}

void CatalogAssetModel::handleMediaDurationFinished() {
  auto* watcher = static_cast<QFutureWatcher<CatalogMediaDurationResult>*>(sender());
  if (watcher == nullptr) return;
  const auto result = watcher->result();
  active_media_duration_watchers_.remove(watcher);
  bool selected_changed = false;
  if (result.generation == media_duration_generation_ && result.seconds > 0.0) {
    for (const auto& asset_id : result.asset_ids) {
      const auto row = all_asset_index_by_id_.value(asset_id, -1);
      if (row < 0 || row >= all_assets_.size()) continue;
      auto& item = all_assets_[row];
      if (item.file_path != result.file_path || !item.online ||
          (item.media_type != QStringLiteral("video") && item.media_type != QStringLiteral("audio"))) continue;
      item.duration_seconds = result.seconds;
      item.duration_text = format_duration_text(result.seconds);
      if (!media_duration_notification_timer_.isActive()) media_duration_notification_timer_.start();
      const auto visible_row = visible_asset_index_by_id_.value(asset_id, -1);
      if (visible_row >= 0 && visible_row < assets_.size()) {
        auto& visible = assets_[visible_row];
        visible.duration_seconds = item.duration_seconds;
        visible.duration_text = item.duration_text;
        emit dataChanged(index(static_cast<int>(visible_row), 0), index(static_cast<int>(visible_row), 0),
                         {DurationSecondsRole, DurationTextRole});
        selected_changed |= asset_id == selectedAssetId();
      }
    }
  }
  if (selected_changed) emit selectedAssetChanged();
  startNextMediaDurationTask();
  if (active_media_duration_watchers_.isEmpty() && next_media_duration_ == pending_media_durations_.size()) {
    pending_media_durations_.clear();
    pending_media_duration_index_by_path_.clear();
    next_media_duration_ = 0;
  }
}

void CatalogAssetModel::queueThumbnailRefreshes() {
  const auto now_msecs = QDateTime::currentMSecsSinceEpoch();
  QStringList next_asset_ids;
  QSet<QString> seen_asset_ids;
  next_asset_ids.reserve(kVisibleThumbnailPrefetchRows * 2 + 1);

  const auto append_asset = [&](const CatalogAssetItem& item) {
    if (item.asset_id.isEmpty() || seen_asset_ids.contains(item.asset_id) || !item.thumbnail_source.isEmpty() ||
        !item.online || item.file_path.trimmed().isEmpty()) {
      return;
    }
    seen_asset_ids.insert(item.asset_id);
    next_asset_ids.push_back(item.asset_id);
  };

  if (const auto* item = selectedItem()) {
    append_asset(*item);
  }
  if (visible_thumbnail_first_index_ >= 0 && visible_thumbnail_last_index_ >= visible_thumbnail_first_index_) {
    const auto first_index = std::max(0, visible_thumbnail_first_index_ - kVisibleThumbnailPrefetchRows);
    const auto last_index =
        std::min(static_cast<int>(assets_.size()) - 1, visible_thumbnail_last_index_ + kVisibleThumbnailPrefetchRows);
    for (auto index = first_index; index <= last_index; ++index) {
      append_asset(assets_.at(index));
    }
  }
  // Work follows the current viewport. Enqueuing the entire catalog creates
  // minutes of obsolete work when the UI is busy or the user changes photos.

  QStringList reordered_queue;
  reordered_queue.reserve(next_asset_ids.size() + pending_thumbnail_asset_ids_.size());
  QSet<QString> queued_asset_ids = queued_thumbnail_asset_ids_;
  queued_asset_ids.unite(active_thumbnail_asset_ids_);

  for (const auto& asset_id : next_asset_ids) {
    if (active_thumbnail_asset_ids_.contains(asset_id)) {
      continue;
    }
    if (!queued_asset_ids.contains(asset_id)) {
      queued_asset_ids.insert(asset_id);
      thumbnail_queued_at_msecs_.insert(asset_id, now_msecs);
    }
    reordered_queue.push_back(asset_id);
  }

  for (const auto& asset_id : pending_thumbnail_asset_ids_) {
    if (!seen_asset_ids.contains(asset_id)) {
      thumbnail_queued_at_msecs_.remove(asset_id);
    }
  }

  pending_thumbnail_asset_ids_ = reordered_queue;
  queued_thumbnail_asset_ids_.clear();
  for (const auto& asset_id : pending_thumbnail_asset_ids_) {
    queued_thumbnail_asset_ids_.insert(asset_id);
  }

  if (thumbnail_work_deferred_for_interactive_render_) {
    return;
  }
  startNextThumbnailTask();
}

void CatalogAssetModel::startNextThumbnailTask() {
  if (thumbnail_work_deferred_for_interactive_render_) {
    return;
  }
  auto started_task = false;
  for (const auto& watcher : thumbnail_watchers_) {
    if (watcher->isRunning() || active_thumbnail_asset_by_watcher_.contains(watcher.get())) {
      continue;
    }

    while (!pending_thumbnail_asset_ids_.isEmpty()) {
      const auto asset_id = pending_thumbnail_asset_ids_.takeFirst();
      queued_thumbnail_asset_ids_.remove(asset_id);
      const auto queued_at_msecs = thumbnail_queued_at_msecs_.take(asset_id);
      const auto queue_wait_ms =
          queued_at_msecs > 0 ? qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - queued_at_msecs) : 0;

      const auto index_iterator = all_asset_index_by_id_.constFind(asset_id);
      const auto asset_index = index_iterator == all_asset_index_by_id_.cend() ? qsizetype{-1} : index_iterator.value();
      if (asset_index < 0 || asset_index >= all_assets_.size() ||
          !all_assets_.at(asset_index).thumbnail_source.isEmpty() || !all_assets_.at(asset_index).online ||
          all_assets_.at(asset_index).file_path.trimmed().isEmpty()) {
        observability::logInfo(
            photoEditorCatalogLog(),
            QStringLiteral("thumbnail.canceled"),
            {
                {QStringLiteral("asset_id"), asset_id},
                {QStringLiteral("queue_wait_ms"), queue_wait_ms},
                {QStringLiteral("cancellation_reason"), QStringLiteral("stale_or_ineligible")},
            });
        continue;
      }

      active_thumbnail_asset_ids_.insert(asset_id);
      active_thumbnail_asset_by_watcher_.insert(watcher.get(), asset_id);
      const auto cancellation_requested =
          std::make_shared<std::atomic_bool>(false);
      active_thumbnail_cancellation_by_watcher_.insert(
          watcher.get(), cancellation_requested);
      const auto& item = all_assets_.at(asset_index);
      const auto file_path = item.file_path;
      const auto adjustment_patch_json =
          item.adjustment_patch_json;
      const auto queue_priority = thumbnailQueuePriorityForAssetId(asset_id);
      observability::logInfo(
          photoEditorCatalogLog(),
          QStringLiteral("thumbnail.started"),
          {
              {QStringLiteral("asset_id"), asset_id},
              {QStringLiteral("file_path"), file_path},
              {QStringLiteral("queue_wait_ms"), queue_wait_ms},
              {QStringLiteral("queue_priority"), queue_priority},
              {QStringLiteral("active_worker_count"), active_thumbnail_asset_ids_.size()},
              {QStringLiteral("pending_worker_count"), pending_thumbnail_asset_ids_.size()},
              {QStringLiteral("worker_limit"), kMaxConcurrentThumbnailTasks},
          });
      WorkerQueueRegistry::logSample(
          QStringLiteral("thumbnail"),
          QStringLiteral("thumbnail"),
          QStringLiteral("library"),
          static_cast<int>(active_thumbnail_asset_ids_.size()),
          static_cast<int>(pending_thumbnail_asset_ids_.size()),
          kMaxConcurrentThumbnailTasks,
          queue_wait_ms);
      watcher->setFuture(QtConcurrent::run(
          [asset_id,
           file_path,
           submitted_at_msecs = QDateTime::currentMSecsSinceEpoch(),
           adjustment_patch_json,
           queue_wait_ms,
           queue_priority,
           cancellation_requested]() {
        QElapsedTimer timer;
        timer.start();
        const auto pool_wait_ms = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - submitted_at_msecs);
        const auto inputs = catalog_thumbnail_inputs(file_path, adjustment_patch_json);
        auto thumbnail_source =
            thumbnail_source_for_path(
                file_path,
                adjustment_patch_json,
                cancellation_requested.get());
        auto dependencies = inputs.revisions;
        const auto source_changed = !thumbnail_revisions_current(dependencies);
        if (source_changed) thumbnail_source.clear();
        const QUrl output_url(thumbnail_source);
        if (output_url.isLocalFile()) dependencies.insert(output_url.toLocalFile(), linked_image_file_revision(output_url.toLocalFile()));
        const auto cache_stats = sample_thumbnail_cache_stats();
        return ThumbnailTaskResult{
            .asset_id = asset_id,
            .file_path = file_path,
            .adjustment_patch_json =
                adjustment_patch_json,
            .thumbnail_source = thumbnail_source,
            .thumbnail_provenance_token = inputs.reusable && output_url.isLocalFile() && !thumbnail_source.isEmpty()
                ? QUuid::createUuid().toString(QUuid::WithoutBraces) : QString{},
            .thumbnail_dependency_revisions = std::move(dependencies),
            .source_changed = source_changed,
            .thumbnail_cache_bytes = cache_stats.bytes,
            .thumbnail_cache_entries = cache_stats.entries,
            .thumbnail_cache_probe_ms = cache_stats.elapsed_ms,
            .queue_wait_ms = queue_wait_ms,
            .elapsed_ms = timer.elapsed(),
            .pool_wait_ms = pool_wait_ms,
            .finished_at_msecs = QDateTime::currentMSecsSinceEpoch(),
            .queue_priority = queue_priority,
        };
      }));
      started_task = true;
      break;
    }
  }

  if (!started_task && pending_thumbnail_asset_ids_.isEmpty() && active_thumbnail_asset_ids_.isEmpty()) {
    thumbnail_queued_at_msecs_.clear();
  }
}

bool CatalogAssetModel::applyThumbnailResult(const ThumbnailTaskResult& result) {
  if (result.asset_id.isEmpty()) {
    return false;
  }

  auto selected_changed = false;
  auto visible_index = -1;
  const auto all_index_iterator = all_asset_index_by_id_.constFind(result.asset_id);
  const auto all_index =
      all_index_iterator == all_asset_index_by_id_.cend() ? qsizetype{-1} : all_index_iterator.value();
  if (all_index < 0 || all_index >= all_assets_.size()) {
    return false;
  }

  auto& all_item = all_assets_[all_index];
  if (all_item.file_path != result.file_path ||
      all_item.adjustment_patch_json !=
          result.adjustment_patch_json ||
      result.thumbnail_source.isEmpty() ||
      (!result.thumbnail_dependency_revisions.isEmpty() && !thumbnail_revisions_current(result.thumbnail_dependency_revisions))) {
    const auto stale_recipe_mismatch =
        all_item.file_path == result.file_path &&
        all_item.adjustment_patch_json !=
            result.adjustment_patch_json;
    if (stale_recipe_mismatch) {
      all_item.thumbnail_stale_detected = true;
      all_item.thumbnail_stale_mismatch = true;
      all_item.thumbnail_stale_detection_reason = QStringLiteral("catalog_thumbnail_recipe_mismatch");
      all_item.thumbnail_stale_previous_recipe_hash =
          thumbnail_adjustment_hash(
              result.adjustment_patch_json);
      all_item.thumbnail_stale_current_recipe_hash =
          thumbnail_adjustment_hash(
              all_item.adjustment_patch_json);
      all_item.thumbnail_stale_detection_signature =
          QStringLiteral("catalog-thumbnail-stale|detected:true|mismatch:true|reason:%1|previous:%2|current:%3")
              .arg(all_item.thumbnail_stale_detection_reason,
                   all_item.thumbnail_stale_previous_recipe_hash,
                   all_item.thumbnail_stale_current_recipe_hash);
      const auto visible_index_iterator = visible_asset_index_by_id_.constFind(result.asset_id);
      if (visible_index_iterator != visible_asset_index_by_id_.cend()) {
        const auto index = visible_index_iterator.value();
        if (index >= 0 && index < assets_.size()) {
          copy_thumbnail_runtime_fields(all_item, &assets_[index]);
          emit dataChanged(
              this->index(static_cast<int>(index), 0),
              this->index(static_cast<int>(index), 0),
              {ThumbnailStaleDetectedRole,
               ThumbnailStaleMismatchRole,
               ThumbnailStaleDetectionReasonRole,
               ThumbnailStalePreviousRecipeHashRole,
               ThumbnailStaleCurrentRecipeHashRole,
               ThumbnailStaleDetectionSignatureRole});
        }
      }
      observability::logWarning(
          photoEditorCatalogLog(),
          QStringLiteral("thumbnail.stale_detected"),
          {
              {QStringLiteral("scope"), QStringLiteral("catalog_grid")},
              {QStringLiteral("asset_id"), result.asset_id},
              {QStringLiteral("reason"), all_item.thumbnail_stale_detection_reason},
              {QStringLiteral("previous_recipe_hash"), all_item.thumbnail_stale_previous_recipe_hash},
              {QStringLiteral("current_recipe_hash"), all_item.thumbnail_stale_current_recipe_hash},
          });
    }
    return false;
  }
  all_item.thumbnail_source = result.thumbnail_source;
  all_item.search_preview_current = true;
  all_item.thumbnail_provenance_token = result.thumbnail_provenance_token;
  all_item.thumbnail_dependency_revisions = result.thumbnail_dependency_revisions;

  const auto visible_index_iterator = visible_asset_index_by_id_.constFind(result.asset_id);
  if (visible_index_iterator != visible_asset_index_by_id_.cend()) {
    const auto index = visible_index_iterator.value();
    if (index >= 0 && index < assets_.size()) {
      auto& item = assets_[index];
      if (item.file_path != result.file_path ||
          item.adjustment_patch_json !=
              result.adjustment_patch_json) {
        return false;
      }
      copy_thumbnail_runtime_fields(all_item, &item);
      visible_index = static_cast<int>(index);
      if (selected_index_ == visible_index) {
        selected_changed = true;
      }
    }
  }

  if (visible_index >= 0) {
    emit dataChanged(
        this->index(visible_index, 0),
        this->index(visible_index, 0),
        {ThumbnailSourceRole,
         ThumbnailStaleDetectedRole,
         ThumbnailStaleMismatchRole,
         ThumbnailStaleDetectionReasonRole,
         ThumbnailStalePreviousRecipeHashRole,
         ThumbnailStaleCurrentRecipeHashRole,
         ThumbnailStaleDetectionSignatureRole});
  }
  if (selected_changed) {
    emit selectedAssetChanged();
  }
  emit searchInputsChanged({result.asset_id});
  return true;
}

QString CatalogAssetModel::thumbnailQueuePriorityForAssetId(const QString& asset_id) const {
  if (asset_id.isEmpty()) {
    return QStringLiteral("unknown");
  }
  if (const auto* item = selectedItem(); item != nullptr && item->asset_id == asset_id) {
    return QStringLiteral("selected");
  }

  const auto visible_index_iterator = visible_asset_index_by_id_.constFind(asset_id);
  const auto visible_index = visible_index_iterator == visible_asset_index_by_id_.cend()
                                 ? -1
                                 : static_cast<int>(visible_index_iterator.value());
  if (visible_index < 0) {
    return QStringLiteral("background");
  }
  if (visible_thumbnail_first_index_ >= 0 && visible_thumbnail_last_index_ >= visible_thumbnail_first_index_) {
    if (visible_index >= visible_thumbnail_first_index_ && visible_index <= visible_thumbnail_last_index_) {
      return QStringLiteral("visible_window");
    }
    if (visible_index >= visible_thumbnail_first_index_ - kVisibleThumbnailPrefetchRows &&
        visible_index <= visible_thumbnail_last_index_ + kVisibleThumbnailPrefetchRows) {
      return QStringLiteral("visible_prefetch");
    }
  }
  return QStringLiteral("filtered");
}

void CatalogAssetModel::setInteractiveRenderingActive(bool active) {
  if (active) {
    ++interactive_render_priority_depth_;
    if (interactive_render_priority_depth_ > 1) {
      return;
    }
    thumbnail_work_deferred_for_interactive_render_ = true;
    for (auto iterator = active_thumbnail_cancellation_by_watcher_.cbegin();
         iterator != active_thumbnail_cancellation_by_watcher_.cend();
         ++iterator) {
      if (iterator.value()) {
        iterator.value()->store(true, std::memory_order_relaxed);
      }
    }
    return;
  }
  if (interactive_render_priority_depth_ <= 0) {
    return;
  }
  --interactive_render_priority_depth_;
  if (interactive_render_priority_depth_ > 0) {
    return;
  }
  thumbnail_work_deferred_for_interactive_render_ = false;
  queueThumbnailRefreshes();
}

bool CatalogAssetModel::interactiveRenderingActive() const {
  return interactive_render_priority_depth_ > 0;
}

int CatalogAssetModel::interactiveRenderingPriorityDepth() const {
  return interactive_render_priority_depth_;
}

int CatalogAssetModel::activeThumbnailWorkerCount() const {
  return static_cast<int>(active_thumbnail_asset_by_watcher_.size());
}

int CatalogAssetModel::pendingThumbnailWorkerCount() const {
  return static_cast<int>(pending_thumbnail_asset_ids_.size());
}

bool CatalogAssetModel::waitForInteractiveThumbnailWorkers(int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!active_thumbnail_asset_by_watcher_.isEmpty()) {
    if (timeout_ms >= 0 && timer.elapsed() >= timeout_ms) {
      return false;
    }
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    QThread::msleep(1);
  }
  return true;
}

void CatalogAssetModel::handleThumbnailFinished() {
  QFutureWatcher<ThumbnailTaskResult>* watcher = nullptr;
  const auto* finished_watcher = sender();
  for (const auto& candidate : thumbnail_watchers_) {
    if (candidate.get() == finished_watcher) {
      watcher = candidate.get();
      break;
    }
  }
  if (watcher == nullptr) {
    return;
  }
  const auto result = watcher->result();
  const auto delivery_wait_ms = result.finished_at_msecs > 0
      ? qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - result.finished_at_msecs) : 0;
  active_thumbnail_cancellation_by_watcher_.remove(watcher);
  active_thumbnail_asset_by_watcher_.remove(watcher);
  active_thumbnail_asset_ids_.remove(result.asset_id);
  const auto applied = applyThumbnailResult(result);
  observability::logInfo(
      photoEditorCatalogLog(),
      QStringLiteral("thumbnail.completed"),
      {
          {QStringLiteral("asset_id"), result.asset_id},
          {QStringLiteral("file_path"), result.file_path},
          {QStringLiteral("success"), !result.thumbnail_source.isEmpty()},
          {QStringLiteral("thumbnail_cache_bytes"), result.thumbnail_cache_bytes},
          {QStringLiteral("cache_bytes"), result.thumbnail_cache_bytes},
          {QStringLiteral("thumbnail_cache_entries"), result.thumbnail_cache_entries},
          {QStringLiteral("thumbnail_cache_probe_ms"), result.thumbnail_cache_probe_ms},
          {QStringLiteral("queue_wait_ms"), result.queue_wait_ms},
          {QStringLiteral("elapsed_ms"), result.elapsed_ms},
          {QStringLiteral("pool_wait_ms"), result.pool_wait_ms},
          {QStringLiteral("delivery_wait_ms"), delivery_wait_ms},
          {QStringLiteral("queue_priority"), result.queue_priority},
          {QStringLiteral("active_worker_count"), active_thumbnail_asset_ids_.size()},
          {QStringLiteral("pending_worker_count"), pending_thumbnail_asset_ids_.size()},
          {QStringLiteral("worker_limit"), kMaxConcurrentThumbnailTasks},
          {QStringLiteral("cancellation_reason"), applied ? QString() : QStringLiteral("stale_result")},
      });
  WorkerQueueRegistry::logSummary(
      QStringLiteral("thumbnail"),
      QStringLiteral("thumbnail"),
      QStringLiteral("library"),
      static_cast<int>(active_thumbnail_asset_ids_.size()),
      static_cast<int>(pending_thumbnail_asset_ids_.size()),
      kMaxConcurrentThumbnailTasks,
      result.queue_wait_ms,
      result.elapsed_ms,
      applied ? 1 : 0,
      applied ? 0 : 1);
  if (!applied && (!result.thumbnail_source.isEmpty() || result.source_changed)) {
    queueThumbnailRefreshes();
    return;
  }
  startNextThumbnailTask();
}

void CatalogAssetModel::setSelectedIndexInternal(int index) {
  if (assets_.isEmpty()) {
    index = -1;
  } else if (index < 0 || index >= assets_.size()) {
    index = 0;
  }

  if (selected_index_ == index) {
    return;
  }

  selected_index_ = index;
  emit selectedIndexChanged();
  emit selectedAssetChanged();
  emit surveyAssetsChanged();
}

void CatalogAssetModel::applyFilters(const QString& preferred_asset_id) {
  QVector<CatalogAssetItem> filtered_assets;
  filtered_assets.reserve(all_assets_.size());

  for (const auto& item : all_assets_) {
    if ((visual_search_active_ && !visual_search_ranks_.contains(item.asset_id)) ||
        !matchesSearchFilter(item) || !matchesProjectFilter(item) || !matchesImportDateFilter(item) ||
        !matchesImportSessionFilter(item) ||
        !matchesCaptureYearFilter(item) || !matchesCameraFilter(item) ||
        !matchesCaptureDateRangeFilter(item) || !matchesMediaTypeFilter(item) || !matchesReviewFilter(item) ||
        !matchesAvailabilityFilter(item) ||
        !matchesMinimumRatingFilter(item)) {
      continue;
    }
    filtered_assets.push_back(item);
  }

  if (visual_search_active_) {
    std::stable_sort(filtered_assets.begin(), filtered_assets.end(), [this](const auto& a, const auto& b) {
      return visual_search_ranks_.value(a.asset_id) < visual_search_ranks_.value(b.asset_id);
    });
  } else if (sort_order_ != QStringLiteral("imported-desc")) {
    std::stable_sort(filtered_assets.begin(), filtered_assets.end(), [this](const auto& a, const auto& b) {
      if (sort_order_ == QStringLiteral("filename-asc")) {
        const auto order = QString::localeAwareCompare(QFileInfo(a.file_path).fileName(), QFileInfo(b.file_path).fileName());
        return order == 0 ? a.asset_id < b.asset_id : order < 0;
      }
      if (sort_order_ == QStringLiteral("rating-desc")) return a.rating == b.rating ? a.asset_id < b.asset_id : a.rating > b.rating;
      const auto capture = [this](const auto& item) {
        const auto time = QDateTime::fromString(asset_metadata_.value(metadataAssetId(item)).value(QStringLiteral("captureTime")).toString(), Qt::ISODate);
        if (time.isValid()) return time;
        return item.capture_timestamp.isValid() ? item.capture_timestamp
            : QDateTime(item.capture_date_value, QTime(0,0), QTimeZone::UTC);
      };
      const auto ca = capture(a), cb = capture(b);
      if (ca == cb) return a.asset_id < b.asset_id;
      return sort_order_ == QStringLiteral("capture-desc") ? ca > cb : ca < cb;
    });
  }

  const auto previous_count = assets_.size();
  beginResetModel();
  assets_ = filtered_assets;
  rebuildVisibleAssetIndexMap();
  endResetModel();
  updateFirstVisiblePhotoIndex();

  if (previous_count != assets_.size()) {
    emit assetCountChanged();
  }

  reconcileSelection(preferred_asset_id);
  refreshVisibleCullingStats();
  queueThumbnailRefreshes();
}

void CatalogAssetModel::updateFirstVisiblePhotoIndex() {
  auto next_first_visible_photo_index = -1;
  for (qsizetype index = 0; index < assets_.size(); ++index) {
    if (assets_.at(index).media_type == QStringLiteral("photo")) {
      next_first_visible_photo_index = static_cast<int>(index);
      break;
    }
  }
  if (first_visible_photo_index_ == next_first_visible_photo_index) {
    return;
  }
  first_visible_photo_index_ = next_first_visible_photo_index;
  emit firstVisiblePhotoIndexChanged();
}

void CatalogAssetModel::rebuildAllAssetIndexMap() {
  all_asset_index_by_id_.clear();
  all_asset_index_by_id_.reserve(static_cast<int>(all_assets_.size()));
  for (qsizetype index = 0; index < all_assets_.size(); ++index) {
    const auto& asset_id = all_assets_.at(index).asset_id;
    if (!asset_id.isEmpty() && !all_asset_index_by_id_.contains(asset_id)) {
      all_asset_index_by_id_.insert(asset_id, index);
    }
  }
}

void CatalogAssetModel::rebuildVisibleAssetIndexMap() {
  visible_asset_index_by_id_.clear();
  visible_asset_index_by_id_.reserve(static_cast<int>(assets_.size()));
  for (qsizetype index = 0; index < assets_.size(); ++index) {
    const auto& asset_id = assets_.at(index).asset_id;
    if (!asset_id.isEmpty() && !visible_asset_index_by_id_.contains(asset_id)) {
      visible_asset_index_by_id_.insert(asset_id, index);
    }
  }
}

void CatalogAssetModel::reconcileSelection(const QString& preferred_asset_id) {
  QSet<QString> next_selected_asset_ids;
  for (const auto& asset_id : selected_asset_ids_) {
    if (visible_asset_index_by_id_.contains(asset_id)) {
      next_selected_asset_ids.insert(asset_id);
    }
  }

  QString next_primary_asset_id;
  if (!preferred_asset_id.isEmpty() && visible_asset_index_by_id_.contains(preferred_asset_id)) {
    next_primary_asset_id = preferred_asset_id;
  } else if (selected_index_ >= 0 && selected_index_ < assets_.size()) {
    const auto current_asset_id = assets_.at(selected_index_).asset_id;
    next_primary_asset_id = current_asset_id;
  }

  if (next_primary_asset_id.isEmpty()) {
    for (const auto& item : assets_) {
      if (next_selected_asset_ids.contains(item.asset_id)) {
        next_primary_asset_id = item.asset_id;
        break;
      }
    }
  }

  if (next_primary_asset_id.isEmpty() && !assets_.isEmpty()) {
    next_primary_asset_id = assets_.front().asset_id;
  }

  auto next_index = -1;
  if (!next_primary_asset_id.isEmpty()) {
    const auto index_iterator = visible_asset_index_by_id_.constFind(next_primary_asset_id);
    if (index_iterator != visible_asset_index_by_id_.cend()) {
      next_index = static_cast<int>(index_iterator.value());
    }
  }

  const auto selection_set_changed = selected_asset_ids_ != next_selected_asset_ids;
  const auto primary_changed = selected_index_ != next_index;
  selected_asset_ids_ = next_selected_asset_ids;
  selected_index_ = next_index;

  if (primary_changed) {
    emit selectedIndexChanged();
  }
  emit selectedAssetChanged();
  emit surveyAssetsChanged();
  if (selection_set_changed) {
    emit selectionChanged();
  }
}

void CatalogAssetModel::bumpProjectStatsRevision() {
  ++project_stats_revision_;
  emit projectStatsRevisionChanged();
}

void CatalogAssetModel::rebuildProjectMetadata() {
  QHash<QString, QStringList> names_by_asset;
  for (const auto& project : projects_) {
    for (const auto& asset_id : project.asset_ids) {
      names_by_asset[asset_id].push_back(project.name);
    }
  }
  for (auto& names : names_by_asset) {
    std::sort(names.begin(), names.end(), [](const QString& left, const QString& right) {
      return left.localeAwareCompare(right) < 0;
    });
  }
  for (auto& item : all_assets_) {
    item.project_names = names_by_asset.value(item.asset_id);
    item.project_summary = project_summary_for_names(item.project_names);
  }
}

void CatalogAssetModel::rebuildCaptureYears() {
  QStringList years;
  QStringList camera_labels;
  QSet<QString> seen_camera_labels;
  QSet<int> seen_years;
  for (const auto& item : all_assets_) {
    if (item.media_type == QStringLiteral("timeline")) {
      continue;
    }
    const auto camera_label = item.camera_label.trimmed();
    const auto camera_key = camera_label.toCaseFolded();
    if (!camera_label.isEmpty() && !seen_camera_labels.contains(camera_key)) {
      seen_camera_labels.insert(camera_key);
      camera_labels.push_back(camera_label);
    }
    if (item.capture_date_value.isValid()) {
      const auto year = item.capture_date_value.year();
      if (!seen_years.contains(year)) {
        seen_years.insert(year);
        years.push_back(QString::number(year));
      }
    }
  }

  std::sort(
      years.begin(),
      years.end(),
      [](const QString& left, const QString& right) { return left.toInt() > right.toInt(); });

  std::sort(
      camera_labels.begin(),
      camera_labels.end(),
      [](const QString& left, const QString& right) {
        return left.localeAwareCompare(right) < 0;
      });

  if (capture_years_ != years) {
    capture_years_ = years;
    emit captureYearsChanged();
  }
  if (camera_labels_ != camera_labels) {
    camera_labels_ = camera_labels;
    emit cameraLabelsChanged();
  }
}

void CatalogAssetModel::rebuildBurstMetadata() {
  QHash<QString, QVector<int>> timed_candidates;
  for (qsizetype position = 0; position < all_assets_.size(); ++position) {
    auto& item = all_assets_[position];
    item.burst_key.clear(); item.burst_size = 1; item.burst_position = 1; item.burst_time_based = false;
    const auto override_time = QDateTime::fromString(asset_metadata_.value(metadataAssetId(item)).value(QStringLiteral("captureTime")).toString(), Qt::ISODateWithMs);
    const auto time = override_time.isValid() ? override_time : item.capture_timestamp;
    if (item.media_type == QStringLiteral("photo") && !item.virtual_copy && time.isValid()) {
      const auto key = item.camera_label.trimmed().toLower() + QStringLiteral("|") + QFileInfo(item.file_path).absolutePath();
      timed_candidates[key].append(static_cast<int>(position));
      item.burst_time_based = true;
    }
  }
  const auto timestamp = [this](int position) {
    const auto& item = all_assets_[position];
    const auto time = QDateTime::fromString(asset_metadata_.value(metadataAssetId(item)).value(QStringLiteral("captureTime")).toString(), Qt::ISODateWithMs);
    return time.isValid() ? time : item.capture_timestamp;
  };
  for (auto group = timed_candidates.begin(); group != timed_candidates.end(); ++group) {
    auto& positions = group.value();
    std::sort(positions.begin(), positions.end(), [&](int left, int right) {
      const auto a = timestamp(left), b = timestamp(right);
      return a == b ? all_assets_[left].asset_id < all_assets_[right].asset_id : a < b;
    });
    qsizetype start = 0;
    while (start < positions.size()) {
      auto end = start + 1;
      while (end < positions.size() && timestamp(positions[end - 1]).msecsTo(timestamp(positions[end])) <= 2000) ++end;
      const auto size = end - start;
      if (size > 1) {
        const auto key = QStringLiteral("capture:%1|%2").arg(group.key(), all_assets_[positions[start]].asset_id);
        for (auto position = start; position < end; ++position) {
          auto& item = all_assets_[positions[position]];
          item.burst_key = key; item.burst_size = static_cast<int>(size); item.burst_position = static_cast<int>(position - start + 1);
        }
      }
      start = end;
    }
  }
  // Older catalog rows lack time-of-day. Keep their useful filename sequence
  // hints, but expose their provenance so the UI does not call them bursts.
  auto index = 0;
  while (index < all_assets_.size()) {
    auto& item = all_assets_[index];
    if (item.burst_time_based) { ++index; continue; }
    const auto burst_key = burst_group_key_for_item(item);
    if (burst_key.isEmpty()) {
      item.burst_key.clear();
      item.burst_size = 1;
      item.burst_position = 1;
      ++index;
      continue;
    }

    auto group_end = index + 1;
    while (group_end < all_assets_.size() && !all_assets_[group_end].burst_time_based && burst_group_key_for_item(all_assets_[group_end]) == burst_key) {
      ++group_end;
    }

    const auto group_size = group_end - index;
    if (group_size <= 1) {
      item.burst_key.clear();
      item.burst_size = 1;
      item.burst_position = 1;
      index = group_end;
      continue;
    }

    for (auto group_index = index; group_index < group_end; ++group_index) {
      all_assets_[group_index].burst_key = burst_key;
      all_assets_[group_index].burst_size = group_size;
      all_assets_[group_index].burst_position = group_index - index + 1;
    }

    index = group_end;
  }
}

void CatalogAssetModel::rebuildIntelligenceMetadata() {
  QHash<QString, QVector<int>> duplicate_groups;
  QHash<QString, QVector<int>> sync_groups;

  for (auto& item : all_assets_) {
    item.duplicate_group_key.clear();
    item.duplicate_group_size = 1;
    item.duplicate_group_position = 1;
    item.best_shot_suggested = false;
    item.sync_group_key.clear();
    item.sync_group_size = 1;
  }

  for (qsizetype index = 0; index < all_assets_.size(); ++index) {
    const auto duplicate_key = duplicate_group_key_for_item(all_assets_.at(index));
    if (!duplicate_key.isEmpty()) {
      duplicate_groups[duplicate_key].push_back(static_cast<int>(index));
    }

    const auto sync_key = sync_group_key_for_item(all_assets_.at(index));
    if (!sync_key.isEmpty()) {
      sync_groups[sync_key].push_back(static_cast<int>(index));
    }
  }

  for (auto iterator = duplicate_groups.begin(); iterator != duplicate_groups.end(); ++iterator) {
    const auto& indices = iterator.value();
    if (indices.size() <= 1) {
      continue;
    }

    auto best_index = indices.front();
    auto best_score = std::numeric_limits<int>::min();
    for (auto position = 0; position < indices.size(); ++position) {
      auto& item = all_assets_[indices.at(position)];
      item.duplicate_group_key = iterator.key();
      item.duplicate_group_size = static_cast<int>(indices.size());
      item.duplicate_group_position = position + 1;

      const auto base_name = QFileInfo(item.file_path).completeBaseName().toLower();
      auto score = 0;
      if (base_name == duplicate_stem_for_path(item.file_path)) {
        score += 100;
      }
      score -= base_name.length();
      if (score > best_score) {
        best_score = score;
        best_index = indices.at(position);
      }
    }

    if (all_assets_.at(best_index).burst_size <= 1) {
      all_assets_[best_index].best_shot_suggested = true;
    }
  }

  for (auto iterator = sync_groups.begin(); iterator != sync_groups.end(); ++iterator) {
    const auto& indices = iterator.value();
    if (indices.size() <= 1) {
      continue;
    }

    for (const auto item_index : indices) {
      auto& item = all_assets_[item_index];
      item.sync_group_key = iterator.key();
      item.sync_group_size = static_cast<int>(indices.size());
    }
  }

  for (auto& item : all_assets_) {
    if (item.burst_size > 1 && item.burst_position == ((item.burst_size - 1) / 2) + 1)
      item.best_shot_suggested = true;
  }
  applyConfirmedDuplicateGroups();
}

void CatalogAssetModel::startTimelineAssetsLoad() {
  if (timeline_assets_loaded_ || timeline_assets_load_started_) {
    return;
  }

  timeline_assets_load_started_ = true;
  timeline_assets_watcher_.setFuture(QtConcurrent::run([]() {
    return load_timeline_asset_items();
  }));
}

void CatalogAssetModel::loadTimelineAssets() {
  for (const auto& item : timeline_assets_) {
    all_assets_.push_back(item);
  }
}

void CatalogAssetModel::handleTimelineAssetsLoaded() {
  timeline_assets_load_started_ = false;
  timeline_assets_loaded_ = true;
  timeline_assets_ = timeline_assets_watcher_.result();
  reconcileTimelineAssets();
}

void CatalogAssetModel::reconcileTimelineAssets() {
  QElapsedTimer reconciliation_timer;
  reconciliation_timer.start();
  const auto preferred_asset_id = selectedAssetId();
  const auto previous_total_count = all_assets_.size();
  const auto previous_visible_count = assets_.size();

  QVector<CatalogAssetItem> normalized_timeline_assets;
  normalized_timeline_assets.reserve(timeline_assets_.size());
  QHash<QString, CatalogAssetItem> timeline_assets_by_id;
  timeline_assets_by_id.reserve(static_cast<int>(timeline_assets_.size()));
  QSet<QString> changed_asset_ids;
  changed_asset_ids.reserve(static_cast<int>(timeline_assets_.size()));

  for (auto item : timeline_assets_) {
    item.asset_id = item.asset_id.trimmed();
    if (item.asset_id.isEmpty()) {
      continue;
    }
    if (timeline_assets_by_id.contains(item.asset_id)) {
      continue;
    }

    item.media_type = QStringLiteral("timeline");
    item.virtual_copy = false;
    item.source_asset_id = item.asset_id;
    item.variant_name = QStringLiteral("Original");
    item.variant_index = 0;
    apply_culling_state_to_item(&item, cullingStateForAsset(item.asset_id));
    const auto adjustment_patch_override = adjustment_patch_overrides_.value(item.asset_id).trimmed();
    if (!adjustment_patch_override.isEmpty()) {
      item.adjustment_patch_json = adjustment_patch_override;
    }
    const auto relinked_path = relinked_file_paths_.value(item.asset_id).trimmed();
    if (!relinked_path.isEmpty()) {
      apply_relinked_path_to_item(&item, relinked_path);
      refresh_online_state_for_item(&item);
    }
    if (item.variant_name.trimmed().isEmpty()) {
      item.variant_name = QStringLiteral("Original");
    }
    clear_thumbnail_source_for_item(&item);

    // These fields are derived from catalog membership, never from the document.
    item.project_names.clear();
    for (const auto& project : projects_) {
      if (project.asset_ids.contains(item.asset_id)) item.project_names.append(project.name);
    }
    std::sort(item.project_names.begin(), item.project_names.end(),
              [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    item.project_summary = project_summary_for_names(item.project_names);
    item.burst_key.clear();
    item.burst_size = item.burst_position = 1;
    item.duplicate_group_key.clear();
    item.duplicate_group_size = item.duplicate_group_position = 1;
    item.best_shot_suggested = false;
    item.sync_group_key.clear();
    item.sync_group_size = 1;
    const auto existing = all_asset_index_by_id_.constFind(item.asset_id);
    if (existing == all_asset_index_by_id_.cend() || all_assets_.at(existing.value()) != item) {
      changed_asset_ids.insert(item.asset_id);
    }
    timeline_assets_by_id.insert(item.asset_id, item);
    normalized_timeline_assets.push_back(item);
  }

  const auto normalize_us = reconciliation_timer.nsecsElapsed() / 1000;
  QSet<QString> removed_ids;
  bool rebuild_photo_metadata = false;
  bool next_survivor_is_photo = false;
  for (qsizetype i = all_assets_.size(); i-- > 0;) {
    const auto& item = all_assets_.at(i);
    if (item.media_type == QStringLiteral("timeline") && !timeline_assets_by_id.contains(item.asset_id)) {
      removed_ids.insert(item.asset_id);
      changed_asset_ids.insert(item.asset_id);
      // Removing a separator can join two contiguous photo bursts.
      if (i > 0 && next_survivor_is_photo &&
          all_assets_.at(i - 1).media_type == QStringLiteral("photo")) rebuild_photo_metadata = true;
    } else {
      next_survivor_is_photo = item.media_type == QStringLiteral("photo");
    }
  }
  const auto record_phases = [&](qint64 merge_us, qint64 metadata_us, qint64 publication_us) {
    last_timeline_reconciliation_ = {
        {QStringLiteral("normalize_us"), normalize_us},
        {QStringLiteral("merge_us"), merge_us - normalize_us},
        {QStringLiteral("metadata_us"), metadata_us - merge_us},
        {QStringLiteral("publication_us"), publication_us - metadata_us},
        {QStringLiteral("scheduling_us"), reconciliation_timer.nsecsElapsed() / 1000 - publication_us}};
    observability::logInfo(photoEditorCatalogLog(), QStringLiteral("catalog.timeline.reconcile"), last_timeline_reconciliation_);
  };
  if (changed_asset_ids.isEmpty()) {
    const auto now = reconciliation_timer.nsecsElapsed() / 1000;
    record_phases(now, now, now);
    return;
  }
  if (!removed_ids.isEmpty()) {
    all_assets_.erase(std::remove_if(all_assets_.begin(), all_assets_.end(),
        [&](const CatalogAssetItem& item) { return removed_ids.contains(item.asset_id); }), all_assets_.end());
    rebuildAllAssetIndexMap();
  }
  for (const auto& incoming : normalized_timeline_assets) {
    if (!changed_asset_ids.contains(incoming.asset_id)) continue;
    const auto existing = all_asset_index_by_id_.constFind(incoming.asset_id);
    if (existing == all_asset_index_by_id_.cend()) {
      all_asset_index_by_id_.insert(incoming.asset_id, all_assets_.size());
      all_assets_.push_back(incoming);
    } else {
      rebuild_photo_metadata |= all_assets_.at(existing.value()).media_type != QStringLiteral("timeline");
      all_assets_[existing.value()] = incoming;
    }
  }
  const auto merge_us = reconciliation_timer.nsecsElapsed() / 1000;
  // Timelines do not participate in photo burst, duplicate, sync, camera or year groups.
  if (rebuild_photo_metadata) {
    rebuildBurstMetadata();
    rebuildIntelligenceMetadata();
    rebuildCaptureYears();
  }
  bumpProjectStatsRevision();
  const auto metadata_us = reconciliation_timer.nsecsElapsed() / 1000;
  const auto visible = [&](const CatalogAssetItem& item) {
    return (!visual_search_active_ || visual_search_ranks_.contains(item.asset_id)) &&
        matchesSearchFilter(item) && matchesProjectFilter(item) &&
        matchesImportDateFilter(item) && matchesImportSessionFilter(item) &&
        matchesCaptureYearFilter(item) && matchesCameraFilter(item) &&
        matchesCaptureDateRangeFilter(item) && matchesMediaTypeFilter(item) &&
        matchesReviewFilter(item) && matchesAvailabilityFilter(item) && matchesMinimumRatingFilter(item);
  };
  QSet<QString> visible_timeline_ids;
  for (const auto& item : normalized_timeline_assets) {
    if (visible(item)) visible_timeline_ids.insert(item.asset_id);
  }
  const auto remove_row = [&](int row) {
    const auto& item = assets_.at(row);
    return (item.media_type == QStringLiteral("timeline") || timeline_assets_by_id.contains(item.asset_id)) &&
        !visible_timeline_ids.contains(item.asset_id);
  };
  bool visible_structure_changed = false;
  // Publish contiguous ranges, preserving persistent photo indexes and delegates.
  for (int last = static_cast<int>(assets_.size()) - 1; last >= 0;) {
    if (!remove_row(last)) { --last; continue; }
    int first = last;
    while (first > 0 && remove_row(first - 1)) --first;
    visible_structure_changed = true;
    beginRemoveRows(QModelIndex(), first, last);
    for (int row = first; row <= last; ++row) visible_asset_index_by_id_.remove(assets_.at(row).asset_id);
    assets_.remove(first, last - first + 1);
    for (qsizetype row = first; row < assets_.size(); ++row)
      visible_asset_index_by_id_.insert(assets_.at(row).asset_id, row);
    endRemoveRows();
    last = first - 1;
  }
  for (int row = 0; row < assets_.size(); ++row) {
    const auto& item = assets_.at(row);
    if (!rebuild_photo_metadata && !changed_asset_ids.contains(item.asset_id)) continue;
    const auto all_index = all_asset_index_by_id_.constFind(item.asset_id);
    if (all_index == all_asset_index_by_id_.cend()) continue;
    const auto& updated = all_assets_.at(all_index.value());
    if (item == updated) continue;
    assets_[row] = updated;
    emit dataChanged(index(row, 0), index(row, 0));
  }
  QVector<CatalogAssetItem> additions;
  for (const auto& item : normalized_timeline_assets) {
    if (visible_timeline_ids.contains(item.asset_id) && !visible_asset_index_by_id_.contains(item.asset_id))
      additions.append(item);
  }
  const auto rank = [&](const CatalogAssetItem& item) {
    return visual_search_ranks_.value(item.asset_id, std::numeric_limits<int>::max());
  };
  if (visual_search_active_) {
    std::stable_sort(additions.begin(), additions.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
  }
  int insertion_row = visual_search_active_ ? 0 : static_cast<int>(assets_.size());
  for (qsizetype first = 0; first < additions.size();) {
    while (insertion_row < assets_.size() && rank(assets_.at(insertion_row)) <= rank(additions.at(first))) ++insertion_row;
    auto end = first + 1;
    while (end < additions.size() && (!visual_search_active_ || insertion_row == assets_.size() ||
           rank(additions.at(end)) < rank(assets_.at(insertion_row)))) ++end;
    const auto count = static_cast<int>(end - first);
    visible_structure_changed = true;
    beginInsertRows(QModelIndex(), insertion_row, insertion_row + count - 1);
    assets_.insert(insertion_row, count, CatalogAssetItem{});
    for (int offset = 0; offset < count; ++offset) assets_[insertion_row + offset] = additions.at(first + offset);
    for (qsizetype row = insertion_row; row < assets_.size(); ++row)
      visible_asset_index_by_id_.insert(assets_.at(row).asset_id, row);
    endInsertRows();
    insertion_row += count;
    first = end;
  }
  if (previous_total_count != all_assets_.size()) emit totalAssetCountChanged();
  if (previous_visible_count != assets_.size()) emit assetCountChanged();
  reconcileSelection(preferred_asset_id);
  refreshVisibleCullingStats();
  emit searchInputsChanged(changed_asset_ids.values());
  const auto publication_us = reconciliation_timer.nsecsElapsed() / 1000;
  // Timeline edits carry their own preview. Structural changes can shift photos
  // into the thumbnail viewport, so keep scheduling those newly visible rows.
  if (visible_structure_changed || rebuild_photo_metadata) queueThumbnailRefreshes();
  updateFirstVisiblePhotoIndex();
  record_phases(merge_us, metadata_us, publication_us);
}

void CatalogAssetModel::loadProjects() {
  projects_.clear();
  culling_states_.clear();
  adjustment_patch_overrides_.clear();
  virtual_copies_.clear();
  relinked_file_paths_.clear();
  permanently_missing_asset_ids_.clear();

  QFile file(projectsFilePath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return;
  }

  const auto project_array = document.object().value(QStringLiteral("projects")).toArray();
  for (const auto& project_value : project_array) {
    const auto project_object = project_value.toObject();
    const auto name = normalizedProjectName(project_object.value(QStringLiteral("name")).toString());
    if (name.isEmpty() || findProject(name) != nullptr) {
      continue;
    }

    ProjectRecord project;
    project.name = name;
    project.created_at = project_object.value(QStringLiteral("createdAt")).toString();

    const auto asset_ids = project_object.value(QStringLiteral("assetIds")).toArray();
    for (const auto& asset_id_value : asset_ids) {
      const auto asset_id = asset_id_value.toString().trimmed();
      if (!asset_id.isEmpty()) {
        project.asset_ids.insert(asset_id);
      }
    }

    projects_.push_back(project);
  }

  const auto culling_object = document.object().value(QStringLiteral("cullingStates")).toObject();
  for (auto iterator = culling_object.begin(); iterator != culling_object.end(); ++iterator) {
    const auto asset_id = iterator.key().trimmed();
    if (asset_id.isEmpty() || !iterator.value().isObject()) {
      continue;
    }

    auto state = normalized_culling_state(
        {
            .rating = iterator.value().toObject().value(QStringLiteral("rating")).toInt(),
            .picked = iterator.value().toObject().value(QStringLiteral("picked")).toBool(),
            .rejected = iterator.value().toObject().value(QStringLiteral("rejected")).toBool(),
            .color_label = iterator.value().toObject().value(QStringLiteral("colorLabel")).toString(),
        });
    if (!culling_state_is_default(state)) {
      culling_states_.insert(asset_id, state);
    }
  }

  const auto recipe_override_object = document.object().value(QStringLiteral("recipeOverrides")).toObject();
  for (auto iterator = recipe_override_object.begin(); iterator != recipe_override_object.end(); ++iterator) {
    const auto asset_id = iterator.key().trimmed();
    const auto adjustment_payload =
        iterator.value().isObject()
            ? QString::fromUtf8(
                  QJsonDocument(
                      iterator.value().toObject())
                      .toJson(
                          QJsonDocument::Compact))
            : iterator.value().toString().trimmed();
    const auto adjustment_patch_json =
        canonical_asset_bound_adjustment_patch_json(
            adjustment_payload);
    if (asset_id.isEmpty() ||
        adjustment_patch_json.isEmpty()) {
      continue;
    }
    adjustment_patch_overrides_.insert(
        asset_id,
        adjustment_patch_json);
  }

  const auto virtual_copy_array = document.object().value(QStringLiteral("virtualCopies")).toArray();
  for (const auto& value : virtual_copy_array) {
    if (!value.isObject()) {
      continue;
    }
    const auto object = value.toObject();
    CatalogAssetItem item;
    item.asset_id = object.value(QStringLiteral("assetId")).toString().trimmed();
    item.source_asset_id = object.value(QStringLiteral("sourceAssetId")).toString().trimmed();
    item.variant_name = object.value(QStringLiteral("variantName")).toString().trimmed();
    item.variant_index = object.value(QStringLiteral("variantIndex")).toInt();
    item.file_path = normalize_local_path(object.value(QStringLiteral("filePath")).toString());
    item.location_path = item.file_path;
    const auto adjustment_value =
        object.value(QStringLiteral("adjustmentPatch"));
    const auto adjustment_payload =
        adjustment_value.isObject()
            ? QString::fromUtf8(
                  QJsonDocument(
                      adjustment_value.toObject())
                      .toJson(
                          QJsonDocument::Compact))
            : object.value(QStringLiteral("recipeJson"))
                  .toString()
                  .trimmed();
    item.adjustment_patch_json =
        canonical_asset_bound_adjustment_patch_json(
            adjustment_payload);
    item.capture_date = object.value(QStringLiteral("captureDate")).toString();
    item.capture_date_value = QDate::fromString(object.value(QStringLiteral("captureDateValue")).toString(), Qt::ISODate);
    item.camera_label = object.value(QStringLiteral("cameraLabel")).toString();
    item.lens_model = object.value(QStringLiteral("lensModel")).toString();
    item.iso_label = object.value(QStringLiteral("iso")).toString();
    item.shutter_speed = object.value(QStringLiteral("shutterSpeed")).toString();
    item.aperture_label = object.value(QStringLiteral("aperture")).toString();
    item.drive_id = object.value(QStringLiteral("driveId")).toString();
    item.import_mode = object.value(QStringLiteral("importMode")).toString();
    item.imported_at = object.value(QStringLiteral("importedAt")).toString();
    item.imported_at_value = QDateTime::fromString(object.value(QStringLiteral("importedAtValue")).toString(), Qt::ISODate);
    item.media_type = QStringLiteral("photo");
    item.online = object.value(QStringLiteral("online")).toBool(true);
    item.virtual_copy = true;
    if (item.asset_id.isEmpty() || item.source_asset_id.isEmpty() || item.file_path.isEmpty()) {
      continue;
    }
    if (item.variant_name.isEmpty()) {
      item.variant_name = QStringLiteral("Virtual Copy");
    }
    virtual_copies_.push_back(item);
  }

  const auto relinked_paths_object = document.object().value(QStringLiteral("relinkedPaths")).toObject();
  for (auto iterator = relinked_paths_object.begin(); iterator != relinked_paths_object.end(); ++iterator) {
    const auto asset_id = iterator.key().trimmed();
    const auto file_path = normalize_local_path(iterator.value().toString());
    if (asset_id.isEmpty() || file_path.isEmpty()) {
      continue;
    }
    relinked_file_paths_.insert(asset_id, file_path);
  }

  const auto permanently_missing_array =
      document.object().value(QStringLiteral("permanentlyMissingAssetIds")).toArray();
  for (const auto& value : permanently_missing_array) {
    const auto asset_id = value.toString().trimmed();
    if (!asset_id.isEmpty()) {
      permanently_missing_asset_ids_.insert(asset_id);
    }
  }
}

void CatalogAssetModel::saveProjects() const {
  const QFileInfo destination_info(projectsFilePath());
  QDir().mkpath(destination_info.dir().absolutePath());

  QJsonArray project_array;
  for (const auto& project : projects_) {
    QStringList asset_ids = project.asset_ids.values();
    std::sort(asset_ids.begin(), asset_ids.end());

    QJsonArray asset_id_array;
    for (const auto& asset_id : asset_ids) {
      asset_id_array.push_back(asset_id);
    }

    QJsonObject project_object;
    project_object.insert(QStringLiteral("name"), project.name);
    project_object.insert(QStringLiteral("createdAt"), project.created_at);
    project_object.insert(QStringLiteral("assetIds"), asset_id_array);
    project_array.push_back(project_object);
  }

  QJsonObject culling_object;
  for (auto iterator = culling_states_.cbegin(); iterator != culling_states_.cend(); ++iterator) {
    if (iterator.key().trimmed().isEmpty() || culling_state_is_default(iterator.value())) {
      continue;
    }

    QJsonObject item_object;
    item_object.insert(QStringLiteral("rating"), iterator.value().rating);
    item_object.insert(QStringLiteral("picked"), iterator.value().picked);
    item_object.insert(QStringLiteral("rejected"), iterator.value().rejected);
    item_object.insert(QStringLiteral("colorLabel"), iterator.value().color_label);
    culling_object.insert(iterator.key(), item_object);
  }

  QJsonObject recipe_override_object;
  for (auto iterator =
           adjustment_patch_overrides_.cbegin();
       iterator !=
       adjustment_patch_overrides_.cend();
       ++iterator) {
    if (iterator.key().trimmed().isEmpty() || iterator.value().trimmed().isEmpty()) {
      continue;
    }
    const auto adjustment_patch =
        canonical_asset_bound_adjustment_patch_json(
            iterator.value());
    if (!adjustment_patch.isEmpty()) {
      recipe_override_object.insert(
          iterator.key(),
          QJsonDocument::fromJson(
              adjustment_patch.toUtf8())
              .object());
    }
  }

  QJsonArray virtual_copy_array;
  for (const auto& item : virtual_copies_) {
    if (item.asset_id.trimmed().isEmpty() || item.source_asset_id.trimmed().isEmpty()) {
      continue;
    }
    QJsonObject object;
    object.insert(QStringLiteral("assetId"), item.asset_id);
    object.insert(QStringLiteral("sourceAssetId"), item.source_asset_id);
    object.insert(QStringLiteral("variantName"), item.variant_name);
    object.insert(QStringLiteral("variantIndex"), item.variant_index);
    object.insert(QStringLiteral("filePath"), item.file_path);
    const auto adjustment_patch =
        canonical_asset_bound_adjustment_patch_json(
            item.adjustment_patch_json);
    if (!adjustment_patch.isEmpty()) {
      object.insert(
          QStringLiteral("adjustmentPatch"),
          QJsonDocument::fromJson(
              adjustment_patch.toUtf8())
              .object());
    }
    object.insert(QStringLiteral("captureDate"), item.capture_date);
    object.insert(QStringLiteral("captureDateValue"), item.capture_date_value.toString(Qt::ISODate));
    object.insert(QStringLiteral("cameraLabel"), item.camera_label);
    object.insert(QStringLiteral("lensModel"), item.lens_model);
    object.insert(QStringLiteral("iso"), item.iso_label);
    object.insert(QStringLiteral("shutterSpeed"), item.shutter_speed);
    object.insert(QStringLiteral("aperture"), item.aperture_label);
    object.insert(QStringLiteral("driveId"), item.drive_id);
    object.insert(QStringLiteral("importMode"), item.import_mode);
    object.insert(QStringLiteral("importedAt"), item.imported_at);
    object.insert(QStringLiteral("importedAtValue"), item.imported_at_value.toString(Qt::ISODate));
    object.insert(QStringLiteral("online"), item.online);
    virtual_copy_array.push_back(object);
  }

  QJsonObject relinked_paths_object;
  for (auto iterator = relinked_file_paths_.cbegin(); iterator != relinked_file_paths_.cend(); ++iterator) {
    const auto file_path = normalize_local_path(iterator.value());
    if (iterator.key().trimmed().isEmpty() || file_path.isEmpty()) {
      continue;
    }
    relinked_paths_object.insert(iterator.key(), file_path);
  }

  QStringList permanently_missing_asset_ids = permanently_missing_asset_ids_.values();
  std::sort(permanently_missing_asset_ids.begin(), permanently_missing_asset_ids.end());
  QJsonArray permanently_missing_array;
  for (const auto& asset_id : permanently_missing_asset_ids) {
    if (!asset_id.trimmed().isEmpty()) {
      permanently_missing_array.push_back(asset_id);
    }
  }

  QJsonObject root;
  root.insert(QStringLiteral("projects"), project_array);
  root.insert(QStringLiteral("cullingStates"), culling_object);
  root.insert(QStringLiteral("recipeOverrides"), recipe_override_object);
  root.insert(QStringLiteral("virtualCopies"), virtual_copy_array);
  root.insert(QStringLiteral("relinkedPaths"), relinked_paths_object);
  root.insert(QStringLiteral("permanentlyMissingAssetIds"), permanently_missing_array);

  QSaveFile file(projectsFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

void CatalogAssetModel::scheduleProjectsSave() {
  projects_save_timer_.start();
}

void CatalogAssetModel::addAssetsToProject(
    const QStringList& asset_ids,
    const QString& project_name) {
  if (!asset_ids.isEmpty()) changeProjectMemberships(project_name, asset_ids, true);
}

bool CatalogAssetModel::changeProjectMemberships(
    const QString& project_name, const QStringList& asset_ids, bool add, bool open_project) {
  auto name = normalizedProjectName(project_name);
  if (name.isEmpty()) return false;
  if (!project_changes_readable_) {
    setErrorMessage(QStringLiteral("Pending project changes could not be read. The recovery file has been preserved."));
    return false;
  }
  const auto* existing = findProject(name);
  if (!add && existing == nullptr) return false;
  // Lookup ignores case; journal and persist the existing project's identity.
  if (existing != nullptr) name = existing->name;
  auto members = pending_project_changes_.value(name).toObject();
  bool changed = existing == nullptr;
  for (const auto& id : asset_ids) {
    const auto asset_id = id.trimmed();
    if (asset_id.isEmpty()) continue;
    if (existing != nullptr && existing->asset_ids.contains(asset_id) == add) continue;
    members.insert(asset_id, add);
    changed = true;
  }
  if (!changed) {
    if (open_project) setProjectFilter(name);
    return true;
  }
  auto next = pending_project_changes_;
  next.insert(name, members);
  // Journal only membership intent, not recipes or the entire library. Do this
  // before changing the visible state so an acknowledged edit is recoverable.
  if (!saveProjectChanges(next)) return false;
  pending_project_changes_ = next;
  project_change_revisions_.insert(name, ++next_project_change_revision_);
  const auto previous_project_count = projects_.size();
  overlayProjectChanges(false);
  if (previous_project_count != projects_.size()) emit projectNamesChanged();
  rebuildProjectMetadata();
  bumpProjectStatsRevision();
  if (open_project && project_filter_ != name) setProjectFilter(name);
  else applyFilters(selectedAssetId());
  setStatus(QStringLiteral("Saving project '%1'.").arg(name));
  startNextProjectChange();
  return true;
}

void CatalogAssetModel::loadProjectChanges() {
  QFile file(QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("project-membership-outbox.json")));
  if (!file.exists()) return;
  QJsonParseError error{};
  const auto document = file.open(QIODevice::ReadOnly)
      ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument();
  bool valid = document.isObject() && error.error == QJsonParseError::NoError &&
      document.object().value(QStringLiteral("version")).toInt() == 1 &&
      document.object().value(QStringLiteral("projects")).isObject();
  const auto changes = document.object().value(QStringLiteral("projects")).toObject();
  for (auto it = changes.begin(); it != changes.end(); ++it) {
    valid = valid && !normalizedProjectName(it.key()).isEmpty() &&
        normalizedProjectName(it.key()) == it.key() && it.value().isObject();
    const auto members = it.value().toObject();
    for (auto member = members.begin(); member != members.end(); ++member) {
      valid = valid && !member.key().trimmed().isEmpty() && member.value().isBool();
    }
  }
  if (!valid) {
    // Keep the original bytes for recovery; never replace an unreadable outbox.
    project_changes_readable_ = false;
    setErrorMessage(QStringLiteral("Pending project changes could not be read. The recovery file has been preserved."));
    return;
  }
  pending_project_changes_ = changes;
  for (auto it = changes.begin(); it != changes.end(); ++it) {
    project_change_revisions_.insert(it.key(), ++next_project_change_revision_);
  }
  overlayProjectChanges(false);
}

bool CatalogAssetModel::saveProjectChanges(const QJsonObject& changes) {
  if (!project_changes_readable_) return false;
  QSaveFile file(QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("project-membership-outbox.json")));
  const auto bytes = QJsonDocument(QJsonObject{
      {QStringLiteral("version"), 1}, {QStringLiteral("projects"), changes}}).toJson(QJsonDocument::Compact);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    setErrorMessage(QStringLiteral("Could not preserve pending project changes: %1").arg(file.errorString()));
    return false;
  }
  return true;
}

void CatalogAssetModel::overlayProjectChanges(bool acknowledge_snapshot) {
  auto remaining = pending_project_changes_;
  for (auto it = pending_project_changes_.begin(); it != pending_project_changes_.end(); ++it) {
    auto* project = findProject(it.key());
    const auto members = it.value().toObject();
    bool confirmed = project != nullptr;
    if (project != nullptr) {
      for (auto member = members.begin(); member != members.end(); ++member) {
        confirmed = confirmed && project->asset_ids.contains(member.key()) == member.value().toBool();
      }
    }
    if (acknowledge_snapshot && confirmed && active_project_change_ != it.key()) {
      remaining.remove(it.key());
      continue;
    }
    if (project == nullptr) {
      ProjectRecord created;
      created.name = it.key();
      created.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
      projects_.push_back(created);
      project = &projects_.last();
    }
    for (auto member = members.begin(); member != members.end(); ++member) {
      if (member.value().toBool()) project->asset_ids.insert(member.key());
      else project->asset_ids.remove(member.key());
    }
  }
  // Only a snapshot that actually contains the desired memberships retires the
  // journal. An older snapshot is overlaid and cannot undo pending local edits.
  if (remaining != pending_project_changes_ && saveProjectChanges(remaining)) {
    pending_project_changes_ = remaining;
  }
}

void CatalogAssetModel::startNextProjectChange() {
  if (!active_project_change_.isEmpty() || !project_changes_readable_) return;
  QString name;
  for (auto it = pending_project_changes_.begin(); it != pending_project_changes_.end(); ++it) {
    if (attempted_project_change_revisions_.value(it.key()) != project_change_revisions_.value(it.key())) {
      name = it.key();
      break;
    }
  }
  if (name.isEmpty()) {
    if (project_refresh_after_save_) {
      refresh();
      project_refresh_after_save_ = false;
    }
    return;
  }
  const auto members = pending_project_changes_.value(name).toObject();
  QJsonArray added;
  QJsonArray removed;
  for (auto it = members.begin(); it != members.end(); ++it) {
    (it.value().toBool() ? added : removed).push_back(it.key());
  }
  const auto payload = QJsonDocument(QJsonObject{
      {QStringLiteral("project_name"), name}, {QStringLiteral("added"), added},
      {QStringLiteral("removed"), removed}}).toJson(QJsonDocument::Compact);
  active_project_change_ = name;
  attempted_project_change_revisions_.insert(name, project_change_revisions_.value(name));
  const auto program = stateProgram();
  const auto base_arguments = stateArguments({});
  const auto working_directory = QDir::currentPath();
  project_change_cancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto cancellation = project_change_cancellation_;
  project_change_watcher_.setFuture(QtConcurrent::run(
      &catalog_state_thread_pool(), [program, base_arguments, working_directory, payload, cancellation]() {
        if (cancellation->load(std::memory_order_relaxed))
          return QVariantMap{{QStringLiteral("success"), false},
              {QStringLiteral("error"), QStringLiteral("Catalog state update canceled.")}};
        QString error;
        QTemporaryFile request(QDir::tempPath() + QStringLiteral("/looksy-project-XXXXXX.json"));
        bool success = request.open() && request.write(payload) == payload.size() && request.flush();
        if (!success) error = request.errorString();
        if (success) {
          request.close();
          QProcessEnvironment environment;
          success = AppBootstrap::prepareProcessEnvironment(&environment, &error);
          if (success) {
            auto arguments = base_arguments;
            arguments.append({QStringLiteral("update-project-memberships"), request.fileName()});
            success = run_catalog_state_process(program, arguments, working_directory, environment, cancellation.get(), &error);
          }
        }
        return QVariantMap{{QStringLiteral("success"), success}, {QStringLiteral("error"), error}};
      }));
}

void CatalogAssetModel::finalizePendingImportProjectAssignment() {
  const auto project_name = normalizedProjectName(pending_import_project_name_);
  const auto pending_file_paths = pending_import_file_paths_;
  pending_import_project_name_.clear();
  pending_import_file_paths_.clear();

  if (pending_file_paths.isEmpty()) {
    return;
  }

  QSet<QString> file_path_set;
  for (const auto& file_path : pending_file_paths) {
    file_path_set.insert(normalize_local_path(file_path));
  }

  QStringList imported_asset_ids;
  imported_asset_ids.reserve(file_path_set.size());
  for (const auto& item : all_assets_) {
    if (!file_path_set.contains(normalize_local_path(item.file_path))) {
      continue;
    }
    imported_asset_ids.push_back(item.asset_id);
  }

  if (imported_asset_ids.isEmpty()) {
    if (!project_name.isEmpty()) {
      setStatus(QStringLiteral("Import completed, but no assets were assigned to '%1'.").arg(project_name));
    }
    return;
  }

  auto applied_preset_count = 0;
  if (!import_preset_adjustment_patch_json_.isEmpty()) {
    applied_preset_count = applyAdjustmentPatchJsonToAssetIds(
        imported_asset_ids,
        import_preset_adjustment_patch_json_,
        import_preset_adjustment_groups_);
  }
  if (!project_name.isEmpty()) {
    addAssetsToProject(imported_asset_ids, project_name);
    setStatus(
        applied_preset_count > 0
            ? QStringLiteral("Imported %1 assets into '%2' and applied an import preset.")
                  .arg(imported_asset_ids.size())
                  .arg(project_name)
            : QStringLiteral("Imported %1 assets into '%2'.").arg(imported_asset_ids.size()).arg(project_name));
  } else if (applied_preset_count > 0) {
    setStatus(QStringLiteral("Applied import preset to %1 imported assets.").arg(applied_preset_count));
  }
}

QString CatalogAssetModel::projectsFilePath() const {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("projects.json"));
}

CullingStateRecord CatalogAssetModel::cullingStateForAsset(const QString& asset_id) const {
  const auto iterator = culling_states_.constFind(asset_id);
  if (iterator == culling_states_.cend()) {
    return {};
  }

  return iterator.value();
}

void CatalogAssetModel::setAssetCullingState(const QString& asset_id, const CullingStateRecord& state) {
  if (asset_id.trimmed().isEmpty()) {
    return;
  }

  const auto normalized_state = normalized_culling_state(state);
  const auto previous_state = cullingStateForAsset(asset_id);
  if (culling_states_equal(previous_state, normalized_state)) {
    return;
  }

  if (snapshot_fetch_active_ || snapshot_parse_active_) snapshot_changed_review_ids_.insert(asset_id);
  if (culling_state_is_default(normalized_state)) {
    culling_states_.remove(asset_id);
  } else {
    culling_states_.insert(asset_id, normalized_state);
  }

  const auto all_index_iterator = all_asset_index_by_id_.constFind(asset_id);
  if (all_index_iterator != all_asset_index_by_id_.cend()) {
    const auto all_index = all_index_iterator.value();
    if (all_index >= 0 && all_index < all_assets_.size()) {
      apply_culling_state_to_item(&all_assets_[all_index], normalized_state);
    }
  }

  int visible_index = -1;
  const auto visible_index_iterator = visible_asset_index_by_id_.constFind(asset_id);
  if (visible_index_iterator != visible_asset_index_by_id_.cend()) {
    const auto index = visible_index_iterator.value();
    if (index >= 0 && index < assets_.size()) {
      apply_culling_state_to_item(&assets_[index], normalized_state);
      visible_index = static_cast<int>(index);
    }
  }

  scheduleProjectsSave();
  queueCullingPersistence(asset_id, normalized_state);

  if (visible_index >= 0) {
    emit dataChanged(
        this->index(visible_index, 0),
        this->index(visible_index, 0),
        {RatingRole, PickedRole, RejectedRole, ColorLabelRole, CullStatusRole});
  }

  if (review_filter_ != QStringLiteral("all") || minimum_rating_filter_ > 0 || sort_order_ == QStringLiteral("rating-desc")) {
    applyFilters(asset_id);
    return;
  }

  refreshVisibleCullingStats();

  if (selectedAssetId() == asset_id) {
    emit selectedAssetChanged();
  }
}

void CatalogAssetModel::queueCullingPersistence(const QString& asset_id, const CullingStateRecord& state) {
  if (asset_id.isEmpty()) return;
  if (!pending_culling_persistence_.contains(asset_id) || active_culling_persistence_.contains(asset_id) ||
      failed_culling_persistence_.contains(asset_id)) {
    if (!pending_culling_persistence_order_.contains(asset_id)) pending_culling_persistence_order_.append(asset_id);
  }
  failed_culling_persistence_.remove(asset_id);
  pending_culling_persistence_.insert(asset_id, normalized_culling_state(state));
  culling_outbox_dirty_ = true;
  // One short window coalesces a burst, including while a previous batch runs.
  if (!culling_save_timer_.isActive()) culling_save_timer_.start(25);
}

bool CatalogAssetModel::saveCullingChanges(const QHash<QString, CullingStateRecord>& states) {
  if (!culling_outbox_readable_) return false;
  QJsonObject records;
  for (auto it = states.cbegin(); it != states.cend(); ++it) records.insert(it.key(), culling_state_json(it.value()));
  QDir().mkpath(AppBootstrap::appDataLocation());
  QSaveFile file(QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("culling-outbox.json")));
  const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
      {QStringLiteral("states"), records}}).toJson(QJsonDocument::Compact);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    setErrorMessage(QStringLiteral("Could not preserve pending ratings and flags: %1").arg(file.errorString()));
    culling_persistence_paused_ = true;
    return false;
  }
  return true;
}

void CatalogAssetModel::loadCullingChanges() {
  QFile file(QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("culling-outbox.json")));
  if (!file.exists()) return;
  QJsonParseError error{};
  const auto document = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument();
  bool valid = document.isObject() && error.error == QJsonParseError::NoError &&
      document.object().value(QStringLiteral("version")) == QJsonValue(1) &&
      document.object().value(QStringLiteral("states")).isObject();
  QHash<QString, CullingStateRecord> states;
  const auto records = document.object().value(QStringLiteral("states")).toObject();
  for (auto it = records.begin(); it != records.end(); ++it) {
    const auto object = it.value().toObject();
    CullingStateRecord state{object.value(QStringLiteral("rating")).toInt(-1),
        object.value(QStringLiteral("picked")).toBool(), object.value(QStringLiteral("rejected")).toBool(),
        object.value(QStringLiteral("color_label")).toString()};
    valid = valid && !it.key().isEmpty() && it.key() == it.key().trimmed() &&
        it.value().isObject() && object == culling_state_json(normalized_culling_state(state));
    states.insert(it.key(), state);
  }
  if (!valid) {
    culling_outbox_readable_ = false;
    setErrorMessage(QStringLiteral("Pending ratings and flags could not be read. The recovery file has been preserved."));
    return;
  }
  pending_culling_persistence_ = std::move(states);
  pending_culling_persistence_order_ = records.keys();
  overlayCullingChanges();
}

void CatalogAssetModel::overlayCullingChanges() {
  for (auto it = pending_culling_persistence_.cbegin(); it != pending_culling_persistence_.cend(); ++it) {
    if (culling_state_is_default(it.value())) culling_states_.remove(it.key());
    else culling_states_.insert(it.key(), it.value());
  }
}

void CatalogAssetModel::startNextCullingPersistence() {
  if (!culling_outbox_readable_) return;
  if (culling_outbox_dirty_) {
    if (!saveCullingChanges(pending_culling_persistence_)) return;
    culling_outbox_dirty_ = false;
  }
  if (!active_culling_persistence_.isEmpty() || culling_persistence_paused_) return;
  QJsonArray states;
  while (!pending_culling_persistence_order_.isEmpty() && states.size() < 256) {
    const auto id = pending_culling_persistence_order_.takeFirst();
    const auto state = pending_culling_persistence_.constFind(id);
    if (state == pending_culling_persistence_.cend() || failed_culling_persistence_.contains(id)) continue;
    active_culling_persistence_.insert(id, state.value());
    auto object = culling_state_json(state.value());
    object.insert(QStringLiteral("asset_id"), id);
    states.append(object);
  }
  if (states.isEmpty()) return;
  // The journal contains this exact batch before the process is allowed to start.
  culling_request_ = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/looksy-culling-XXXXXX.json"));
  const auto payload = QJsonDocument(QJsonObject{{QStringLiteral("states"), states}}).toJson(QJsonDocument::Compact);
  QProcessEnvironment environment;
  QString environment_error;
  if (!culling_request_->open() || culling_request_->write(payload) != payload.size() || !culling_request_->flush() ||
      !AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    handleCullingPersistenceFinished(-1, QProcess::CrashExit);
    return;
  }
  culling_request_->close();
  culling_persistence_process_.setWorkingDirectory(QDir::currentPath());
  culling_persistence_process_.setProcessEnvironment(environment);
  culling_timeout_timer_.start(30000);
  culling_persistence_process_.start(stateProgram(), stateArguments(
      {QStringLiteral("set-review-states"), culling_request_->fileName()}));
}

void CatalogAssetModel::handleCullingPersistenceFinished(int exit_code, QProcess::ExitStatus exit_status) {
  if (active_culling_persistence_.isEmpty()) return;
  culling_timeout_timer_.stop();
  QJsonParseError parse_error{};
  const auto receipt = QJsonDocument::fromJson(culling_persistence_process_.readAllStandardOutput(), &parse_error).object();
  QSet<QString> applied, missing, seen;
  bool valid = exit_code == 0 && exit_status == QProcess::NormalExit &&
      parse_error.error == QJsonParseError::NoError && receipt.value(QStringLiteral("applied")).isArray() &&
      receipt.value(QStringLiteral("missing")).isArray();
  const auto collect = [&](const QJsonArray& ids, QSet<QString>& target) {
    for (const auto& value : ids) {
      const auto id = value.toString();
      valid = valid && value.isString() && active_culling_persistence_.contains(id) && !seen.contains(id);
      target.insert(id); seen.insert(id);
    }
  };
  collect(receipt.value(QStringLiteral("applied")).toArray(), applied);
  collect(receipt.value(QStringLiteral("missing")).toArray(), missing);
  valid = valid && seen.size() == active_culling_persistence_.size();
  auto remaining = pending_culling_persistence_;
  for (auto it = active_culling_persistence_.cbegin(); it != active_culling_persistence_.cend(); ++it) {
    // Protect a fetch that began before this transaction's acknowledgement.
    if (snapshot_fetch_active_ || snapshot_parse_active_) snapshot_changed_review_ids_.insert(it.key());
    const auto current = remaining.constFind(it.key());
    if (current == remaining.cend() || !culling_states_equal(current.value(), it.value())) continue;
    if (valid && applied.contains(it.key())) remaining.remove(it.key());
    else failed_culling_persistence_.insert(it.key());
  }
  const bool checkpoint_saved = saveCullingChanges(remaining);
  if (checkpoint_saved) {
    pending_culling_persistence_ = std::move(remaining);
    culling_outbox_dirty_ = false;
  }
  // Keep a checkpoint write error visible instead of claiming newer edits are
  // safely retained when that checkpoint could not be committed.
  if (checkpoint_saved && (!valid || !missing.isEmpty())) {
    setErrorMessage(valid
        ? QStringLiteral("%1 ratings or flags are saved locally because their assets are not in the catalog. Refresh to retry.").arg(missing.size())
        : QStringLiteral("Ratings and flags could not be synced. Pending changes are retained locally; refresh to retry."));
  }
  active_culling_persistence_.clear();
  culling_request_.reset();
  // Yield between batches and avoid recursion on process-start failures.
  if (!culling_persistence_paused_ && !pending_culling_persistence_order_.isEmpty()) culling_save_timer_.start(0);
}

void CatalogAssetModel::
    queueAdjustmentPatchPersistence(
        const QString& asset_id,
        const QString& adjustment_patch_json,
        const QString& task_id) {
  if (asset_id.trimmed().isEmpty()) return;
  AdjustmentPatchPersistenceRevision revision{
      .asset_id = asset_id,
      .patch_json = adjustment_patch_json,
      .task_id = task_id,
      .revision_id = QUuid::createUuid().toString(QUuid::WithoutBraces),
  };
  if (task_id.isEmpty()) {
    // Keep ordinary user edits latest-value coalesced without ever replacing
    // an immutable task-attributed revision.
    for (auto index = pending_adjustment_patch_persistence_.size(); index > 0; --index) {
      auto& queued = pending_adjustment_patch_persistence_[index - 1];
      if (queued.asset_id != asset_id) continue;
      if (queued.task_id.isEmpty()) {
        queued = std::move(revision);
        failed_adjustment_patch_persistence_asset_ids_.remove(asset_id);
        emit agentTaskRecipePersistenceChanged(asset_id);
        startNextAdjustmentPatchPersistence();
        return;
      }
      break;
    }
  }
  pending_adjustment_patch_persistence_.push_back(std::move(revision));
  failed_adjustment_patch_persistence_asset_ids_.remove(asset_id);
  emit agentTaskRecipePersistenceChanged(asset_id);
  startNextAdjustmentPatchPersistence();
}

void CatalogAssetModel::
    startNextAdjustmentPatchPersistence() {
  if (recipe_persistence_process_.state() != QProcess::NotRunning ||
      adjustment_patch_persistence_queue_blocked_ ||
      pending_adjustment_patch_persistence_.isEmpty()) {
    return;
  }

  while (!pending_adjustment_patch_persistence_.isEmpty()) {
    const auto revision = pending_adjustment_patch_persistence_.constFirst();
    const auto& asset_id = revision.asset_id;
    const auto& adjustment_patch_json = revision.patch_json;
    const auto asset_index =
        all_asset_index_by_id_.value(asset_id, -1);
    if (asset_index < 0 ||
        asset_index >= all_assets_.size()) {
      failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
      if (!revision.task_id.isEmpty()) {
        adjustment_patch_persistence_queue_blocked_ = true;
        return;
      }
      pending_adjustment_patch_persistence_.removeFirst();
      emit agentTaskRecipePersistenceChanged(asset_id);
      continue;
    }
    const auto& item = all_assets_.at(asset_index);
    const auto decoded_adjustments =
        parse_adjustment_payload_or_legacy_recipe(
            adjustment_patch_json);
    if (!decoded_adjustments.ok()) {
      failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
      if (!revision.task_id.isEmpty()) {
        adjustment_patch_persistence_queue_blocked_ = true;
        return;
      }
      pending_adjustment_patch_persistence_.removeFirst();
      emit agentTaskRecipePersistenceChanged(asset_id);
      continue;
    }
    const auto state =
        *decoded_adjustments.state;
    const auto persistence =
        prepare_desktop_recipe_persistence(
            asset_id,
            state,
            resolve_decoder_provenance_for_source(
                item.file_path,
                state),
            QDir(AppBootstrap::appDataLocation())
                .filePath(QStringLiteral("recipe-assets")));
    if (!persistence.ok()) {
      failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
      setStatus(persistence.error);
      if (!revision.task_id.isEmpty()) {
        adjustment_patch_persistence_queue_blocked_ = true;
        return;
      }
      pending_adjustment_patch_persistence_.removeFirst();
      emit agentTaskRecipePersistenceChanged(asset_id);
      continue;
    }

    QProcessEnvironment environment;
    QString environment_error;
    if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
      failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
      if (!revision.task_id.isEmpty()) {
        adjustment_patch_persistence_queue_blocked_ = true;
        return;
      }
      pending_adjustment_patch_persistence_.removeFirst();
      emit agentTaskRecipePersistenceChanged(asset_id);
      continue;
    }

    QStringList payload_arguments;
    if (persistence.legacy_only) {
      payload_arguments = {
          QStringLiteral("--legacy-v1"), asset_id, persistence.legacy_recipe_json};
    } else {
      payload_arguments = {
          asset_id,
          persistence.legacy_recipe_json,
          persistence.canonical_document.recipe_payload,
          persistence.canonical_document.cache_key,
          persistence.canonical_document.provenance_payload,
      };
    }
    const auto payload = catalog_helper_argument_payload(payload_arguments);
    if (payload_arguments.size() > 32 || payload.size() > kMaxCatalogHelperPayloadBytes) {
      failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
      if (!revision.task_id.isEmpty()) {
        adjustment_patch_persistence_queue_blocked_ = true;
        return;
      }
      pending_adjustment_patch_persistence_.removeFirst();
      emit agentTaskRecipePersistenceChanged(asset_id);
      continue;
    }

    QVariantMap persistence_context{
        {QStringLiteral("taskId"), revision.task_id},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("revisionId"), revision.revision_id},
        {QStringLiteral("attemptId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("sourceOwner"), QStringLiteral("catalog")},
        {QStringLiteral("sourcePath"), item.file_path},
        {QStringLiteral("patchJson"), adjustment_patch_json},
        {QStringLiteral("payload"), payload},
        {QStringLiteral("legacyRecipeJson"), persistence.legacy_recipe_json},
        {QStringLiteral("legacyOnly"), persistence.legacy_only},
    };
    if (!persistence.legacy_only) {
      persistence_context.insert(QStringLiteral("canonicalRecipePayload"),
          persistence.canonical_document.recipe_payload);
      persistence_context.insert(QStringLiteral("canonicalCacheKey"),
          persistence.canonical_document.cache_key);
      persistence_context.insert(QStringLiteral("canonicalProvenancePayload"),
          persistence.canonical_document.provenance_payload);
    }
    if (!revision.task_id.isEmpty()) {
      if (!agent_recipe_persistence_handler_) {
        failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
        adjustment_patch_persistence_queue_blocked_ = true;
        return;
      }
      const auto guarded = agent_recipe_persistence_handler_(
          QStringLiteral("guard"), persistence_context);
      const auto guarded_payload = guarded.value(QStringLiteral("payload")).toByteArray();
      if (!guarded.value(QStringLiteral("ok")).toBool() || guarded_payload.isEmpty() ||
          guarded_payload.size() > kMaxCatalogHelperPayloadBytes) {
        failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
        adjustment_patch_persistence_queue_blocked_ = true;
        const auto error = guarded.value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) setStatus(error);
        emit agentTaskRecipePersistenceChanged(asset_id);
        return;
      }
      persistence_context.insert(QStringLiteral("payload"), guarded_payload);
      pending_adjustment_patch_persistence_.removeFirst();
      active_adjustment_patch_persistence_asset_id_ = asset_id;
      active_adjustment_patch_persistence_task_id_ = revision.task_id;
      active_adjustment_patch_persistence_revision_id_ = revision.revision_id;
      active_adjustment_patch_persistence_context_ = persistence_context;
      const auto admission = agent_recipe_persistence_handler_(
          QStringLiteral("prepare"), persistence_context);
      if (!admission.value(QStringLiteral("ok")).toBool()) {
        failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
        active_adjustment_patch_persistence_asset_id_.clear();
        active_adjustment_patch_persistence_task_id_.clear();
        active_adjustment_patch_persistence_revision_id_.clear();
        active_adjustment_patch_persistence_context_.clear();
        pending_adjustment_patch_persistence_.prepend(revision);
        adjustment_patch_persistence_queue_blocked_ = true;
        if (!admission.value(QStringLiteral("error")).toString().isEmpty())
          setStatus(admission.value(QStringLiteral("error")).toString());
        return;
      }
    } else {
      pending_adjustment_patch_persistence_.removeFirst();
      active_adjustment_patch_persistence_asset_id_ = asset_id;
      active_adjustment_patch_persistence_task_id_.clear();
      active_adjustment_patch_persistence_revision_id_ = revision.revision_id;
      active_adjustment_patch_persistence_context_ = persistence_context;
    }
    active_adjustment_patch_persistence_owner_accepted_ = revision.task_id.isEmpty();

    recipe_persistence_process_.setWorkingDirectory(QDir::currentPath());
    recipe_persistence_process_.setProcessEnvironment(environment);
    const auto helper_payload =
        active_adjustment_patch_persistence_context_.value(QStringLiteral("payload")).toByteArray();
    const auto launched = start_catalog_helper_stdin_payload(
        &recipe_persistence_process_,
        AppBootstrap::helperProgram(QStringLiteral("save_edit_recipe")),
        AppBootstrap::helperArguments(
            QStringLiteral("save_edit_recipe"), {QStringLiteral("--stdin-json-v1")}),
        helper_payload,
        nullptr);
    if (!launched && recipe_persistence_process_.state() == QProcess::NotRunning)
      failed_adjustment_patch_persistence_asset_ids_.insert(asset_id);
    emit agentTaskRecipePersistenceChanged(asset_id);
    return;
  }
}

void CatalogAssetModel::handleRecipePersistenceFinished(
    int exit_code,
    QProcess::ExitStatus exit_status) {
  const auto completed_asset_id = active_adjustment_patch_persistence_asset_id_;
  if (completed_asset_id.isEmpty()) return;
  const auto completed_task_id = active_adjustment_patch_persistence_task_id_;
  const auto completed_revision_id = active_adjustment_patch_persistence_revision_id_;
  auto completed_context = active_adjustment_patch_persistence_context_;
  const auto helper_succeeded = exit_code == 0 && exit_status == QProcess::NormalExit;
  QVariantMap owner_result{{QStringLiteral("ok"), true}};
  if (!completed_task_id.isEmpty()) {
    completed_context.insert(QStringLiteral("ok"), helper_succeeded);
    completed_context.insert(QStringLiteral("exitCode"), exit_code);
    completed_context.insert(QStringLiteral("exitStatus"),
        exit_status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash"));
    owner_result = agent_recipe_persistence_handler_
        ? agent_recipe_persistence_handler_(QStringLiteral("finished"), completed_context)
        : QVariantMap{{QStringLiteral("ok"), false}};
  }
  const auto owner_accepted = owner_result.value(QStringLiteral("ok")).toBool() &&
      (completed_task_id.isEmpty() || active_adjustment_patch_persistence_owner_accepted_);
  if (!completed_task_id.isEmpty()) {
    const auto payload = completed_context.value(QStringLiteral("payload")).toByteArray();
    task_recipe_terminal_revisions_.insert(
        agent_task_recipe_terminal_key(
            completed_task_id, completed_asset_id, completed_revision_id),
        {{QStringLiteral("taskId"), completed_task_id},
         {QStringLiteral("assetId"), completed_asset_id},
         {QStringLiteral("revisionId"), completed_revision_id},
         {QStringLiteral("attemptId"), QJsonValue::fromVariant(
              completed_context.value(QStringLiteral("attemptId")))},
         {QStringLiteral("payloadSha256"), QString::fromLatin1(
              QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())},
         {QStringLiteral("helperSucceeded"), helper_succeeded},
         {QStringLiteral("ownerAccepted"), owner_accepted}});
  }
  const auto revision = AdjustmentPatchPersistenceRevision{
      .asset_id = completed_asset_id,
      .patch_json = completed_context.value(QStringLiteral("patchJson")).toString(),
      .task_id = completed_task_id,
      .revision_id = completed_revision_id,
  };
  if (!helper_succeeded || !owner_accepted)
    failed_adjustment_patch_persistence_asset_ids_.insert(completed_asset_id);
  else
    failed_adjustment_patch_persistence_asset_ids_.remove(completed_asset_id);
  active_adjustment_patch_persistence_asset_id_.clear();
  active_adjustment_patch_persistence_task_id_.clear();
  active_adjustment_patch_persistence_revision_id_.clear();
  active_adjustment_patch_persistence_context_.clear();
  active_adjustment_patch_persistence_owner_accepted_ = true;
  if (!completed_task_id.isEmpty()) {
    adjustment_patch_persistence_queue_blocked_ = true;
    adjustment_patch_persistence_waiting_task_id_ = completed_task_id;
  }
  if (!completed_task_id.isEmpty() && (!helper_succeeded || !owner_accepted)) {
    pending_adjustment_patch_persistence_.prepend(revision);
    adjustment_patch_persistence_queue_blocked_ = true;
  }
  if (!adjustment_patch_persistence_queue_blocked_) {
    // The task owner settles its exact DB receipt on the next event-loop turn.
    // Start the following revision afterward so it checkpoints the settled
    // previous revision instead of racing the owner callback.
    QTimer::singleShot(0, this, [this] { startNextAdjustmentPatchPersistence(); });
  }
  if (!completed_asset_id.isEmpty()) emit agentTaskRecipePersistenceChanged(completed_asset_id);
}

void CatalogAssetModel::recordCullingChange(
    const QString& asset_id,
    const CullingStateRecord& before,
    const CullingStateRecord& after,
    const QString& label) {
  const auto normalized_before = normalized_culling_state(before);
  const auto normalized_after = normalized_culling_state(after);
  if (asset_id.isEmpty() || culling_states_equal(normalized_before, normalized_after)) {
    return;
  }

  culling_undo_stack_.push_back(
      {.asset_id = asset_id, .before = normalized_before, .after = normalized_after, .label = label});
  constexpr qsizetype maximum_history_entries = 2000;
  if (culling_undo_stack_.size() > maximum_history_entries) {
    culling_undo_stack_.removeFirst();
  }
  culling_redo_stack_.clear();
  emit cullingHistoryChanged();
}

ProjectRecord* CatalogAssetModel::findProject(const QString& project_name) {
  const auto normalized_name = normalizedProjectName(project_name);
  for (auto& project : projects_) {
    if (normalizedProjectName(project.name).compare(normalized_name, Qt::CaseInsensitive) == 0) {
      return &project;
    }
  }

  return nullptr;
}

const ProjectRecord* CatalogAssetModel::findProject(const QString& project_name) const {
  const auto normalized_name = normalizedProjectName(project_name);
  for (const auto& project : projects_) {
    if (normalizedProjectName(project.name).compare(normalized_name, Qt::CaseInsensitive) == 0) {
      return &project;
    }
  }

  return nullptr;
}

QString CatalogAssetModel::normalizedProjectName(const QString& project_name) const {
  return project_name.simplified();
}

bool CatalogAssetModel::matchesImportDateFilter(const CatalogAssetItem& item) const {
  if (import_date_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (!item.imported_at_value.isValid()) {
    return false;
  }

  const auto imported_date = item.imported_at_value.date();
  const auto today = QDate::currentDate();

  if (import_date_filter_ == QStringLiteral("today")) {
    return imported_date == today;
  }

  if (import_date_filter_ == QStringLiteral("7d")) {
    return imported_date >= today.addDays(-6) && imported_date <= today;
  }

  if (import_date_filter_ == QStringLiteral("30d")) {
    return imported_date >= today.addDays(-29) && imported_date <= today;
  }

  if (import_date_filter_ == QStringLiteral("year")) {
    return imported_date.year() == today.year();
  }

  return true;
}

bool CatalogAssetModel::matchesImportSessionFilter(const CatalogAssetItem& item) const {
  if (import_session_from_filter_.isEmpty() && import_session_to_filter_.isEmpty()) {
    return true;
  }
  if (!item.imported_at_value.isValid()) {
    return false;
  }

  const auto from = parse_iso_datetime(import_session_from_filter_);
  const auto to = parse_iso_datetime(import_session_to_filter_);
  return (!from.isValid() || item.imported_at_value >= from) &&
         (!to.isValid() || item.imported_at_value <= to);
}

bool CatalogAssetModel::matchesCaptureYearFilter(const CatalogAssetItem& item) const {
  if (capture_year_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (capture_year_filter_ == QStringLiteral("unknown")) {
    return !item.capture_date_value.isValid();
  }

  return item.capture_date_value.isValid()
      && QString::number(item.capture_date_value.year()) == capture_year_filter_;
}

bool CatalogAssetModel::matchesCameraFilter(const CatalogAssetItem& item) const {
  if (camera_filter_.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  if (camera_filter_.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) == 0) {
    return item.camera_label.trimmed().isEmpty();
  }
  return item.camera_label.compare(camera_filter_, Qt::CaseInsensitive) == 0;
}

bool CatalogAssetModel::matchesCaptureDateRangeFilter(const CatalogAssetItem& item) const {
  if (capture_date_from_filter_.isEmpty() && capture_date_to_filter_.isEmpty()) {
    return true;
  }
  if (!item.capture_date_value.isValid()) {
    return false;
  }
  const auto from = QDate::fromString(capture_date_from_filter_, Qt::ISODate);
  const auto to = QDate::fromString(capture_date_to_filter_, Qt::ISODate);
  return (!from.isValid() || item.capture_date_value >= from) &&
         (!to.isValid() || item.capture_date_value <= to);
}

bool CatalogAssetModel::matchesProjectFilter(const CatalogAssetItem& item) const {
  if (project_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (project_filter_ == QStringLiteral("unassigned")) {
    return item.project_names.isEmpty();
  }

  return item.project_names.contains(project_filter_, Qt::CaseInsensitive);
}

bool CatalogAssetModel::matchesMediaTypeFilter(const CatalogAssetItem& item) const {
  // Collection scope survives clearing search/review filters. Document records
  // remain in all_assets_ for their own pages and media pickers.
  if (media_collection_filter_ == QStringLiteral("images") &&
      ((item.media_type != QStringLiteral("photo") && item.media_type != QStringLiteral("video")) ||
       document_source_exclusions_.contains(normalize_local_path(item.file_path)))) {
    return false;
  }
  if (media_type_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (media_type_filter_ == QStringLiteral("images")) {
    return item.media_type == QStringLiteral("photo") ||
           item.media_type == QStringLiteral("video");
  }

  if (media_type_filter_ == QStringLiteral("photos")) {
    return item.media_type == QStringLiteral("photo");
  }

  if (media_type_filter_ == QStringLiteral("videos")) {
    return item.media_type == QStringLiteral("video");
  }

  if (media_type_filter_ == QStringLiteral("timelines")) {
    return item.media_type == QStringLiteral("timeline");
  }

  return true;
}

bool CatalogAssetModel::matchesReviewFilter(const CatalogAssetItem& item) const {
  if (review_filter_ == QStringLiteral("confirmed-duplicates")) return confirmed_duplicate_groups_.contains(item.asset_id);
  if (review_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (review_filter_ == QStringLiteral("picked")) {
    return item.picked;
  }

  if (review_filter_ == QStringLiteral("rejected")) {
    return item.rejected;
  }

  if (review_filter_ == QStringLiteral("unmarked")) {
    return !item.picked && !item.rejected;
  }

  if (review_filter_ == QStringLiteral("rated")) {
    return item.rating > 0;
  }

  if (review_filter_ == QStringLiteral("unrated")) {
    return item.rating == 0;
  }

  return true;
}

bool CatalogAssetModel::matchesAvailabilityFilter(const CatalogAssetItem& item) const {
  if (availability_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (availability_filter_ == QStringLiteral("online")) {
    return item.online;
  }

  if (availability_filter_ == QStringLiteral("offline")) {
    return !item.online;
  }

  return true;
}

bool CatalogAssetModel::matchesMinimumRatingFilter(const CatalogAssetItem& item) const {
  return minimum_rating_filter_ <= 0 || item.rating >= minimum_rating_filter_;
}

bool CatalogAssetModel::matchesSearchFilter(const CatalogAssetItem& item) const {
  if (search_filter_.isEmpty()) {
    return true;
  }
  // Substring match on filename, camera, lens, location, capture date
  const auto filename = QFileInfo(item.file_path).fileName();
  const auto metadata = asset_metadata_.value(metadataAssetId(item));
  for (auto it = metadata.cbegin(); it != metadata.cend(); ++it) {
    const auto text = it.key() == QStringLiteral("keywords") ? it.value().toStringList().join(QStringLiteral(" ")) : it.value().toString();
    if (text.contains(search_filter_, Qt::CaseInsensitive)) return true;
  }
  return filename.contains(search_filter_, Qt::CaseInsensitive) ||
         item.camera_label.contains(search_filter_, Qt::CaseInsensitive) ||
         item.lens_model.contains(search_filter_, Qt::CaseInsensitive) ||
         item.iso_label.contains(search_filter_, Qt::CaseInsensitive) ||
         item.shutter_speed.contains(search_filter_, Qt::CaseInsensitive) ||
         item.aperture_label.contains(search_filter_, Qt::CaseInsensitive) ||
         item.location_path.contains(search_filter_, Qt::CaseInsensitive) ||
         item.capture_date.contains(search_filter_, Qt::CaseInsensitive) ||
         item.media_type.contains(search_filter_, Qt::CaseInsensitive);
}

bool CatalogAssetModel::refreshAssetOnlineState(const QString& asset_id) {
  auto changed = false;
  for (auto& item : all_assets_) {
    if (item.asset_id == asset_id) {
      changed = refresh_online_state_for_item(&item) || changed;
    }
  }
  for (auto& item : assets_) {
    if (item.asset_id == asset_id) {
      changed = refresh_online_state_for_item(&item) || changed;
    }
  }
  return changed;
}

int CatalogAssetModel::relinkAssetsToFolder(const QString& folder_url, bool selected_only) {
  const auto folder_path = normalize_local_path(localPathForUrl(folder_url));
  const QFileInfo folder_info(folder_path);
  if (folder_path.isEmpty() || !folder_info.exists() || !folder_info.isDir()) {
    setStatus(QStringLiteral("Choose a valid folder before relinking offline media."));
    return 0;
  }

  QVector<CatalogAssetItem> targets;
  for (const auto& item : all_assets_) {
    if (item.media_type == QStringLiteral("timeline")) {
      continue;
    }
    if (selected_only) {
      if (item.asset_id == selectedAssetId()) {
        targets.push_back(item);
      }
      continue;
    }
    if (!item.online) {
      targets.push_back(item);
    }
  }

  if (targets.isEmpty()) {
    setStatus(selected_only ? QStringLiteral("Select an offline asset to relink.")
                            : QStringLiteral("No offline assets need relinking."));
    return 0;
  }

  QHash<QString, QStringList> candidates_by_name;
  QDirIterator iterator(
      folder_path,
      QDir::Files | QDir::NoDotAndDotDot,
      QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const auto candidate_path = normalize_local_path(iterator.next());
    if (!has_supported_import_extension(candidate_path)) {
      continue;
    }
    const auto key = QFileInfo(candidate_path).fileName().toLower();
    if (!key.isEmpty()) {
      candidates_by_name[key].push_back(candidate_path);
    }
  }

  if (candidates_by_name.isEmpty()) {
    setStatus(QStringLiteral("No supported photo or video files were found in the chosen folder."));
    return 0;
  }

  auto updated_count = 0;
  auto persisted_all = true;
  for (const auto& target : targets) {
    const auto candidate = best_relink_candidate(target, candidates_by_name);
    if (candidate.isEmpty()) {
      continue;
    }

    auto applied = false;
    for (auto& item : all_assets_) {
      if (item.asset_id != target.asset_id) {
        continue;
      }
      apply_relinked_path_to_item(&item, candidate);
      refresh_online_state_for_item(&item);
      emit searchInputsChanged({item.asset_id});
      applied = true;
      break;
    }

    if (!applied) {
      continue;
    }

    QString error_message;
    if (!runCatalogStateCommand(
            {QStringLiteral("set-relink-path"), target.asset_id, candidate},
            &error_message)) {
      persisted_all = false;
    }

    if (snapshot_fetch_active_ || snapshot_parse_active_) snapshot_changed_relink_ids_.insert(target.asset_id);
    relinked_file_paths_.insert(target.asset_id, candidate);
    permanently_missing_asset_ids_.remove(target.asset_id);

    const auto jobs = loadPersistedJobRecords();
    for (auto job : jobs) {
      const auto source_failure =
          job.error_message.contains(QStringLiteral("source"), Qt::CaseInsensitive) ||
          job.error_message.contains(QStringLiteral("missing"), Qt::CaseInsensitive) ||
          job.error_message.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive) ||
          !QFileInfo::exists(job.source_path);
      if (job.asset_id != target.asset_id ||
          job.job_kind != QStringLiteral("asset_export") ||
          job.status != QStringLiteral("Failed") ||
          !source_failure) {
        continue;
      }
      job.source_path = candidate;
      job.status = QStringLiteral("Queued");
      job.error_message.clear();
      job.progress = 0;
      job.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
      job.metadata_json.insert(QStringLiteral("resumedAfterRelink"), true);
      job.metadata_json.insert(QStringLiteral("relinkedSourcePath"), candidate);
      (void)upsertPersistedJobRecord(job, nullptr);
    }
    ++updated_count;
  }

  if (updated_count == 0) {
    setStatus(QStringLiteral("No matching files were found for the current offline selection."));
    return 0;
  }

  if (selected_only && availability_filter_ == QStringLiteral("offline")) {
    availability_filter_ = QStringLiteral("all");
    emit filtersChanged();
  }

  if (!persisted_all) {
    saveProjects();
  }
  applyFilters(selectedAssetId());
  restartMediaDurationRefresh();
  setStatus(
      QStringLiteral("Relinked %1 offline %2 from %3.")
          .arg(updated_count)
          .arg(updated_count == 1 ? QStringLiteral("asset") : QStringLiteral("assets"))
          .arg(QFileInfo(folder_path).fileName().isEmpty() ? folder_path : QFileInfo(folder_path).fileName()));
  return updated_count;
}

const CatalogAssetItem* CatalogAssetModel::selectedItem() const {
  if (selected_index_ < 0 || selected_index_ >= assets_.size()) {
    return nullptr;
  }
  return &assets_.at(selected_index_);
}

QString CatalogAssetModel::snapshotProgram() const {
  return AppBootstrap::helperProgram(QStringLiteral("catalog_snapshot"));
}

QStringList CatalogAssetModel::snapshotArguments() const {
  return AppBootstrap::helperArguments(QStringLiteral("catalog_snapshot"), {QStringLiteral("0")});
}

QString CatalogAssetModel::importProgram() const {
  return AppBootstrap::helperProgram(QStringLiteral("import_catalog"));
}

QStringList CatalogAssetModel::importArguments(const QString& folder_path) const {
  const auto drive_id = derive_drive_id(folder_path);
  const auto drive_name = derive_drive_name(folder_path);
  const auto import_mode = QStringLiteral("managed");

  QStringList arguments = AppBootstrap::helperArguments(
      QStringLiteral("import_catalog"),
      {folder_path, drive_id, drive_name, import_mode});

  if (!import_selection_manifest_path_.isEmpty()) {
    arguments.push_back(import_selection_manifest_path_);
  }

  return arguments;
}

QString CatalogAssetModel::stateProgram() const {
  return AppBootstrap::helperProgram(QStringLiteral("catalog_state"));
}

QStringList CatalogAssetModel::stateArguments(const QStringList& command_arguments) const {
  return AppBootstrap::helperArguments(QStringLiteral("catalog_state"), command_arguments);
}

#include "catalog_asset_metadata.inc"

}  // namespace photo_editor::desktop
