#include "catalog_asset_model.h"

#include "app_bootstrap.h"
#include "catalog_recipe_projection.h"
#include "render_plan_store.h"
#include "desktop_recipe_persistence_service.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "observability.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "timeline_asset_store.h"
#include "worker_queue_registry.h"

#include <algorithm>
#include <cmath>
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
#include <QStorageInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

namespace photo_editor::desktop {

namespace {

constexpr int kThumbnailEdge = 768;
constexpr int kThumbnailDecodeCeiling = 1280;
constexpr int kMaxConcurrentThumbnailTasks = 4;
constexpr int kVisibleThumbnailPrefetchRows = 18;
constexpr qint64 kImportSourceRefreshCacheWindowMs = 30000;
constexpr int kCatalogStateStartTimeoutMs = 5000;
constexpr int kCatalogStateFinishTimeoutMs = 15000;

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

QString photo_thumbnail_cache_key(
    const QString& file_path,
    const QString& adjustment_payload) {
  return QStringLiteral(
             "photo-v5-canonical-adjustments:%1:%2")
      .arg(
          file_path,
          thumbnail_adjustment_identity(
              adjustment_payload));
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

  const auto cache_path = thumbnail_cache_path_for_key(QStringLiteral("video-v2:%1").arg(file_path));
  const QFileInfo cache_info(cache_path);
  if (!cache_info.exists() || cache_info.lastModified() < source_info.lastModified()) {
    return {};
  }

  return versioned_local_file_url(cache_path);
}

double probe_video_duration_seconds(const QString& file_path) {
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
  if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return 0.0;
  }

  bool okay = false;
  const auto seconds = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toDouble(&okay);
  return okay ? qMax(0.0, seconds) : 0.0;
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

  const auto cache_path =
      thumbnail_cache_path_for_key(
          photo_thumbnail_cache_key(
              file_path,
              adjustment_payload));
  const QFileInfo cache_info(cache_path);

  if (cache_info.exists() && cache_info.lastModified() >= source_info.lastModified()) {
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

  QImageWriter writer(cache_path, "jpg");
  writer.setQuality(90);
  writer.setOptimizedWrite(true);
  if (!writer.write(image)) {
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

  const auto cache_path = thumbnail_cache_path_for_key(QStringLiteral("video-v2:%1").arg(file_path));
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

QStringList collect_importable_file_paths(const QString& root_path) {
  QStringList file_paths;
  const auto normalized_root = normalize_local_path(root_path);
  if (normalized_root.isEmpty()) {
    return file_paths;
  }

  QDirIterator iterator(
      normalized_root,
      QDir::Files | QDir::NoDotAndDotDot,
      QDirIterator::Subdirectories);

  while (iterator.hasNext()) {
    const auto file_path = normalize_local_path(iterator.next());
    if (!has_supported_import_extension(file_path)) {
      continue;
    }
    file_paths.push_back(file_path);
  }

  return file_paths;
}

QStringList collect_import_candidate_paths(const QString& root_path) {
  QStringList file_paths;
  const auto normalized_root = normalize_local_path(root_path);
  if (normalized_root.isEmpty()) {
    return file_paths;
  }

  QDirIterator iterator(
      normalized_root,
      QDir::Files | QDir::NoDotAndDotDot,
      QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    file_paths.push_back(normalize_local_path(iterator.next()));
  }
  return file_paths;
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

  if (item->media_type == QStringLiteral("video") || item->media_type == QStringLiteral("audio")) {
    item->duration_seconds = probe_video_duration_seconds(normalized_path);
    item->duration_text = format_duration_text(item->duration_seconds);
  } else {
    item->duration_seconds = 0.0;
    item->duration_text.clear();
  }
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

QVector<CatalogAssetItem> parse_assets(const QJsonArray& asset_array) {
  QVector<CatalogAssetItem> items;
  items.reserve(asset_array.size());

  for (const auto& asset_value : asset_array) {
    const auto asset = asset_value.toObject();
    const auto location = asset.value("location").toObject();

    CatalogAssetItem item;
    item.asset_id = asset.value("asset_id").toString();
    item.capture_date = format_capture_date(asset, &item.capture_date_value);
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
      item.duration_seconds = probe_video_duration_seconds(item.file_path);
      item.duration_text = format_duration_text(item.duration_seconds);
      if (item.lens_model.isEmpty()) {
        item.lens_model = QStringLiteral("Video clip");
      }
    } else if (item.media_type == QStringLiteral("audio")) {
      item.camera_label = QFileInfo(item.file_path).completeBaseName();
      item.duration_seconds = probe_video_duration_seconds(item.file_path);
      item.duration_text = format_duration_text(item.duration_seconds);
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
            QDir(AppBootstrap::appDataLocation())
                .filePath(
                    QStringLiteral(
                        "recipe-assets")));
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
    items.push_back(item);
  }

  return items;
}

}  // namespace

CatalogAssetModel::CatalogAssetModel(QObject* parent)
    : QAbstractListModel(parent),
      import_job_observation_started_at_(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)) {
  loadProjects();
  rebuildImportSources();

  snapshot_process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(
      &snapshot_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleSnapshotFinished);

  import_process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(
      &import_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleImportFinished);

  culling_persistence_process_.setProcessChannelMode(QProcess::MergedChannels);
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
  connect(
      &recipe_persistence_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleRecipePersistenceFinished);

  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &CatalogAssetModel::pollActiveImportJob);

  thumbnail_watchers_.reserve(kMaxConcurrentThumbnailTasks);
  for (int index = 0; index < kMaxConcurrentThumbnailTasks; ++index) {
    auto watcher = std::make_unique<QFutureWatcher<ThumbnailTaskResult>>(this);
    connect(watcher.get(), &QFutureWatcher<ThumbnailTaskResult>::finished, this, &CatalogAssetModel::handleThumbnailFinished);
    thumbnail_watchers_.push_back(std::move(watcher));
  }
  connect(&delete_watcher_, &QFutureWatcher<DeleteAssetsTaskResult>::finished, this, &CatalogAssetModel::handleDeleteFinished);
}

CatalogAssetModel::~CatalogAssetModel() {
  ++delete_request_generation_;
  if (delete_cancellation_ != nullptr) {
    delete_cancellation_->store(true, std::memory_order_relaxed);
  }
  if (projects_save_timer_.isActive()) {
    projects_save_timer_.stop();
    saveProjects();
  }
  pending_culling_persistence_.clear();
  pending_culling_persistence_order_.clear();
  if (culling_persistence_process_.state() != QProcess::NotRunning) {
    culling_persistence_process_.terminate();
    if (!culling_persistence_process_.waitForFinished(500)) {
      culling_persistence_process_.kill();
      culling_persistence_process_.waitForFinished(500);
    }
  }

  pending_adjustment_patch_persistence_.clear();
  pending_adjustment_patch_persistence_order_.clear();
  if (recipe_persistence_process_.state() != QProcess::NotRunning) {
    recipe_persistence_process_.terminate();
    if (!recipe_persistence_process_.waitForFinished(500)) {
      recipe_persistence_process_.kill();
      recipe_persistence_process_.waitForFinished(500);
    }
  }
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

bool CatalogAssetModel::busy() const { return loading_ || importing_ || deleting_; }

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

QVariantMap CatalogAssetModel::assetMap(const CatalogAssetItem& item) const {
  QVariantMap map;
  map.insert(QStringLiteral("assetId"), item.asset_id);
  map.insert(QStringLiteral("captureDate"), item.capture_date);
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
  map.insert(
      QStringLiteral("adjustmentPatchJson"),
      adjustmentPatchProjection(
          item.adjustment_patch_json));
  map.insert(QStringLiteral("driveId"), item.drive_id);
  map.insert(QStringLiteral("importMode"), item.import_mode);
  map.insert(QStringLiteral("importedAt"), item.imported_at);
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
  map.insert(QStringLiteral("duplicateGroupKey"), item.duplicate_group_key);
  map.insert(QStringLiteral("duplicateGroupSize"), item.duplicate_group_size);
  map.insert(QStringLiteral("duplicateGroupPosition"), item.duplicate_group_position);
  map.insert(QStringLiteral("bestShotSuggested"), item.best_shot_suggested);
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

QString CatalogAssetModel::captureYearFilter() const { return capture_year_filter_; }

QString CatalogAssetModel::cameraFilter() const { return camera_filter_; }

QString CatalogAssetModel::captureDateFromFilter() const { return capture_date_from_filter_; }

QString CatalogAssetModel::captureDateToFilter() const { return capture_date_to_filter_; }

QString CatalogAssetModel::mediaTypeFilter() const { return media_type_filter_; }

QString CatalogAssetModel::reviewFilter() const { return review_filter_; }

QString CatalogAssetModel::availabilityFilter() const { return availability_filter_; }

int CatalogAssetModel::minimumRatingFilter() const { return minimum_rating_filter_; }
QString CatalogAssetModel::searchFilter() const { return search_filter_; }

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

    auto map = assetMap(assets_.at(index));
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
  if (snapshot_process_.state() != QProcess::NotRunning) {
    snapshot_process_.kill();
    snapshot_process_.waitForFinished(1000);
  }

  setLoading(true);
  setErrorMessage({});
  setStatus(QStringLiteral("Loading catalog snapshot"));

  snapshot_process_.setWorkingDirectory(QDir::currentPath());
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    setLoading(false);
    setErrorMessage(environment_error);
    setStatus(QStringLiteral("Catalog unavailable"));
    return;
  }
  snapshot_process_.setProcessEnvironment(environment);
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

QVariantMap CatalogAssetModel::importPreflight(
    const QString& source_root_path,
    const QStringList& file_paths,
    const QString& import_mode,
    const QString& duplicate_policy,
    const QString& organization_template,
    const QString& raw_jpeg_policy) const {
  const auto normalized_root = normalize_local_path(source_root_path);
  const auto normalized_mode = import_mode.trimmed().compare(QStringLiteral("referenced"), Qt::CaseInsensitive) == 0
                                   ? QStringLiteral("referenced")
                                   : QStringLiteral("managed");
  const auto normalized_duplicate_policy =
      duplicate_policy.trimmed().compare(QStringLiteral("replace"), Qt::CaseInsensitive) == 0
          ? QStringLiteral("replace")
          : QStringLiteral("skip");
  const auto normalized_organization_template =
      normalized_mode == QStringLiteral("managed") &&
              organization_template.trimmed().compare(QStringLiteral("date-camera"), Qt::CaseInsensitive) == 0
          ? QStringLiteral("date-camera")
          : QStringLiteral("source");
  const auto requested_raw_jpeg_policy = raw_jpeg_policy.trimmed().toLower();
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
  if (normalized_root.isEmpty() || !QFileInfo::exists(normalized_root)) {
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

  const auto candidate_paths =
      file_paths.isEmpty() ? collect_import_candidate_paths(normalized_root) : file_paths;
  const auto normalized_root_prefix = normalized_root.endsWith(QDir::separator())
                                          ? normalized_root
                                          : normalized_root + QDir::separator();
  QSet<QString> catalog_paths;
  for (const auto& asset : all_assets_) {
    if (!asset.file_path.trimmed().isEmpty()) {
      catalog_paths.insert(normalize_local_path(asset.file_path));
    }
  }

  QSet<QString> seen_paths;
  QSet<QString> raw_pair_keys;
  QSet<QString> jpeg_pair_keys;
  for (const auto& candidate_path : candidate_paths) {
    const auto normalized_path = normalize_local_path(candidate_path);
    const QFileInfo info(normalized_path);
    if (!info.isFile() || !has_supported_import_extension(normalized_path)) {
      continue;
    }
    const auto pair_key = QDir::cleanPath(info.absolutePath()) + QLatin1Char('/') +
                          info.completeBaseName().toLower();
    if (is_raw_import_path(normalized_path)) {
      raw_pair_keys.insert(pair_key);
    } else if (is_jpeg_import_path(normalized_path)) {
      jpeg_pair_keys.insert(pair_key);
    }
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
  const auto photo_library_root = AppBootstrap::photoLibraryLocation();
  const auto managed_drive_root = QDir(photo_library_root)
                                      .filePath(derive_drive_id(normalized_root));
  const auto originals_backup_root = AppBootstrap::originalsBackupLocation();
  const auto originals_backup_drive_root = originals_backup_root.isEmpty()
                                               ? QString{}
                                               : QDir(originals_backup_root)
                                                     .filePath(derive_drive_id(normalized_root));

  for (const auto& candidate_path : candidate_paths) {
    const auto normalized_path = normalize_local_path(candidate_path);
    const auto within_root =
        normalized_path == normalized_root || normalized_path.startsWith(normalized_root_prefix);
    const QFileInfo info(normalized_path);
    if (normalized_path.isEmpty() || !within_root || !info.isFile()) {
      ++unreadable_count;
      continue;
    }
    if (seen_paths.contains(normalized_path)) {
      ++duplicate_count;
      continue;
    }
    seen_paths.insert(normalized_path);

    if (!has_supported_import_extension(normalized_path)) {
      ++unsupported_count;
      continue;
    }

    QFile readable_file(normalized_path);
    if (!info.isReadable() || !readable_file.open(QIODevice::ReadOnly)) {
      ++unreadable_count;
      continue;
    }
    readable_file.close();

    const auto file_bytes = qMax<qint64>(0, info.size());
    selected_bytes += file_bytes;
    if (is_video_import_path(normalized_path)) {
      ++video_count;
    } else if (is_audio_import_path(normalized_path)) {
      ++audio_count;
    } else {
      ++photo_count;
    }

    const auto pair_key = QDir::cleanPath(info.absolutePath()) + QLatin1Char('/') +
                          info.completeBaseName().toLower();
    const auto raw_path = is_raw_import_path(normalized_path);
    const auto jpeg_path = is_jpeg_import_path(normalized_path);
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
  result.insert(QStringLiteral("candidateCount"), candidate_paths.size());
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

  const auto normalized_root = normalize_local_path(source_root_path);
  if (normalized_root.isEmpty()) {
    return;
  }

  const auto preflight = importPreflight(
      normalized_root,
      file_paths,
      import_mode,
      duplicate_policy,
      organization_template,
      raw_jpeg_policy);
  if (!preflight.value(QStringLiteral("ok")).toBool()) {
    setErrorMessage(QStringLiteral("Import preflight found no files that can be imported."));
    setStatus(QStringLiteral("Import needs attention"));
    return;
  }

  startImport(
      normalized_root,
      preflight.value(QStringLiteral("eligiblePaths")).toStringList(),
      project_name,
      preflight.value(QStringLiteral("importMode")).toString(),
      preflight.value(QStringLiteral("duplicatePolicy")).toString(),
      preflight);
}

void CatalogAssetModel::startImport(
    const QString& folder_path,
    const QStringList& selected_file_paths,
    const QString& project_name,
    const QString& import_mode,
    const QString& duplicate_policy,
    const QVariantMap& reviewed_preflight) {
  if (busy()) {
    return;
  }

  const auto normalized_path = normalize_local_path(folder_path);
  if (normalized_path.isEmpty() || !QFileInfo(normalized_path).exists()) {
    setErrorMessage(QStringLiteral("The selected import source is unavailable."));
    setStatus(QStringLiteral("Import failed"));
    return;
  }

  const auto import_preflight = reviewed_preflight.isEmpty()
                                    ? importPreflight(
                                          normalized_path,
                                          selected_file_paths,
                                          import_mode,
                                          duplicate_policy)
                                    : reviewed_preflight;
  if (!import_preflight.value(QStringLiteral("ok")).toBool()) {
    setErrorMessage(QStringLiteral("Import preflight found no files that can be imported."));
    setStatus(QStringLiteral("Import needs attention"));
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
      setStatus(QStringLiteral("Import failed"));
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
      normalized_project_name.isEmpty() &&
          import_preset_adjustment_patch_json_.isEmpty()
          ? QStringList{}
          : (selected_file_paths.isEmpty() ? collect_importable_file_paths(normalized_path) : selected_file_paths);
  beginImportJob(
      normalized_path,
      normalized_project_name,
      selected_file_paths,
      static_cast<int>(selected_file_paths.size()),
      import_mode,
      QJsonObject::fromVariantMap(import_preflight));

  setImporting(true);
  setErrorMessage({});
  setStatus(selected_file_paths.isEmpty()
                ? QStringLiteral("Queued source import")
                : QStringLiteral("Queued selected photo import"));
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

QString CatalogAssetModel::localPathForUrl(const QString& url) const {
  const auto parsed = QUrl(url);
  if (!parsed.isValid()) {
    return url;
  }

  return parsed.isLocalFile() ? parsed.toLocalFile() : url;
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
  if (trimmed_name.isEmpty()) {
    return;
  }

  if (findProject(trimmed_name) != nullptr) {
    setStatus(QStringLiteral("Project '%1' already exists.").arg(trimmed_name));
    return;
  }

  QString error_message;
  const auto persisted_to_catalog =
      runCatalogStateCommand({QStringLiteral("create-project"), trimmed_name}, &error_message);

  ProjectRecord project;
  project.name = trimmed_name;
  project.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  projects_.push_back(project);
  if (!persisted_to_catalog) {
    saveProjects();
  }
  emit projectNamesChanged();
  bumpProjectStatsRevision();
  setProjectFilter(trimmed_name);
  setStatus(QStringLiteral("Created project '%1'.").arg(trimmed_name));
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
  const auto trimmed_name = normalizedProjectName(project_name);
  if (asset_id.isEmpty() || trimmed_name.isEmpty()) {
    return;
  }

  if (assetInProject(asset_id, trimmed_name)) {
    setStatus(QStringLiteral("Asset is already in '%1'.").arg(trimmed_name));
    return;
  }

  addAssetsToProject({asset_id}, trimmed_name);
  setStatus(QStringLiteral("Added photo to '%1'.").arg(trimmed_name));
}

void CatalogAssetModel::addSelectedAssetsToProject(const QString& project_name) {
  const auto trimmed_name = normalizedProjectName(project_name);
  if (trimmed_name.isEmpty()) {
    return;
  }

  QStringList asset_ids_to_add;
  const auto selected_ids = selectedAssetIds();
  asset_ids_to_add.reserve(selected_ids.size());
  for (const auto& asset_id : selected_ids) {
    if (!asset_id.isEmpty() && !assetInProject(asset_id, trimmed_name)) {
      asset_ids_to_add.push_back(asset_id);
    }
  }

  if (asset_ids_to_add.isEmpty()) {
    setStatus(QStringLiteral("Selection is already in '%1'.").arg(trimmed_name));
    return;
  }

  addAssetsToProject(asset_ids_to_add, trimmed_name);
  setStatus(QStringLiteral("Added %1 %2 to '%3'.")
                .arg(asset_ids_to_add.size())
                .arg(asset_ids_to_add.size() == 1 ? QStringLiteral("asset") : QStringLiteral("assets"))
                .arg(trimmed_name));
}

void CatalogAssetModel::removeAssetFromProject(const QString& asset_id, const QString& project_name) {
  const auto trimmed_name = normalizedProjectName(project_name);
  if (asset_id.isEmpty() || trimmed_name.isEmpty()) {
    return;
  }

  auto* project = findProject(trimmed_name);
  if (project == nullptr || !project->asset_ids.contains(asset_id)) {
    return;
  }

  QString error_message;
  const auto persisted_to_catalog = runCatalogStateCommand(
      {QStringLiteral("remove-asset-from-project"), asset_id, trimmed_name},
      &error_message);

  project->asset_ids.remove(asset_id);
  if (!persisted_to_catalog) {
    saveProjects();
  }
  rebuildProjectMetadata();
  bumpProjectStatsRevision();
  applyFilters(asset_id);
  setStatus(QStringLiteral("Removed photo from '%1'.").arg(trimmed_name));
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
    bool clipboard_paste) {
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
          next_adjustment_patch_json);
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
  queueThumbnailRefreshes();
}

void CatalogAssetModel::replaceAssetsForTesting(const QVector<CatalogAssetItem>& items) {
  updateAssets(items);
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
  updateAssets(base_assets);
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
  setLoading(false);
  rebuildImportSources();

  if (exit_status != QProcess::NormalExit || exit_code != 0) {
    pending_import_file_paths_.clear();
    pending_import_project_name_.clear();
    setErrorMessage(QString::fromUtf8(snapshot_process_.readAllStandardOutput()).trimmed());
    setStatus(QStringLiteral("Catalog unavailable"));
    updateAssets({});
    setDriveCount(0);
    return;
  }

  const auto payload = QJsonDocument::fromJson(snapshot_process_.readAllStandardOutput());
  if (!payload.isObject()) {
    pending_import_file_paths_.clear();
    pending_import_project_name_.clear();
    setErrorMessage(QStringLiteral("Catalog snapshot output was not valid JSON."));
    setStatus(QStringLiteral("Catalog unavailable"));
    updateAssets({});
    setDriveCount(0);
    return;
  }

  const auto root = payload.object();
  loadPersistedLibraryState(root);
  updateAssets(parse_assets(root.value("assets").toArray()));
  setDriveCount(static_cast<int>(root.value("drives").toArray().size()));
  setStatus(QStringLiteral("Loaded %1 assets across %2 drives.").arg(all_assets_.size()).arg(drive_count_));
  finalizePendingImportProjectAssignment();
}

void CatalogAssetModel::handleImportFinished(int exit_code, QProcess::ExitStatus exit_status) {
  Q_UNUSED(exit_code);
  Q_UNUSED(exit_status);
}

void CatalogAssetModel::pollActiveImportJob() {
  if (active_import_job_.job_id.isEmpty()) {
    const auto records = loadPersistedJobRecords();
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

  const auto records = loadPersistedJobRecords();
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
    active_import_job_ = PersistentJobRecord{};
    setStatus(QStringLiteral("Import failed"));
    return;
  }

  if (job.status != QStringLiteral("Completed")) {
    return;
  }

  active_import_job_ = PersistentJobRecord{};
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

void CatalogAssetModel::beginImportJob(
    const QString& folder_path,
    const QString& project_name,
    const QStringList& selected_file_paths,
    int selection_count,
    const QString& import_mode,
    const QJsonObject& import_preflight) {
  active_import_job_ = PersistentJobRecord{};
  active_import_job_.job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
  (void)upsertPersistedJobRecord(active_import_job_, nullptr);
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

void CatalogAssetModel::loadPersistedLibraryState(const QJsonObject& root) {
  const auto has_projects = root.contains(QStringLiteral("projects"));
  const auto has_review_states = root.contains(QStringLiteral("review_states"));
  const auto has_relinked_paths = root.contains(QStringLiteral("relinked_paths"));
  if (!has_projects && !has_review_states && !has_relinked_paths) {
    return;
  }

  projects_.clear();
  culling_states_.clear();
  relinked_file_paths_.clear();

  const auto project_array = root.value(QStringLiteral("projects")).toArray();
  for (const auto& project_value : project_array) {
    if (!project_value.isObject()) {
      continue;
    }

    const auto project_object = project_value.toObject();
    ProjectRecord project;
    project.name = normalizedProjectName(project_object.value(QStringLiteral("name")).toString());
    project.created_at = project_object.value(QStringLiteral("created_at")).toString().trimmed();
    if (project.created_at.isEmpty()) {
      project.created_at = project_object.value(QStringLiteral("createdAt")).toString().trimmed();
    }
    const auto asset_ids = project_object.value(QStringLiteral("asset_ids")).toArray();
    for (const auto& asset_id_value : asset_ids) {
      const auto asset_id = asset_id_value.toString().trimmed();
      if (!asset_id.isEmpty()) {
        project.asset_ids.insert(asset_id);
      }
    }

    if (!project.name.isEmpty()) {
      projects_.push_back(project);
    }
  }

  const auto culling_object = root.value(QStringLiteral("review_states")).toObject();
  for (auto iterator = culling_object.begin(); iterator != culling_object.end(); ++iterator) {
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
      culling_states_.insert(asset_id, state);
    }
  }

  const auto relinked_paths_object = root.value(QStringLiteral("relinked_paths")).toObject();
  for (auto iterator = relinked_paths_object.begin(); iterator != relinked_paths_object.end(); ++iterator) {
    const auto asset_id = iterator.key().trimmed();
    const auto file_path = normalize_local_path(iterator.value().toString());
    if (asset_id.isEmpty() || file_path.isEmpty()) {
      continue;
    }
    relinked_file_paths_.insert(asset_id, file_path);
  }
}

void CatalogAssetModel::updateAssets(const QVector<CatalogAssetItem>& items) {
  emit searchInputsChanged({});
  const auto preferred_asset_id = selectedAssetId();
  const auto previous_total_count = all_assets_.size();

  all_assets_ = items;
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
    // Keep the last known preview available while an original is offline. Online
    // assets can safely regenerate against the current source and recipe.
    if (item.online || item.thumbnail_source.trimmed().isEmpty()) {
      clear_thumbnail_source_for_item(&item);
    }
  }
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
        const auto thumbnail_source =
            thumbnail_source_for_path(
                file_path,
                adjustment_patch_json,
                cancellation_requested.get());
        const auto cache_stats = sample_thumbnail_cache_stats();
        return ThumbnailTaskResult{
            .asset_id = asset_id,
            .file_path = file_path,
            .adjustment_patch_json =
                adjustment_patch_json,
            .thumbnail_source = thumbnail_source,
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
      result.thumbnail_source.isEmpty()) {
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
  if (!applied && !result.thumbnail_source.isEmpty()) {
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
  }

  const auto previous_count = assets_.size();
  beginResetModel();
  assets_ = filtered_assets;
  rebuildVisibleAssetIndexMap();
  endResetModel();

  if (previous_count != assets_.size()) {
    emit assetCountChanged();
  }

  reconcileSelection(preferred_asset_id);
  refreshVisibleCullingStats();
  queueThumbnailRefreshes();
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
  for (auto& item : all_assets_) {
    QStringList names;
    for (const auto& project : projects_) {
      if (project.asset_ids.contains(item.asset_id)) {
        names.push_back(project.name);
      }
    }

    std::sort(
        names.begin(),
        names.end(),
        [](const QString& left, const QString& right) {
          return left.localeAwareCompare(right) < 0;
        });

    item.project_names = names;
    item.project_summary = project_summary_for_names(names);
  }
}

void CatalogAssetModel::rebuildCaptureYears() {
  QStringList years;
  QStringList camera_labels;
  for (const auto& item : all_assets_) {
    if (item.media_type == QStringLiteral("timeline")) {
      continue;
    }
    const auto camera_label = item.camera_label.trimmed();
    if (!camera_label.isEmpty() && !camera_labels.contains(camera_label, Qt::CaseInsensitive)) {
      camera_labels.push_back(camera_label);
    }
    if (item.capture_date_value.isValid()) {
      const auto year = QString::number(item.capture_date_value.year());
      if (!years.contains(year)) {
        years.push_back(year);
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
  auto index = 0;
  while (index < all_assets_.size()) {
    auto& item = all_assets_[index];
    const auto burst_key = burst_group_key_for_item(item);
    if (burst_key.isEmpty()) {
      item.burst_key.clear();
      item.burst_size = 1;
      item.burst_position = 1;
      ++index;
      continue;
    }

    auto group_end = index + 1;
    while (group_end < all_assets_.size() && burst_group_key_for_item(all_assets_[group_end]) == burst_key) {
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

  auto index = 0;
  while (index < all_assets_.size()) {
    const auto group_size = all_assets_.at(index).burst_size;
    if (group_size <= 1) {
      ++index;
      continue;
    }

    const auto suggested_index = index + ((group_size - 1) / 2);
    all_assets_[suggested_index].best_shot_suggested = true;
    index += group_size;
  }
}

void CatalogAssetModel::loadTimelineAssets() {
  const auto timeline_items = load_timeline_asset_items();
  for (const auto& item : timeline_items) {
    all_assets_.push_back(item);
  }
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
  const auto trimmed_name = normalizedProjectName(project_name);
  if (trimmed_name.isEmpty() || asset_ids.isEmpty()) {
    return;
  }

  auto persisted_to_catalog = true;
  QString error_message;
  if (findProject(trimmed_name) == nullptr &&
      !runCatalogStateCommand({QStringLiteral("create-project"), trimmed_name}, &error_message)) {
    persisted_to_catalog = false;
  }
  for (const auto& asset_id : asset_ids) {
    if (asset_id.isEmpty()) {
      continue;
    }
    if (!runCatalogStateCommand(
            {QStringLiteral("add-asset-to-project"), asset_id, trimmed_name},
            &error_message)) {
      persisted_to_catalog = false;
    }
  }

  auto* project = findProject(trimmed_name);
  auto created_project = false;
  if (project == nullptr) {
    ProjectRecord new_project;
    new_project.name = trimmed_name;
    new_project.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    projects_.push_back(new_project);
    project = &projects_.last();
    created_project = true;
  }

  auto added_any = false;
  for (const auto& asset_id : asset_ids) {
    if (asset_id.isEmpty() || project->asset_ids.contains(asset_id)) {
      continue;
    }

    project->asset_ids.insert(asset_id);
    added_any = true;
  }

  if (!created_project && !added_any) {
    return;
  }

  if (!persisted_to_catalog) {
    saveProjects();
  }
  if (created_project) {
    emit projectNamesChanged();
  }
  rebuildProjectMetadata();
  bumpProjectStatsRevision();
  applyFilters(selectedAssetId());
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

  if (review_filter_ != QStringLiteral("all") || minimum_rating_filter_ > 0) {
    applyFilters(asset_id);
    return;
  }

  refreshVisibleCullingStats();

  if (selectedAssetId() == asset_id) {
    emit selectedAssetChanged();
  }
}

void CatalogAssetModel::queueCullingPersistence(const QString& asset_id, const CullingStateRecord& state) {
  if (asset_id.isEmpty()) {
    return;
  }

  if (!pending_culling_persistence_.contains(asset_id)) {
    pending_culling_persistence_order_.push_back(asset_id);
  }
  pending_culling_persistence_.insert(asset_id, normalized_culling_state(state));
  startNextCullingPersistence();
}

void CatalogAssetModel::startNextCullingPersistence() {
  if (culling_persistence_process_.state() != QProcess::NotRunning) {
    return;
  }

  while (!pending_culling_persistence_order_.isEmpty()) {
    const auto asset_id = pending_culling_persistence_order_.takeFirst();
    const auto state_iterator = pending_culling_persistence_.find(asset_id);
    if (state_iterator == pending_culling_persistence_.end()) {
      continue;
    }
    const auto state = state_iterator.value();
    pending_culling_persistence_.erase(state_iterator);

    QProcessEnvironment environment;
    QString environment_error;
    if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
      continue;
    }

    culling_persistence_process_.setWorkingDirectory(QDir::currentPath());
    culling_persistence_process_.setProcessEnvironment(environment);
    culling_persistence_process_.start(
        stateProgram(),
        stateArguments(
            {
                QStringLiteral("set-review-state"),
                asset_id,
                QString::number(state.rating),
                state.picked ? QStringLiteral("true") : QStringLiteral("false"),
                state.rejected ? QStringLiteral("true") : QStringLiteral("false"),
                state.color_label,
            }));
    return;
  }
}

void CatalogAssetModel::handleCullingPersistenceFinished(
    int exit_code,
    QProcess::ExitStatus exit_status) {
  Q_UNUSED(exit_code)
  Q_UNUSED(exit_status)
  startNextCullingPersistence();
}

void CatalogAssetModel::
    queueAdjustmentPatchPersistence(
        const QString& asset_id,
        const QString&
            adjustment_patch_json) {
  if (asset_id.isEmpty()) {
    return;
  }
  if (!pending_adjustment_patch_persistence_
           .contains(asset_id)) {
    pending_adjustment_patch_persistence_order_
        .push_back(asset_id);
  }
  pending_adjustment_patch_persistence_.insert(
      asset_id,
      adjustment_patch_json);
  startNextAdjustmentPatchPersistence();
}

void CatalogAssetModel::
    startNextAdjustmentPatchPersistence() {
  if (recipe_persistence_process_.state() != QProcess::NotRunning) {
    return;
  }

  while (
      !pending_adjustment_patch_persistence_order_
           .isEmpty()) {
    const auto asset_id =
        pending_adjustment_patch_persistence_order_
            .takeFirst();
    const auto adjustment_iterator =
        pending_adjustment_patch_persistence_.find(
            asset_id);
    if (adjustment_iterator ==
        pending_adjustment_patch_persistence_.end()) {
      continue;
    }
    const auto adjustment_patch_json =
        adjustment_iterator.value();
    pending_adjustment_patch_persistence_.erase(
        adjustment_iterator);
    const auto asset_index =
        all_asset_index_by_id_.value(asset_id, -1);
    if (asset_index < 0 ||
        asset_index >= all_assets_.size()) {
      continue;
    }
    const auto& item = all_assets_.at(asset_index);
    const auto decoded_adjustments =
        parse_adjustment_payload_or_legacy_recipe(
            adjustment_patch_json);
    const auto state =
        decoded_adjustments.ok()
            ? *decoded_adjustments.state
            : EditRecipeState{};
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
      setStatus(persistence.error);
      continue;
    }

    QProcessEnvironment environment;
    QString environment_error;
    if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
      continue;
    }

    recipe_persistence_process_.setWorkingDirectory(QDir::currentPath());
    recipe_persistence_process_.setProcessEnvironment(environment);
    recipe_persistence_process_.start(
        AppBootstrap::helperProgram(
            QStringLiteral("save_edit_recipe")),
        AppBootstrap::helperArguments(
            QStringLiteral("save_edit_recipe"),
            {
                asset_id,
                persistence.legacy_recipe_json,
                persistence.canonical_document.recipe_payload,
                persistence.canonical_document.cache_key,
                persistence.canonical_document.provenance_payload,
            }));
    return;
  }
}

void CatalogAssetModel::handleRecipePersistenceFinished(
    int exit_code,
    QProcess::ExitStatus exit_status) {
  Q_UNUSED(exit_code)
  Q_UNUSED(exit_status)
  startNextAdjustmentPatchPersistence();
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
  if (media_type_filter_ == QStringLiteral("all")) {
    return true;
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

}  // namespace photo_editor::desktop
