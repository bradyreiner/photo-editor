#pragma once

#include <functional>

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "thumbnail_cache_session.h"
#include "thumbnail_refresh_queue_service.h"

namespace photo_editor::desktop {

struct ThumbnailPresentationFacts final {
  QString asset_id;
  QString file_path;
  QString state_signature;
  int mask_revision = 0;
  QVariantMap queue_fields;
  ThumbnailRefreshQueueCompletion queue_completion;
  bool has_asset = false;
};

struct ThumbnailPresentationDependencies final {
  std::function<QVariantMap(const QString& reason)> schedule_refresh;
  std::function<bool(const QString& reason)> consume_stale_warning;
  std::function<void(const QString& reason, const QVariantMap& fields)>
      log_stale_warning;
  std::function<void(
      const QString& domain,
      const ThumbnailRefreshQueueCompletion& completion)> log_contained_failure;
};

class ThumbnailPresentationService final {
 public:
  [[nodiscard]] QVariantList groupMaskSets(
      const QVariantList& masks) const;
  [[nodiscard]] QVariantList groupRetouchStack(
      const QVariantList& masks) const;

  [[nodiscard]] QVariantList presentMaskSets(
      const QVariantList& current_sets,
      const ThumbnailPresentationFacts& facts,
      const ThumbnailPresentationDependencies& dependencies,
      ThumbnailCacheSession* session) const;

  [[nodiscard]] QVariantList presentPhotoLayers(
      const QVariantList& current_layers,
      const ThumbnailPresentationFacts& facts,
      const ThumbnailPresentationDependencies& dependencies,
      ThumbnailCacheSession* session) const;

 private:
  static void copyThumbnailFields(const QVariantMap& source, QVariantMap* target);
  static bool insertProgressiveFields(
      QVariantMap* target,
      const QString& reason,
      const QString& current_recipe_hash,
      const QVariantMap& queue_fields,
      bool log_stale_warning,
      const ThumbnailPresentationDependencies& dependencies);
  static QVariantList reuseMaskThumbnails(
      const QVariantList& current_sets,
      const QVariantList& cached_sets,
      const QString& reason,
      const ThumbnailPresentationFacts& facts,
      const ThumbnailPresentationDependencies& dependencies,
      bool log_stale_warning);
  static QVariantList reusePhotoLayerThumbnails(
      const QVariantList& current_layers,
      const QVariantList& cached_layers,
      const QString& reason,
      const ThumbnailPresentationFacts& facts,
      const ThumbnailPresentationDependencies& dependencies,
      bool log_stale_warning);
};

}  // namespace photo_editor::desktop
