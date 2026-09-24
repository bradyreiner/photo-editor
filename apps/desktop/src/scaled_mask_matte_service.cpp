#include "scaled_mask_matte_service.h"

#include "brush_matte_rebuild.h"
#include "edit_recipe_mask_schema.h"
#include "segmentation_mask_ops.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

namespace photo_editor::desktop {
namespace {

struct ScaledMaskMatteCacheEntry final {
  QByteArray matte_data;
  QSize source_size;
  QSize target_size;
  QString purpose;
};

struct ScaledMaskMatteCache final {
  QMutex mutex;
  QHash<QString, ScaledMaskMatteCacheEntry> mattes;
  QList<QString> access_order;
  qint64 total_bytes = 0;
  int hit_count = 0;
  int miss_count = 0;
  int eviction_count = 0;

  void touch(const QString& key) {
    access_order.removeOne(key);
    access_order.prepend(key);
  }

  bool find(const QString& key, QByteArray* matte_data) {
    if (key.isEmpty() || matte_data == nullptr) {
      return false;
    }
    const auto cached = mattes.constFind(key);
    if (cached == mattes.cend()) {
      ++miss_count;
      return false;
    }
    *matte_data = cached.value().matte_data;
    touch(key);
    ++hit_count;
    return true;
  }

  void insert(
      const QString& key,
      const QByteArray& matte_data,
      const QSize& source_size,
      const QSize& target_size,
      const QString& purpose) {
    if (key.isEmpty() || matte_data.isEmpty() || !source_size.isValid() ||
        !target_size.isValid() ||
        matte_data.size() != target_size.width() * target_size.height()) {
      return;
    }
    if (mattes.contains(key)) {
      total_bytes -= static_cast<qint64>(mattes.value(key).matte_data.size());
      mattes.remove(key);
      access_order.removeOne(key);
    }
    const auto matte_bytes = static_cast<qint64>(matte_data.size());
    if (matte_bytes > kMaxScaledMaskMatteCacheMemoryBytes) {
      return;
    }
    while (total_bytes + matte_bytes > kMaxScaledMaskMatteCacheMemoryBytes &&
           !access_order.isEmpty()) {
      const auto& evict_key = access_order.last();
      total_bytes -= static_cast<qint64>(mattes.value(evict_key).matte_data.size());
      mattes.remove(evict_key);
      access_order.removeLast();
      ++eviction_count;
    }
    mattes.insert(
        key,
        ScaledMaskMatteCacheEntry{
            .matte_data = matte_data,
            .source_size = source_size,
            .target_size = target_size,
            .purpose = purpose,
        });
    access_order.prepend(key);
    total_bytes += matte_bytes;
  }

  void clear() {
    mattes.clear();
    access_order.clear();
    total_bytes = 0;
    hit_count = 0;
    miss_count = 0;
    eviction_count = 0;
  }
};

ScaledMaskMatteCache& scaled_mask_matte_cache() {
  static ScaledMaskMatteCache cache;
  return cache;
}

QString scaled_mask_matte_cache_key(
    const QByteArray& source_data,
    int source_width,
    int source_height,
    int target_width,
    int target_height,
    const QString& purpose) {
  if (!valid_segmentation_payload(source_data, source_width, source_height) ||
      target_width <= 0 || target_height <= 0) {
    return {};
  }
  QVariantMap payload;
  payload.insert(QStringLiteral("version"), QStringLiteral("scaled-mask-matte-v1"));
  payload.insert(QStringLiteral("purpose"), purpose);
  payload.insert(QStringLiteral("sourceWidth"), source_width);
  payload.insert(QStringLiteral("sourceHeight"), source_height);
  payload.insert(QStringLiteral("targetWidth"), target_width);
  payload.insert(QStringLiteral("targetHeight"), target_height);
  payload.insert(QStringLiteral("bytes"), source_data.size());
  payload.insert(
      QStringLiteral("sha1"),
      QString::fromLatin1(
          QCryptographicHash::hash(source_data, QCryptographicHash::Sha1).toHex()));
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex());
}

}  // namespace

QVariantMap mask_with_scaled_segmentation_matte(
    const QVariantMap& mask,
    const QSize& target_size,
    const QString& purpose) {
  if (!target_size.isValid()) {
    return mask;
  }

  const auto rebuilt_brush = mask_with_rebuilt_brush_matte(mask, target_size, purpose);
  if (!rebuilt_brush.isEmpty()) {
    return rebuilt_brush;
  }

  const auto source_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto source_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto source_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(source_data, source_width, source_height)) {
    return mask;
  }

  const auto target_width = std::max(1, target_size.width());
  const auto target_height = std::max(1, target_size.height());
  if (source_width == target_width && source_height == target_height) {
    auto scaled_mask = mask;
    scaled_mask.insert(QStringLiteral("scaledMattePurpose"), purpose);
    scaled_mask.insert(
        QStringLiteral("scaledMatteSourceWidth"),
        mask.value(QStringLiteral("scaledMatteSourceWidth"), source_width).toInt());
    scaled_mask.insert(
        QStringLiteral("scaledMatteSourceHeight"),
        mask.value(QStringLiteral("scaledMatteSourceHeight"), source_height).toInt());
    scaled_mask.insert(QStringLiteral("scaledMatteWidth"), target_width);
    scaled_mask.insert(QStringLiteral("scaledMatteHeight"), target_height);
    return scaled_mask;
  }

  const auto cache_key = scaled_mask_matte_cache_key(
      source_data,
      source_width,
      source_height,
      target_width,
      target_height,
      purpose);
  QByteArray scaled;
  if (!cache_key.isEmpty()) {
    auto& cache = scaled_mask_matte_cache();
    QMutexLocker locker(&cache.mutex);
    cache.find(cache_key, &scaled);
  }
  if (scaled.isEmpty()) {
    scaled = resample_segmentation_mask(
        source_data,
        source_width,
        source_height,
        target_width,
        target_height);
    if (!scaled.isEmpty() && !cache_key.isEmpty()) {
      auto& cache = scaled_mask_matte_cache();
      QMutexLocker locker(&cache.mutex);
      cache.insert(
          cache_key,
          scaled,
          QSize(source_width, source_height),
          QSize(target_width, target_height),
          purpose);
    }
  }
  if (scaled.isEmpty()) {
    return mask;
  }

  auto scaled_mask = mask;
  scaled_mask.insert(QStringLiteral("segmentationData"), scaled);
  scaled_mask.insert(QStringLiteral("segmentationWidth"), target_width);
  scaled_mask.insert(QStringLiteral("segmentationHeight"), target_height);
  scaled_mask.insert(QStringLiteral("scaledMattePurpose"), purpose);
  scaled_mask.insert(QStringLiteral("scaledMatteSourceWidth"), source_width);
  scaled_mask.insert(QStringLiteral("scaledMatteSourceHeight"), source_height);
  scaled_mask.insert(QStringLiteral("scaledMatteWidth"), target_width);
  scaled_mask.insert(QStringLiteral("scaledMatteHeight"), target_height);
  return scaled_mask;
}

QVariantMap scaled_mask_matte_metadata(const QVariantMap& mask) {
  QVariantMap metadata;
  for (const auto& key : {
           QStringLiteral("scaledMattePurpose"),
           QStringLiteral("scaledMatteSourceWidth"),
           QStringLiteral("scaledMatteSourceHeight"),
           QStringLiteral("scaledMatteWidth"),
           QStringLiteral("scaledMatteHeight"),
           QStringLiteral("scaledMatteRebuiltFromBrushStrokes"),
           QStringLiteral("scaledMatteBrushReplayPointCount"),
       }) {
    if (mask.contains(key)) {
      metadata.insert(key, mask.value(key));
    }
  }
  return metadata;
}

void clear_scaled_mask_matte_cache() {
  auto& cache = scaled_mask_matte_cache();
  QMutexLocker locker(&cache.mutex);
  cache.clear();
}

ScaledMaskMatteCacheSnapshot scaled_mask_matte_cache_snapshot() {
  auto& cache = scaled_mask_matte_cache();
  QMutexLocker locker(&cache.mutex);
  return ScaledMaskMatteCacheSnapshot{
      .entries = static_cast<int>(cache.mattes.size()),
      .bytes = cache.total_bytes,
      .hit_count = cache.hit_count,
      .miss_count = cache.miss_count,
      .eviction_count = cache.eviction_count,
  };
}

}  // namespace photo_editor::desktop
