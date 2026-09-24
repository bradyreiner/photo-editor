#include "thumbnail_refresh_queue_service.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QImage>
#include <QStringList>
#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor::desktop;

  const auto baseline_signature =
      thumbnail_state_signature(QStringLiteral("asset-a"), 2, 3, 4);
  if (baseline_signature.isEmpty() ||
      baseline_signature !=
          thumbnail_state_signature(QStringLiteral("asset-a"), 2, 3, 4) ||
      baseline_signature ==
          thumbnail_state_signature(QStringLiteral("asset-b"), 2, 3, 4) ||
      baseline_signature ==
          thumbnail_state_signature(QStringLiteral("asset-a"), 5, 3, 4) ||
      baseline_signature ==
          thumbnail_state_signature(QStringLiteral("asset-a"), 2, 6, 4) ||
      baseline_signature ==
          thumbnail_state_signature(QStringLiteral("asset-a"), 2, 3, 7)) {
    return fail("thumbnail identity must be deterministic and cover every revision fact");
  }

  EditRecipeState state;
  state.exposure = 1.25;
  QImage published_preview(2, 2, QImage::Format_ARGB32);
  published_preview.fill(QColor(12, 34, 56));
  QVariantList photo_layers{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("layer-a")}}};
  QVariantList mask_stack{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("mask-a")}}};
  QVariantList mask_sets{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("set-a")}}};
  const auto cancellation = std::make_shared<std::atomic_bool>(false);

  auto request = make_thumbnail_refresh_queue_request(
      ThumbnailRefreshQueueRequestFacts{
          .asset_id = QStringLiteral("asset-a"),
          .file_path = QStringLiteral("/tmp/source.raw"),
          .reason = QStringLiteral("mask-edit"),
          .queue_signature = QStringLiteral("queue-a"),
          .state = state,
          .published_preview_image = published_preview,
          .published_preview_state_signature = QStringLiteral("preview-a"),
          .photo_layers = photo_layers,
          .mask_stack = mask_stack,
          .mask_sets = mask_sets,
          .generation = 7,
          .edit_intent_revision = 2,
          .mask_revision = 3,
          .photo_layer_revision = 4,
          .mask_thumbnail_dirty = true,
          .photo_layer_thumbnail_dirty = true,
          .cancellation = cancellation,
      });

  state.exposure = -4.0;
  published_preview.fill(Qt::black);
  photo_layers[0] = QVariantMap{{QStringLiteral("id"), QStringLiteral("mutated")}};
  mask_stack.clear();
  mask_sets.clear();

  if (request.job.job_id != QStringLiteral("thumbnail-refresh-queue-7") ||
      request.job.job_kind != QStringLiteral("thumbnail_refresh") ||
      request.job.stage != QStringLiteral("thumbnail_refresh_queue") ||
      request.job.asset_id != QStringLiteral("asset-a") ||
      request.job.file_path != QStringLiteral("/tmp/source.raw") ||
      request.job.state_signature != baseline_signature ||
      request.job.generation != 7 || request.job.state_revision != 3 ||
      request.reason != QStringLiteral("mask-edit") ||
      request.queue_signature != QStringLiteral("queue-a") ||
      request.state.exposure != 1.25 ||
      request.published_preview_image.pixelColor(0, 0) != QColor(12, 34, 56) ||
      request.photo_layers.size() != 1 || request.mask_stack.size() != 1 ||
      request.mask_sets.size() != 1 || !request.mask_thumbnail_dirty ||
      !request.photo_layer_thumbnail_dirty) {
    return fail("request factory must own the complete immutable worker snapshot and identity");
  }

  if (thumbnail_refresh_cancelled(request)) {
    return fail("fresh request cancellation must be clear");
  }
  cancellation->store(true, std::memory_order_relaxed);
  if (!thumbnail_refresh_cancelled(request)) {
    return fail("request cancellation must observe its shared atomic token");
  }

  ThumbnailRefreshQueueResult result;
  result.photo_layer_thumbnail_entries.insert(
      QStringLiteral("photo-only"),
      QVariantMap{{QStringLiteral("owner"), QStringLiteral("photo")}});
  result.photo_layer_thumbnail_entries.insert(
      QStringLiteral("shared"),
      QVariantMap{{QStringLiteral("owner"), QStringLiteral("photo")}});
  result.mask_thumbnail_entries.insert(
      QStringLiteral("mask-only"),
      QVariantMap{{QStringLiteral("owner"), QStringLiteral("mask")}});
  result.mask_thumbnail_entries.insert(
      QStringLiteral("shared"),
      QVariantMap{{QStringLiteral("owner"), QStringLiteral("mask")}});

  QStringList publication_order;
  QVariantMap published_entries;
  const auto counts = publish_thumbnail_refresh_queue_entries(
      result,
      [&publication_order, &published_entries](
          const QString& cache_key,
          const QVariantMap& entry) {
        publication_order.append(
            entry.value(QStringLiteral("owner")).toString() +
            QStringLiteral(":") + cache_key);
        published_entries.insert(cache_key, entry);
      });
  if (counts.photo_layer_entries != 2 || counts.mask_entries != 2 ||
      counts.total_entries != 4 || publication_order.size() != 4 ||
      !publication_order[0].startsWith(QStringLiteral("photo:")) ||
      !publication_order[1].startsWith(QStringLiteral("photo:")) ||
      !publication_order[2].startsWith(QStringLiteral("mask:")) ||
      !publication_order[3].startsWith(QStringLiteral("mask:")) ||
      published_entries.value(QStringLiteral("shared"))
              .toMap()
              .value(QStringLiteral("owner"))
              .toString() != QStringLiteral("mask")) {
    return fail("publication must retain photo-before-mask ordering and exact counts");
  }

  const auto empty_counts = publish_thumbnail_refresh_queue_entries(result, {});
  if (empty_counts.total_entries != 0 || empty_counts.photo_layer_entries != 0 ||
      empty_counts.mask_entries != 0) {
    return fail("publication without a cache callback must be a safe no-op");
  }

  return EXIT_SUCCESS;
}
