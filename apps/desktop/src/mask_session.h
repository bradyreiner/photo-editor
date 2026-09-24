#pragma once
#include "ai_inpaint_control.h"
#include <memory>

#include <utility>

#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace photo_editor::desktop {

class MaskSession final {
 public:
  ~MaskSession() { flushSelectionPreferences(); }
  void setSelectionStorageDirectory(const QString& directory) {
    if (selection_storage_directory_ == directory) return;
    flushSelectionPreferences();
    selection_storage_directory_ = directory;
  }
  bool flushSelectionPreferences() {
    if (dirty_selection_assets_.isEmpty() || selection_storage_directory_.isEmpty()) return true;
    if (!QDir{}.mkpath(selection_storage_directory_)) return false;
    bool success = true;
    const auto pending = dirty_selection_assets_;
    for (const auto& asset : pending) {
      QJsonObject components;
      const auto preferences = last_component_by_asset_.value(asset);
      for (auto it = preferences.cbegin(); it != preferences.cend(); ++it)
        components.insert(it.key(), it.value());
      QSaveFile file(selectionPreferencesPath(asset));
      const auto bytes = QJsonDocument(components).toJson(QJsonDocument::Compact);
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        success = false;
        continue;
      }
      dirty_selection_assets_.remove(asset);
    }
    return success;
  }
  struct RetryRequest final {
    QString action;
    QString label;
    QString detail;
    QVariantList points;
    QString intent;
    bool create_new = false;
    QString retouch_mode;
  };

  [[nodiscard]] const QVariantList& stack() const { return stack_; }
  [[nodiscard]] int activeIndex() const { return active_index_; }
  void setAssetScope(const QString& asset_id) {
    if (asset_scope_ == asset_id) return;
    rememberActiveComponent();
    flushSelectionPreferences();
    asset_scope_ = asset_id;
    if (!selection_storage_directory_.isEmpty() && !asset_scope_.isEmpty() &&
        !last_component_by_asset_.contains(asset_scope_)) {
      QFile file(selectionPreferencesPath(asset_scope_));
      if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024) {
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        auto& preferences = last_component_by_asset_[asset_scope_];
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
          if (it.value().isString()) preferences.insert(it.key(), it.value().toString());
      }
    }
    // The existing stack belongs to the previous asset until replaceStack.
    active_index_ = -1;
    invalidateActiveDefinition();
  }
  [[nodiscard]] bool hasActiveMask() const {
    return active_index_ >= 0 && active_index_ < stack_.size();
  }

  void replaceStack(QVariantList stack, int active_index) {
    rememberActiveComponent();
    stack_ = std::move(stack);
    active_index_ = normalizedActiveIndex(active_index);
    invalidateActiveDefinition();
    rememberActiveComponent();
  }

  [[nodiscard]] bool setActiveIndex(int active_index) {
    const auto normalized = normalizedActiveIndex(active_index);
    if (normalized == active_index_) {
      return false;
    }
    active_index_ = normalized;
    invalidateActiveDefinition();
    rememberActiveComponent();
    return true;
  }

  [[nodiscard]] int indexForMaskSet(const QString& set_id,
                                    const QString& component_id = {}) const {
    if (set_id.isEmpty()) return -1;
    const auto preferred = component_id.isEmpty()
        ? last_component_by_asset_.value(asset_scope_).value(set_id) : component_id;
    int first = -1;
    for (int index = 0; index < stack_.size(); ++index) {
      const auto mask = stack_.at(index).toMap();
      if (mask.value(QStringLiteral("maskSetId")).toString() != set_id) continue;
      if (first < 0) first = index;
      if (mask.value(QStringLiteral("id")).toString() == preferred) return index;
    }
    // An explicitly requested, deleted component must not select a sibling.
    return component_id.isEmpty() ? first : -1;
  }

  [[nodiscard]] bool normalizeActiveIndex() {
    return setActiveIndex(active_index_);
  }

  [[nodiscard]] QVariantMap activeDefinition() const {
    if (!hasActiveMask()) {
      return {};
    }
    if (active_definition_dirty_) {
      cached_active_definition_ = stack_.at(active_index_).toMap();
      active_definition_dirty_ = false;
    }
    return cached_active_definition_;
  }

  void invalidateActiveDefinition() {
    active_definition_dirty_ = true;
    cached_active_definition_.clear();
  }

  void clearPreviewCache() const {
    preview_cache_key_.clear();
    preview_cache_source_.clear();
    preview_lru_.clear();
    preview_lru_order_.clear();
  }

  [[nodiscard]] bool previewCacheLookup(
      const QString& key,
      QString* source) const {
    const auto it = preview_lru_.constFind(key);
    if (it == preview_lru_.cend()) {
      ++preview_cache_miss_count_;
      return false;
    }
    ++preview_cache_hit_count_;
    preview_cache_key_ = key;
    preview_cache_source_ = it.value();
    preview_lru_order_.removeAll(key);
    preview_lru_order_.prepend(key);
    if (source != nullptr) {
      *source = it.value();
    }
    return true;
  }

  void storePreviewCacheEntry(
      const QString& key,
      const QString& source,
      int capacity) const {
    preview_cache_key_ = key;
    preview_cache_source_ = source;
    if (key.isEmpty() || source.isEmpty() || capacity <= 0) {
      return;
    }
    preview_lru_.insert(key, source);
    preview_lru_order_.removeAll(key);
    preview_lru_order_.prepend(key);
    while (preview_lru_order_.size() > capacity) {
      const auto evicted_key = preview_lru_order_.takeLast();
      if (preview_lru_.remove(evicted_key) > 0) {
        ++preview_cache_eviction_count_;
      }
    }
  }

  [[nodiscard]] const QString& previewCacheKey() const {
    return preview_cache_key_;
  }
  [[nodiscard]] const QString& previewCacheSource() const {
    return preview_cache_source_;
  }
  [[nodiscard]] int previewCacheEntryCount() const {
    return static_cast<int>(preview_lru_.size());
  }
  [[nodiscard]] qint64 previewCacheBytes() const {
    auto bytes = qint64{0};
    for (auto it = preview_lru_.cbegin(); it != preview_lru_.cend(); ++it) {
      bytes += static_cast<qint64>(it.value().size()) *
               static_cast<qint64>(sizeof(QChar));
    }
    return bytes;
  }
  [[nodiscard]] int previewCacheHitCount() const {
    return preview_cache_hit_count_;
  }
  [[nodiscard]] int previewCacheMissCount() const {
    return preview_cache_miss_count_;
  }
  [[nodiscard]] int previewCacheEvictionCount() const {
    return preview_cache_eviction_count_;
  }

  void clearOutlineCache() const {
    outline_cache_key_.clear();
    outline_cache_paths_.clear();
    outline_cache_trace_fields_.clear();
  }

  [[nodiscard]] bool outlineCacheLookup(
      const QString& key,
      QVariantList* paths,
      QVariantMap* trace_fields) const {
    if (key != outline_cache_key_) {
      return false;
    }
    ++outline_cache_hit_count_;
    if (paths != nullptr) {
      *paths = outline_cache_paths_;
    }
    if (trace_fields != nullptr) {
      *trace_fields = outline_cache_trace_fields_;
    }
    return true;
  }

  void storeOutlineCache(
      QString key,
      QVariantList paths,
      QVariantMap trace_fields) const {
    outline_cache_key_ = std::move(key);
    outline_cache_paths_ = std::move(paths);
    outline_cache_trace_fields_ = std::move(trace_fields);
  }

  void recordOutlineCacheMiss(bool fast_identity) const {
    ++outline_cache_miss_count_;
    if (fast_identity) {
      ++outline_fast_key_count_;
    } else {
      ++outline_full_key_count_;
    }
  }

  [[nodiscard]] int outlineCacheHitCount() const {
    return outline_cache_hit_count_;
  }
  [[nodiscard]] int outlineCacheMissCount() const {
    return outline_cache_miss_count_;
  }
  [[nodiscard]] int outlineFastKeyCount() const {
    return outline_fast_key_count_;
  }
  [[nodiscard]] int outlineFullKeyCount() const {
    return outline_full_key_count_;
  }

  void clearColorToolPreview() const {
    color_tool_preview_key_.clear();
    color_tool_preview_source_.clear();
  }
  [[nodiscard]] bool colorToolPreviewLookup(
      const QString& key,
      QString* source) const {
    if (key != color_tool_preview_key_) {
      return false;
    }
    if (source != nullptr) {
      *source = color_tool_preview_source_;
    }
    return true;
  }
  void storeColorToolPreview(QString key, QString source) const {
    color_tool_preview_key_ = std::move(key);
    color_tool_preview_source_ = std::move(source);
  }
  [[nodiscard]] const QString& colorToolPreviewSource() const {
    return color_tool_preview_source_;
  }

  void clearSmartSelectionPreview() const {
    smart_selection_preview_key_.clear();
    smart_selection_preview_source_.clear();
  }
  [[nodiscard]] bool smartSelectionPreviewLookup(
      const QString& key,
      QString* source) const {
    if (key != smart_selection_preview_key_) {
      return false;
    }
    if (source != nullptr) {
      *source = smart_selection_preview_source_;
    }
    return true;
  }
  void storeSmartSelectionPreview(QString key, QString source) const {
    smart_selection_preview_key_ = std::move(key);
    smart_selection_preview_source_ = std::move(source);
  }
  [[nodiscard]] const QString& smartSelectionPreviewSource() const {
    return smart_selection_preview_source_;
  }

  [[nodiscard]] const QString& previewMode() const { return preview_mode_; }
  [[nodiscard]] bool setPreviewMode(QString preview_mode) {
    if (preview_mode == preview_mode_) {
      return false;
    }
    preview_mode_ = std::move(preview_mode);
    return true;
  }

  [[nodiscard]] const RetryRequest& retryRequest() const { return retry_; }
  [[nodiscard]] bool retryAvailable() const { return !retry_.action.isEmpty(); }

  void setSmartSelectionRetry(
      QString detail,
      QVariantList points,
      QString intent,
      bool create_new) {
    retry_ = RetryRequest{
        .action = QStringLiteral("smart-selection"),
        .label = QStringLiteral("Retry Selection"),
        .detail = std::move(detail),
        .points = std::move(points),
        .intent = std::move(intent),
        .create_new = create_new,
    };
  }

  void setInpaintRetry(QString detail, QString retouch_mode) {
    retry_ = RetryRequest{
        .action = QStringLiteral("inpaint"),
        .label = QStringLiteral("Retry Inpaint"),
        .detail = std::move(detail),
        .retouch_mode = std::move(retouch_mode),
    };
  }

  [[nodiscard]] bool clearRetry() {
    if (!retryAvailable() && retry_.label.isEmpty() && retry_.detail.isEmpty() &&
        retry_.points.isEmpty() && retry_.intent.isEmpty() &&
        retry_.retouch_mode.isEmpty() && !retry_.create_new) {
      return false;
    }
    retry_ = {};
    return true;
  }

  [[nodiscard]] int advanceWorkRevision() { return ++work_revision_; }
  [[nodiscard]] int workRevision() const { return work_revision_; }

  [[nodiscard]] int advanceBrushRasterGeneration() {
    return ++brush_raster_generation_;
  }
  [[nodiscard]] int brushRasterGeneration() const {
    return brush_raster_generation_;
  }

  [[nodiscard]] int advanceSmartSelectionGeneration() {
    return ++smart_selection_generation_;
  }
  [[nodiscard]] int smartSelectionGeneration() const {
    return smart_selection_generation_;
  }

  [[nodiscard]] int advanceSmartPreviewGeneration() {
    return ++smart_preview_generation_;
  }
  [[nodiscard]] int smartPreviewGeneration() const {
    return smart_preview_generation_;
  }

  [[nodiscard]] int advanceInpaintGeneration() {
    inpaint_control_->cancel();
    inpaint_control_ = std::make_shared<AiInpaintControl>();
    return ++inpaint_generation_;
  }
  std::shared_ptr<AiInpaintControl> inpaintControl() const { return inpaint_control_; }
  [[nodiscard]] int inpaintGeneration() const { return inpaint_generation_; }

  void recordBrushRasterCancellation() { ++brush_raster_cancelled_count_; }
  [[nodiscard]] int brushRasterCancelledCount() const {
    return brush_raster_cancelled_count_;
  }

  void recordSmartSelectionCancellation() {
    ++smart_selection_cancelled_count_;
  }
  [[nodiscard]] int smartSelectionCancelledCount() const {
    return smart_selection_cancelled_count_;
  }

  void recordInpaintCancellation() { ++inpaint_cancelled_count_; }
  [[nodiscard]] int inpaintCancelledCount() const {
    return inpaint_cancelled_count_;
  }

  [[nodiscard]] bool isCurrentBrushRaster(int generation) const {
    return generation == brush_raster_generation_;
  }
  [[nodiscard]] bool isCurrentSmartSelection(int generation) const {
    return generation == smart_selection_generation_;
  }
  [[nodiscard]] bool isCurrentSmartPreview(int generation) const {
    return generation == smart_preview_generation_;
  }
  [[nodiscard]] bool isCurrentInpaint(int generation) const {
    return generation == inpaint_generation_;
  }
  [[nodiscard]] bool isCurrentWorkRevision(int revision) const {
    return revision == work_revision_;
  }

 private:
  QString selectionPreferencesPath(const QString& asset) const {
    const auto key = QCryptographicHash::hash(asset.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(selection_storage_directory_).filePath(QString::fromLatin1(key) + QStringLiteral(".json"));
  }
  void rememberActiveComponent() {
    if (!hasActiveMask()) return;
    const auto mask = stack_.at(active_index_).toMap();
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    const auto component_id = mask.value(QStringLiteral("id")).toString();
    if (!set_id.isEmpty() && !component_id.isEmpty() &&
        last_component_by_asset_.value(asset_scope_).value(set_id) != component_id) {
      last_component_by_asset_[asset_scope_].insert(set_id, component_id);
      if (!asset_scope_.isEmpty()) dirty_selection_assets_.insert(asset_scope_);
    }
  }

  QString asset_scope_;
  QString selection_storage_directory_;
  QSet<QString> dirty_selection_assets_;
  QHash<QString, QHash<QString, QString>> last_component_by_asset_;
  [[nodiscard]] int normalizedActiveIndex(int active_index) const {
    return stack_.isEmpty() || active_index < 0
               ? -1
               : qBound(
                     0, active_index, static_cast<int>(stack_.size()) - 1);
  }

  QVariantList stack_;
  int active_index_ = -1;
  mutable bool active_definition_dirty_ = true;
  mutable QVariantMap cached_active_definition_;
  mutable QString preview_cache_key_;
  mutable QString preview_cache_source_;
  mutable QHash<QString, QString> preview_lru_;
  mutable QStringList preview_lru_order_;
  mutable int preview_cache_hit_count_ = 0;
  mutable int preview_cache_miss_count_ = 0;
  mutable int preview_cache_eviction_count_ = 0;
  mutable QString outline_cache_key_;
  mutable QVariantList outline_cache_paths_;
  mutable QVariantMap outline_cache_trace_fields_;
  mutable int outline_cache_hit_count_ = 0;
  mutable int outline_cache_miss_count_ = 0;
  mutable int outline_fast_key_count_ = 0;
  mutable int outline_full_key_count_ = 0;
  mutable QString color_tool_preview_key_;
  mutable QString color_tool_preview_source_;
  mutable QString smart_selection_preview_key_;
  mutable QString smart_selection_preview_source_;
  QString preview_mode_ = QStringLiteral("overlay");
  RetryRequest retry_;
  int work_revision_ = 0;
  int brush_raster_generation_ = 0;
  int smart_selection_generation_ = 0;
  int smart_preview_generation_ = 0;
  int inpaint_generation_ = 0;
  std::shared_ptr<AiInpaintControl> inpaint_control_ = std::make_shared<AiInpaintControl>();
  int brush_raster_cancelled_count_ = 0;
  int smart_selection_cancelled_count_ = 0;
  int inpaint_cancelled_count_ = 0;
};

}  // namespace photo_editor::desktop
