#include "decoded_image_cache.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QTimeZone>

namespace photo_editor::desktop {

namespace {

constexpr qint64 kMaxDecodedCacheMemoryBytes = 512 * 1024 * 1024;

struct DecodedImageCache final {
  QMutex mutex;
  QHash<QString, QImage> images;
  QList<QString> access_order;  // front = most recently used
  qint64 total_bytes = 0;

  void touch(const QString& key) {
    access_order.removeOne(key);
    access_order.prepend(key);
  }

  void insert(const QString& key, const QImage& image) {
    if (key.isEmpty() || image.isNull()) {
      return;
    }

    if (images.contains(key)) {
      total_bytes -= static_cast<qint64>(images.value(key).sizeInBytes());
      images.remove(key);
      access_order.removeOne(key);
    }

    const auto image_bytes = static_cast<qint64>(image.sizeInBytes());
    while (total_bytes + image_bytes > kMaxDecodedCacheMemoryBytes && !access_order.isEmpty()) {
      const auto& evict_key = access_order.last();
      total_bytes -= static_cast<qint64>(images.value(evict_key).sizeInBytes());
      images.remove(evict_key);
      access_order.removeLast();
    }

    images.insert(key, image);
    access_order.prepend(key);
    total_bytes += image_bytes;
  }
};

DecodedImageCache& decoded_image_cache() {
  static DecodedImageCache cache;
  return cache;
}

DecodedImageCacheSnapshot snapshot_locked(const DecodedImageCache& cache) {
  return DecodedImageCacheSnapshot{
      .entries = static_cast<int>(cache.images.size()),
      .bytes = cache.total_bytes,
  };
}

}  // namespace

QString decoded_image_cache_key(const QFileInfo& file_info, int max_edge) {
  return QStringLiteral("%1|%2|%3|%4")
      .arg(file_info.absoluteFilePath())
      .arg(max_edge)
      .arg(file_info.size())
      .arg(file_info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
}

DecodedImageCacheSnapshot decoded_image_cache_snapshot() {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  return snapshot_locked(cache);
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
  const auto cached = cache.images.constFind(key);
  if (cached == cache.images.cend()) {
    if (snapshot != nullptr) {
      *snapshot = snapshot_locked(cache);
    }
    return false;
  }

  *image = cached.value();
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
  cache.insert(key, image);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
}

}  // namespace photo_editor::desktop
