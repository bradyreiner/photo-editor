#include "editor_preview_controller.h"
#include "linked_image_source_monitor.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static QVariantList layers(const EditorPreviewController& controller) {
    // Compare persisted content, not thumbnailSource and other refreshed UI cache fields.
    return parse_edit_recipe_json(serialize_edit_recipe_json(controller.currentState())).layers;
  }
  static QStringList watched(const EditorPreviewController& controller) { return controller.linked_source_monitor_->paths(); }
  static bool idle(const EditorPreviewController& controller) {
    return !controller.processing() && !controller.interactive_render_watcher_.isRunning() &&
        !controller.full_render_watcher_.isRunning() && !controller.interactive_request_pending_ &&
        !controller.full_request_pending_ && !controller.render_scheduler_.interactiveActive() &&
        !controller.render_scheduler_.fullActive();
  }
};
}
using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QTemporaryDir root;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  if (!root.isValid()) return 1;
  QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::red);
  const auto base = root.filePath("base.png"), source = root.filePath("source.png"), replacement = root.filePath("replacement.png");
  if (!image.save(base) || !image.save(source)) return 1;
  image = QImage(48, 48, QImage::Format_RGB32); image.fill(Qt::blue);
  if (!image.save(replacement)) return 1;
  EditorPreviewController controller;
  controller.loadAsset("relink-test", base, {}, {}, {}, {}, {});
  auto wait = [&](const std::function<bool()>& done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 12000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
    if (!done()) std::cerr << "Timed out: " << controller.status().toStdString() << '\n';
    return done();
  };
  auto idle = [&] { return EditorPreviewControllerTestAccess::idle(controller); };
  if (!wait(idle) || !controller.addSourceLayer(source, "Keep my name")) return 1;
  controller.setPhotoLayerTransform(0, 3, 4, 0.75, 12);
  controller.setPhotoLayerMaskEnabled(0, true);
  controller.setPhotoLayerMaskOpacity(0, 0.6);
  if (!wait(idle)) return 1;
  const auto before = EditorPreviewControllerTestAccess::layers(controller).first().toMap();
  const auto id = before.value("id").toString();
  const auto undo_before = controller.undoLabel();
  const auto original_preview_source = controller.previewSource();
  const QImage original_preview(QUrl(original_preview_source).toLocalFile());
  std::cout << "Initial source layer and mask rendered\n";
  for (const auto& bad : {root.filePath("missing.png"), root.path(), QString("https://example.invalid/image.png")}) {
    if (controller.relinkSourceLayer(id, bad)) return 1;
  }
  if (controller.relinkSourceLayer("removed-layer", replacement) || controller.undoLabel() != undo_before ||
      EditorPreviewControllerTestAccess::layers(controller).first().toMap() != before) return 1;
  std::cout << "Invalid targets preserved state and history\n";
  if (!controller.relinkSourceLayer(id, QUrl::fromLocalFile(replacement).toString())) return 1;
  if (!wait(idle)) return 1;
  auto after = EditorPreviewControllerTestAccess::layers(controller).first().toMap();
  const QImage relinked_preview(QUrl(controller.previewSource()).toLocalFile());
  if (original_preview.isNull() || relinked_preview.isNull() ||
      original_preview_source == controller.previewSource() || original_preview == relinked_preview) {
    std::cerr << "Relink must publish changed, full-resolution pixels\n";
    return 1;
  }
  if (after.value("sourcePath") != replacement || controller.undoLabel() != "Relink image layer") {
    std::cerr << "Wrong replacement path or undo label: " << after.value("sourcePath").toString().toStdString()
              << " / " << controller.undoLabel().toStdString() << '\n';
    return 1;
  }
  after.insert("sourcePath", before.value("sourcePath"));
  if (after != before || EditorPreviewControllerTestAccess::watched(controller) != QStringList{replacement}) {
    for (auto it = after.cbegin(); it != after.cend(); ++it) {
      if (it.value() != before.value(it.key())) std::cerr << "Relink changed " << it.key().toStdString() << '\n';
    }
    std::cerr << "Watched paths: " << EditorPreviewControllerTestAccess::watched(controller).join(',').toStdString() << '\n';
    return 1;
  }
  controller.undo();
  std::cout << "Relink preserved layer fields; checking undo\n";
  if (!wait(idle) || EditorPreviewControllerTestAccess::layers(controller).first().toMap() != before) return 1;
  controller.redo();
  std::cout << "Undo preserved layer fields; checking redo\n";
  if (!wait(idle) || EditorPreviewControllerTestAccess::watched(controller) != QStringList{replacement}) return 1;
  const auto recipe_before_external_change = EditorPreviewControllerTestAccess::layers(controller);
  const auto history_before_external_change = controller.undoLabel();
  const auto before_external_source = controller.previewSource();
  const QImage before_external_pixels(QUrl(before_external_source).toLocalFile());
  image.fill(Qt::green);
  if (!image.save(replacement)) return 1;
  if (!wait([&] { return idle() && controller.previewSource() != before_external_source; })) return 1;
  const QImage after_external_pixels(QUrl(controller.previewSource()).toLocalFile());
  if (before_external_pixels.isNull() || after_external_pixels.isNull() ||
      before_external_pixels == after_external_pixels) {
    std::cerr << "External edit must automatically publish changed pixels\n";
    return 1;
  }
  if (!QFile::rename(replacement, root.filePath("replacement-held.png"))) return 1;
  if (!wait([&] { return idle() && controller.previewFailureStage() == "layer_source"; })) return 1;
  if (!QFile::rename(root.filePath("replacement-held.png"), replacement)) return 1;
  if (!wait([&] { return idle() && controller.previewFailureStage().isEmpty(); })) return 1;
  if (EditorPreviewControllerTestAccess::layers(controller) != recipe_before_external_change ||
      controller.undoLabel() != history_before_external_change) return 1;
  controller.setPhotoLayerLocked(0, true);
  if (controller.relinkSourceLayer(id, source)) return 1;
  if (!wait(idle)) return 1;
  controller.loadAsset("other-document", base, {}, {}, {}, {}, {});
  if (!wait(idle) || !EditorPreviewControllerTestAccess::watched(controller).isEmpty()) return 1;
  std::cout << "Relink preserves complete layer state, validates targets, respects locks, undoes/redoes, auto-recovers missing files without history edits, and releases old document watches\n";
  return 0;
}
