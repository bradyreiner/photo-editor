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

struct BrushReplayMatteCacheEntry final {
  QByteArray matte_data;
  QSize source_size;
  QSize target_size;
  int replay_point_count = 0;
};

struct ScaledMaskMatteCache final {
  QMutex mutex;
  QHash<QString, ScaledMaskMatteCacheEntry> mattes;
  QHash<QString, BrushReplayMatteCacheEntry> brush_replay_mattes;
  QList<QString> access_order;
  qint64 total_bytes = 0;
  int hit_count = 0;
  int miss_count = 0;
  int eviction_count = 0;
  int brush_replay_hit_count = 0;
  int brush_replay_miss_count = 0;
  int brush_replay_eviction_count = 0;

  void touch(const QString& key) {
    access_order.removeOne(key);
    access_order.prepend(key);
  }

  bool find_scaled(const QString& key, QByteArray* matte_data) {
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

  bool find_brush_replay(
      const QString& key,
      BrushReplayMatteCacheEntry* entry) {
    if (key.isEmpty() || entry == nullptr) {
      return false;
    }
    const auto cached = brush_replay_mattes.constFind(key);
    if (cached == brush_replay_mattes.cend()) {
      ++brush_replay_miss_count;
      return false;
    }
    *entry = cached.value();
    touch(key);
    ++brush_replay_hit_count;
    return true;
  }

  void evict_one() {
    if (access_order.isEmpty()) {
      return;
    }
    const auto evict_key = access_order.takeLast();
    const auto brush_entry = brush_replay_mattes.find(evict_key);
    if (brush_entry != brush_replay_mattes.end()) {
      total_bytes -= static_cast<qint64>(brush_entry->matte_data.size());
      brush_replay_mattes.erase(brush_entry);
      ++brush_replay_eviction_count;
      return;
    }
    const auto scaled_entry = mattes.find(evict_key);
    if (scaled_entry != mattes.end()) {
      total_bytes -= static_cast<qint64>(scaled_entry->matte_data.size());
      mattes.erase(scaled_entry);
      ++eviction_count;
    }
  }

  int entry_count() const {
    return static_cast<int>(mattes.size() + brush_replay_mattes.size());
  }

  bool make_room(const QString& key, qint64 matte_bytes) {
    if (matte_bytes <= 0 || matte_bytes > kMaxScaledMaskMatteCacheMemoryBytes) {
      return false;
    }
    while ((total_bytes + matte_bytes > kMaxScaledMaskMatteCacheMemoryBytes ||
            entry_count() >= kMaxScaledMaskMatteCacheEntries) &&
           !access_order.isEmpty()) {
      if (access_order.last() == key) {
        // Replacing an existing item is handled before make_room().
        return false;
      }
      evict_one();
    }
    return total_bytes + matte_bytes <= kMaxScaledMaskMatteCacheMemoryBytes &&
           entry_count() < kMaxScaledMaskMatteCacheEntries;
  }

  void insert_scaled(
      const QString& key,
      const QByteArray& matte_data,
      const QSize& source_size,
      const QSize& target_size,
      const QString& purpose) {
    if (key.isEmpty() || matte_data.isEmpty() || !source_size.isValid() ||
        !target_size.isValid() || target_size.width() <= 0 ||
        target_size.height() <= 0 ||
        static_cast<qint64>(matte_data.size()) !=
            static_cast<qint64>(target_size.width()) * target_size.height()) {
      return;
    }
    if (mattes.contains(key)) {
      total_bytes -= static_cast<qint64>(mattes.value(key).matte_data.size());
      mattes.remove(key);
      access_order.removeOne(key);
    }
    const auto matte_bytes = static_cast<qint64>(matte_data.size());
    if (!make_room(key, matte_bytes)) {
      return;
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

  void insert_brush_replay(
      const QString& key,
      const QByteArray& matte_data,
      const QSize& source_size,
      const QSize& target_size,
      int replay_point_count) {
    if (key.isEmpty() || matte_data.isEmpty() || !source_size.isValid() ||
        !target_size.isValid() || target_size.width() <= 0 ||
        target_size.height() <= 0 ||
        static_cast<qint64>(matte_data.size()) !=
            static_cast<qint64>(target_size.width()) * target_size.height()) {
      return;
    }
    if (brush_replay_mattes.contains(key)) {
      total_bytes -= static_cast<qint64>(brush_replay_mattes.value(key).matte_data.size());
      brush_replay_mattes.remove(key);
      access_order.removeOne(key);
    }
    const auto matte_bytes = static_cast<qint64>(matte_data.size());
    if (!make_room(key, matte_bytes)) {
      return;
    }
    brush_replay_mattes.insert(
        key,
        BrushReplayMatteCacheEntry{
            .matte_data = matte_data,
            .source_size = source_size,
            .target_size = target_size,
            .replay_point_count = replay_point_count,
        });
    access_order.prepend(key);
    total_bytes += matte_bytes;
  }

  void clear() {
    mattes.clear();
    brush_replay_mattes.clear();
    access_order.clear();
    total_bytes = 0;
    hit_count = 0;
    miss_count = 0;
    eviction_count = 0;
    brush_replay_hit_count = 0;
    brush_replay_miss_count = 0;
    brush_replay_eviction_count = 0;
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

QString brush_replay_matte_cache_key(
    const QVariantMap& mask,
    int target_width,
    int target_height,
    const QString& purpose) {
  if (normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()) !=
          QStringLiteral("brush") ||
      target_width <= 1 || target_height <= 1 ||
      mask.value(QStringLiteral("brushAutoMask")).toBool() ||
      mask.value(QStringLiteral("brushProtectEdges")).toBool()) {
    return {};
  }
  for (const auto& stroke_value : mask.value(QStringLiteral("brushStrokes")).toList()) {
    const auto settings = stroke_value.toMap().value(QStringLiteral("settings")).toMap();
    if (settings.value(QStringLiteral("autoMask")).toBool() ||
        settings.value(QStringLiteral("protectEdges")).toBool()) {
      return {};
    }
  }

  QVariantMap payload;
  payload.insert(QStringLiteral("version"), QStringLiteral("brush-replay-matte-v1"));
  payload.insert(QStringLiteral("purpose"), purpose);
  payload.insert(QStringLiteral("targetWidth"), target_width);
  payload.insert(QStringLiteral("targetHeight"), target_height);
  payload.insert(
      QStringLiteral("sourceWidth"),
      mask.value(QStringLiteral("segmentationWidth"), target_width).toInt());
  payload.insert(
      QStringLiteral("sourceHeight"),
      mask.value(QStringLiteral("segmentationHeight"), target_height).toInt());
  payload.insert(QStringLiteral("brushSize"),
      mask.value(QStringLiteral("brushSize"), 0.08).toDouble());
  payload.insert(QStringLiteral("brushFlow"),
      mask.value(QStringLiteral("brushFlow"), 1.0).toDouble());
  payload.insert(QStringLiteral("feather"),
      mask.value(QStringLiteral("feather"), 0.75).toDouble());
  payload.insert(QStringLiteral("brushAutoMask"), false);
  payload.insert(QStringLiteral("brushProtectEdges"), false);
  payload.insert(QStringLiteral("brushAutoMaskTolerance"),
      mask.value(QStringLiteral("brushAutoMaskTolerance"), 0.18).toDouble());

  const auto signature = mask.value(QStringLiteral("brushAlphaSourceSignature")).toString();
  if (!signature.isEmpty()) {
    payload.insert(QStringLiteral("brushAlphaSourceSignature"), signature);
  } else {
    // Older/imported masks may not carry the source signature. Include the
    // complete ordered replay history as a correctness-preserving fallback.
    payload.insert(QStringLiteral("brushPoints"), mask.value(QStringLiteral("brushPoints")));
    payload.insert(QStringLiteral("brushStrokes"), mask.value(QStringLiteral("brushStrokes")));
  }

  const auto digest = QCryptographicHash::hash(
      QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex();
  return QStringLiteral("brush:") + QString::fromLatin1(digest);
}

QVariantMap brush_replay_result_from_cache(
    const QVariantMap& mask,
    const BrushReplayMatteCacheEntry& entry,
    const QString& purpose) {
  auto replayed_mask = mask;
  replayed_mask.insert(QStringLiteral("segmentationData"), entry.matte_data);
  replayed_mask.insert(QStringLiteral("segmentationWidth"), entry.target_size.width());
  replayed_mask.insert(QStringLiteral("segmentationHeight"), entry.target_size.height());
  replayed_mask.insert(QStringLiteral("scaledMattePurpose"), purpose);
  replayed_mask.insert(QStringLiteral("scaledMatteSourceWidth"), entry.source_size.width());
  replayed_mask.insert(QStringLiteral("scaledMatteSourceHeight"), entry.source_size.height());
  replayed_mask.insert(QStringLiteral("scaledMatteWidth"), entry.target_size.width());
  replayed_mask.insert(QStringLiteral("scaledMatteHeight"), entry.target_size.height());
  replayed_mask.insert(QStringLiteral("scaledMatteRebuiltFromBrushStrokes"), true);
  replayed_mask.insert(
      QStringLiteral("scaledMatteBrushReplayPointCount"), entry.replay_point_count);
  // A cache hit performs no raster work in this call. Keep the existing
  // telemetry fields present, with zero work time and the reused output area.
  replayed_mask.insert(QStringLiteral("scaled_matte_rebuild_ms"), 0);
  const auto output_pixels =
      static_cast<qint64>(entry.target_size.width()) * entry.target_size.height();
  replayed_mask.insert(QStringLiteral("scaled_matte_rebuild_pixels"), output_pixels);
  if (purpose == QStringLiteral("full-resolution-render")) {
    replayed_mask.insert(QStringLiteral("full_resolution_matte_rebuild_ms"), 0);
    replayed_mask.insert(QStringLiteral("full_resolution_matte_rebuild_pixels"), output_pixels);
  }
  return replayed_mask;
}

}  // namespace

QVariantMap mask_with_scaled_segmentation_matte(
    const QVariantMap& mask,
    const QSize& target_size,
    const QString& purpose) {
  if (!target_size.isValid()) {
    return mask;
  }

  const auto target_width = std::max(1, target_size.width());
  const auto target_height = std::max(1, target_size.height());
  const auto brush_cache_key = brush_replay_matte_cache_key(
      mask, target_width, target_height, purpose);
  if (!brush_cache_key.isEmpty()) {
    BrushReplayMatteCacheEntry cached_brush;
    auto& cache = scaled_mask_matte_cache();
    {
      QMutexLocker locker(&cache.mutex);
      if (cache.find_brush_replay(brush_cache_key, &cached_brush)) {
        return brush_replay_result_from_cache(mask, cached_brush, purpose);
      }
    }

    const auto rebuilt_brush = mask_with_rebuilt_brush_matte(mask, target_size, purpose);
    if (!rebuilt_brush.isEmpty()) {
      const auto matte = rebuilt_brush.value(QStringLiteral("segmentationData")).toByteArray();
      const auto source_size = QSize(
          rebuilt_brush.value(QStringLiteral("scaledMatteSourceWidth")).toInt(),
          rebuilt_brush.value(QStringLiteral("scaledMatteSourceHeight")).toInt());
      const auto replay_point_count =
          rebuilt_brush.value(QStringLiteral("scaledMatteBrushReplayPointCount")).toInt();
      QMutexLocker locker(&cache.mutex);
      cache.insert_brush_replay(
          brush_cache_key,
          matte,
          source_size,
          QSize(target_width, target_height),
          replay_point_count);
      return rebuilt_brush;
    }
  } else {
    const auto rebuilt_brush = mask_with_rebuilt_brush_matte(mask, target_size, purpose);
    if (!rebuilt_brush.isEmpty()) {
      return rebuilt_brush;
    }
  }

  const auto source_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto source_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto source_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(source_data, source_width, source_height)) {
    return mask;
  }

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
    cache.find_scaled(cache_key, &scaled);
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
      cache.insert_scaled(
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
      .bytes = [&cache]() {
        qint64 bytes = 0;
        for (auto it = cache.mattes.cbegin(); it != cache.mattes.cend(); ++it) {
          bytes += static_cast<qint64>(it.value().matte_data.size());
        }
        return bytes;
      }(),
      .hit_count = cache.hit_count,
      .miss_count = cache.miss_count,
      .eviction_count = cache.eviction_count,
      .brush_replay_entries = static_cast<int>(cache.brush_replay_mattes.size()),
      .brush_replay_bytes = [&cache]() {
        qint64 bytes = 0;
        for (auto it = cache.brush_replay_mattes.cbegin();
             it != cache.brush_replay_mattes.cend(); ++it) {
          bytes += static_cast<qint64>(it.value().matte_data.size());
        }
        return bytes;
      }(),
      .brush_replay_hit_count = cache.brush_replay_hit_count,
      .brush_replay_miss_count = cache.brush_replay_miss_count,
      .brush_replay_eviction_count = cache.brush_replay_eviction_count,
      .total_bytes = cache.total_bytes,
      .total_entries = cache.entry_count(),
  };
}

}  // namespace photo_editor::desktop
