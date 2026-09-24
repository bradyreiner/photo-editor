#include "thumbnail_cache_session.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  photo_editor::desktop::ThumbnailCacheSession session;
  if (!session.maskDirty() || !session.photoLayerDirty() ||
      session.photoLayerRevision() != 0 || session.entryCount() != 0) {
    return fail("new thumbnail sessions must start dirty with an empty bounded cache");
  }

  session.maskDirty() = false;
  session.photoLayerDirty() = false;
  session.maskSetsWithThumbnails().append(
      QVariantMap{{QStringLiteral("id"), QStringLiteral("mask")}});
  session.photoLayersWithThumbnails().append(
      QVariantMap{{QStringLiteral("id"), QStringLiteral("layer")}});
  session.maskSetsDeferredKey() = QStringLiteral("mask-key");
  session.photoLayerAssetKey() = QStringLiteral("layer-key");
  ++session.photoLayerRevision();
  if (session.maskDirty() || session.photoLayerDirty() ||
      session.maskSetsWithThumbnails().size() != 1 ||
      session.photoLayersWithThumbnails().size() != 1 ||
      session.maskSetsDeferredKey() != QStringLiteral("mask-key") ||
      session.photoLayerAssetKey() != QStringLiteral("layer-key") ||
      session.photoLayerRevision() != 1) {
    return fail("derived thumbnail cache identity must be session-owned and mutable atomically");
  }

  QVariantMap entry;
  if (session.entryLookup(QStringLiteral("missing"), &entry) ||
      session.entryMissCount() != 1) {
    return fail("missing entry lookup must increment miss telemetry");
  }

  session.storeEntry(
      QStringLiteral("a"),
      QVariantMap{{QStringLiteral("value"), 1}},
      2,
      4096);
  session.storeEntry(
      QStringLiteral("b"),
      QVariantMap{{QStringLiteral("value"), 2}},
      2,
      4096);
  if (session.entryCount() != 2 || session.entryStoreCount() != 2 ||
      session.entryBytes() <= 0 ||
      !session.entryLookup(QStringLiteral("a"), &entry) ||
      entry.value(QStringLiteral("value")).toInt() != 1 ||
      session.entryHitCount() != 1) {
    return fail("stored entry lookup must retain payload, byte, order, and hit accounting");
  }

  session.storeEntry(
      QStringLiteral("c"),
      QVariantMap{{QStringLiteral("value"), 3}},
      2,
      4096);
  if (session.entryCount() != 2 || session.entryEvictionCount() != 1 ||
      session.entryLookup(QStringLiteral("b"), nullptr)) {
    return fail("entry-count pressure must evict the least-recently-used thumbnail");
  }

  const auto count_before_oversize = session.entryCount();
  session.storeEntry(
      QStringLiteral("oversize"),
      QVariantMap{{QStringLiteral("payload"), QString(4096, QLatin1Char('x'))}},
      2,
      128);
  if (session.entryCount() != count_before_oversize) {
    return fail("an entry larger than the byte budget must fail closed");
  }

  session.latestMaskJob().job_id = QStringLiteral("mask-job");
  session.latestPhotoLayerJob().job_id = QStringLiteral("layer-job");
  ++session.maskJobGeneration();
  ++session.photoLayerJobGeneration();
  ++session.maskHitCount();
  ++session.photoLayerHitCount();
  ++session.photoLayerRefreshCount();
  ++session.photoLayerDeferredCount();
  if (session.latestMaskJob().job_id != QStringLiteral("mask-job") ||
      session.latestPhotoLayerJob().job_id != QStringLiteral("layer-job") ||
      session.maskJobGeneration() != 1 || session.photoLayerJobGeneration() != 1 ||
      session.maskHitCount() != 1 || session.photoLayerHitCount() != 1 ||
      session.photoLayerRefreshCount() != 1 ||
      session.photoLayerDeferredCount() != 1) {
    return fail("worker identity and thumbnail telemetry must remain session-owned");
  }

  session.clearEntries();
  if (session.entryCount() != 0 || session.entryBytes() != 0 ||
      session.entryHitCount() != 1 || session.entryStoreCount() != 3) {
    return fail("cache clearing must release entries while preserving lifetime telemetry");
  }

  return EXIT_SUCCESS;
}
