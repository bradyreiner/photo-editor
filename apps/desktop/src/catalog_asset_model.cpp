#include "catalog_asset_model.h"

#include "app_bootstrap.h"
#include "edit_recipe_state.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "timeline_asset_store.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QCoreApplication>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
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
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QUrl>
#include <QVariantMap>

namespace photo_editor::desktop {

namespace {

constexpr int kThumbnailEdge = 768;
constexpr int kThumbnailDecodeCeiling = 1280;

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

QString photo_thumbnail_cache_key(const QString& file_path, const QString& recipe_json) {
  return QStringLiteral("photo-v2:%1:%2").arg(file_path, recipe_json.trimmed());
}

QString cached_photo_thumbnail(const QString& file_path, const QString& recipe_json) {
  if (!preview_supported(file_path)) {
    return {};
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto cache_path = thumbnail_cache_path_for_key(photo_thumbnail_cache_key(file_path, recipe_json));
  const QFileInfo cache_info(cache_path);
  if (!cache_info.exists() || cache_info.lastModified() < source_info.lastModified()) {
    return {};
  }

  return QUrl::fromLocalFile(cache_path).toString();
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

  return QUrl::fromLocalFile(cache_path).toString();
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

QString ensure_thumbnail(const QString& file_path, const QString& recipe_json) {
  if (!preview_supported(file_path)) {
    return {};
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto cache_path = thumbnail_cache_path_for_key(photo_thumbnail_cache_key(file_path, recipe_json));
  const QFileInfo cache_info(cache_path);

  if (cache_info.exists() && cache_info.lastModified() >= source_info.lastModified()) {
    return QUrl::fromLocalFile(cache_path).toString();
  }

  QDir().mkpath(thumbnail_cache_root());

  const auto state = parse_edit_recipe_json(recipe_json);
  const auto decode_edge = std::min(kThumbnailDecodeCeiling, kThumbnailEdge + 256);
  auto image = QImage{};
  if (!decode_source_image(file_path, &image, decode_edge, state)) {
    return {};
  }

  if (image.isNull()) {
    return {};
  }

  image = apply_crop(image, state);
  apply_color_adjustments(&image, state);
  if (image.isNull()) {
    return {};
  }

  const auto target_size = scaled_size_for_edge(image.size(), kThumbnailEdge);

  if (target_size.isValid() &&
      (image.width() > target_size.width() || image.height() > target_size.height())) {
    image = image.scaled(target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }

  QImageWriter writer(cache_path, "jpg");
  writer.setQuality(90);
  writer.setOptimizedWrite(true);
  if (!writer.write(image)) {
    return {};
  }

  return QUrl::fromLocalFile(cache_path).toString();
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
    return QUrl::fromLocalFile(cache_path).toString();
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

  return QFileInfo::exists(cache_path) ? QUrl::fromLocalFile(cache_path).toString() : QString{};
}

QString thumbnail_source_for_path(const QString& file_path, const QString& recipe_json = {}) {
  const auto media_type = media_type_for_path(file_path);
  if (media_type == QStringLiteral("video")) {
    return ensure_video_poster(file_path);
  }
  if (media_type == QStringLiteral("audio")) {
    return {};
  }
  return ensure_thumbnail(file_path, recipe_json);
}

QString cached_thumbnail_source_for_path(const QString& file_path, const QString& recipe_json = {}) {
  const auto media_type = media_type_for_path(file_path);
  if (media_type == QStringLiteral("video")) {
    return cached_video_poster(file_path);
  }
  if (media_type == QStringLiteral("audio")) {
    return {};
  }
  return cached_photo_thumbnail(file_path, recipe_json);
}

void refresh_thumbnail_source_for_item(CatalogAssetItem* item) {
  if (item == nullptr || item->media_type == QStringLiteral("timeline")) {
    return;
  }

  item->thumbnail_source = cached_thumbnail_source_for_path(item->file_path, item->recipe_json);
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
      item.thumbnail_source = QUrl::fromLocalFile(record.poster_path).toString();
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
  if (state.rejected) {
    state.picked = false;
  }
  return state;
}

bool culling_state_is_default(const CullingStateRecord& state) {
  return state.rating == 0 && !state.picked && !state.rejected;
}

void apply_culling_state_to_item(CatalogAssetItem* item, const CullingStateRecord& state) {
  if (item == nullptr) {
    return;
  }

  item->rating = state.rating;
  item->picked = state.picked;
  item->rejected = state.rejected;
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
  item->thumbnail_source = cached_thumbnail_source_for_path(normalized_path, item->recipe_json);

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
    next_online = source_info.exists() && source_info.isFile();
  }

  if (next_online && item->thumbnail_source.isEmpty()) {
    item->thumbnail_source = cached_thumbnail_source_for_path(item->file_path, item->recipe_json);
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
    const auto recipe_value = asset.value("edit_recipe");
    if (recipe_value.isObject()) {
      item.recipe_json = QString::fromUtf8(QJsonDocument(recipe_value.toObject()).toJson(QJsonDocument::Compact));
    }
    item.thumbnail_source = cached_thumbnail_source_for_path(item.file_path, item.recipe_json);
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

CatalogAssetModel::CatalogAssetModel(QObject* parent) : QAbstractListModel(parent) {
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

  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &CatalogAssetModel::pollActiveImportJob);

  connect(&thumbnail_watcher_, &QFutureWatcher<ThumbnailTaskResult>::finished, this, &CatalogAssetModel::handleThumbnailFinished);
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
    case RecipeJsonRole:
      return item.recipe_json;
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
    case TimelineDocumentPathRole:
      return item.timeline_document_path;
    case RatingRole:
      return item.rating;
    case PickedRole:
      return item.picked;
    case RejectedRole:
      return item.rejected;
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
      {RecipeJsonRole, "recipeJson"},
      {DriveIdRole, "driveId"},
      {ImportModeRole, "importMode"},
      {ImportedAtRole, "importedAt"},
      {ProjectNamesRole, "projectNames"},
      {ProjectSummaryRole, "projectSummary"},
      {MediaTypeRole, "mediaType"},
      {DurationTextRole, "durationText"},
      {TimelineDocumentPathRole, "timelineDocumentPath"},
      {RatingRole, "rating"},
      {PickedRole, "picked"},
      {RejectedRole, "rejected"},
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
  };
}

QString CatalogAssetModel::status() const { return status_; }

QString CatalogAssetModel::errorMessage() const { return error_message_; }

bool CatalogAssetModel::loading() const { return loading_; }

bool CatalogAssetModel::importing() const { return importing_; }

bool CatalogAssetModel::busy() const { return loading_ || importing_; }

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

QString CatalogAssetModel::selectedRecipeJson() const {
  if (const auto* item = selectedItem()) {
    return item->recipe_json;
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
  map.insert(QStringLiteral("recipeJson"), item.recipe_json);
  map.insert(QStringLiteral("driveId"), item.drive_id);
  map.insert(QStringLiteral("importMode"), item.import_mode);
  map.insert(QStringLiteral("importedAt"), item.imported_at);
  map.insert(QStringLiteral("projectNames"), item.project_names);
  map.insert(QStringLiteral("projectSummary"), item.project_summary);
  map.insert(QStringLiteral("mediaType"), item.media_type);
  map.insert(QStringLiteral("durationText"), item.duration_text);
  map.insert(QStringLiteral("timelineDocumentPath"), item.timeline_document_path);
  map.insert(QStringLiteral("rating"), item.rating);
  map.insert(QStringLiteral("picked"), item.picked);
  map.insert(QStringLiteral("rejected"), item.rejected);
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
  return map;
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

QString CatalogAssetModel::projectFilter() const { return project_filter_; }

QString CatalogAssetModel::importDateFilter() const { return import_date_filter_; }

QString CatalogAssetModel::captureYearFilter() const { return capture_year_filter_; }

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

QVariantMap CatalogAssetModel::assetAt(int index) const {
  if (index < 0 || index >= assets_.size()) {
    return {};
  }

  return assetMap(assets_.at(index));
}

bool CatalogAssetModel::assetSelected(const QString& asset_id) const {
  return selected_asset_ids_.contains(asset_id);
}

void CatalogAssetModel::refresh() {
  if (snapshot_process_.state() != QProcess::NotRunning) {
    snapshot_process_.kill();
    snapshot_process_.waitForFinished(1000);
  }

  setLoading(true);
  setErrorMessage({});
  setStatus(QStringLiteral("Loading catalog snapshot..."));

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

void CatalogAssetModel::refreshImportSources() { rebuildImportSources(); }

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

void CatalogAssetModel::importSelection(
    const QString& source_root_path,
    const QStringList& file_paths,
    const QString& project_name) {
  if (busy()) {
    return;
  }

  const auto normalized_root = normalize_local_path(source_root_path);
  if (normalized_root.isEmpty()) {
    return;
  }

  QStringList normalized_file_paths;
  QSet<QString> seen_paths;
  const auto normalized_root_prefix = normalized_root.endsWith(QDir::separator())
                                          ? normalized_root
                                          : normalized_root + QDir::separator();

  for (const auto& file_path : file_paths) {
    const auto normalized_file_path = normalize_local_path(file_path);
    if (normalized_file_path.isEmpty() || seen_paths.contains(normalized_file_path)) {
      continue;
    }

    const auto within_root =
        normalized_file_path == normalized_root || normalized_file_path.startsWith(normalized_root_prefix);
    if (!within_root || !QFileInfo(normalized_file_path).isFile() || !has_supported_import_extension(normalized_file_path)) {
      continue;
    }

    seen_paths.insert(normalized_file_path);
    normalized_file_paths.push_back(normalized_file_path);
  }

  startImport(normalized_root, normalized_file_paths, project_name);
}

void CatalogAssetModel::startImport(
    const QString& folder_path,
    const QStringList& selected_file_paths,
    const QString& project_name) {
  if (busy()) {
    return;
  }

  const auto normalized_path = normalize_local_path(folder_path);
  if (normalized_path.isEmpty() || !QFileInfo(normalized_path).exists()) {
    setErrorMessage(QStringLiteral("The selected import source is unavailable."));
    setStatus(QStringLiteral("Import failed"));
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
      normalized_project_name.isEmpty()
          ? QStringList{}
          : (selected_file_paths.isEmpty() ? collect_importable_file_paths(normalized_path) : selected_file_paths);
  beginImportJob(
      normalized_path,
      normalized_project_name,
      selected_file_paths,
      static_cast<int>(selected_file_paths.size()));

  setImporting(true);
  setErrorMessage({});
  setStatus(selected_file_paths.isEmpty()
                ? QStringLiteral("Queued source import...")
                : QStringLiteral("Queued selected photo import..."));
}

void CatalogAssetModel::selectIndex(int index) {
  setSelectedIndexInternal(index);
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

void CatalogAssetModel::setSelectedRating(int rating) {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  auto state = cullingStateForAsset(asset_id);
  state.rating = rating;
  state = normalized_culling_state(state);
  setAssetCullingState(asset_id, state);

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

  auto state = cullingStateForAsset(asset_id);
  state.picked = !state.picked;
  if (state.picked) {
    state.rejected = false;
  }
  state = normalized_culling_state(state);
  setAssetCullingState(asset_id, state);
  setStatus(state.picked ? QStringLiteral("Marked selection as picked.")
                         : QStringLiteral("Cleared picked state."));
}

void CatalogAssetModel::toggleSelectedReject() {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  auto state = cullingStateForAsset(asset_id);
  state.rejected = !state.rejected;
  if (state.rejected) {
    state.picked = false;
  }
  state = normalized_culling_state(state);
  setAssetCullingState(asset_id, state);
  setStatus(state.rejected ? QStringLiteral("Marked selection as rejected.")
                           : QStringLiteral("Cleared rejected state."));
}

void CatalogAssetModel::clearSelectedCullState() {
  const auto asset_id = selectedAssetId();
  if (asset_id.isEmpty()) {
    return;
  }

  setAssetCullingState(asset_id, {});
  setStatus(QStringLiteral("Cleared culling state for the selection."));
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
  const auto selected_ids = selectedAssetIds();
  if (selected_ids.isEmpty()) {
    return;
  }

  QString error_message;
  for (const auto& asset_id : selected_ids) {
    if (asset_id.isEmpty()) {
      continue;
    }
    if (!runCatalogStateCommand({QStringLiteral("delete-asset"), asset_id}, &error_message)) {
      setStatus(error_message.isEmpty() ? QStringLiteral("Unable to remove the selection from the gallery.")
                                        : error_message);
      return;
    }
  }

  QSet<QString> deleted_asset_ids;
  deleted_asset_ids.reserve(selected_ids.size());
  for (const auto& asset_id : selected_ids) {
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
    recipe_overrides_.remove(asset_id);
    relinked_file_paths_.remove(asset_id);
    queued_thumbnail_asset_ids_.remove(asset_id);
    pending_thumbnail_asset_ids_.removeAll(asset_id);
  }

  rebuildProjectMetadata();
  rebuildCaptureYears();
  rebuildBurstMetadata();
  rebuildIntelligenceMetadata();
  bumpProjectStatsRevision();
  saveProjects();
  applyFilters();

  if (previous_total_count != all_assets_.size()) {
    emit totalAssetCountChanged();
  }

  setStatus(QStringLiteral("Removed %1 %2 from the gallery.")
                .arg(deleted_asset_ids.size())
                .arg(deleted_asset_ids.size() == 1 ? QStringLiteral("asset") : QStringLiteral("assets")));
}

int CatalogAssetModel::applyRecipeJsonToAssetIds(
    const QStringList& asset_ids,
    const QString& recipe_json,
    const QStringList& adjustment_groups) {
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

  const auto source_state = parse_edit_recipe_json(recipe_json);
  QHash<QString, QString> updated_recipe_jsons;
  QHash<QString, QString> updated_thumbnail_sources;
  auto updated_count = 0;
  auto persisted_all = true;

  for (auto& item : all_assets_) {
    if (!target_asset_ids.contains(item.asset_id) || item.media_type != QStringLiteral("photo")) {
      continue;
    }

    const auto current_state = parse_edit_recipe_json(item.recipe_json);
    const auto merged_state = merge_edit_recipe_state_by_groups(current_state, source_state, adjustment_groups);
    const auto next_recipe_json = serialize_edit_recipe_json(merged_state);
    if (next_recipe_json == item.recipe_json) {
      continue;
    }

    item.recipe_json = next_recipe_json;
    item.thumbnail_source = cached_thumbnail_source_for_path(item.file_path, next_recipe_json);
    updated_recipe_jsons.insert(item.asset_id, next_recipe_json);
    updated_thumbnail_sources.insert(item.asset_id, item.thumbnail_source);
    QString error_message;
    if (!runCatalogStateCommand(
            {QStringLiteral("set-edit-recipe"), item.asset_id, next_recipe_json},
            &error_message)) {
      recipe_overrides_.insert(item.asset_id, next_recipe_json);
      persisted_all = false;
    }
    ++updated_count;
  }

  if (updated_count == 0) {
    return 0;
  }

  for (qsizetype index = 0; index < assets_.size(); ++index) {
    auto& item = assets_[index];
    const auto iterator = updated_recipe_jsons.constFind(item.asset_id);
    if (iterator == updated_recipe_jsons.cend()) {
      continue;
    }

    item.recipe_json = iterator.value();
    item.thumbnail_source = updated_thumbnail_sources.value(item.asset_id);
    const auto model_index = static_cast<int>(index);
    emit dataChanged(
        this->index(model_index, 0),
        this->index(model_index, 0),
        {RecipeJsonRole, ThumbnailSourceRole});
  }

  if (!persisted_all) {
    saveProjects();
  }

  if (target_asset_ids.contains(selectedAssetId())) {
    emit selectedAssetChanged();
  }

  queueThumbnailRefreshes();

  setStatus(
      QStringLiteral("Applied adjustments to %1 %2.")
          .arg(updated_count)
          .arg(updated_count == 1 ? QStringLiteral("photo") : QStringLiteral("photos")));
  return updated_count;
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

void CatalogAssetModel::replaceAssetsForTesting(const QVector<CatalogAssetItem>& items) {
  updateAssets(items);
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
      next_filter != QStringLiteral("rated")) {
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
    setStatus(QStringLiteral("Queued source import..."));
    return;
  }

  if (job.status == QStringLiteral("Running")) {
    setImporting(true);
    setStatus(QStringLiteral("Importing source into catalog..."));
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

void CatalogAssetModel::beginImportJob(
    const QString& folder_path,
    const QString& project_name,
    const QStringList& selected_file_paths,
    int selection_count) {
  active_import_job_ = PersistentJobRecord{};
  active_import_job_.job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  active_import_job_.job_kind = QStringLiteral("catalog_import");
  active_import_job_.display_label = QFileInfo(folder_path).fileName().trimmed().isEmpty()
                                         ? folder_path
                                         : QFileInfo(folder_path).fileName().trimmed();
  active_import_job_.source_path = folder_path;
  active_import_job_.preset_name = QStringLiteral("referenced");
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
      {QStringLiteral("importMode"), QStringLiteral("referenced")},
      {QStringLiteral("selectedPaths"), selected_paths},
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
  if (import_sources_ == next_sources) {
    return;
  }

  import_sources_ = next_sources;
  emit importSourcesChanged();
}

bool CatalogAssetModel::runCatalogStateCommand(
    const QStringList& command_arguments,
    QString* error_message) const {
  QProcess process;
  process.setWorkingDirectory(QDir::currentPath());
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    if (error_message != nullptr) {
      *error_message = environment_error;
    }
    return false;
  }

  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(stateProgram(), stateArguments(command_arguments));
  if (!process.waitForStarted(5000)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to start catalog state helper.");
    }
    return false;
  }

  if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_message != nullptr) {
      const auto output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
      *error_message = output.isEmpty() ? QStringLiteral("Catalog state update failed.") : output;
    }
    return false;
  }

  return true;
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
  const auto preferred_asset_id = selectedAssetId();
  const auto previous_total_count = all_assets_.size();

  all_assets_ = items;
  loadTimelineAssets();
  for (auto& item : all_assets_) {
    apply_culling_state_to_item(&item, cullingStateForAsset(item.asset_id));
    const auto recipe_override = recipe_overrides_.value(item.asset_id);
    if (!recipe_override.trimmed().isEmpty()) {
      item.recipe_json = recipe_override;
    }
    const auto relinked_path = relinked_file_paths_.value(item.asset_id).trimmed();
    if (!relinked_path.isEmpty()) {
      apply_relinked_path_to_item(&item, relinked_path);
      refresh_online_state_for_item(&item);
    }
    refresh_thumbnail_source_for_item(&item);
  }
  rebuildProjectMetadata();
  rebuildBurstMetadata();
  rebuildIntelligenceMetadata();
  bumpProjectStatsRevision();
  rebuildCaptureYears();

  if (previous_total_count != all_assets_.size()) {
    emit totalAssetCountChanged();
  }

  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::queueThumbnailRefreshes() {
  QStringList next_asset_ids;
  QSet<QString> seen_asset_ids;
  next_asset_ids.reserve(assets_.size() + all_assets_.size());

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
  for (const auto& item : assets_) {
    append_asset(item);
  }
  for (const auto& item : all_assets_) {
    append_asset(item);
  }

  QStringList reordered_queue;
  reordered_queue.reserve(next_asset_ids.size() + pending_thumbnail_asset_ids_.size());
  QSet<QString> queued_asset_ids = queued_thumbnail_asset_ids_;
  queued_asset_ids.insert(active_thumbnail_asset_id_);

  for (const auto& asset_id : next_asset_ids) {
    if (!queued_asset_ids.contains(asset_id)) {
      queued_asset_ids.insert(asset_id);
    }
    reordered_queue.push_back(asset_id);
  }

  for (const auto& asset_id : pending_thumbnail_asset_ids_) {
    if (!seen_asset_ids.contains(asset_id)) {
      reordered_queue.push_back(asset_id);
    }
  }

  pending_thumbnail_asset_ids_ = reordered_queue;
  queued_thumbnail_asset_ids_.clear();
  for (const auto& asset_id : pending_thumbnail_asset_ids_) {
    queued_thumbnail_asset_ids_.insert(asset_id);
  }

  startNextThumbnailTask();
}

void CatalogAssetModel::startNextThumbnailTask() {
  if (thumbnail_watcher_.isRunning()) {
    return;
  }

  while (!pending_thumbnail_asset_ids_.isEmpty()) {
    const auto asset_id = pending_thumbnail_asset_ids_.takeFirst();
    queued_thumbnail_asset_ids_.remove(asset_id);

    const auto iterator = std::find_if(
        all_assets_.cbegin(),
        all_assets_.cend(),
        [&asset_id](const CatalogAssetItem& item) { return item.asset_id == asset_id; });
    if (iterator == all_assets_.cend() || !iterator->thumbnail_source.isEmpty() || !iterator->online ||
        iterator->file_path.trimmed().isEmpty()) {
      continue;
    }

    active_thumbnail_asset_id_ = asset_id;
    const auto file_path = iterator->file_path;
    const auto recipe_json = iterator->recipe_json;
    thumbnail_watcher_.setFuture(QtConcurrent::run([asset_id, file_path, recipe_json]() {
      return ThumbnailTaskResult{
          .asset_id = asset_id,
          .file_path = file_path,
          .recipe_json = recipe_json,
          .thumbnail_source = thumbnail_source_for_path(file_path, recipe_json),
      };
    }));
    return;
  }

  active_thumbnail_asset_id_.clear();
}

void CatalogAssetModel::applyThumbnailResult(const ThumbnailTaskResult& result) {
  if (result.asset_id.isEmpty()) {
    return;
  }

  auto selected_changed = false;
  auto visible_index = -1;
  auto updated_any = false;
  for (auto& item : all_assets_) {
    if (item.asset_id != result.asset_id || item.file_path != result.file_path || item.recipe_json != result.recipe_json) {
      continue;
    }
    if (!result.thumbnail_source.isEmpty()) {
      item.thumbnail_source = result.thumbnail_source;
      updated_any = true;
    }
    break;
  }

  if (!updated_any) {
    return;
  }

  for (qsizetype index = 0; index < assets_.size(); ++index) {
    auto& item = assets_[index];
    if (item.asset_id != result.asset_id || item.file_path != result.file_path || item.recipe_json != result.recipe_json) {
      continue;
    }
    item.thumbnail_source = result.thumbnail_source;
    visible_index = static_cast<int>(index);
    if (selected_index_ == visible_index) {
      selected_changed = true;
    }
    break;
  }

  if (visible_index >= 0) {
    emit dataChanged(
        this->index(visible_index, 0),
        this->index(visible_index, 0),
        {ThumbnailSourceRole});
  }
  if (selected_changed) {
    emit selectedAssetChanged();
  }
}

void CatalogAssetModel::handleThumbnailFinished() {
  const auto result = thumbnail_watcher_.result();
  active_thumbnail_asset_id_.clear();
  applyThumbnailResult(result);
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
}

void CatalogAssetModel::applyFilters(const QString& preferred_asset_id) {
  QVector<CatalogAssetItem> filtered_assets;
  filtered_assets.reserve(all_assets_.size());

  for (const auto& item : all_assets_) {
    if (!matchesSearchFilter(item) || !matchesProjectFilter(item) || !matchesImportDateFilter(item) ||
        !matchesCaptureYearFilter(item) || !matchesMediaTypeFilter(item) || !matchesReviewFilter(item) ||
        !matchesAvailabilityFilter(item) ||
        !matchesMinimumRatingFilter(item)) {
      continue;
    }
    filtered_assets.push_back(item);
  }

  const auto previous_count = assets_.size();
  beginResetModel();
  assets_ = filtered_assets;
  endResetModel();

  if (previous_count != assets_.size()) {
    emit assetCountChanged();
  }

  reconcileSelection(preferred_asset_id);
  refreshVisibleCullingStats();
  queueThumbnailRefreshes();
}

void CatalogAssetModel::reconcileSelection(const QString& preferred_asset_id) {
  QSet<QString> visible_asset_ids;
  visible_asset_ids.reserve(assets_.size());
  for (const auto& item : assets_) {
    visible_asset_ids.insert(item.asset_id);
  }

  QSet<QString> next_selected_asset_ids;
  for (const auto& asset_id : selected_asset_ids_) {
    if (visible_asset_ids.contains(asset_id)) {
      next_selected_asset_ids.insert(asset_id);
    }
  }

  QString next_primary_asset_id;
  if (!preferred_asset_id.isEmpty() && visible_asset_ids.contains(preferred_asset_id)) {
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
    for (qsizetype index = 0; index < assets_.size(); ++index) {
      if (assets_.at(index).asset_id == next_primary_asset_id) {
        next_index = static_cast<int>(index);
        break;
      }
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
  for (const auto& item : all_assets_) {
    if (item.media_type == QStringLiteral("timeline")) {
      continue;
    }
    if (!item.capture_date_value.isValid()) {
      continue;
    }

    const auto year = QString::number(item.capture_date_value.year());
    if (!years.contains(year)) {
      years.push_back(year);
    }
  }

  std::sort(
      years.begin(),
      years.end(),
      [](const QString& left, const QString& right) { return left.toInt() > right.toInt(); });

  if (capture_years_ == years) {
    return;
  }

  capture_years_ = years;
  emit captureYearsChanged();
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
  recipe_overrides_.clear();
  relinked_file_paths_.clear();

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
        });
    if (!culling_state_is_default(state)) {
      culling_states_.insert(asset_id, state);
    }
  }

  const auto recipe_override_object = document.object().value(QStringLiteral("recipeOverrides")).toObject();
  for (auto iterator = recipe_override_object.begin(); iterator != recipe_override_object.end(); ++iterator) {
    const auto asset_id = iterator.key().trimmed();
    const auto recipe_json = iterator.value().toString().trimmed();
    if (asset_id.isEmpty() || recipe_json.isEmpty()) {
      continue;
    }
    recipe_overrides_.insert(asset_id, recipe_json);
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
    culling_object.insert(iterator.key(), item_object);
  }

  QJsonObject recipe_override_object;
  for (auto iterator = recipe_overrides_.cbegin(); iterator != recipe_overrides_.cend(); ++iterator) {
    if (iterator.key().trimmed().isEmpty() || iterator.value().trimmed().isEmpty()) {
      continue;
    }
    recipe_override_object.insert(iterator.key(), iterator.value());
  }

  QJsonObject relinked_paths_object;
  for (auto iterator = relinked_file_paths_.cbegin(); iterator != relinked_file_paths_.cend(); ++iterator) {
    const auto file_path = normalize_local_path(iterator.value());
    if (iterator.key().trimmed().isEmpty() || file_path.isEmpty()) {
      continue;
    }
    relinked_paths_object.insert(iterator.key(), file_path);
  }

  QJsonObject root;
  root.insert(QStringLiteral("projects"), project_array);
  root.insert(QStringLiteral("cullingStates"), culling_object);
  root.insert(QStringLiteral("recipeOverrides"), recipe_override_object);
  root.insert(QStringLiteral("relinkedPaths"), relinked_paths_object);

  QSaveFile file(projectsFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
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

  if (project_name.isEmpty() || pending_file_paths.isEmpty()) {
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
    setStatus(QStringLiteral("Import completed, but no assets were assigned to '%1'.").arg(project_name));
    return;
  }

  addAssetsToProject(imported_asset_ids, project_name);
  setStatus(QStringLiteral("Imported %1 assets into '%2'.").arg(imported_asset_ids.size()).arg(project_name));
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
  if (previous_state.rating == normalized_state.rating && previous_state.picked == normalized_state.picked &&
      previous_state.rejected == normalized_state.rejected) {
    return;
  }

  QString error_message;
  const auto persisted_to_catalog = runCatalogStateCommand(
      {
          QStringLiteral("set-review-state"),
          asset_id,
          QString::number(normalized_state.rating),
          normalized_state.picked ? QStringLiteral("true") : QStringLiteral("false"),
          normalized_state.rejected ? QStringLiteral("true") : QStringLiteral("false"),
      },
      &error_message);

  if (culling_state_is_default(normalized_state)) {
    culling_states_.remove(asset_id);
  } else {
    culling_states_.insert(asset_id, normalized_state);
  }

  for (auto& item : all_assets_) {
    if (item.asset_id == asset_id) {
      apply_culling_state_to_item(&item, normalized_state);
    }
  }

  int visible_index = -1;
  for (qsizetype index = 0; index < assets_.size(); ++index) {
    if (assets_.at(index).asset_id != asset_id) {
      continue;
    }

    apply_culling_state_to_item(&assets_[index], normalized_state);
    visible_index = static_cast<int>(index);
    break;
  }

  if (!persisted_to_catalog) {
    saveProjects();
  }

  if (visible_index >= 0) {
    emit dataChanged(
        this->index(visible_index, 0),
        this->index(visible_index, 0),
        {RatingRole, PickedRole, RejectedRole, CullStatusRole});
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
  if (useBuiltSnapshotBinary()) {
    return builtBinaryPath(QStringLiteral("catalog_snapshot"));
  }

  return QStringLiteral("cargo");
}

QStringList CatalogAssetModel::snapshotArguments() const {
  if (useBuiltSnapshotBinary()) {
    return {QStringLiteral("0")};
  }

  return {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("catalog_snapshot"),
      QStringLiteral("--"),
      QStringLiteral("0"),
  };
}

QString CatalogAssetModel::importProgram() const {
  if (useBuiltImportBinary()) {
    return builtBinaryPath(QStringLiteral("import_catalog"));
  }

  return QStringLiteral("cargo");
}

QStringList CatalogAssetModel::importArguments(const QString& folder_path) const {
  const auto drive_id = derive_drive_id(folder_path);
  const auto drive_name = derive_drive_name(folder_path);
  const auto import_mode = QStringLiteral("referenced");

  QStringList arguments;
  if (useBuiltImportBinary()) {
    arguments = {folder_path, drive_id, drive_name, import_mode};
  } else {
    arguments = {
        QStringLiteral("run"),
        QStringLiteral("--quiet"),
        QStringLiteral("--manifest-path"),
        QStringLiteral("rust/Cargo.toml"),
        QStringLiteral("-p"),
        QStringLiteral("photo-catalog"),
        QStringLiteral("--bin"),
        QStringLiteral("import_catalog"),
        QStringLiteral("--"),
        folder_path,
        drive_id,
        drive_name,
        import_mode,
    };
  }

  if (!import_selection_manifest_path_.isEmpty()) {
    arguments.push_back(import_selection_manifest_path_);
  }

  return arguments;
}

QString CatalogAssetModel::stateProgram() const {
  if (useBuiltStateBinary()) {
    return builtBinaryPath(QStringLiteral("catalog_state"));
  }

  return QStringLiteral("cargo");
}

QStringList CatalogAssetModel::stateArguments(const QStringList& command_arguments) const {
  if (useBuiltStateBinary()) {
    return command_arguments;
  }

  QStringList arguments = {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("catalog_state"),
      QStringLiteral("--"),
  };
  arguments.append(command_arguments);
  return arguments;
}

bool CatalogAssetModel::useBuiltSnapshotBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("catalog_snapshot")));
  return binary.exists() && binary.isExecutable();
}

bool CatalogAssetModel::useBuiltImportBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("import_catalog")));
  return binary.exists() && binary.isExecutable();
}

bool CatalogAssetModel::useBuiltStateBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("catalog_state")));
  return binary.exists() && binary.isExecutable();
}

QString CatalogAssetModel::builtBinaryPath(const QString& binary_name) const {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

}  // namespace photo_editor::desktop
