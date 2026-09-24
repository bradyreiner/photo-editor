#include "decoded_image_cache.h"
#include "linked_image_source.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QTimeZone>

#include <optional>

namespace photo_editor::desktop {

namespace {

constexpr qint64 kDefaultDecodedCacheMemoryBytes = 512 * 1024 * 1024;
constexpr qint64 kMinimumDecodedCacheMemoryBytes = 32 * 1024 * 1024;
constexpr qint64 kMaximumDecodedCacheMemoryBytes = 8LL * 1024 * 1024 * 1024;

struct DecodedImageCache final {
  struct Entry final {
    QImage image;
    std::optional<photo_editor::pipeline::DecodedImageMetadata> metadata;
  };

  QMutex mutex;
  QHash<QString, Entry> entries;
  QList<QString> access_order;  // front = most recently used
  qint64 total_bytes = 0;
  qint64 budget_bytes = kDefaultDecodedCacheMemoryBytes;

  void touch(const QString& key) {
    access_order.removeOne(key);
    access_order.prepend(key);
  }

  void insert(
      const QString& key,
      const QImage& image,
      std::optional<photo_editor::pipeline::DecodedImageMetadata> metadata) {
    if (key.isEmpty() || image.isNull()) {
      return;
    }

    if (entries.contains(key)) {
      total_bytes -=
          static_cast<qint64>(entries.value(key).image.sizeInBytes());
      entries.remove(key);
      access_order.removeOne(key);
    }

    const auto image_bytes = static_cast<qint64>(image.sizeInBytes());
    if (image_bytes > budget_bytes) {
      return;
    }
    while (total_bytes + image_bytes > budget_bytes && !access_order.isEmpty()) {
      const auto& evict_key = access_order.last();
      total_bytes -=
          static_cast<qint64>(entries.value(evict_key).image.sizeInBytes());
      entries.remove(evict_key);
      access_order.removeLast();
    }

    entries.insert(key, Entry{.image = image, .metadata = std::move(metadata)});
    access_order.prepend(key);
    total_bytes += image_bytes;
  }

  void trim() {
    while (total_bytes > budget_bytes && !access_order.isEmpty()) {
      const auto evict_key = access_order.takeLast();
      total_bytes -= static_cast<qint64>(entries.value(evict_key).image.sizeInBytes());
      entries.remove(evict_key);
    }
  }
};

DecodedImageCache& decoded_image_cache() {
  static DecodedImageCache cache;
  return cache;
}

DecodedImageCacheSnapshot snapshot_locked(const DecodedImageCache& cache) {
  return DecodedImageCacheSnapshot{
      .entries = static_cast<int>(cache.entries.size()),
      .bytes = cache.total_bytes,
  };
}

}  // namespace

QString decoded_image_cache_key(const QFileInfo& file_info, int max_edge) {
  return QStringLiteral("%1|%2|%3|%4|%5")
      .arg(file_info.absoluteFilePath())
      .arg(max_edge)
      .arg(file_info.size())
      .arg(file_info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch())
      .arg(linked_image_file_revision(file_info.absoluteFilePath()));
}

DecodedImageCacheSnapshot decoded_image_cache_snapshot() {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  return snapshot_locked(cache);
}

qint64 decoded_image_cache_budget_bytes() {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  return cache.budget_bytes;
}

void set_decoded_image_cache_budget_bytes(qint64 bytes) {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.budget_bytes = qBound(
      kMinimumDecodedCacheMemoryBytes, bytes, kMaximumDecodedCacheMemoryBytes);
  cache.trim();
}

void clear_decoded_image_cache() {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.entries.clear();
  cache.access_order.clear();
  cache.total_bytes = 0;
}

bool find_decoded_image_cache_entry(
    const QString& key,
    QImage* image,
    DecodedImageCacheSnapshot* snapshot) {
  if (key.isEmpty() || image == nullptr) {
    return false;
  }

  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  const auto cached = cache.entries.constFind(key);
  if (cached == cache.entries.cend()) {
    if (snapshot != nullptr) {
      *snapshot = snapshot_locked(cache);
    }
    return false;
  }

  *image = cached.value().image;
  cache.touch(key);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
  return !image->isNull();
}

void insert_decoded_image_cache_entry(
    const QString& key,
    const QImage& image,
    DecodedImageCacheSnapshot* snapshot) {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.insert(key, image, std::nullopt);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
}

bool find_decoded_image_cache_entry_with_metadata(
    const QString& key,
    QImage* image,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    DecodedImageCacheSnapshot* snapshot) {
  if (key.isEmpty() || image == nullptr || metadata == nullptr) {
    return false;
  }

  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  const auto cached = cache.entries.constFind(key);
  if (cached == cache.entries.cend() || !cached.value().metadata.has_value()) {
    if (snapshot != nullptr) {
      *snapshot = snapshot_locked(cache);
    }
    return false;
  }

  *image = cached.value().image;
  *metadata = *cached.value().metadata;
  cache.touch(key);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
  return !image->isNull();
}

void insert_decoded_image_cache_entry_with_metadata(
    const QString& key,
    const QImage& image,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    DecodedImageCacheSnapshot* snapshot) {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.insert(key, image, metadata);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
}

}  // namespace photo_editor::desktop
