#include "thumbnail_presentation_service.h"

#include <cstdlib>
#include <iostream>

#include <QByteArray>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QString mask_key(
    const QString& asset_id,
    const QString& state_signature,
    const QString& mask_id,
    int revision) {
  using namespace photo_editor::desktop;
  return thumbnail_entry_cache_key(
      asset_id,
      state_signature,
      QString(),
      mask_id,
      kMaskThumbnailBackingEdge,
      kMaskThumbnailBackingEdge,
      static_cast<double>(kMaskThumbnailBackingEdge) /
          static_cast<double>(kMaskSetThumbnailDisplayEdge),
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      revision);
}

QString layer_key(
    const QString& asset_id,
    const QVariantMap& layer,
    const QString& layer_id,
    int revision) {
  using namespace photo_editor::desktop;
  return thumbnail_entry_cache_key(
      asset_id,
      photo_layer_thumbnail_entry_recipe_hash(layer),
      layer_id,
      QString(),
      kLayerThumbnailEdge,
      kLayerThumbnailEdge,
      kLayerThumbnailDevicePixelRatio,
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      revision);
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;
  ThumbnailPresentationService service;
  ThumbnailCacheSession session;
  const auto grouped_sets = service.groupMaskSets(
      QVariantList{
          QVariantMap{
              {QStringLiteral("id"), QStringLiteral("mask-a")},
              {QStringLiteral("name"), QStringLiteral("Parent")},
              {QStringLiteral("toolRole"), QStringLiteral("selection")},
              {QStringLiteral("maskSetId"), QStringLiteral("set-a")},
              {QStringLiteral("maskSetName"), QStringLiteral("Portrait")},
              {QStringLiteral("kind"), QStringLiteral("brush")},
              {QStringLiteral("brushPoints"), QVariantList{QVariantMap{}}},
              {QStringLiteral("operation"), QStringLiteral("add")},
              {QStringLiteral("enabled"), false},
              {QStringLiteral("maskSetPreviewBypassed"), true},
              {QStringLiteral("reusableOutput"), true},
              {QStringLiteral("outputId"), QStringLiteral("matte-a")},
          },
          QVariantMap{
              {QStringLiteral("id"), QStringLiteral("mask-b")},
              {QStringLiteral("name"), QStringLiteral("Child")},
              {QStringLiteral("toolRole"), QStringLiteral("selection")},
              {QStringLiteral("maskSetId"), QStringLiteral("set-a")},
              {QStringLiteral("maskSetName"), QStringLiteral("Portrait")},
              {QStringLiteral("kind"), QStringLiteral("subject")},
              {QStringLiteral("segmentationData"), QByteArray(1, static_cast<char>(255))},
              {QStringLiteral("segmentationWidth"), 1},
              {QStringLiteral("segmentationHeight"), 1},
              {QStringLiteral("operation"), QStringLiteral("subtract")},
              {QStringLiteral("enabled"), true},
              {QStringLiteral("solo"), true},
              {QStringLiteral("invert"), true},
              {QStringLiteral("parentId"), QStringLiteral("mask-a")},
              {QStringLiteral("generatedMaskStale"), true},
              {QStringLiteral("generatedMaskMetadata"),
               QVariantMap{
                   {QStringLiteral("modelName"), QStringLiteral("Vision")},
                   {QStringLiteral("promptType"), QStringLiteral("subject")},
                   {QStringLiteral("sourceResolution"), QStringLiteral("640x480")},
               }},
          },
          QVariantMap{
              {QStringLiteral("id"), QStringLiteral("retouch")},
              {QStringLiteral("toolRole"), QStringLiteral("retouch")},
          },
      });
  const auto grouped_set =
      grouped_sets.size() == 1 ? grouped_sets.constFirst().toMap() : QVariantMap{};
  const auto grouped_sources = grouped_set.value(QStringLiteral("sources")).toList();
  const auto first_grouped_source =
      grouped_sources.size() == 2 ? grouped_sources.at(0).toMap() : QVariantMap{};
  const auto second_grouped_source =
      grouped_sources.size() == 2 ? grouped_sources.at(1).toMap() : QVariantMap{};
  if (grouped_sets.size() != 1 || grouped_sources.size() != 2 ||
      grouped_set.value(QStringLiteral("sourceCount")).toInt() != 2 ||
      grouped_set.value(QStringLiteral("primarySourceKind")).toString() !=
          QStringLiteral("composite") ||
      grouped_set.value(QStringLiteral("sourceKindLabel")).toString() !=
          QStringLiteral("Composite") ||
      !grouped_set.value(QStringLiteral("enabled")).toBool() ||
      !grouped_set.value(QStringLiteral("solo")).toBool() ||
      grouped_set.value(QStringLiteral("maskSetPreviewBypassed")).toBool() ||
      grouped_set.value(QStringLiteral("combineModes")).toList() !=
          QVariantList{QStringLiteral("add"), QStringLiteral("subtract")} ||
      !grouped_set.value(QStringLiteral("combineModeSummary"))
           .toString()
           .contains(QStringLiteral("Inverted")) ||
      !first_grouped_source.value(
          QStringLiteral("mask_alpha_full_resolution_available")).toBool() ||
      !first_grouped_source.value(QStringLiteral("mask_alpha_trace_deferred")).toBool() ||
      !first_grouped_source.value(QStringLiteral("dependencyLabel"))
           .toString()
           .contains(QStringLiteral("Reusable output: matte-a")) ||
      !second_grouped_source.value(QStringLiteral("dependencyLabel"))
           .toString()
           .contains(QStringLiteral("Depends on Parent")) ||
      !second_grouped_source.value(QStringLiteral("dependencyLabel"))
           .toString()
           .contains(QStringLiteral("Generated: Vision · subject · 640x480")) ||
      !second_grouped_source.value(QStringLiteral("sourceAuditLabel"))
           .toString()
           .contains(QStringLiteral("stale")) ||
      !second_grouped_source.value(QStringLiteral("sourceAuditLabel"))
           .toString()
           .contains(QStringLiteral("inverted")) ||
      !service.groupMaskSets({}).isEmpty()) {
    return fail("mask-set grouping must preserve source order, audit metadata, combination policy, and dependency provenance");
  }
  const auto retouch_stack = service.groupRetouchStack(QVariantList{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("retouch-a")},
          {QStringLiteral("toolRole"), QStringLiteral("retouch")},
          {QStringLiteral("kind"), QStringLiteral("brush")},
          {QStringLiteral("retouchMode"), QStringLiteral("heal")},
          {QStringLiteral("retouchOrder"), 4},
          {QStringLiteral("committed"), true},
          {QStringLiteral("enabled"), false},
          {QStringLiteral("brushPoints"),
           QVariantList{QVariantMap{}, QVariantMap{}}},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("selection-a")},
          {QStringLiteral("toolRole"), QStringLiteral("selection")},
      },
  });
  const auto retouch =
      retouch_stack.size() == 1 ? retouch_stack.constFirst().toMap() : QVariantMap{};
  if (retouch_stack.size() != 1 ||
      retouch.value(QStringLiteral("retouchOrder")).toInt() != 4 ||
      retouch.value(QStringLiteral("retouchOrderLabel")).toString() !=
          QStringLiteral("#4") ||
      retouch.value(QStringLiteral("retouchModeLabel")).toString() !=
          QStringLiteral("Heal") ||
      retouch.value(QStringLiteral("retouchStatus")).toString() !=
          QStringLiteral("applied") ||
      retouch.value(QStringLiteral("retouchPointCount")).toInt() != 2 ||
      !retouch.value(QStringLiteral("retouchAuditLabel"))
           .toString()
           .contains(QStringLiteral("off · 2 pts"))) {
    return fail(
        "retouch grouping must reject selections and publish canonical order, mode, status, and audit facts");
  }
  auto scheduled_count = 0;
  auto warning_count = 0;
  auto contained_failure_count = 0;
  const auto dependencies = ThumbnailPresentationDependencies{
      .schedule_refresh = [&scheduled_count](const QString&) {
        ++scheduled_count;
        return QVariantMap{{QStringLiteral("thumbnailQueuePending"), true}};
      },
      .consume_stale_warning = [](const QString&) { return true; },
      .log_stale_warning = [&warning_count](const QString&, const QVariantMap&) {
        ++warning_count;
      },
      .log_contained_failure =
          [&contained_failure_count](
              const QString&,
              const ThumbnailRefreshQueueCompletion&) {
            ++contained_failure_count;
          },
  };
  const auto facts = ThumbnailPresentationFacts{
      .asset_id = QStringLiteral("asset"),
      .file_path = QStringLiteral("/tmp/image.raw"),
      .state_signature = QStringLiteral("state-a"),
      .mask_revision = 4,
      .queue_fields = QVariantMap{{QStringLiteral("thumbnailQueuePending"), false}},
      .queue_completion = ThumbnailRefreshQueueCompletion{
          .job_id = QStringLiteral("worker-job"),
      },
      .has_asset = true,
  };

  const QVariantList mask_sets{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("set-a")},
          {QStringLiteral("mask_set_id"), QStringLiteral("set-a")},
          {QStringLiteral("sources"),
           QVariantList{QVariantMap{
               {QStringLiteral("id"), QStringLiteral("mask-a")},
               {QStringLiteral("mask_source_id"), QStringLiteral("mask-a")},
           }}},
      },
  };
  session.storeEntry(
      mask_key(QStringLiteral("asset"), QStringLiteral("state-a"), QStringLiteral("mask-a"), 4),
      QVariantMap{
          {QStringLiteral("thumbnailSource"), QStringLiteral("source-thumb")},
          {QStringLiteral("thumbnailReady"), true},
          {QStringLiteral("thumbnailCacheKey"), QStringLiteral("source-cache")},
      },
      32,
      1024 * 1024);
  session.storeEntry(
      mask_key(QStringLiteral("asset"), QStringLiteral("state-a"), QStringLiteral("set-a"), 4),
      QVariantMap{
          {QStringLiteral("thumbnailSource"), QStringLiteral("set-thumb")},
          {QStringLiteral("thumbnailReady"), true},
          {QStringLiteral("thumbnailCacheKey"), QStringLiteral("set-cache")},
      },
      32,
      1024 * 1024);
  const auto presented_masks = service.presentMaskSets(
      mask_sets, facts, dependencies, &session);
  const auto presented_set = presented_masks.constFirst().toMap();
  const auto presented_source =
      presented_set.value(QStringLiteral("sources")).toList().constFirst().toMap();
  if (session.maskDirty() || session.maskRefreshCount() != 1 ||
      scheduled_count != 0 ||
      presented_set.value(QStringLiteral("thumbnailSource")).toString() !=
          QStringLiteral("set-thumb") ||
      presented_source.value(QStringLiteral("thumbnailSource")).toString() !=
          QStringLiteral("source-thumb") ||
      !presented_set.value(QStringLiteral("thumbnailCacheHit")).toBool() ||
      presented_set.value(QStringLiteral("thumbnailRenderQueuedWorkerJobId")).toString() !=
          QStringLiteral("worker-job")) {
    return fail("fresh mask entries must publish synchronously from cache without rendering");
  }

  const QVariantList layers{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("layer-a")},
          {QStringLiteral("kind"), QStringLiteral("adjustment")},
      },
  };
  session.photoLayerDirty() = true;
  session.storeEntry(
      layer_key(
          QStringLiteral("asset"),
          layers.constFirst().toMap(),
          QStringLiteral("layer-a"),
          4),
      QVariantMap{
          {QStringLiteral("thumbnailSource"), QStringLiteral("layer-thumb")},
          {QStringLiteral("thumbnailReady"), true},
          {QStringLiteral("thumbnailCacheKey"), QStringLiteral("layer-cache")},
      },
      32,
      1024 * 1024);
  const auto presented_layers = service.presentPhotoLayers(
      layers, facts, dependencies, &session);
  if (session.photoLayerDirty() || session.photoLayerRefreshCount() != 1 ||
      scheduled_count != 0 ||
      presented_layers.constFirst().toMap()
              .value(QStringLiteral("thumbnailSource"))
              .toString() != QStringLiteral("layer-thumb")) {
    return fail("fresh layer entries must publish synchronously from the shared entry cache");
  }

  session.photoLayerDirty() = true;
  auto stale_facts = facts;
  stale_facts.state_signature = QStringLiteral("state-b");
  stale_facts.mask_revision = 5;
  const auto progressive_layers = service.presentPhotoLayers(
      layers, stale_facts, dependencies, &session);
  const auto progressive_layer = progressive_layers.constFirst().toMap();
  if (!session.photoLayerDirty() || session.photoLayerDeferredCount() != 1 ||
      scheduled_count != 1 ||
      progressive_layer.value(QStringLiteral("thumbnailSource")).toString() !=
          QStringLiteral("layer-thumb") ||
      !progressive_layer.value(QStringLiteral("thumbnailStale")).toBool() ||
      !progressive_layer.value(QStringLiteral("thumbnailProgressive")).toBool() ||
      progressive_layer.value(QStringLiteral("thumbnailProgressiveReason")).toString() !=
          QStringLiteral("photo_layer_thumbnail_refresh_deferred")) {
    return fail("cache misses must schedule async work and retain the last good progressive image");
  }
  if (warning_count != 0) {
    return fail("a stale warning requires a prior recipe identity mismatch, not only a cache miss");
  }

  const auto scheduled_before_clean_read = scheduled_count;
  const auto clean_hit_count_before = session.photoLayerHitCount();
  session.photoLayerDirty() = false;
  session.photoLayerAssetKey() = QStringLiteral("asset|/tmp/image.raw");
  const auto clean_layers = service.presentPhotoLayers(
      layers, facts, dependencies, &session);
  if (scheduled_count != scheduled_before_clean_read ||
      session.photoLayerDirty() ||
      session.photoLayerHitCount() != clean_hit_count_before + 1 ||
      clean_layers.constFirst().toMap()
              .value(QStringLiteral("thumbnailSource"))
              .toString() != QStringLiteral("layer-thumb")) {
    return fail("clean layer presentation reads must reuse derived thumbnails without queue work");
  }

  session.photoLayerDirty() = true;
  auto mismatched_entry = session.photoLayersWithThumbnails().constFirst().toMap();
  mismatched_entry.insert(
      QStringLiteral("thumbnailInvalidationRecipeHash"),
      QStringLiteral("recipe:older-layer-a"));
  session.photoLayersWithThumbnails() = QVariantList{mismatched_entry};
  static_cast<void>(service.presentPhotoLayers(
      layers, stale_facts, dependencies, &session));
  if (warning_count != 1) {
    return fail("a last-good thumbnail with a different recipe identity must log one stale warning");
  }

  auto failure_facts = stale_facts;
  failure_facts.queue_completion = ThumbnailRefreshQueueCompletion{
      .job_id = QStringLiteral("failed-worker-job"),
      .state_signature = QStringLiteral("state-b"),
      .failure_stage = QStringLiteral("thumbnail_source_decode"),
      .failure_detail = QStringLiteral("decode failed"),
      .state_revision = 5,
      .photo_layer_thumbnail_failed = true,
      .success = false,
  };
  const auto scheduled_before_failure = scheduled_count;
  session.photoLayerDirty() = true;
  const auto failed_layers = service.presentPhotoLayers(
      layers, failure_facts, dependencies, &session);
  const auto failed_layer = failed_layers.constFirst().toMap();
  if (scheduled_count != scheduled_before_failure ||
      contained_failure_count != 1 ||
      !failed_layer.value(QStringLiteral("thumbnailFailureContained")).toBool() ||
      failed_layer.value(QStringLiteral("thumbnailFailureStage")).toString() !=
          QStringLiteral("thumbnail_source_decode") ||
      !failed_layer.value(QStringLiteral("thumbnailFailureSignature"))
           .toString()
           .contains(QStringLiteral("reused:true"))) {
    return fail("completed worker failures must retain last-good rows without scheduling a retry loop");
  }

  return EXIT_SUCCESS;
}
