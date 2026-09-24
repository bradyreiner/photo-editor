#pragma once

#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "async_mask_job.h"

namespace photo_editor::desktop {

class ThumbnailCacheSession final {
 public:
  [[nodiscard]] bool maskDirty() const { return mask_dirty_; }
  [[nodiscard]] bool& maskDirty() { return mask_dirty_; }
  [[nodiscard]] bool photoLayerDirty() const { return photo_layer_dirty_; }
  [[nodiscard]] bool& photoLayerDirty() { return photo_layer_dirty_; }
  [[nodiscard]] bool& maskDeferCounted() { return mask_defer_counted_; }
  [[nodiscard]] bool& photoLayerDeferCounted() {
    return photo_layer_defer_counted_;
  }

  [[nodiscard]] QVariantList& maskSetsWithThumbnails() {
    return mask_sets_with_thumbnails_;
  }
  [[nodiscard]] const QVariantList& maskSetsWithThumbnails() const {
    return mask_sets_with_thumbnails_;
  }
  [[nodiscard]] QString& maskSetsDeferredKey() {
    return mask_sets_deferred_key_;
  }
  [[nodiscard]] const QString& maskSetsDeferredKey() const {
    return mask_sets_deferred_key_;
  }
  [[nodiscard]] QVariantList& photoLayersWithThumbnails() {
    return photo_layers_with_thumbnails_;
  }
  [[nodiscard]] const QVariantList& photoLayersWithThumbnails() const {
    return photo_layers_with_thumbnails_;
  }
  [[nodiscard]] QString& photoLayerAssetKey() { return photo_layer_asset_key_; }
  [[nodiscard]] const QString& photoLayerAssetKey() const {
    return photo_layer_asset_key_;
  }
  [[nodiscard]] int& photoLayerRevision() { return photo_layer_revision_; }
  [[nodiscard]] int photoLayerRevision() const { return photo_layer_revision_; }
  [[nodiscard]] QImage& publishedPreviewImage() {
    return published_preview_image_;
  }
  [[nodiscard]] const QImage& publishedPreviewImage() const {
    return published_preview_image_;
  }
  [[nodiscard]] QString& publishedPreviewStateSignature() {
    return published_preview_state_signature_;
  }
  [[nodiscard]] const QString& publishedPreviewStateSignature() const {
    return published_preview_state_signature_;
  }

  [[nodiscard]] bool entryLookup(const QString& key, QVariantMap* entry) {
    if (key.isEmpty() || !entry_cache_.contains(key)) {
      ++entry_cache_miss_count_;
      return false;
    }
    if (entry != nullptr) {
      *entry = entry_cache_.value(key);
    }
    entry_cache_order_.removeAll(key);
    entry_cache_order_.prepend(key);
    ++entry_cache_hit_count_;
    return true;
  }

  void storeEntry(
      const QString& key,
      const QVariantMap& entry,
      int maximum_entries,
      qint64 maximum_bytes) {
    if (key.isEmpty() || entry.isEmpty()) {
      return;
    }
    const auto entry_bytes = estimatedEntryBytes(key, entry);
    if (entry_bytes <= 0 || entry_bytes > maximum_bytes) {
      return;
    }
    if (entry_cache_.contains(key)) {
      entry_cache_bytes_ -= estimatedEntryBytes(key, entry_cache_.value(key));
    }
    entry_cache_.insert(key, entry);
    entry_cache_bytes_ += entry_bytes;
    entry_cache_order_.removeAll(key);
    entry_cache_order_.prepend(key);
    ++entry_cache_store_count_;
    while ((entry_cache_order_.size() > maximum_entries ||
            entry_cache_bytes_ > maximum_bytes) &&
           !entry_cache_order_.isEmpty()) {
      const auto evicted_key = entry_cache_order_.takeLast();
      const auto evicted = entry_cache_.take(evicted_key);
      if (!evicted.isEmpty()) {
        entry_cache_bytes_ -= estimatedEntryBytes(evicted_key, evicted);
        ++entry_cache_eviction_count_;
      }
    }
  }

  void clearEntries() {
    entry_cache_.clear();
    entry_cache_order_.clear();
    entry_cache_bytes_ = 0;
  }

  [[nodiscard]] qint64 entryBytes() const { return entry_cache_bytes_; }
  [[nodiscard]] int entryCount() const {
    return static_cast<int>(entry_cache_.size());
  }
  [[nodiscard]] int entryHitCount() const { return entry_cache_hit_count_; }
  [[nodiscard]] int entryMissCount() const { return entry_cache_miss_count_; }
  [[nodiscard]] int entryStoreCount() const { return entry_cache_store_count_; }
  [[nodiscard]] int entryEvictionCount() const {
    return entry_cache_eviction_count_;
  }

  [[nodiscard]] AsyncMaskJob& latestMaskJob() { return latest_mask_job_; }
  [[nodiscard]] const AsyncMaskJob& latestMaskJob() const {
    return latest_mask_job_;
  }
  [[nodiscard]] int& maskJobGeneration() { return mask_job_generation_; }
  [[nodiscard]] int maskJobGeneration() const { return mask_job_generation_; }
  [[nodiscard]] int& maskHitCount() { return mask_hit_count_; }
  [[nodiscard]] int maskHitCount() const { return mask_hit_count_; }
  [[nodiscard]] int& maskRefreshCount() { return mask_refresh_count_; }
  [[nodiscard]] int maskRefreshCount() const { return mask_refresh_count_; }
  [[nodiscard]] int& maskDeferredCount() { return mask_deferred_count_; }
  [[nodiscard]] int maskDeferredCount() const { return mask_deferred_count_; }
  [[nodiscard]] AsyncMaskJob& latestPhotoLayerJob() {
    return latest_photo_layer_job_;
  }
  [[nodiscard]] const AsyncMaskJob& latestPhotoLayerJob() const {
    return latest_photo_layer_job_;
  }
  [[nodiscard]] int& photoLayerJobGeneration() {
    return photo_layer_job_generation_;
  }
  [[nodiscard]] int photoLayerJobGeneration() const {
    return photo_layer_job_generation_;
  }
  [[nodiscard]] int& photoLayerHitCount() { return photo_layer_hit_count_; }
  [[nodiscard]] int photoLayerHitCount() const { return photo_layer_hit_count_; }
  [[nodiscard]] int& photoLayerRefreshCount() {
    return photo_layer_refresh_count_;
  }
  [[nodiscard]] int photoLayerRefreshCount() const {
    return photo_layer_refresh_count_;
  }
  [[nodiscard]] int& photoLayerDeferredCount() {
    return photo_layer_deferred_count_;
  }
  [[nodiscard]] int photoLayerDeferredCount() const {
    return photo_layer_deferred_count_;
  }

 private:
  static qint64 estimatedEntryBytes(
      const QString& cache_key,
      const QVariantMap& cache_entry) {
    const auto serialized_bytes = static_cast<qint64>(
        QJsonDocument(QJsonObject::fromVariantMap(cache_entry))
            .toJson(QJsonDocument::Compact)
            .size());
    return serialized_bytes * 2 +
           static_cast<qint64>(cache_key.size()) *
               static_cast<qint64>(sizeof(QChar)) +
           512;
  }

  bool mask_dirty_ = true;
  bool photo_layer_dirty_ = true;
  bool mask_defer_counted_ = false;
  bool photo_layer_defer_counted_ = false;
  QVariantList mask_sets_with_thumbnails_;
  QString mask_sets_deferred_key_;
  QVariantList photo_layers_with_thumbnails_;
  QString photo_layer_asset_key_;
  int photo_layer_revision_ = 0;
  QImage published_preview_image_;
  QString published_preview_state_signature_;
  QHash<QString, QVariantMap> entry_cache_;
  QStringList entry_cache_order_;
  qint64 entry_cache_bytes_ = 0;
  int entry_cache_hit_count_ = 0;
  int entry_cache_miss_count_ = 0;
  int entry_cache_store_count_ = 0;
  int entry_cache_eviction_count_ = 0;
  AsyncMaskJob latest_mask_job_;
  int mask_job_generation_ = 0;
  int mask_hit_count_ = 0;
  int mask_refresh_count_ = 0;
  int mask_deferred_count_ = 0;
  AsyncMaskJob latest_photo_layer_job_;
  int photo_layer_job_generation_ = 0;
  int photo_layer_hit_count_ = 0;
  int photo_layer_refresh_count_ = 0;
  int photo_layer_deferred_count_ = 0;
};

}  // namespace photo_editor::desktop
