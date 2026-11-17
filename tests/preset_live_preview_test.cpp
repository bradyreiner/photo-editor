#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "thumbnail_shared_preview_adapter.h"

#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <functional>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess final {
  static EditState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }

  static RenderRequest request(const EditorPreviewController& controller, bool full_resolution = false) {
    return controller.currentRenderRequest(full_resolution);
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;

#define CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      std::cerr << message << '\n'; \
      return 1; \
    } \
  } while (false)

namespace {

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 30000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) return true;
    QThread::msleep(2);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

QImage render(const RenderRequest& request) {
  const auto result = execute_preview_render_request(request);
  return result.success ? result.preview_image : QImage{};
}

QImage source_image() {
  QImage image(320, 240, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(
          (x * 7 + y * 3) % 256,
          (x * 2 + y * 11) % 256,
          ((x / 8 + y / 5) % 2) == 0 ? 214 : 38));
    }
  }
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  return image;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir temporary;
  if (!temporary.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyUiTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PresetLivePreview"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     temporary.filePath(QStringLiteral("settings")));
  { QSettings settings; settings.clear(); }
  if (!AppBootstrap::ensureRuntimeDirectories()) return 1;

  const auto source_path = temporary.filePath(QStringLiteral("source.png"));
  CHECK(source_image().save(source_path), "Create a photographic render fixture");

  EditorPreviewImageProvider image_provider;
  EditorPreviewController controller;
  controller.setImageProvider(&image_provider);
  const auto published_pixels = [&]() {
    return image_provider.image(QUrl(controller.displayPreviewSource()).path().mid(1));
  };
  controller.loadAsset(QStringLiteral("preset-live-preview"), source_path,
                       {}, {}, {}, {}, QString());
  CHECK(wait_until([&]() {
          return !controller.displayPreviewSource().isEmpty() &&
              !controller.processing() && !controller.fullRenderRunning();
        }), "Load and render the source image");

  const QString preset_name = QStringLiteral("Clean Portrait");
  CHECK(controller.presetNames().contains(preset_name),
        "The fixture preset is available");
  const auto original_state = EditorPreviewControllerTestAccess::state(controller);
  const auto original_history = controller.editHistoryEntries();
  const auto original_history_index = controller.editHistoryCurrentIndex();
  const auto baseline_request = EditorPreviewControllerTestAccess::request(controller);
  const auto baseline_pixels = render(baseline_request);
  CHECK(!baseline_pixels.isNull(), "Render the unmodified baseline request");
  const auto baseline_published_pixels = published_pixels();
  CHECK(!baseline_published_pixels.isNull(), "The display image provider contains the published baseline");

  const auto baseline_export_path = temporary.filePath(QStringLiteral("baseline.png"));
  CHECK(controller.exportFlattenedPhoto(baseline_export_path), "Export the baseline image");
  CHECK(wait_until([&]() { return !controller.photoExportBusy(); }) &&
            controller.photoExportError().isEmpty(),
        "Finish baseline export");
  const QImage baseline_export(baseline_export_path);
  CHECK(!baseline_export.isNull(), "Read baseline export pixels");

  CHECK(controller.previewPreset(preset_name, -1, {}, 1.0),
        "Start a noncommitting base-image preset preview");
  CHECK(controller.presetPreviewActive(), "Expose the preview as active");
  CHECK(EditorPreviewControllerTestAccess::state(controller) == original_state,
        "Preview must not mutate the current edit recipe");
  CHECK(controller.editHistoryEntries() == original_history &&
            controller.editHistoryCurrentIndex() == original_history_index,
        "Preview must not add an undo/history entry");
  const auto preview_request = EditorPreviewControllerTestAccess::request(controller);
  CHECK(preview_request.state != original_state,
        "Interactive and full-resolution render requests should include the temporary recipe");
  const auto preview_pixels = render(preview_request);
  CHECK(!preview_pixels.isNull() && preview_pixels != baseline_pixels,
        "The live preview should render visibly different pixels");
  CHECK(EditorPreviewControllerTestAccess::request(controller, true).state == preview_request.state,
        "Full-resolution interactive requests must include the same temporary recipe");
  CHECK(wait_until([&]() {
          return !controller.processing() && !controller.fullRenderRunning() &&
              !published_pixels().isNull() && published_pixels() != baseline_published_pixels;
        }), "The asynchronous canvas image provider must publish the preset preview pixels");

  const auto preview_export_path = temporary.filePath(QStringLiteral("during-preview.png"));
  CHECK(controller.exportFlattenedPhoto(preview_export_path),
        "Export while the temporary preview is active");
  CHECK(wait_until([&]() { return !controller.photoExportBusy(); }) &&
            controller.photoExportError().isEmpty(),
        "Finish export while preview is active");
  CHECK(QImage(preview_export_path) == baseline_export,
        "Flattened export must ignore the temporary preview");

  CHECK(controller.previewPreset(preset_name, -1, {}, 0.35),
        "Adjust the temporary preview intensity");
  const auto lighter_request = EditorPreviewControllerTestAccess::request(controller);
  CHECK(lighter_request.state != preview_request.state,
        "Changing the preview amount should rebuild the transient render recipe");
  CHECK(EditorPreviewControllerTestAccess::state(controller) == original_state,
        "Changing preview amount must still leave the edit recipe untouched");

  CHECK(controller.clearPresetPreview(), "Clear the active preview");
  CHECK(!controller.presetPreviewActive(), "Clear removes the transient overlay");
  const auto cleared_request = EditorPreviewControllerTestAccess::request(controller);
  CHECK(cleared_request.state == original_state,
        "Clear restores the current recipe to render requests");
  CHECK(render(cleared_request) == baseline_pixels,
        "Clear restores the original rendered pixels exactly");
  CHECK(wait_until([&]() {
          return !controller.processing() && !controller.fullRenderRunning() &&
              published_pixels() == baseline_published_pixels;
        }), "Clear must publish the original canvas pixels again");

  CHECK(controller.previewPreset(preset_name, -1, {}, 1.0) && controller.applyPreset(preset_name),
        "Apply a preset while its temporary preview is active");
  const auto applied_state = EditorPreviewControllerTestAccess::state(controller);
  CHECK(!controller.presetPreviewActive() && applied_state != original_state &&
            controller.editHistoryCurrentIndex() == original_history_index + 1,
        "Apply must clear the preview and record exactly one committed undo step");
  controller.undo();
  CHECK(EditorPreviewControllerTestAccess::state(controller) == original_state &&
            !controller.presetPreviewActive(),
        "One undo restores the pre-preview recipe without reviving its transient overlay");
  controller.redo();
  CHECK(EditorPreviewControllerTestAccess::state(controller) == applied_state,
        "Redo restores the applied preset recipe");
  controller.undo();

  CHECK(controller.previewPreset(preset_name, -1, {}, 1.0),
        "Restart a preview before editing");
  controller.setExposure(0.32);
  CHECK(!controller.presetPreviewActive(),
        "A real global adjustment clears the preview instead of hiding the edit");
  const auto edited_state = EditorPreviewControllerTestAccess::state(controller);
  CHECK(edited_state.exposure == 0.32 &&
            EditorPreviewControllerTestAccess::request(controller).state == edited_state,
        "After an edit, render requests show the edited recipe without the stale overlay");

  CHECK(controller.addAdjustmentLayer(QStringLiteral("Preview target")),
        "Create an adjustment-layer target");
  const auto layer_state = EditorPreviewControllerTestAccess::state(controller);
  CHECK(controller.previewPreset(preset_name, 0, {}, 1.0),
        "Start a preset preview on an unlocked adjustment layer");
  CHECK(EditorPreviewControllerTestAccess::state(controller) == layer_state,
        "Layer preview must not persist its generated recipe");
  CHECK(EditorPreviewControllerTestAccess::request(controller).state.layers !=
            layer_state.layers,
        "Layer preview render request should include its temporary layer recipe");
  CHECK(controller.clearPresetPreview(), "Clear the layer preview");
  CHECK(controller.setPhotoLayerLocked(0, true), "Lock the layer preview target");
  const auto locked_state = EditorPreviewControllerTestAccess::state(controller);
  CHECK(!controller.previewPreset(preset_name, 0, {}, 1.0) &&
            !controller.presetPreviewActive(),
        "A locked layer must reject a temporary preset preview");
  CHECK(EditorPreviewControllerTestAccess::state(controller) == locked_state,
        "Rejected locked-layer preview must not modify the layer recipe");

  return 0;
}
