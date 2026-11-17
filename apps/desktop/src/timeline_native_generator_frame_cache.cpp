#include "timeline_native_generator_frame_cache.h"

#include "timeline_desktop_light_field.h"
#include "timeline_desktop_light_field_metal.h"
#include "timeline_vector_path.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainterPath>
#include <QPen>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <limits>
#include <utility>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace photo_editor::desktop::timeline_motion {
namespace {

constexpr qint64 kMaximumPixelsPerFrame = 64LL * 1024LL * 1024LL;
// PNG compression stays lossless. A lower level avoids spending most of the
// native source-frame path in zlib while keeping the cache's 2 GiB cap intact.
constexpr int kNativeGeneratorFramePngCompression = 11;

QMutex& frame_sequence_cache_mutex() {
  static QMutex mutex;
  return mutex;
}

QHash<QString, int>& pinned_frame_sequence_directories() {
  static QHash<QString, int> directories;
  return directories;
}

std::atomic_int& active_frame_sequence_work() {
  static std::atomic_int count{0};
  return count;
}

struct FrameSequenceActivity final {
  FrameSequenceActivity() {
    active_frame_sequence_work().fetch_add(1, std::memory_order_relaxed);
  }
  ~FrameSequenceActivity() {
    active_frame_sequence_work().fetch_sub(1, std::memory_order_relaxed);
  }
};

struct CacheFileStamp final {
  bool exists = false;
  qint64 size = -1;
  QDateTime modified;
  QDateTime metadata_changed;
  quint64 device = 0;
  quint64 inode = 0;
  qint64 modified_seconds = 0;
  qint64 modified_nanoseconds = 0;
  qint64 metadata_changed_seconds = 0;
  qint64 metadata_changed_nanoseconds = 0;
  bool high_resolution_identity = false;

  bool operator==(const CacheFileStamp&) const = default;
};

struct CacheEntry final {
  QString path;
  qint64 bytes = 0;
  QDateTime modified;
  CacheFileStamp manifest_stamp;
  QByteArray manifest_sha256;
};

struct CacheInventory final {
  QMap<QString, CacheEntry> entries;
  qint64 total_bytes = 0;
  CacheFileStamp root_stamp;
  bool initialized = false;
};

QHash<QString, CacheInventory>& cache_inventories() {
  static QHash<QString, CacheInventory> inventories;
  return inventories;
}

#ifdef PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS
std::atomic_int& cache_inventory_full_scan_count() {
  static std::atomic_int count{0};
  return count;
}

std::atomic_int& cache_inventory_manifest_refresh_count() {
  static std::atomic_int count{0};
  return count;
}

std::atomic_int& cache_inventory_manifest_unchanged_skip_count() {
  static std::atomic_int count{0};
  return count;
}
#endif

QString normalized_directory_key(const QString& directory) {
  return QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
}

bool cancelled(const std::atomic_bool* cancellation) {
  return cancellation != nullptr && cancellation->load(std::memory_order_relaxed);
}

bool cancellation_requested(const std::atomic_bool* cancellation,
                            const std::function<bool()>& check) {
  return cancelled(cancellation) || (check && check());
}

bool request_is_valid(const NativeGeneratorFrameSequenceRequest& request,
                      QString* error_message) {
  const auto kind = request.generator_kind.trimmed().toLower();
  if ((kind != QStringLiteral("desktop-light") &&
       kind != QStringLiteral("transition-veil") &&
       kind != QStringLiteral("vector-path")) ||
      request.cache_root.trimmed().isEmpty() ||
      !request.frame_size.isValid() || request.frame_size.isEmpty() ||
      !std::isfinite(request.frames_per_second) || request.frames_per_second <= 0.0 ||
      !std::isfinite(request.clip_start_seconds) || request.clip_start_seconds < 0.0 ||
      !std::isfinite(request.clip_duration_seconds) || request.clip_duration_seconds <= 0.0 ||
      !std::isfinite(request.time_offset_seconds) || request.samples.isEmpty() ||
      request.samples.size() > 1000000 ||
      static_cast<qint64>(request.frame_size.width()) * request.frame_size.height() >
          kMaximumPixelsPerFrame) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Native generator source cache settings are invalid.");
    }
    return false;
  }
  if (kind == QStringLiteral("transition-veil") &&
      (!std::isfinite(request.veil_gather) || request.veil_gather < 0.0 ||
       request.veil_gather > 1.0 || !std::isfinite(request.veil_field_opacity) ||
       request.veil_field_opacity < 0.0 || request.veil_field_opacity > 1.0)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Native transition veil source parameters are invalid.");
    }
    return false;
  }
  if (kind == QStringLiteral("vector-path")) {
    const auto vector_error = validate_vector_path_parameters(
        request.vector_path_parameters);
    if (!vector_error.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Native vector-path source parameters are invalid: %1")
                             .arg(vector_error);
      }
      return false;
    }
  }
  auto previous_seconds = -1.0;
  for (const auto& sample : request.samples) {
    if (!std::isfinite(sample.sequence_seconds) || sample.sequence_seconds < 0.0 ||
        sample.sequence_seconds <= previous_seconds + 1.0e-9 ||
        !std::isfinite(sample.local_seconds) || sample.local_seconds < 0.0 ||
        !std::isfinite(sample.gather) || sample.gather < 0.0 || sample.gather > 1.0 ||
        !std::isfinite(sample.time_scale) || sample.time_scale < 0.0 ||
        sample.time_scale > 100.0) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Native generator source frame times are invalid.");
      }
      return false;
    }
    previous_seconds = sample.sequence_seconds;
  }
  return true;
}

QJsonObject request_object(const NativeGeneratorFrameSequenceRequest& request) {
  QJsonArray samples;
  for (const auto& sample : request.samples) {
    samples.append(QJsonObject{
        {QStringLiteral("sequenceSeconds"), sample.sequence_seconds},
        {QStringLiteral("localSeconds"), sample.local_seconds},
        {QStringLiteral("gather"), sample.gather},
        {QStringLiteral("timeScale"), sample.time_scale},
    });
  }
  return {
      {QStringLiteral("generatorKind"), request.generator_kind.trimmed().toLower()},
      {QStringLiteral("width"), request.frame_size.width()},
      {QStringLiteral("height"), request.frame_size.height()},
      {QStringLiteral("framesPerSecond"), request.frames_per_second},
      {QStringLiteral("clipStartSeconds"), request.clip_start_seconds},
      {QStringLiteral("clipDurationSeconds"), request.clip_duration_seconds},
      {QStringLiteral("timeOffsetSeconds"), request.time_offset_seconds},
      {QStringLiteral("veilGather"), request.veil_gather},
      {QStringLiteral("veilFieldOpacity"), request.veil_field_opacity},
      {QStringLiteral("vectorPath"),
       QJsonObject::fromVariantMap(request.vector_path_parameters)},
      {QStringLiteral("samples"), samples},
  };
}

QString frame_file_name(int index) {
  return QStringLiteral("frame_%1.png").arg(index, 6, 10, QLatin1Char('0'));
}

QString frame_pattern(const QString& directory) {
  return QDir(directory).filePath(QStringLiteral("frame_%06d.png"));
}

CacheFileStamp cache_file_stamp(const QString& path) {
  QFileInfo info(path);
  info.refresh();
  if (!info.exists()) {
    return {};
  }
  CacheFileStamp stamp;
  stamp.exists = true;
  stamp.size = info.size();
  stamp.modified = info.lastModified();
  stamp.metadata_changed = info.fileTime(QFileDevice::FileMetadataChangeTime);
#ifdef Q_OS_UNIX
  const auto encoded_path = QFile::encodeName(path);
  struct stat file_status {};
  if (::stat(encoded_path.constData(), &file_status) == 0) {
    stamp.device = static_cast<quint64>(file_status.st_dev);
    stamp.inode = static_cast<quint64>(file_status.st_ino);
#if defined(__APPLE__)
    stamp.modified_seconds = static_cast<qint64>(file_status.st_mtimespec.tv_sec);
    stamp.modified_nanoseconds = static_cast<qint64>(file_status.st_mtimespec.tv_nsec);
    stamp.metadata_changed_seconds = static_cast<qint64>(file_status.st_ctimespec.tv_sec);
    stamp.metadata_changed_nanoseconds = static_cast<qint64>(file_status.st_ctimespec.tv_nsec);
#else
    stamp.modified_seconds = static_cast<qint64>(file_status.st_mtim.tv_sec);
    stamp.modified_nanoseconds = static_cast<qint64>(file_status.st_mtim.tv_nsec);
    stamp.metadata_changed_seconds = static_cast<qint64>(file_status.st_ctim.tv_sec);
    stamp.metadata_changed_nanoseconds = static_cast<qint64>(file_status.st_ctim.tv_nsec);
#endif
    stamp.high_resolution_identity = true;
  }
#endif
  return stamp;
}

QString normalized_cache_root(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool parse_cache_entry(const QString& path,
                       const QByteArray& manifest_bytes,
                       CacheEntry* entry) {
  if (entry == nullptr) {
    return false;
  }
  const auto manifest = QJsonDocument::fromJson(manifest_bytes).object();
  const auto bytes = manifest.value(QStringLiteral("encodedBytes")).toVariant().toLongLong();
  if (bytes <= 0) {
    return false;
  }
  entry->path = normalized_directory_key(path);
  entry->bytes = bytes;
  const auto manifest_path = QDir(path).filePath(QStringLiteral("manifest.json"));
  entry->manifest_stamp = cache_file_stamp(manifest_path);
  entry->modified = entry->manifest_stamp.modified;
  entry->manifest_sha256 = QCryptographicHash::hash(
      manifest_bytes, QCryptographicHash::Sha256);
  return true;
}

bool read_cache_entry(const QString& path, CacheEntry* entry) {
  QFile manifest_file(QDir(path).filePath(QStringLiteral("manifest.json")));
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    return false;
  }
  return parse_cache_entry(path, manifest_file.readAll(), entry);
}

bool scan_cache_inventory(const QString& cache_root, CacheInventory* inventory) {
  if (inventory == nullptr) {
    return false;
  }
  for (int attempt = 0; attempt < 2; ++attempt) {
    const auto root_stamp_before = cache_file_stamp(cache_root);
    QMap<QString, CacheEntry> entries;
    qint64 total_bytes = 0;
    const QDir root(cache_root);
    const auto directories = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                            QDir::Name);
    for (const auto& name : directories) {
      if (name.contains(QStringLiteral(".partial-"))) {
        continue;
      }
      CacheEntry entry;
      if (!read_cache_entry(root.filePath(name), &entry)) {
        continue;
      }
      if (entry.bytes > std::numeric_limits<qint64>::max() - total_bytes) {
        inventory->initialized = false;
        return false;
      }
      total_bytes += entry.bytes;
      entries.insert(entry.path, std::move(entry));
    }
    const auto root_stamp_after = cache_file_stamp(cache_root);
    if (root_stamp_before != root_stamp_after) {
      continue;
    }
    inventory->entries = std::move(entries);
    inventory->total_bytes = total_bytes;
    inventory->root_stamp = root_stamp_after;
    inventory->initialized = true;
#ifdef PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS
    cache_inventory_full_scan_count().fetch_add(1, std::memory_order_relaxed);
#endif
    return true;
  }
  inventory->initialized = false;
  return false;
}

void erase_cache_entry(CacheInventory* inventory, QMap<QString, CacheEntry>::iterator entry) {
  if (inventory == nullptr || entry == inventory->entries.end()) {
    return;
  }
  inventory->total_bytes -= entry->bytes;
  inventory->entries.erase(entry);
}

bool refresh_cache_inventory(const QString& cache_root, CacheInventory* inventory) {
  if (inventory == nullptr) {
    return false;
  }
  if (!inventory->initialized || cache_file_stamp(cache_root) != inventory->root_stamp) {
    return scan_cache_inventory(cache_root, inventory);
  }

  for (auto entry = inventory->entries.begin(); entry != inventory->entries.end();) {
    const auto manifest_path = QDir(entry->path).filePath(QStringLiteral("manifest.json"));
    const auto current_stamp = cache_file_stamp(manifest_path);
    if (!current_stamp.exists) {
      inventory->total_bytes -= entry->bytes;
      entry = inventory->entries.erase(entry);
      continue;
    }
    if (current_stamp.high_resolution_identity &&
        current_stamp == entry->manifest_stamp) {
#ifdef PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS
      cache_inventory_manifest_unchanged_skip_count().fetch_add(1, std::memory_order_relaxed);
#endif
      ++entry;
      continue;
    }
    QFile manifest_file(manifest_path);
    if (!manifest_file.open(QIODevice::ReadOnly)) {
      inventory->total_bytes -= entry->bytes;
      entry = inventory->entries.erase(entry);
      continue;
    }
    const auto manifest_bytes = manifest_file.readAll();
    const auto manifest_sha256 = QCryptographicHash::hash(
        manifest_bytes, QCryptographicHash::Sha256);
    if (manifest_sha256 == entry->manifest_sha256) {
      entry->manifest_stamp = current_stamp;
      entry->modified = current_stamp.modified;
      ++entry;
      continue;
    }
    {
      CacheEntry refreshed;
      if (!parse_cache_entry(entry->path, manifest_bytes, &refreshed)) {
        inventory->total_bytes -= entry->bytes;
        entry = inventory->entries.erase(entry);
        continue;
      }
      const auto total_without_entry = inventory->total_bytes - entry->bytes;
      if (refreshed.bytes > std::numeric_limits<qint64>::max() - total_without_entry) {
        inventory->initialized = false;
        return false;
      }
      inventory->total_bytes = total_without_entry + refreshed.bytes;
      entry.value() = std::move(refreshed);
#ifdef PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS
      cache_inventory_manifest_refresh_count().fetch_add(1, std::memory_order_relaxed);
#endif
    }
    ++entry;
  }

  const auto root_stamp_after = cache_file_stamp(cache_root);
  if (root_stamp_after != inventory->root_stamp) {
    return scan_cache_inventory(cache_root, inventory);
  }
  return true;
}

void note_cache_root_directory_change(const QString& cache_root) {
  const auto key = normalized_cache_root(cache_root);
  auto inventory = cache_inventories().find(key);
  if (inventory != cache_inventories().end() && inventory->initialized) {
    inventory->root_stamp = cache_file_stamp(key);
  }
}

void note_cache_entry_published(const QString& cache_root,
                                const QString& directory,
                                qint64 encoded_bytes) {
  const auto root_key = normalized_cache_root(cache_root);
  auto inventory = cache_inventories().find(root_key);
  if (inventory == cache_inventories().end() || !inventory->initialized) {
    return;
  }
  const auto entry_key = normalized_directory_key(directory);
  auto existing = inventory->entries.find(entry_key);
  if (existing != inventory->entries.end()) {
    inventory->total_bytes -= existing->bytes;
    inventory->entries.erase(existing);
  }
  CacheEntry entry;
  if (!read_cache_entry(entry_key, &entry) || entry.bytes != encoded_bytes) {
    inventory->initialized = false;
    return;
  }
  if (encoded_bytes > std::numeric_limits<qint64>::max() - inventory->total_bytes) {
    inventory->initialized = false;
    return;
  }
  inventory->total_bytes += encoded_bytes;
  inventory->entries.insert(entry_key, std::move(entry));
  inventory->root_stamp = cache_file_stamp(root_key);
}

void note_cache_entry_touch(const QString& cache_root, const QString& directory) {
  const auto root_key = normalized_cache_root(cache_root);
  auto inventory = cache_inventories().find(root_key);
  if (inventory == cache_inventories().end() || !inventory->initialized) {
    return;
  }
  const auto entry_key = normalized_directory_key(directory);
  auto entry = inventory->entries.find(entry_key);
  if (entry == inventory->entries.end()) {
    return;
  }
  entry->manifest_stamp = cache_file_stamp(
      QDir(entry_key).filePath(QStringLiteral("manifest.json")));
  entry->modified = entry->manifest_stamp.modified;
}

void note_cache_entry_removed(const QString& cache_root, const QString& directory) {
  const auto root_key = normalized_cache_root(cache_root);
  auto inventory = cache_inventories().find(root_key);
  if (inventory == cache_inventories().end() || !inventory->initialized) {
    return;
  }
  const auto entry_key = normalized_directory_key(directory);
  erase_cache_entry(&inventory.value(), inventory->entries.find(entry_key));
  inventory->root_stamp = cache_file_stamp(root_key);
}

bool manifest_matches(const NativeGeneratorFrameSequenceRequest& request,
                      const QString& key,
                      const QString& directory,
                      NativeGeneratorFrameSequenceResult* result) {
  QFile manifest_file(QDir(directory).filePath(QStringLiteral("manifest.json")));
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
  const auto frame_count = manifest.value(QStringLiteral("frameCount")).toInt(-1);
  const auto encoded_bytes = manifest.value(QStringLiteral("encodedBytes")).toVariant().toLongLong();
  if (manifest.value(QStringLiteral("cacheKey")).toString() != key ||
      manifest.value(QStringLiteral("generatorKind")).toString() !=
          request.generator_kind.trimmed().toLower() ||
      manifest.value(QStringLiteral("width")).toInt() != request.frame_size.width() ||
      manifest.value(QStringLiteral("height")).toInt() != request.frame_size.height() ||
      frame_count != static_cast<int>(request.samples.size()) || encoded_bytes <= 0 ||
      encoded_bytes > kNativeGeneratorFrameCacheBudgetBytes) {
    return false;
  }
  for (int index = 0; index < frame_count; ++index) {
    const QFileInfo frame_info(QDir(directory).filePath(frame_file_name(index)));
    if (!frame_info.isFile() || frame_info.size() <= 0) {
      return false;
    }
  }
  if (result != nullptr) {
    result->ok = true;
    result->cache_hit = true;
    result->cache_key = key;
    result->frame_directory = directory;
    result->frame_pattern = frame_pattern(directory);
    result->frame_count = frame_count;
    result->encoded_bytes = encoded_bytes;
    result->backend = manifest.value(QStringLiteral("backend")).toString();
    result->metal_frame_count = manifest.value(QStringLiteral("metalFrameCount")).toInt();
    result->cpu_frame_count = manifest.value(QStringLiteral("cpuFrameCount")).toInt();
    result->fallback_reason = manifest.value(QStringLiteral("fallbackReason")).toString();
  }
  return true;
}

bool image_has_opaque_alpha(const QImage& image) {
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  if (rgba.isNull()) {
    return false;
  }
  for (int row = 0; row < rgba.height(); ++row) {
    const auto* pixels = rgba.constScanLine(row);
    for (int column = 0; column < rgba.width(); ++column) {
      if (pixels[column * 4 + 3] != 255) {
        return false;
      }
    }
  }
  return true;
}

bool validate_written_frame(const QString& path,
                            const QSize& expected_size,
                            bool require_opaque_alpha,
                            qint64* encoded_bytes,
                            QString* error_message) {
  QImageReader reader(path, "PNG");
  auto image = reader.read();
  if (image.isNull() || image.size() != expected_size) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("A generated source frame could not be verified.");
    }
    return false;
  }
  if (require_opaque_alpha && !image_has_opaque_alpha(image)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The generated desktop-light source has non-opaque pixels.");
    }
    return false;
  }
  const auto size = QFileInfo(path).size();
  if (size <= 0 || encoded_bytes == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("A generated source frame has an invalid file size.");
    }
    return false;
  }
  *encoded_bytes += size;
  if (*encoded_bytes > kNativeGeneratorFrameCacheBudgetBytes) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native generator frame cache exceeds its 2 GiB budget.");
    }
    return false;
  }
  return true;
}

bool make_cache_space(const QString& cache_root,
                      const QString& protected_directory,
                      qint64 required_bytes) {
  auto& inventory = cache_inventories()[normalized_cache_root(cache_root)];
  if (!refresh_cache_inventory(cache_root, &inventory)) {
    return false;
  }
  if (required_bytes > kNativeGeneratorFrameCacheBudgetBytes) {
    return false;
  }
  const auto target_bytes = kNativeGeneratorFrameCacheBudgetBytes - required_bytes;
  if (inventory.total_bytes > target_bytes) {
    QVector<CacheEntry> entries;
    entries.reserve(inventory.entries.size());
    for (const auto& entry : inventory.entries) {
      entries.push_back(entry);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const CacheEntry& left,
                                                        const CacheEntry& right) {
      return left.modified < right.modified;
    });
    const auto protected_key = normalized_directory_key(protected_directory);
    bool root_changed = false;
    for (const auto& entry : entries) {
      if (inventory.total_bytes <= target_bytes) {
        break;
      }
      if (entry.path == protected_key ||
          pinned_frame_sequence_directories().contains(entry.path)) {
        continue;
      }
      if (QDir(entry.path).removeRecursively()) {
        erase_cache_entry(&inventory, inventory.entries.find(entry.path));
        root_changed = true;
      }
    }
    if (root_changed) {
      inventory.root_stamp = cache_file_stamp(cache_root);
    }
  }
  return inventory.total_bytes <= target_bytes;
}

struct PreparedVectorPathCommand final {
  QChar verb;
  qsizetype coordinate_offset = 0;
};

struct PreparedVectorPathFrame final {
  double seconds = 0.0;
  QVector<double> coordinates;
};

struct PreparedVectorPath final {
  std::shared_ptr<const PreparedRigScene> rig_scene;
  QSize size;
  double view_box_width = 0.0;
  double view_box_height = 0.0;
  QVector<PreparedVectorPathCommand> commands;
  QVector<double> base_coordinates;
  QVector<PreparedVectorPathFrame> frames;
  QColor stroke;
  QColor fill;
  double stroke_width = 1.0;
};

// request_is_valid() checks the complete public vector payload once before
// this cache-local prepared representation is built. Keep this path private to
// that validated request so interpolation never skips admission checks.
bool prepare_vector_path(const QVariantMap& parameters,
                         const QSize& size,
                         PreparedVectorPath* prepared) {
  if (prepared == nullptr) {
    return false;
  }
  PreparedVectorPath result;
  result.size = size;
  if (parameters.contains(QStringLiteral("scene"))) {
    result.rig_scene = prepare_rig_scene(parameters.value(QStringLiteral("scene")).toMap());
    if (!result.rig_scene || size.isEmpty()) return false;
    *prepared = std::move(result);
    return true;
  }
  result.view_box_width = parameters.value(QStringLiteral("viewBoxWidth")).toDouble();
  result.view_box_height = parameters.value(QStringLiteral("viewBoxHeight")).toDouble();
  result.stroke = QColor(parameters.value(QStringLiteral("stroke"),
                                           QStringLiteral("#00000000")).toString());
  result.fill = QColor(parameters.value(QStringLiteral("fill"),
                                         QStringLiteral("#00000000")).toString());
  result.stroke_width = parameters.value(QStringLiteral("strokeWidth"), 1.0).toDouble();

  const auto commands = parameters.value(QStringLiteral("commands")).toList();
  result.commands.reserve(commands.size());
  for (const auto& entry : commands) {
    const auto command = entry.toMap();
    const auto points = command.value(QStringLiteral("points")).toList();
    const auto verb_name = command.value(QStringLiteral("verb")).toString();
    if (verb_name.size() != 1 ||
        (verb_name != QStringLiteral("M") && verb_name != QStringLiteral("L") &&
         verb_name != QStringLiteral("C") && verb_name != QStringLiteral("Q") &&
         verb_name != QStringLiteral("Z"))) {
      return false;
    }
    result.commands.push_back({verb_name.at(0), result.base_coordinates.size()});
    result.base_coordinates.reserve(result.base_coordinates.size() + points.size());
    for (const auto& point : points) {
      result.base_coordinates.push_back(point.toDouble());
    }
  }

  const auto frames = parameters.value(QStringLiteral("frames")).toList();
  result.frames.reserve(frames.size());
  for (const auto& entry : frames) {
    const auto frame = entry.toMap();
    const auto values = frame.value(QStringLiteral("coordinates")).toList();
    if (values.size() != result.base_coordinates.size()) {
      return false;
    }
    PreparedVectorPathFrame prepared_frame;
    prepared_frame.seconds = frame.value(QStringLiteral("seconds")).toDouble();
    prepared_frame.coordinates.reserve(values.size());
    for (const auto& value : values) {
      prepared_frame.coordinates.push_back(value.toDouble());
    }
    result.frames.push_back(std::move(prepared_frame));
  }

  if (!result.size.isValid() || result.size.isEmpty() || result.commands.isEmpty() ||
      result.base_coordinates.isEmpty() || !result.stroke.isValid() ||
      !result.fill.isValid() || !std::isfinite(result.view_box_width) ||
      !std::isfinite(result.view_box_height) || result.view_box_width <= 0.0 ||
      result.view_box_height <= 0.0) {
    return false;
  }
  *prepared = std::move(result);
  return true;
}

QImage render_prepared_vector_path_frame(const PreparedVectorPath& prepared,
                                        double local_seconds) {
  if (prepared.rig_scene)
    return render_rig_scene_frame(prepared.size, *prepared.rig_scene, local_seconds);
  if (!std::isfinite(local_seconds) || prepared.size.isEmpty()) {
    return {};
  }

  const QVector<double>* coordinates = &prepared.base_coordinates;
  QVector<double> interpolated_coordinates;
  if (!prepared.frames.isEmpty()) {
    const auto upper = std::lower_bound(
        prepared.frames.cbegin(), prepared.frames.cend(), local_seconds,
        [](const PreparedVectorPathFrame& frame, double seconds) {
          return frame.seconds < seconds;
        });
    if (upper == prepared.frames.cbegin()) {
      coordinates = &upper->coordinates;
    } else if (upper == prepared.frames.cend()) {
      coordinates = &prepared.frames.constLast().coordinates;
    } else {
      const auto& after = *upper;
      const auto& before = *(upper - 1);
      const auto amount = std::clamp(
          (local_seconds - before.seconds) / (after.seconds - before.seconds),
          0.0, 1.0);
      interpolated_coordinates.reserve(before.coordinates.size());
      for (qsizetype index = 0; index < before.coordinates.size(); ++index) {
        const auto start = before.coordinates.at(index);
        const auto end = after.coordinates.at(index);
        interpolated_coordinates.push_back(start + (end - start) * amount);
      }
      coordinates = &interpolated_coordinates;
    }
  }

  const auto point = [coordinates](qsizetype index) {
    return QPointF(coordinates->at(index), coordinates->at(index + 1));
  };
  QPainterPath path;
  for (const auto& command : prepared.commands) {
    switch (command.verb.unicode()) {
      case u'M':
        path.moveTo(point(command.coordinate_offset));
        break;
      case u'L':
        path.lineTo(point(command.coordinate_offset));
        break;
      case u'C':
        path.cubicTo(point(command.coordinate_offset),
                     point(command.coordinate_offset + 2),
                     point(command.coordinate_offset + 4));
        break;
      case u'Q':
        path.quadTo(point(command.coordinate_offset),
                    point(command.coordinate_offset + 2));
        break;
      case u'Z':
        path.closeSubpath();
        break;
      default:
        return {};
    }
  }

  QImage image(prepared.size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.scale(prepared.size.width() / prepared.view_box_width,
                prepared.size.height() / prepared.view_box_height);
  painter.setPen(prepared.stroke.alpha() == 0 || prepared.stroke_width == 0.0
                     ? QPen(Qt::NoPen)
                     : QPen(prepared.stroke, prepared.stroke_width,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(prepared.fill);
  painter.drawPath(path);
  return image;
}

bool generator_phase_timings_enabled() {
  static const bool enabled =
      qEnvironmentVariableIntValue("PHOTO_EDITOR_TIMELINE_EXPORT_TIMINGS") == 1;
  return enabled;
}

void report_generator_phase_timings(const QString& kind,
                                    int frame_count,
                                    const QSize& size,
                                    bool cache_hit,
                                    const QString& backend,
                                    qint64 validation_us,
                                    qint64 key_us,
                                    qint64 lock_wait_us,
                                    qint64 manifest_us,
                                    qint64 inventory_us,
                                    qint64 vector_prepare_us,
                                    qint64 render_us,
                                    qint64 convert_us,
                                    qint64 png_encode_us,
                                    qint64 png_verify_us,
                                    qint64 publish_us,
                                    qint64 total_us) {
  if (!generator_phase_timings_enabled()) {
    return;
  }
  qInfo().noquote()
      << QStringLiteral(
             "timeline.generator.sequence kind=%1 frames=%2 size=%3x%4 hit=%5 backend=%6 validationUs=%7 keyUs=%8 lockWaitUs=%9 manifestUs=%10 inventoryUs=%11 vectorPrepareUs=%12 renderUs=%13 convertUs=%14 pngEncodeUs=%15 pngVerifyUs=%16 publishUs=%17 totalUs=%18")
             .arg(kind)
             .arg(frame_count)
             .arg(size.width())
             .arg(size.height())
             .arg(cache_hit ? 1 : 0)
             .arg(backend)
             .arg(validation_us)
             .arg(key_us)
             .arg(lock_wait_us)
             .arg(manifest_us)
             .arg(inventory_us)
             .arg(vector_prepare_us)
             .arg(render_us)
             .arg(convert_us)
             .arg(png_encode_us)
             .arg(png_verify_us)
             .arg(publish_us)
             .arg(total_us);
}

NativeGeneratorFrameSequenceResult failure_result(
    const QString& key,
    const QString& error,
    qint64 elapsed_milliseconds) {
  NativeGeneratorFrameSequenceResult result;
  result.cache_key = key;
  result.error = error;
  result.elapsed_milliseconds = elapsed_milliseconds;
  return result;
}

}  // namespace

QString native_generator_frame_sequence_cache_key(
    const NativeGeneratorFrameSequenceRequest& request) {
  const auto canonical = QJsonDocument(request_object(request)).toJson(QJsonDocument::Compact);
  const auto versioned = QByteArrayLiteral("timeline-native-generator-source-v1\n") + canonical;
  return QString::fromLatin1(
      QCryptographicHash::hash(versioned, QCryptographicHash::Sha256).toHex());
}

bool native_generator_frame_sequence_is_cached(
    const NativeGeneratorFrameSequenceRequest& request,
    NativeGeneratorFrameSequenceResult* result) {
  QString error;
  if (!request_is_valid(request, &error)) {
    return false;
  }
  const auto key = native_generator_frame_sequence_cache_key(request);
  const auto directory = QDir(request.cache_root).filePath(key);
  QMutexLocker locker(&frame_sequence_cache_mutex());
  return manifest_matches(request, key, directory, result);
}

void pin_native_generator_frame_sequence_directory(const QString& directory) {
  if (directory.trimmed().isEmpty()) {
    return;
  }
  QMutexLocker locker(&frame_sequence_cache_mutex());
  const auto key = normalized_directory_key(directory);
  ++pinned_frame_sequence_directories()[key];
  active_frame_sequence_work().fetch_add(1, std::memory_order_relaxed);
}

void unpin_native_generator_frame_sequence_directory(const QString& directory) {
  if (directory.trimmed().isEmpty()) {
    return;
  }
  QMutexLocker locker(&frame_sequence_cache_mutex());
  const auto key = normalized_directory_key(directory);
  auto& pinned = pinned_frame_sequence_directories();
  const auto found = pinned.find(key);
  if (found == pinned.end()) {
    return;
  }
  if (found.value() <= 1) {
    pinned.erase(found);
  } else {
    --found.value();
  }
  active_frame_sequence_work().fetch_sub(1, std::memory_order_relaxed);
}

bool native_generator_frame_sequence_cache_busy() {
  return active_frame_sequence_work().load(std::memory_order_relaxed) > 0;
}

#ifdef PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS
int native_generator_frame_cache_inventory_full_scans_for_testing() noexcept {
  return cache_inventory_full_scan_count().load(std::memory_order_relaxed);
}

int native_generator_frame_cache_inventory_manifest_refreshes_for_testing() noexcept {
  return cache_inventory_manifest_refresh_count().load(std::memory_order_relaxed);
}

int native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing() noexcept {
  return cache_inventory_manifest_unchanged_skip_count().load(std::memory_order_relaxed);
}
#endif

bool reserve_native_generator_frame_cache_bytes(
    const QString& cache_root,
    const QString& staged_directory,
    qint64 encoded_bytes,
    bool* cache_capacity_failure,
    QString* error_message) {
  if (cache_capacity_failure != nullptr) {
    *cache_capacity_failure = false;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  const auto normalized_root = QDir(cache_root).absolutePath();
  const auto normalized_stage = normalized_directory_key(staged_directory);
  if (cache_root.trimmed().isEmpty() || staged_directory.trimmed().isEmpty() ||
      encoded_bytes < 0 || encoded_bytes > kNativeGeneratorFrameCacheBudgetBytes ||
      QDir::cleanPath(QFileInfo(normalized_stage).absolutePath()) !=
          QDir::cleanPath(normalized_root)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native frame cache reservation is invalid.");
    }
    return false;
  }

  QMutexLocker locker(&frame_sequence_cache_mutex());
  if (!pinned_frame_sequence_directories().contains(normalized_stage)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native frame cache reservation is not pinned.");
    }
    return false;
  }
  const auto manifest_path = QDir(normalized_stage).filePath(QStringLiteral("manifest.json"));
  QFile manifest_file(manifest_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native frame cache stage has no reservation manifest.");
    }
    return false;
  }
  auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
  manifest_file.close();
  if (manifest.isEmpty() || !manifest.contains(QStringLiteral("cacheKey")) ||
      !manifest.contains(QStringLiteral("complete")) ||
      manifest.value(QStringLiteral("complete")).toBool()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Only an incomplete native frame cache stage can reserve bytes.");
    }
    return false;
  }
  manifest.insert(QStringLiteral("encodedBytes"), encoded_bytes);
  const auto manifest_bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
  QSaveFile replacement(manifest_path);
  if (!replacement.open(QIODevice::WriteOnly) ||
      replacement.write(manifest_bytes) != manifest_bytes.size() ||
      !replacement.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to persist the native frame cache byte reservation.");
    }
    return false;
  }
  if (!make_cache_space(normalized_root, normalized_stage, 0)) {
    if (cache_capacity_failure != nullptr) {
      *cache_capacity_failure = true;
    }
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "The pinned native frame cache exceeds its shared 2 GiB encoded budget.");
    }
    return false;
  }
  return true;
}

bool touch_native_generator_frame_cache_entry(
    const QString& cache_root,
    const QString& directory,
    bool* cache_capacity_failure,
    QString* error_message) {
  if (cache_capacity_failure != nullptr) {
    *cache_capacity_failure = false;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  const auto normalized_root = QDir(cache_root).absolutePath();
  const auto normalized_directory = normalized_directory_key(directory);
  if (cache_root.trimmed().isEmpty() || directory.trimmed().isEmpty() ||
      QDir::cleanPath(QFileInfo(normalized_directory).absolutePath()) !=
          QDir::cleanPath(normalized_root)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native frame cache entry path is invalid.");
    }
    return false;
  }

  QMutexLocker locker(&frame_sequence_cache_mutex());
  if (!pinned_frame_sequence_directories().contains(normalized_directory)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native frame cache entry is not pinned.");
    }
    return false;
  }
  const auto manifest_path = QDir(normalized_directory).filePath(QStringLiteral("manifest.json"));
  QFile manifest_file(manifest_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native frame cache entry has no manifest.");
    }
    return false;
  }
  const auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
  manifest_file.close();
  if (manifest.isEmpty() || !manifest.contains(QStringLiteral("cacheKey")) ||
      (manifest.contains(QStringLiteral("complete")) &&
       !manifest.value(QStringLiteral("complete")).toBool())) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native frame cache entry is not complete.");
    }
    return false;
  }
  if (!make_cache_space(normalized_root, normalized_directory, 0)) {
    if (cache_capacity_failure != nullptr) {
      *cache_capacity_failure = true;
    }
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "The pinned native frame cache exceeds its shared 2 GiB encoded budget.");
    }
    return false;
  }
  QFile timestamp_file(manifest_path);
  if (!timestamp_file.open(QIODevice::ReadWrite) ||
      !timestamp_file.setFileTime(QDateTime::currentDateTimeUtc(),
                                  QFileDevice::FileModificationTime)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to refresh the native frame cache LRU timestamp.");
    }
    return false;
  }
  note_cache_entry_touch(normalized_root, normalized_directory);
  return true;
}

NativeGeneratorFrameSequenceLease::~NativeGeneratorFrameSequenceLease() {
  clear();
}

NativeGeneratorFrameSequenceLease::NativeGeneratorFrameSequenceLease(
    NativeGeneratorFrameSequenceLease&& other) noexcept
    : directories_(std::move(other.directories_)),
      results_by_cache_key_(std::move(other.results_by_cache_key_)) {
  other.directories_.clear();
  other.results_by_cache_key_.clear();
}

NativeGeneratorFrameSequenceLease& NativeGeneratorFrameSequenceLease::operator=(
    NativeGeneratorFrameSequenceLease&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  clear();
  directories_ = std::move(other.directories_);
  results_by_cache_key_ = std::move(other.results_by_cache_key_);
  other.directories_.clear();
  other.results_by_cache_key_.clear();
  return *this;
}

void NativeGeneratorFrameSequenceLease::adoptPinnedDirectory(
    const QString& directory) {
  if (!directory.trimmed().isEmpty()) {
    directories_.push_back(directory);
  }
}

bool NativeGeneratorFrameSequenceLease::adoptPinnedResult(
    const NativeGeneratorFrameSequenceResult& result) {
  if (!result.ok || !result.lease_acquired || result.cache_key.trimmed().isEmpty() ||
      result.frame_directory.trimmed().isEmpty() || result.frame_pattern.trimmed().isEmpty() ||
      result.frame_count <= 0) {
    return false;
  }
  directories_.push_back(result.frame_directory);
  results_by_cache_key_.insert(result.cache_key, result);
  return true;
}

bool NativeGeneratorFrameSequenceLease::lookup(
    const QString& cache_key,
    NativeGeneratorFrameSequenceResult* result) const {
  if (result == nullptr || cache_key.trimmed().isEmpty()) {
    return false;
  }
  const auto found = results_by_cache_key_.constFind(cache_key);
  if (found == results_by_cache_key_.cend() || !found->ok || !found->lease_acquired ||
      !directories_.contains(found->frame_directory)) {
    return false;
  }
  *result = found.value();
  return true;
}

void NativeGeneratorFrameSequenceLease::append(
    NativeGeneratorFrameSequenceLease&& other) {
  if (this == &other) {
    return;
  }
  directories_.append(std::move(other.directories_));
  for (auto result = other.results_by_cache_key_.cbegin();
       result != other.results_by_cache_key_.cend(); ++result) {
    results_by_cache_key_.insert(result.key(), result.value());
  }
  other.directories_.clear();
  other.results_by_cache_key_.clear();
}

void NativeGeneratorFrameSequenceLease::clear() {
  for (const auto& directory : directories_) {
    unpin_native_generator_frame_sequence_directory(directory);
  }
  directories_.clear();
  results_by_cache_key_.clear();
}

NativeGeneratorFrameSequenceResult ensure_native_generator_frame_sequence(
    const NativeGeneratorFrameSequenceRequest& request,
    const std::atomic_bool* cancellation,
    const std::function<bool()>& cancellation_check,
    bool pin_result) {
  [[maybe_unused]] const FrameSequenceActivity activity;
  QElapsedTimer timer;
  timer.start();
  QElapsedTimer phase_timer;
  phase_timer.start();
  qint64 validation_us = 0;
  qint64 key_us = 0;
  qint64 lock_wait_us = 0;
  qint64 manifest_us = 0;
  qint64 inventory_us = 0;
  qint64 vector_prepare_us = 0;
  qint64 render_us = 0;
  qint64 convert_us = 0;
  qint64 png_encode_us = 0;
  qint64 png_verify_us = 0;
  qint64 publish_us = 0;
  QString validation_error;
  if (!request_is_valid(request, &validation_error)) {
    return failure_result({}, validation_error, timer.elapsed());
  }
  validation_us = phase_timer.nsecsElapsed() / 1000;
  if (cancellation_requested(cancellation, cancellation_check)) {
    return failure_result({}, QStringLiteral("Native generator source rendering was cancelled."),
                          timer.elapsed());
  }
  phase_timer.restart();
  const auto key = native_generator_frame_sequence_cache_key(request);
  key_us = phase_timer.nsecsElapsed() / 1000;
  const auto normalized_root = QDir(request.cache_root).absolutePath();
  const auto destination = QDir(normalized_root).filePath(key);
  phase_timer.restart();
  QMutexLocker locker(&frame_sequence_cache_mutex());
  lock_wait_us = phase_timer.nsecsElapsed() / 1000;
  phase_timer.restart();
  NativeGeneratorFrameSequenceResult cached;
  const auto cache_hit = manifest_matches(request, key, destination, &cached);
  manifest_us = phase_timer.nsecsElapsed() / 1000;
  if (cache_hit) {
    phase_timer.restart();
    if (!make_cache_space(normalized_root, destination, 0)) {
      return failure_result(
          key,
          QStringLiteral("The native generator frame cache is over its 2 GiB budget; clear timeline render caches before rendering."),
          timer.elapsed());
    }
    inventory_us = phase_timer.nsecsElapsed() / 1000;
    QFile manifest_file(QDir(destination).filePath(QStringLiteral("manifest.json")));
    if (manifest_file.open(QIODevice::ReadWrite)) {
      manifest_file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
      note_cache_entry_touch(normalized_root, destination);
    }
    cached.elapsed_milliseconds = timer.elapsed();
    if (pin_result) {
      ++pinned_frame_sequence_directories()[normalized_directory_key(destination)];
      active_frame_sequence_work().fetch_add(1, std::memory_order_relaxed);
      cached.lease_acquired = true;
    }
    report_generator_phase_timings(
        request.generator_kind.trimmed().toLower(), cached.frame_count,
        request.frame_size, true, cached.backend, validation_us, key_us,
        lock_wait_us, manifest_us, inventory_us, vector_prepare_us, render_us,
        convert_us, png_encode_us, png_verify_us, publish_us,
        timer.nsecsElapsed() / 1000);
    return cached;
  }
  if (QDir(destination).exists() && QDir(destination).removeRecursively()) {
    note_cache_entry_removed(normalized_root, destination);
  }
  if (!QDir().mkpath(normalized_root)) {
    return failure_result(key, QStringLiteral("Unable to create the native generator frame cache."),
                          timer.elapsed());
  }
  const auto stage_name = key + QStringLiteral(".partial-") +
                          QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto stage_directory = QDir(normalized_root).filePath(stage_name);
  if (!QDir().mkpath(stage_directory)) {
    return failure_result(key, QStringLiteral("Unable to stage native generator source frames."),
                          timer.elapsed());
  }
  note_cache_root_directory_change(normalized_root);
  const auto cleanup_stage = [&stage_directory, &normalized_root]() {
    if (QDir(stage_directory).removeRecursively()) {
      note_cache_root_directory_change(normalized_root);
    }
  };

  NativeGeneratorFrameSequenceResult result;
  result.cache_key = key;
  result.frame_directory = destination;
  result.frame_pattern = frame_pattern(destination);
  result.frame_count = static_cast<int>(request.samples.size());
  auto source = request.generator_kind.trimmed().toLower();
  const auto require_opaque_alpha = source == QStringLiteral("desktop-light");
  PreparedVectorPath prepared_vector_path;
  if (source == QStringLiteral("vector-path")) {
    phase_timer.restart();
    if (!prepare_vector_path(request.vector_path_parameters, request.frame_size,
                             &prepared_vector_path)) {
      cleanup_stage();
      return failure_result(
          key,
          QStringLiteral("The native vector-path renderer could not prepare a validated source."),
          timer.elapsed());
    }
    vector_prepare_us = phase_timer.nsecsElapsed() / 1000;
  }
  for (qsizetype index = 0; index < request.samples.size(); ++index) {
    if (cancellation_requested(cancellation, cancellation_check)) {
      cleanup_stage();
      return failure_result(key,
                            QStringLiteral("Native generator source rendering was cancelled."),
                            timer.elapsed());
    }
    const auto& sample = request.samples.at(index);
    QImage image;
    QString metal_error;
    phase_timer.restart();
    if (source == QStringLiteral("vector-path")) {
      image = render_prepared_vector_path_frame(prepared_vector_path,
                                                sample.local_seconds);
      if (image.isNull()) {
        cleanup_stage();
        return failure_result(
            key,
            QStringLiteral("The native vector-path renderer returned no source frame."),
            timer.elapsed());
      }
      ++result.cpu_frame_count;
    } else {
      if (source == QStringLiteral("desktop-light")) {
        const auto light_time = sample.sequence_seconds * sample.time_scale +
                                request.time_offset_seconds;
        image = render_desktop_light_field_metal(
            request.frame_size,
            DesktopLightFieldParameters{light_time, sample.gather, 1.0},
            cancellation,
            &metal_error);
      } else {
        const auto local_time = sample.sequence_seconds -
                                request.clip_start_seconds - 0.5;
        image = render_transition_veil_field_metal(
            request.frame_size,
            local_time,
            sample.sequence_seconds,
            request.veil_gather,
            request.veil_field_opacity,
            cancellation,
            &metal_error);
      }
      if (image.isNull()) {
        if (cancellation_requested(cancellation, cancellation_check)) {
          cleanup_stage();
          return failure_result(key,
                                QStringLiteral("Native generator source rendering was cancelled."),
                                timer.elapsed());
        }
        if (source == QStringLiteral("desktop-light")) {
          const auto light_time = sample.sequence_seconds * sample.time_scale +
                                  request.time_offset_seconds;
          image = render_desktop_light_field(
              request.frame_size,
              DesktopLightFieldParameters{light_time, sample.gather, 1.0});
        } else {
          image = render_transition_veil_frame(
              request.frame_size,
              sample.sequence_seconds - request.clip_start_seconds - 0.5,
              sample.sequence_seconds,
              request.veil_gather,
              1.0,
              request.veil_field_opacity);
        }
        if (cancellation_requested(cancellation, cancellation_check)) {
          cleanup_stage();
          return failure_result(key,
                                QStringLiteral("Native generator source rendering was cancelled."),
                                timer.elapsed());
        }
        const auto cpu_error = image.isNull()
                                   ? QStringLiteral("the CPU source renderer returned no image")
                                   : QString{};
        if (image.isNull()) {
          cleanup_stage();
          const auto combined_error = QStringLiteral("Metal backend failed (%1); CPU fallback failed (%2).")
                                          .arg(metal_error, cpu_error);
          return failure_result(key, combined_error, timer.elapsed());
        }
        if (result.fallback_reason.isEmpty()) {
          result.fallback_reason = metal_error;
        }
        ++result.cpu_frame_count;
      } else {
        ++result.metal_frame_count;
      }
    }
    render_us += phase_timer.nsecsElapsed() / 1000;
    if (cancellation_requested(cancellation, cancellation_check)) {
      cleanup_stage();
      return failure_result(key,
                            QStringLiteral("Native generator source rendering was cancelled."),
                            timer.elapsed());
    }
    phase_timer.restart();
    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull() || image.size() != request.frame_size ||
        (require_opaque_alpha && !image_has_opaque_alpha(image))) {
      cleanup_stage();
      return failure_result(
          key,
          require_opaque_alpha
              ? QStringLiteral("The desktop-light source frame is not full-size opaque RGBA.")
              : QStringLiteral("The native generator source frame is not full-size RGBA."),
          timer.elapsed());
    }
    convert_us += phase_timer.nsecsElapsed() / 1000;
    const auto frame_path = QDir(stage_directory).filePath(
        frame_file_name(static_cast<int>(index)));
    QImageWriter writer(frame_path, "PNG");
    writer.setCompression(kNativeGeneratorFramePngCompression);
    phase_timer.restart();
    if (!writer.write(image)) {
      cleanup_stage();
      return failure_result(key, QStringLiteral("Unable to encode a native generator source frame."),
                            timer.elapsed());
    }
    png_encode_us += phase_timer.nsecsElapsed() / 1000;
    phase_timer.restart();
    if (!validate_written_frame(frame_path, request.frame_size, require_opaque_alpha,
                                &result.encoded_bytes, &validation_error)) {
      cleanup_stage();
      return failure_result(key, validation_error, timer.elapsed());
    }
    png_verify_us += phase_timer.nsecsElapsed() / 1000;
  }
  if (cancellation_requested(cancellation, cancellation_check)) {
    cleanup_stage();
    return failure_result(key, QStringLiteral("Native generator source rendering was cancelled."),
                          timer.elapsed());
  }
  if (result.cpu_frame_count > 0 && result.metal_frame_count > 0) {
    result.backend = QStringLiteral("mixed");
  } else if (result.cpu_frame_count > 0) {
    result.backend = QStringLiteral("cpu");
  } else {
    result.backend = QStringLiteral("metal");
  }

  auto manifest = request_object(request);
  manifest.insert(QStringLiteral("cacheKey"), key);
  manifest.insert(QStringLiteral("frameCount"), result.frame_count);
  manifest.insert(QStringLiteral("encodedBytes"), result.encoded_bytes);
  manifest.insert(QStringLiteral("backend"), result.backend);
  manifest.insert(QStringLiteral("metalFrameCount"), result.metal_frame_count);
  manifest.insert(QStringLiteral("cpuFrameCount"), result.cpu_frame_count);
  manifest.insert(QStringLiteral("fallbackReason"), result.fallback_reason);
  QSaveFile manifest_file(QDir(stage_directory).filePath(QStringLiteral("manifest.json")));
  const auto manifest_bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
  if (!manifest_file.open(QIODevice::WriteOnly) ||
      manifest_file.write(manifest_bytes) != manifest_bytes.size() || !manifest_file.commit()) {
    cleanup_stage();
    return failure_result(key, QStringLiteral("Unable to publish the native generator frame manifest."),
                          timer.elapsed());
  }
  if (cancellation_requested(cancellation, cancellation_check)) {
    cleanup_stage();
    return failure_result(key, QStringLiteral("Native generator source rendering was cancelled."),
                          timer.elapsed());
  }
  phase_timer.restart();
  if (!make_cache_space(normalized_root, destination, result.encoded_bytes)) {
    cleanup_stage();
    return failure_result(
        key,
        QStringLiteral("The native generator frame cache is pinned at its 2 GiB budget; finish active renders or clear old timeline render caches."),
        timer.elapsed());
  }
  inventory_us = phase_timer.nsecsElapsed() / 1000;
  phase_timer.restart();
  QDir parent(normalized_root);
  if (!parent.rename(stage_name, key)) {
    cleanup_stage();
    return failure_result(key, QStringLiteral("Unable to atomically publish native generator frames."),
                          timer.elapsed());
  }
  note_cache_entry_published(normalized_root, destination, result.encoded_bytes);
  publish_us = phase_timer.nsecsElapsed() / 1000;
  result.ok = true;
  result.frame_directory = destination;
  result.frame_pattern = frame_pattern(destination);
  result.elapsed_milliseconds = timer.elapsed();
  if (pin_result) {
    ++pinned_frame_sequence_directories()[normalized_directory_key(destination)];
    active_frame_sequence_work().fetch_add(1, std::memory_order_relaxed);
    result.lease_acquired = true;
  }
  report_generator_phase_timings(
      source, result.frame_count, request.frame_size, false, result.backend,
      validation_us, key_us, lock_wait_us, manifest_us, inventory_us,
      vector_prepare_us, render_us, convert_us, png_encode_us, png_verify_us,
      publish_us, timer.nsecsElapsed() / 1000);
  return result;
}

}  // namespace photo_editor::desktop::timeline_motion
