#include "customer_edit_access.h"
#include "editor_preview_controller.h"
#include "carousel_document_controller.h"
#include "timeline_document_controller.h"
#include "app_bootstrap.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer timer; timer.start();
  while (!ready() && timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(10);
  }
  return ready();
}
}
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", runtime.filePath("boards").toUtf8());
  // Exercise local document persistence without a real catalog connection or
  // connection retries when this test runs outside the filesystem sandbox.
  qputenv("PHOTO_EDITOR_DATABASE_URL", "invalid-test-catalog-url");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  if (!AppBootstrap::ensureRuntimeDirectories()) return 1;
  QImage fixture(96, 64, QImage::Format_RGB32); fixture.fill(QColor(100, 130, 170));
  const auto source = runtime.filePath("source.png");
  if (!fixture.save(source)) return 1;
  bool active = true;
  auto access = [&] { return active; };
  CustomerEditAccess admission;
  admission.setPolicy(access);
  {
    const auto accepted = admission.enter();
    active = false;
    if (!accepted || !admission.enter()) return fail("Admitted nested work must finish");
  }
  if (admission.enter()) return fail("The next operation must check expiry");
  active = true;
  EditorPreviewController editor;
  editor.setEditAccessPolicy(access);
  editor.loadAsset("customer-photo", source, {}, {}, {}, {}, {});
  editor.setExposure(0.5);
  if (editor.exposure() != 0.5) return fail("Active editing must work");
  if (!editor.addTextLayer("Existing typography") ||
      !editor.setPhotoLayerTypography(0, {{"size", 22.5}}) || !editor.addPaintLayer())
    return fail("Creative tool fixture must be editable before expiry");
  const auto photo_layers = editor.photoLayerSnapshot();
  active = false;
  editor.setExposure(2.0); editor.resetAdjustments(); editor.undo();
  editor.setCropZoom(2.0);
  if (editor.cropZoom() != 1.0 || !editor.pickMaskAt(0.5, 0.5, "selection", true).isEmpty())
    return fail("Crop and asynchronous selection entry points must reject new edits");
  if (editor.exposure() != 0.5) return fail("Expired editing must preserve the existing recipe");
  if (editor.addTextLayer("Blocked")) return fail("Expired access must not add layers");
  if (editor.addPaintLayer() ||
      editor.beginPhotoPaintStroke(0, {{"color", "#ff7040"}, {"size", 10}, {"opacity", 0.65}}) ||
      editor.setPhotoLayerTypography(1, {{"size", 34.5}}) ||
      editor.addPhotoShape("rectangle", 5, 5, 40, 40, 96, 64, "#ffffff", "#000000", 1.0) ||
      editor.groupPhotoLayers({0, 1}, "Blocked group") || editor.photoLayerSnapshot() != photo_layers)
    return fail("Expired creative tools must preserve paint, typography, shapes and groups");
  if (editor.createEditHistorySnapshot("Preserved after expiry").isEmpty()) return fail("Saving an existing edit must remain available");
  const auto output = runtime.filePath("existing-work.png");
  if (!editor.startFlattenedPhotoExport(output) || !waitFor([&] { return !editor.photoExportBusy(); }) ||
      QImage(output).isNull()) {
    std::cerr << editor.photoExportError().toStdString() << '\n';
    return fail("Existing photo export must remain available");
  }
  editor.loadAsset("other-photo", source, {}, {}, {}, {}, {});
  if (!editor.hasAsset() || editor.exposure() != 0.0) return fail("Expired users must be able to open another existing photo");

  active = true;
  CarouselDocumentController boards;
  boards.setEditAccessPolicy(access);
  boards.addTextItem("Existing board");
  const auto items = boards.canvasItems();
  active = false;
  boards.addTextItem("Blocked"); boards.removeSelectedItem(); boards.undo();
  if (boards.canvasItems() != items) return fail("Expired users cannot mutate board contents");
  if (!boards.saveDocument("Existing board")) return fail("Expired users can save boards");
  const auto board_id = boards.savedDocumentId();
  boards.closeCarousel();
  if (!boards.loadDocument(board_id) || boards.canvasItems() != items) return fail("Expired users can reopen boards");
  if (!QDir().mkpath(runtime.filePath("board-export"))) return 1;
  if (!boards.exportCarouselToDirectory(runtime.filePath("board-export")) ||
      !waitFor([&] { return !boards.exportBusy(); })) {
    std::cerr << boards.status().toStdString() << '\n';
    return fail("Expired users can export boards");
  }

  active = true;
  TimelineDocumentController timeline;
  timeline.setEditAccessPolicy(access);
  timeline.createTimeline("Existing timeline");
  timeline.addClipFromGallery("still:customer", source, "Existing still", 2.0, {});
  if (timeline.clipCount() != 1) return fail("Timeline fixture must contain a clip");
  timeline.selectClip(0);
  timeline.setSelectedFramePerspectiveX(0.2);
  timeline.addSelectedClipEffect("Exposure");
  const auto timeline_clips = timeline.clips();
  active = false;
  timeline.removeSelectedClip(); timeline.undo(); timeline.addTitleClip();
  timeline.setSelectedFramePerspectiveX(0.7);
  timeline.setSelectedFramePerspectiveY(0.5);
  timeline.setSelectedClipEffectMask("Exposure", {{"enabled", true}, {"shape", "linear"}, {"x", 0.3}});
  timeline.setSelectedClipEffectAmount("Exposure", 0.1);
  if (timeline.setSelectedClipVideoInputTransform("rec709") || timeline.clips() != timeline_clips)
    return fail("Expired timeline perspective, effect masks and color edits must preserve clips");
  if (timeline.clipCount() != 1) return fail("Expired users cannot mutate a timeline");
  if (!timeline.saveTimeline()) return fail("Expired users can save timelines");
  const auto timeline_path = timeline.savedDocumentPath();
  timeline.closeTimeline();
  if (!timeline.loadTimeline(timeline_path) || timeline.clipCount() != 1) return fail("Expired users can reopen timelines");
  active = true;
  timeline.removeSelectedClip();
  if (timeline.clipCount() != 0) return fail("Renewed access restores editing");
  std::cout << "Expiry preserves existing work, saves, exports and loading; renewal restores editing.\n";
}
