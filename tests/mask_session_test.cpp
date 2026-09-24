#include "mask_session.h"

#include <cstdlib>
#include <iostream>

#include <QVariantMap>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QProcess>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  const auto arguments = application.arguments();
  if (arguments.size() == 3) {
    photo_editor::desktop::MaskSession session;
    session.setSelectionStorageDirectory(arguments.at(2));
    session.setAssetScope(QStringLiteral("process-photo"));
    const QVariantMap first{{QStringLiteral("id"), QStringLiteral("first")},
                           {QStringLiteral("maskSetId"), QStringLiteral("set")}};
    const QVariantMap second{{QStringLiteral("id"), QStringLiteral("second")},
                            {QStringLiteral("maskSetId"), QStringLiteral("set")}};
    if (arguments.at(1) == QStringLiteral("write")) {
      session.replaceStack({first, second}, 1);
      static_cast<void>(session.setActiveIndex(-1));
      return EXIT_SUCCESS;
    }
    session.replaceStack({first, second}, -1);
    return session.indexForMaskSet(QStringLiteral("set")) == 1 ? EXIT_SUCCESS
        : fail("a separate process must restore the saved component");
  }
  {
    QTemporaryDir storage;
    if (!storage.isValid()) return fail("process persistence test needs temporary storage");
    for (const auto& mode : {QStringLiteral("write"), QStringLiteral("read")}) {
      QProcess process;
      process.start(application.applicationFilePath(), {mode, storage.path()});
      if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return fail("separate-process mask preference persistence failed");
    }
  }
  {
    QTemporaryDir storage;
    if (!storage.isValid()) return fail("selection preference fixture needs temporary storage");
    const QVariantMap first{{QStringLiteral("id"), QStringLiteral("first")},
                            {QStringLiteral("maskSetId"), QStringLiteral("set")}};
    const QVariantMap second{{QStringLiteral("id"), QStringLiteral("second")},
                             {QStringLiteral("maskSetId"), QStringLiteral("set")}};
    {
      const auto blocked_path = storage.filePath(QStringLiteral("blocked"));
      QFile obstruction(blocked_path);
      if (!obstruction.open(QIODevice::WriteOnly)) return fail("cannot create storage failure fixture");
      obstruction.close();
      photo_editor::desktop::MaskSession retry;
      retry.setSelectionStorageDirectory(blocked_path);
      retry.setAssetScope(QStringLiteral("unsaved-photo"));
      retry.replaceStack({first, second}, 1);
      if (retry.flushSelectionPreferences()) return fail("blocked storage must report write failure");
      retry.setAssetScope(QStringLiteral("another-photo"));
      if (!obstruction.remove() || !retry.flushSelectionPreferences())
        return fail("failed asset preference write must remain retryable after switching assets");
      photo_editor::desktop::MaskSession recovered;
      recovered.setSelectionStorageDirectory(blocked_path);
      recovered.setAssetScope(QStringLiteral("unsaved-photo"));
      recovered.replaceStack({first, second}, -1);
      if (recovered.indexForMaskSet(QStringLiteral("set")) != 1)
        return fail("retried storage must retain the outgoing asset's component");
    }
    {
      photo_editor::desktop::MaskSession saved;
      saved.setSelectionStorageDirectory(storage.path());
      saved.setAssetScope(QStringLiteral("photo/one"));
      saved.replaceStack({first, second}, 1);
      static_cast<void>(saved.setActiveIndex(-1));
      saved.setAssetScope(QStringLiteral("photo/two"));
      saved.replaceStack({first, second}, 0);
    }
    photo_editor::desktop::MaskSession reopened;
    reopened.setSelectionStorageDirectory(storage.path());
    reopened.setAssetScope(QStringLiteral("photo/one"));
    reopened.replaceStack({second, first}, -1);
    if (reopened.indexForMaskSet(QStringLiteral("set")) != 0)
      return fail("fresh session must restore a saved component by ID after reorder");
    reopened.replaceStack({first}, -1);
    if (reopened.indexForMaskSet(QStringLiteral("set")) != 0)
      return fail("reopened selection must safely fall back when its saved component was deleted");
    reopened.setAssetScope(QStringLiteral("photo/two"));
    reopened.replaceStack({second, first}, -1);
    if (reopened.indexForMaskSet(QStringLiteral("set")) != 1)
      return fail("session destruction must save the second asset independently");
  }
  {
    photo_editor::desktop::MaskSession selection;
    const auto component = [](const QString& id) {
      return QVariantMap{{QStringLiteral("id"), id},
                         {QStringLiteral("maskSetId"), QStringLiteral("set-a")}};
    };
    const auto first = component(QStringLiteral("first"));
    const auto second = component(QStringLiteral("second"));
    selection.setAssetScope(QStringLiteral("photo-a"));
    selection.replaceStack({first, second}, 1);
    for (int cycle = 0; cycle < 20; ++cycle) {
      static_cast<void>(selection.setActiveIndex(-1));
      if (selection.indexForMaskSet(QStringLiteral("set-a")) != 1)
        return fail("mask reselection must retain the last component after deselection");
      static_cast<void>(selection.setActiveIndex(1));
    }
    selection.replaceStack({second, first}, -1);
    if (selection.indexForMaskSet(QStringLiteral("set-a")) != 0 ||
        selection.indexForMaskSet(QStringLiteral("set-a"), QStringLiteral("first")) != 1 ||
        selection.indexForMaskSet(QStringLiteral("missing")) != -1)
      return fail("mask reselection must resolve stable IDs after reordering");
    selection.setAssetScope(QStringLiteral("photo-b"));
    selection.replaceStack({first, second}, 0);
    selection.setAssetScope(QStringLiteral("photo-a"));
    selection.replaceStack({first, second}, -1);
    if (selection.indexForMaskSet(QStringLiteral("set-a")) != 1)
      return fail("assets sharing mask IDs must retain independent last components");
    selection.setAssetScope(QStringLiteral("photo-b"));
    selection.replaceStack({first, second}, -1);
    if (selection.indexForMaskSet(QStringLiteral("set-a")) != 0)
      return fail("asset return must restore its own component preference");
    selection.setAssetScope(QStringLiteral("photo-a"));
    selection.replaceStack({first}, -1);
    if (selection.indexForMaskSet(QStringLiteral("set-a")) != 0 ||
        selection.indexForMaskSet(QStringLiteral("set-a"), QStringLiteral("second")) != -1)
      return fail("deleted remembered components may fall back; explicit missing IDs must fail");
  }
  photo_editor::desktop::MaskSession session;
  session.replaceStack(
      QVariantList{
          QVariantMap{{QStringLiteral("id"), QStringLiteral("mask-a")}},
          QVariantMap{{QStringLiteral("id"), QStringLiteral("mask-b")}},
      },
      20);
  if (session.stack().size() != 2 || session.activeIndex() != 1 ||
      !session.hasActiveMask() || session.setActiveIndex(1) ||
      !session.setActiveIndex(0) || session.activeIndex() != 0) {
    return fail("MaskSession must own a clamped active selection over its stack");
  }
  if (session.activeDefinition().value(QStringLiteral("id")).toString() !=
      QStringLiteral("mask-a")) {
    return fail("active-mask definition cache must follow the session selection");
  }
  session.storePreviewCacheEntry(
      QStringLiteral("a"), QStringLiteral("source-a"), 2);
  session.storePreviewCacheEntry(
      QStringLiteral("b"), QStringLiteral("source-b"), 2);
  QString cached_source;
  if (!session.previewCacheLookup(QStringLiteral("a"), &cached_source) ||
      cached_source != QStringLiteral("source-a")) {
    return fail("active-mask preview cache must return exact stored sources");
  }
  session.storePreviewCacheEntry(
      QStringLiteral("c"), QStringLiteral("source-c"), 2);
  if (session.previewCacheEntryCount() != 2 ||
      session.previewCacheLookup(QStringLiteral("b"), nullptr) ||
      session.previewCacheHitCount() != 1 ||
      session.previewCacheMissCount() != 1 ||
      session.previewCacheEvictionCount() != 1) {
    return fail("active-mask preview cache must use bounded LRU eviction and telemetry");
  }
  session.clearPreviewCache();
  if (session.previewCacheEntryCount() != 0 ||
      !session.previewCacheKey().isEmpty() ||
      !session.previewCacheSource().isEmpty()) {
    return fail("active-mask preview cache clearing must remove all resident state");
  }
  session.recordOutlineCacheMiss(true);
  session.storeOutlineCache(
      QStringLiteral("outline-a"),
      QVariantList{QVariantList{0.0, 1.0}},
      QVariantMap{{QStringLiteral("source"), QStringLiteral("alpha")}});
  QVariantList outline_paths;
  QVariantMap outline_fields;
  if (!session.outlineCacheLookup(
          QStringLiteral("outline-a"), &outline_paths, &outline_fields) ||
      outline_paths.isEmpty() ||
      outline_fields.value(QStringLiteral("source")).toString() !=
          QStringLiteral("alpha") ||
      session.outlineCacheHitCount() != 1 ||
      session.outlineCacheMissCount() != 1 ||
      session.outlineFastKeyCount() != 1 ||
      session.outlineFullKeyCount() != 0) {
    return fail("mask outline cache must preserve paths, trace fields, and key telemetry");
  }
  session.clearOutlineCache();
  session.recordOutlineCacheMiss(false);
  if (session.outlineCacheLookup(
          QStringLiteral("outline-a"), nullptr, nullptr) ||
      session.outlineCacheMissCount() != 2 ||
      session.outlineFullKeyCount() != 1) {
    return fail("cleared outline entries must miss without losing identity telemetry");
  }
  session.storeColorToolPreview(
      QStringLiteral("color-a"), QStringLiteral("color-source"));
  session.storeSmartSelectionPreview(
      QStringLiteral("smart-a"), QStringLiteral("smart-source"));
  QString keyed_source;
  if (!session.colorToolPreviewLookup(
          QStringLiteral("color-a"), &keyed_source) ||
      keyed_source != QStringLiteral("color-source") ||
      !session.smartSelectionPreviewLookup(
          QStringLiteral("smart-a"), &keyed_source) ||
      keyed_source != QStringLiteral("smart-source")) {
    return fail("mask keyed preview caches must preserve exact identity and source");
  }
  session.clearColorToolPreview();
  session.clearSmartSelectionPreview();
  if (!session.colorToolPreviewSource().isEmpty() ||
      !session.smartSelectionPreviewSource().isEmpty()) {
    return fail("mask keyed preview caches must clear atomically");
  }
  session.replaceStack({}, 0);
  if (!session.stack().isEmpty() || session.activeIndex() != -1 ||
      session.hasActiveMask()) {
    return fail("replacing the stack must atomically normalize active selection");
  }
  if (session.previewMode() != QStringLiteral("overlay") ||
      !session.setPreviewMode(QStringLiteral("matte")) ||
      session.setPreviewMode(QStringLiteral("matte"))) {
    return fail("mask preview mode changes must be normalized before session publication");
  }

  session.setSmartSelectionRetry(
      QStringLiteral("selection failed"),
      QVariantList{QVariantMap{{QStringLiteral("x"), 0.25}}},
      QStringLiteral("selection"),
      true);
  if (!session.retryAvailable() ||
      session.retryRequest().action != QStringLiteral("smart-selection") ||
      session.retryRequest().points.size() != 1 ||
      !session.retryRequest().create_new) {
    return fail("smart-selection retry payload must remain complete and session-owned");
  }
  session.setInpaintRetry(
      QStringLiteral("inpaint failed"), QStringLiteral("heal"));
  if (session.retryRequest().action != QStringLiteral("inpaint") ||
      session.retryRequest().retouch_mode != QStringLiteral("heal") ||
      !session.retryRequest().points.isEmpty() ||
      !session.clearRetry() || session.retryAvailable() || session.clearRetry()) {
    return fail("inpaint retry replacement and clearing must be atomic");
  }

  if (session.advanceWorkRevision() != 1 ||
      !session.isCurrentWorkRevision(1) ||
      session.isCurrentWorkRevision(0)) {
    return fail("mask edits must invalidate prior state revisions");
  }

  const auto brush = session.advanceBrushRasterGeneration();
  const auto selection = session.advanceSmartSelectionGeneration();
  const auto preview = session.advanceSmartPreviewGeneration();
  const auto inpaint = session.advanceInpaintGeneration();
  if (!session.isCurrentBrushRaster(brush) ||
      !session.isCurrentSmartSelection(selection) ||
      !session.isCurrentSmartPreview(preview) ||
      !session.isCurrentInpaint(inpaint)) {
    return fail("each mask worker class must retain an independent generation");
  }

  static_cast<void>(session.advanceBrushRasterGeneration());
  static_cast<void>(session.advanceSmartSelectionGeneration());
  static_cast<void>(session.advanceSmartPreviewGeneration());
  static_cast<void>(session.advanceInpaintGeneration());
  if (session.isCurrentBrushRaster(brush) ||
      session.isCurrentSmartSelection(selection) ||
      session.isCurrentSmartPreview(preview) ||
      session.isCurrentInpaint(inpaint)) {
    return fail("superseded mask worker results must become stale independently");
  }

  session.recordBrushRasterCancellation();
  session.recordSmartSelectionCancellation();
  session.recordInpaintCancellation();
  if (session.brushRasterCancelledCount() != 1 ||
      session.smartSelectionCancelledCount() != 1 ||
      session.inpaintCancelledCount() != 1) {
    return fail("mask cancellation telemetry must remain session-owned");
  }

  return EXIT_SUCCESS;
}
