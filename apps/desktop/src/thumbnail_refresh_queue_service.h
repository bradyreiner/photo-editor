#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>

#include <QImage>
#include <QSize>
#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "async_mask_job.h"
#include "edit_recipe_state.h"

namespace photo_editor::desktop {

inline constexpr auto kThumbnailRefreshQueueWorkerServiceContract =
    "thumbnail-refresh-queue-worker-service|cancellable|entry-cache:v1";
inline constexpr auto kThumbnailRefreshQueueWorkerEntryContract =
    "thumbnail-refresh-queue-worker-entry:v1";
inline constexpr auto kLayerThumbnailServiceOwnershipContract =
    "layer-thumbnail-service:v1|requests:ThumbnailRefreshQueueRequest|cancellation:atomic|cache:thumbnail-entry-cache|status:thumbnailRefreshQueueFields|invalidation:thumbnail_invalidation_key|independent-main-preview:true";

struct ThumbnailRefreshQueueRequest final {
  AsyncMaskJob job;
  QString reason;
  QString queue_signature;
  EditRecipeState state;
  QVariantList photo_layers;
  QVariantList mask_stack;
  QVariantList mask_sets;
  bool mask_thumbnail_dirty = false;
  bool photo_layer_thumbnail_dirty = false;
  std::shared_ptr<std::atomic_bool> cancellation;
};

struct ThumbnailRefreshQueueResult final {
  ThumbnailRefreshQueueRequest request;
  QVariantMap photo_layer_thumbnail_entries;
  QVariantMap mask_thumbnail_entries;
  QString worker_thread;
  qint64 elapsed_ms = 0;
  int photo_layer_thumbnail_entry_count = 0;
  int mask_thumbnail_entry_count = 0;
  QString service_contract;
  bool cancelled = false;
  bool success = false;
};

struct ThumbnailRefreshQueueCompletion final {
  QString job_id;
  QString state_signature;
  QString queue_signature;
  QString reason;
  bool mask_thumbnail_dirty = false;
  bool photo_layer_thumbnail_dirty = false;
  qint64 elapsed_ms = 0;
  int photo_layer_thumbnail_entry_count = 0;
  int mask_thumbnail_entry_count = 0;
  QString service_contract;
};

inline constexpr int kLayerThumbnailDisplayEdge = 32;
inline constexpr double kLayerThumbnailDevicePixelRatio = 2.0;
inline constexpr int kLayerThumbnailEdge =
    static_cast<int>(kLayerThumbnailDisplayEdge * kLayerThumbnailDevicePixelRatio);
inline constexpr int kMaskSetThumbnailDisplayEdge = 34;
inline constexpr int kMaskThumbnailBackingEdge = 96;
inline constexpr auto kThumbnailColorPipelineVersion = "thumbnail-color-pipeline-v1";

struct ThumbnailRefreshQueueAdjustmentLayerDependencies final {
  std::function<QString(const QVariantMap& layer)> recipe_hash;
  std::function<QString(const QVariantMap& layer)> entry_recipe_hash;
  std::function<QString(
      const QVariantMap& layer,
      const QImage& cropped_image,
      const ThumbnailRefreshQueueRequest& request,
      QVariantMap* metadata)> thumbnail_source;
};

struct ThumbnailRefreshQueueMaskThumbnailDependencies final {
  std::function<QVariantMap(
      const QVariantMap& source,
      const QSize& target_size,
      const QString& purpose)> scaled_mask_source;
  std::function<QVariantMap(
      const QImage& thumbnail_image,
      const QVariantList& masks,
      double exposure_gain,
      const std::array<double, 3>& white_balance)> thumbnail_payload;
  std::function<QVariantMap(const QVariantMap& mask)> thumbnail_metadata;
  std::function<void(
      QVariantMap* target,
      const QVariantMap& mask,
      const QString& source_revision,
      const QSize& raster_size)> insert_source_alpha_trace_fields;
  std::function<QVariantMap(
      const QString& trace_source,
      const QVariantList& component_masks,
      const QString& set_id,
      const QString& source_revision,
      const QSize& raster_size,
      int state_revision)> composite_alpha_trace_fields;
};

struct ThumbnailRefreshQueueRenderers final {
  ThumbnailRefreshQueueAdjustmentLayerDependencies adjustment_layer_dependencies;
  std::function<QImage(const ThumbnailRefreshQueueRequest& request)>
      adjustment_thumbnail_source_image;
  std::function<QVariantMap(const ThumbnailRefreshQueueRequest& request)>
      mask_thumbnail_entries;
};

inline QString thumbnail_refresh_queue_worker_service_contract() {
  return QString::fromLatin1(kThumbnailRefreshQueueWorkerServiceContract);
}

inline QString thumbnail_refresh_queue_worker_entry_contract() {
  return QString::fromLatin1(kThumbnailRefreshQueueWorkerEntryContract);
}

inline QString layer_thumbnail_service_ownership_contract() {
  return QString::fromLatin1(kLayerThumbnailServiceOwnershipContract);
}

inline bool thumbnail_refresh_cancelled(const ThumbnailRefreshQueueRequest& request) {
  return request.cancellation != nullptr &&
         request.cancellation->load(std::memory_order_relaxed);
}

inline void insert_thumbnail_queue_worker_entry_fields(
    QVariantMap* entry,
    const QString& job_id,
    const QString& source,
    qint64 elapsed_ms) {
  if (entry == nullptr) {
    return;
  }
  entry->insert(QStringLiteral("thumbnailQueueWorkerRendered"), true);
  entry->insert(QStringLiteral("thumbnailQueueWorkerRenderJobId"), job_id);
  entry->insert(QStringLiteral("thumbnailQueueWorkerRenderSource"), source);
  entry->insert(QStringLiteral("thumbnailQueueWorkerRenderElapsedMs"), elapsed_ms);
  entry->insert(
      QStringLiteral("thumbnailQueueWorkerRenderContract"),
      thumbnail_refresh_queue_worker_entry_contract());
  entry->insert(
      QStringLiteral("thumbnailQueueWorkerServiceContract"),
      thumbnail_refresh_queue_worker_service_contract());
  entry->insert(
      QStringLiteral("layerThumbnailServiceOwnershipContract"),
      layer_thumbnail_service_ownership_contract());
}

inline void insert_thumbnail_queue_completion_fields(
    QVariantMap* fields,
    const ThumbnailRefreshQueueCompletion& completion) {
  if (fields == nullptr || completion.job_id.isEmpty()) {
    return;
  }
  fields->insert(QStringLiteral("thumbnailRenderQueuedWorkerJobId"), completion.job_id);
  fields->insert(QStringLiteral("thumbnailRenderQueuedWorkerStateSignature"), completion.state_signature);
  fields->insert(QStringLiteral("thumbnailRenderQueuedWorkerQueueSignature"), completion.queue_signature);
  fields->insert(QStringLiteral("thumbnailRenderQueuedWorkerReason"), completion.reason);
  fields->insert(QStringLiteral("thumbnailRenderQueuedWorkerElapsedMs"), completion.elapsed_ms);
  fields->insert(
      QStringLiteral("thumbnailRenderQueuedWorkerEntryCount"),
      completion.photo_layer_thumbnail_entry_count);
  fields->insert(
      QStringLiteral("thumbnailRenderQueuedWorkerMaskEntryCount"),
      completion.mask_thumbnail_entry_count);
  fields->insert(
      QStringLiteral("thumbnailRenderQueuedWorkerMaskDirty"),
      completion.mask_thumbnail_dirty);
  fields->insert(
      QStringLiteral("thumbnailRenderQueuedWorkerLayerDirty"),
      completion.photo_layer_thumbnail_dirty);
  fields->insert(
      QStringLiteral("thumbnailRenderQueuedWorkerContract"),
      QStringLiteral("thumbnail-render-consumed-queue-worker:v1"));
  fields->insert(
      QStringLiteral("thumbnailRenderQueuedWorkerServiceContract"),
      completion.service_contract);
  fields->insert(
      QStringLiteral("layerThumbnailServiceOwnershipContract"),
      layer_thumbnail_service_ownership_contract());
}

void insert_thumbnail_pixel_metrics(
    QVariantMap* target,
    const QSize& backing_size,
    const QSize& display_size);

QString thumbnail_invalidation_key(
    const QString& asset_id,
    const QString& layer_id,
    const QString& mask_id,
    const QString& recipe_hash,
    const QString& mask_alpha_hash,
    const QString& blend_mode,
    double opacity,
    bool visible);

void insert_thumbnail_invalidation_fields(
    QVariantMap* target,
    const QString& asset_id,
    const QString& layer_id,
    const QString& mask_id,
    const QString& recipe_hash,
    const QString& mask_alpha_hash,
    const QString& blend_mode,
    double opacity,
    bool visible);

QString thumbnail_entry_cache_key(
    const QString& asset_id,
    const QString& recipe_hash,
    const QString& layer_id,
    const QString& mask_id,
    int backing_width,
    int backing_height,
    double device_pixel_ratio,
    const QString& color_pipeline_version,
    int mask_revision);

void insert_thumbnail_entry_cache_fields(
    QVariantMap* target,
    const QString& cache_key,
    const QString& asset_id,
    const QString& recipe_hash,
    const QString& layer_id,
    const QString& mask_id,
    int backing_width,
    int backing_height,
    double device_pixel_ratio,
    const QString& color_pipeline_version,
    int mask_revision,
    bool hit,
    const QString& source);

void clear_thumbnail_transient_fields(QVariantMap* target);

QString encoded_layer_thumbnail_source(const QImage& image);
QImage scaled_square_thumbnail_image(QImage image, int edge);
QImage scaled_layer_thumbnail_image(QImage image);
QString rendered_photo_layer_thumbnail_source(const QVariantMap& layer);
QString photo_layer_thumbnail_kind(const QVariantMap& layer);
QString non_adjustment_photo_layer_thumbnail_recipe_hash(const QVariantMap& layer);
QString non_adjustment_photo_layer_thumbnail_entry_recipe_hash(const QVariantMap& layer);
QStringList selection_mask_set_ids_for_layer(const QVariantMap& layer);
QStringList selection_mask_set_names_for_layer(const QVariantMap& layer);
QString selection_mask_set_identity_signature(const QVariantMap& layer);

ThumbnailRefreshQueueResult compute_thumbnail_refresh_queue_result(
    ThumbnailRefreshQueueRequest request,
    const ThumbnailRefreshQueueRenderers& renderers);

bool photo_layer_thumbnail_queue_worker_supported(const QVariantMap& layer);

QVariantMap rendered_adjustment_photo_layer_thumbnail_entry(
    const QVariantMap& source_layer,
    const QImage& cropped_image,
    const ThumbnailRefreshQueueRequest& request,
    const ThumbnailRefreshQueueAdjustmentLayerDependencies& dependencies);

QVariantMap rendered_non_adjustment_photo_layer_thumbnail_entry(
    const QVariantMap& source_layer,
    const ThumbnailRefreshQueueRequest& request);

QVariantMap rendered_mask_source_thumbnail_entry(
    const QVariantMap& source,
    const QImage& thumbnail_image,
    const QString& thumbnail_source_revision,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    const ThumbnailRefreshQueueRequest& request,
    const ThumbnailRefreshQueueMaskThumbnailDependencies& dependencies);

QVariantMap rendered_mask_set_thumbnail_entry(
    const QVariantMap& set,
    const QVariantList& thumbnail_masks,
    const QImage& thumbnail_image,
    const QString& thumbnail_source_revision,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    const ThumbnailRefreshQueueRequest& request,
    const ThumbnailRefreshQueueMaskThumbnailDependencies& dependencies);

}  // namespace photo_editor::desktop
