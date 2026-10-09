#include <cstdlib>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QRectF>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include "edit_recipe_state.h"
#include "editor_preview_controller.h"
#include "editor_preview_texture.h"
#include "scaled_mask_matte_service.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static qint64 source_size_cache_misses(const EditorPreviewController& controller) {
    return controller.source_size_cache_misses_;
  }
  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }

  static bool full_native_coverage(const EditorPreviewController& controller) {
    return QRegion(QRect(QPoint(), controller.displayPreviewNativeSize())).subtracted(
               controller.display_preview_native_full_resolution_coverage_).isEmpty() &&
        controller.display_preview_native_full_resolution_coverage_size_ ==
            controller.displayPreviewNativeSize();
  }

  static QString recipe_json(const EditorPreviewController& controller) {
    return serialize_edit_recipe_json(controller.currentState());
  }

  static bool all_idle(const EditorPreviewController& controller) {
    return !controller.render_scheduler_.interactiveActive() &&
        !controller.render_scheduler_.fullActive() &&
        !controller.render_scheduler_.pendingDispatchActive() &&
        !controller.interactive_render_watcher_.isRunning() &&
        !controller.full_render_watcher_.isRunning() &&
        !controller.interactive_request_pending_ && !controller.full_request_pending_;
  }

  static void publish_visible_patch(EditorPreviewController& controller, bool in_flight) {
    RenderResult result;
    result.success = true;
    result.full_resolution = true;
    result.generation = controller.preview_session_.generation();
    result.file_path = controller.file_path_;
    result.render_reason = QStringLiteral("viewport_change");
    result.render_mode = QStringLiteral("max_preview_visible_tile");
    result.tile_visible_region_active = true;
    result.tile_visible_region = controller.preview_viewport_rect_;
    result.output_width = 1024;
    result.output_height = 768;
    result.source_width = 1024;
    result.source_height = 768;
    result.texture_upload_rect = QRect(256, 192, 128, 96);
    result.texture_upload_image = QImage(128, 96, QImage::Format_ARGB32);
    result.texture_upload_image.fill(QColor(180, 84, 52));
    result.partial_texture_upload_candidate = true;
    if (in_flight) {
      controller.full_render_watcher_.setFuture(QtConcurrent::run([result] {
        QThread::msleep(50);
        return result;
      }));
    } else {
      controller.handleRenderResult(true, result);
    }
  }

  static void handle_render_result(
      EditorPreviewController& controller,
      bool full_resolution,
      const RenderResult& result) {
    controller.handleRenderResult(full_resolution, result);
  }

  static RenderRequest pending_full_render_request(
      const EditorPreviewController& controller) {
    return controller.pending_full_request_;
  }

  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }

  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }

  static bool overlay_render_pending(const EditorPreviewController& controller) {
    return controller.overlay_request_pending_;
  }

  static bool overlay_render_running(const EditorPreviewController& controller) {
    return controller.overlay_render_watcher_.isRunning();
  }

  static int viewport_max_preview_schedule_count(
      const EditorPreviewController& controller) {
    return controller.viewport_max_preview_schedule_count_;
  }

  static void set_display_preview_texture(
      EditorPreviewController& controller,
      std::unique_ptr<EditorPreviewTexture> texture) {
    controller.display_preview_texture_ = std::move(texture);
    controller.display_preview_native_revision_ = 0;
    controller.display_preview_native_rendered_cache_key_.clear();
    controller.display_preview_native_full_resolution_coverage_ = {};
    controller.display_preview_native_full_resolution_coverage_size_ = {};
    controller.display_preview_native_full_resolution_coverage_state_signature_.clear();
  }

  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }

  static QVariantMap scaled_mask(
      const QVariantMap& mask,
      const QSize& target_size,
      const QString& purpose) {
    return mask_with_scaled_segmentation_matte(mask, target_size, purpose);
  }

  static void clear_scaled_mask_matte_cache() {
    photo_editor::desktop::clear_scaled_mask_matte_cache();
  }

  static int scaled_mask_matte_cache_entries() {
    return scaled_mask_matte_cache_snapshot().entries;
  }

  static int scaled_mask_matte_cache_hits() {
    return scaled_mask_matte_cache_snapshot().hit_count;
  }

  static int scaled_mask_matte_cache_misses() {
    return scaled_mask_matte_cache_snapshot().miss_count;
  }
};

class FakeEditorPreviewTexture final : public EditorPreviewTexture {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] const char* backendName() const override { return "fake"; }

  bool ensureNativeTexture(const QSize& size, QImage::Format format) override {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
      return false;
    }
    texture_size_ = size;
    texture_format_name_ =
        format == QImage::Format_RGBA16FPx4 ? "rgba16float" : "bgra8unorm";
    texture_bytes_per_pixel_ = format == QImage::Format_RGBA16FPx4 ? 8 : 4;
    ++ensure_texture_count_;
    return true;
  }

  bool upload(const QImage& image) override {
    if (image.isNull()) {
      return false;
    }
    texture_size_ = image.size();
    texture_format_name_ =
        image.format() == QImage::Format_RGBA16FPx4 ? "rgba16float" : "bgra8unorm";
    texture_bytes_per_pixel_ = image.format() == QImage::Format_RGBA16FPx4 ? 8 : 4;
    ++upload_count_;
    return true;
  }

  bool uploadRegion(const QImage& image, const QRect& region) override {
    if (image.isNull() || region.isEmpty() || image.size() != texture_size_) {
      return false;
    }
    ++partial_upload_count_;
    last_partial_upload_rect_ = region;
    return true;
  }

  bool uploadPatch(const QImage& image, const QRect& destination_region) override {
    if (image.isNull() || destination_region.isEmpty() ||
        destination_region.size() != image.size() ||
        !texture_size_.isValid() ||
        destination_region.right() >= texture_size_.width() ||
        destination_region.bottom() >= texture_size_.height()) {
      return false;
    }
    ++partial_upload_count_;
    ++patch_upload_count_;
    last_patch_upload_rect_ = destination_region;
    return true;
  }

  void clear() override { texture_size_ = {}; }
  [[nodiscard]] bool hasNativeTexture() const override { return texture_size_.isValid(); }
  [[nodiscard]] quint64 nativeTextureHandle() const override { return hasNativeTexture() ? 1 : 0; }
  [[nodiscard]] QSize nativeTextureSize() const override { return texture_size_; }
  [[nodiscard]] const char* nativeTextureFormatName() const override {
    return hasNativeTexture() ? texture_format_name_ : "none";
  }
  [[nodiscard]] int nativeTextureBytesPerPixel() const override {
    return hasNativeTexture() ? texture_bytes_per_pixel_ : 0;
  }
  [[nodiscard]] int ensureTextureCount() const { return ensure_texture_count_; }
  [[nodiscard]] int uploadCount() const { return upload_count_; }
  [[nodiscard]] int patchUploadCount() const { return patch_upload_count_; }
  [[nodiscard]] QRect lastPatchUploadRect() const { return last_patch_upload_rect_; }

 private:
  QSize texture_size_;
  const char* texture_format_name_ = "none";
  int texture_bytes_per_pixel_ = 0;
  int ensure_texture_count_ = 0;
  int upload_count_ = 0;
  int partial_upload_count_ = 0;
  int patch_upload_count_ = 0;
  QRect last_partial_upload_rect_;
  QRect last_patch_upload_rect_;
};

}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

int selected_pixel_count(const QByteArray& data) {
  auto count = 0;
  for (const auto byte : data) {
    if (static_cast<unsigned char>(byte) > 0U) {
      ++count;
    }
  }
  return count;
}

unsigned char alpha_at(const QVariantMap& mask, int x, int y) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (x < 0 || y < 0 || x >= width || y >= height ||
      data.size() != width * height) {
    return 0;
  }
  return static_cast<unsigned char>(data.at(y * width + x));
}

QVariantMap normalized_point(double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), x);
  point.insert(QStringLiteral("y"), y);
  return point;
}

QString read_source_file(const QString& relative_path) {
  QFile file(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(relative_path));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

bool wait_for_smart_commit(
    photo_editor::desktop::EditorPreviewController& controller,
    int expected_mask_count,
    int timeout_ms = 4000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller.processing() && controller.maskCount() >= expected_mask_count) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return !controller.processing() && controller.maskCount() >= expected_mask_count;
}

bool wait_for_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 4000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller.processing()) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return !controller.processing();
}

bool finish_brush_stroke(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 6000) {
  controller.endActiveBrushStroke();
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto telemetry =
        photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
            controller);
    if (!controller.processing() &&
        telemetry.value(QStringLiteral("operation")).toString() ==
            QStringLiteral("brush_rasterization")) {
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  const auto telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          controller);
  return !controller.processing() &&
         telemetry.value(QStringLiteral("operation")).toString() ==
             QStringLiteral("brush_rasterization");
}

bool pixel_mask_matches(
    const QVariantMap& mask,
    const QString& operation,
    const QString& expected_set_id,
    int expected_width,
    int expected_height,
    int expected_x,
    int expected_y) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  return mask.value(QStringLiteral("kind")).toString() == QStringLiteral("alpha") &&
         mask.value(QStringLiteral("operation")).toString() == operation &&
         mask.value(QStringLiteral("maskSetId")).toString() == expected_set_id &&
         mask.value(QStringLiteral("pixelSelection")).toBool() &&
         mask.value(QStringLiteral("pixelSelectionContract")).toString() ==
             QStringLiteral("pixel-selection-alpha:v1|source-resolution:true|exact-byte:true|dirty-rect:true") &&
         mask.value(QStringLiteral("segmentationWidth")).toInt() == expected_width &&
         mask.value(QStringLiteral("segmentationHeight")).toInt() == expected_height &&
         selected_pixel_count(data) == 1 &&
         alpha_at(mask, expected_x, expected_y) == 255U &&
         mask.value(QStringLiteral("pixelDirtyX")).toInt() == expected_x &&
         mask.value(QStringLiteral("pixelDirtyY")).toInt() == expected_y &&
         mask.value(QStringLiteral("pixelDirtyWidth")).toInt() == 1 &&
         mask.value(QStringLiteral("pixelDirtyHeight")).toInt() == 1;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("pixel zoom runtime directory should be available");
  }

  if (app.arguments().contains(QStringLiteral("--zoom-out-coverage"))) {
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
    qputenv("PHOTO_EDITOR_CACHE_DIR", runtime_root.filePath("cache").toUtf8());
    qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
    QImage source(1024, 768, QImage::Format_ARGB32);
    source.fill(QColor(92, 116, 140));
    const auto path = runtime_root.filePath("zoom-out.png");
    if (!source.save(path)) return fail("cannot save zoom-out fixture");
    photo_editor::desktop::EditorPreviewController zoom_controller;
    const auto wait_idle = [&] {
      QElapsedTimer timer;
      timer.start();
      while (timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (!zoom_controller.processing() && Access::all_idle(zoom_controller)) return true;
        QThread::msleep(25);
      }
      return false;
    };
    Access::set_display_preview_texture(zoom_controller,
        std::make_unique<photo_editor::desktop::FakeEditorPreviewTexture>());
    zoom_controller.loadAsset("zoom-out", path, {}, {}, {}, {}, {});
    if (!wait_idle())
      return fail("zoom-out fixture must finish loading");
    zoom_controller.setExposure(0.7);
    if (!wait_idle())
      return fail("zoom-out fixture edit must settle");
    const auto recipe = Access::recipe_json(zoom_controller);
    const auto history_index = zoom_controller.editHistoryCurrentIndex();
    const auto generation = Access::render_generation(zoom_controller);
    for (const bool wait_for_tile : {true, false}) {
      // Recreate a source-size backing containing only a viewport patch. This
      // is the native publication state left by a visible-only render when a
      // resident full-image base is unavailable after an edit.
      zoom_controller.setPreviewViewport(0.25, 0.25, 0.125, 0.125, 128, true);
      Access::set_display_preview_texture(zoom_controller,
          std::make_unique<photo_editor::desktop::FakeEditorPreviewTexture>());
      Access::publish_visible_patch(zoom_controller, !wait_for_tile);
      if (wait_for_tile &&
          (!wait_idle() ||
           !zoom_controller.displayPreviewNativeViewportFullResolution() ||
           Access::full_native_coverage(zoom_controller))) {
        std::cerr << "viewport_ready=" << zoom_controller.displayPreviewNativeViewportFullResolution()
                  << " whole_image=" << Access::full_native_coverage(zoom_controller)
                  << " native=" << zoom_controller.displayPreviewNativeSize().width()
                  << " scheduled=" << Access::viewport_max_preview_schedule_count(zoom_controller) << '\n';
        return fail("zoom-out fixture must have visible-only native coverage");
      }
      zoom_controller.setPreviewViewport(0, 0, 0, 0, 0, false);
      if (!wait_idle() ||
          !Access::full_native_coverage(zoom_controller))
        return fail("zooming out must restore all edited pixels without another edit");
      if (Access::recipe_json(zoom_controller) != recipe ||
          zoom_controller.editHistoryCurrentIndex() != history_index ||
          Access::render_generation(zoom_controller) != generation)
        return fail("zoom-out restoration must not change recipe, history, or edit generation");
    }
    const auto resident_revision = zoom_controller.displayPreviewNativeRevision();
    zoom_controller.setPreviewViewport(0.25, 0.25, 0.125, 0.125, 128, true);
    zoom_controller.setPreviewViewport(0, 0, 0, 0, 0, false);
    if (!wait_idle() ||
        zoom_controller.displayPreviewNativeRevision() != resident_revision)
      return fail("ordinary zoom out must reuse complete resident pixels");
    return EXIT_SUCCESS;
  }

  if (app.arguments().contains(QStringLiteral("--resident-zoom"))) {
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
    qputenv("PHOTO_EDITOR_CACHE_DIR", runtime_root.filePath("cache").toUtf8());
    qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
    const auto resident_source_path = runtime_root.filePath("resident.png");
    QImage resident_source(1024, 768, QImage::Format_ARGB32);
    resident_source.fill(QColor(92, 116, 140));
    if (!resident_source.save(resident_source_path)) return fail("cannot save zoom fixture");
    // A resident full-quality texture already covers every zoom/pan destination.
    // Moving the viewport must not re-render those pixels.
    photo_editor::desktop::EditorPreviewController resident_controller;
    photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
        resident_controller,
        std::make_unique<photo_editor::desktop::FakeEditorPreviewTexture>());
    resident_controller.loadAsset(QStringLiteral("resident-zoom"), resident_source_path,
        QString(), QString(), QString(), QString(), QString());
    if (!wait_for_preview_idle(resident_controller) ||
        !resident_controller.displayPreviewNativeViewportFullResolution())
      return fail("resident zoom fixture must contain current full-resolution pixels");
    const auto resident_schedule_before = photo_editor::desktop::EditorPreviewControllerTestAccess::
        viewport_max_preview_schedule_count(resident_controller);
    const auto resident_revision_before = resident_controller.displayPreviewNativeRevision();
    QElapsedTimer resident_timer;
    resident_timer.start();
    for (int step = 0; step < 30; ++step) {
      resident_controller.setPreviewViewport(0.01 * step, 0.01 * step, 0.125, 0.125, 128, true);
    }
    const auto resident_admission_ns = resident_timer.nsecsElapsed();
    if (!wait_for_preview_idle(resident_controller))
      return fail("resident zoom should remain idle");
    const auto resident_scheduled = photo_editor::desktop::EditorPreviewControllerTestAccess::
        viewport_max_preview_schedule_count(resident_controller) - resident_schedule_before;
    std::cout << "resident_zoom_admission_ns=" << resident_admission_ns
              << " resident_zoom_scheduled=" << resident_scheduled
              << " resident_zoom_total_ms=" << resident_timer.elapsed() << '\n';
    if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_ZOOM_BASELINE") &&
        (resident_scheduled != 0 ||
         resident_controller.displayPreviewNativeRevision() != resident_revision_before ||
         !resident_controller.displayPreviewNativeViewportFullResolution()))
      return fail("zoom/pan within current full-quality coverage must reuse resident pixels");

    return EXIT_SUCCESS;
  }

  photo_editor::desktop::EditorPreviewController controller;

  const auto patch_edge = [](
      double source_width, double source_height, double display_width,
      double display_height, double device_scale, bool mask_work,
      bool pixel_inspection) {
    return photo_editor::desktop::PreviewPublisher::patchEdgeForPresentation(
        photo_editor::desktop::PreviewViewportPresentation{
            .visible_source_pixel_width = source_width,
            .visible_source_pixel_height = source_height,
            .visible_display_width = display_width,
            .visible_display_height = display_height,
            .device_pixel_ratio = device_scale,
            .mask_work_active = mask_work,
            .pixel_inspection_active = pixel_inspection});
  };
  const auto inactive_edge =
      patch_edge(512.0, 384.0, 64.0, 48.0, 1.0, false, false);
  if (inactive_edge != 64) {
    return fail("non-inspection viewport requests should follow display-pixel demand");
  }

  const auto source_pixel_edge =
      patch_edge(512.0, 384.0, 64.0, 48.0, 1.0, false, true);
  if (source_pixel_edge != 512) {
    return fail("pixel inspection should request at least the visible source-pixel edge for 1:1 inspection");
  }

  const auto high_dpi_edge =
      patch_edge(80.0, 60.0, 120.0, 90.0, 2.0, false, true);
  if (high_dpi_edge != 240) {
    return fail("pixel inspection should preserve higher device-pixel display demand when it exceeds source pixels");
  }

  const auto mask_work_edge =
      patch_edge(128.0, 128.0, 220.0, 180.0, 1.0, true, true);
  if (mask_work_edge != 220) {
    return fail("pixel inspection should bypass the mask-work preview floor when source/display demand is lower");
  }

  controller.setPreviewViewport(0.25, 0.25, 0.125, 0.125, source_pixel_edge, true);
  if (!controller.previewViewportActive() ||
      !controller.previewViewportRect().isValid() ||
      controller.previewViewportPatchMaxEdge() != source_pixel_edge) {
    return fail("controller should publish the source-pixel-aware viewport patch edge");
  }

  controller.setPreviewViewport(0.0, 0.0, 0.0, 0.0, 0, false);
  if (controller.previewViewportActive() ||
      controller.previewViewportPatchMaxEdge() != 0) {
    return fail("clearing the preview viewport should clear the pixel-inspection patch edge");
  }

  const auto studio_workspace_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioImageViewport.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioPixelGridOverlay.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioDiagnosticOverlayStack.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioViewportLoupe.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioViewportInputCommandSurface.qml")) +
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioViewportBrushCursors.qml"));
  const auto controller_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.cpp"));
  const auto controller_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.h"));
  const auto texture_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_texture.h"));
  const auto mask_inspector_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskInspectorSections.qml"));
  const auto shared_mask_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/SharedMaskOverlay.qml"));
  const auto crop_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/CropOverlay.qml"));
  const auto mask_canvas_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskCanvasOverlay.qml"));
  if (studio_workspace_source.isEmpty() ||
      controller_source.isEmpty() ||
	      controller_header_source.isEmpty() ||
	      texture_header_source.isEmpty() ||
	      mask_inspector_source.isEmpty() ||
	      shared_mask_overlay_source.isEmpty() ||
	      crop_overlay_source.isEmpty() ||
	      mask_canvas_overlay_source.isEmpty() ||
	      !controller_source.contains(QStringLiteral("const auto source_size = sourceImageSize();\n  if (source_size.width() > 0)")) ||
	      !controller_source.contains(QStringLiteral("const auto source_size = sourceImageSize();\n  if (source_size.height() > 0)")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property bool pixelGridFullResolutionBacked: nativePreviewActive &&")) ||
	      !studio_workspace_source.contains(QStringLiteral("previewContext.displayPreviewNativeViewportFullResolution")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property bool pixelInspectionActive: pixelInspectionRequested &&")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property bool pixelGridVisible: pixelGridRequested &&")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property real oneToOneZoom: Math.max(")) ||
	      !studio_workspace_source.contains(QStringLiteral("basePaintedContentWidth * inspectionDevicePixelRatio")) ||
	      !studio_workspace_source.contains(QStringLiteral("const targetZoom = workspaceContext.clamp(")) ||
	      !studio_workspace_source.contains(QStringLiteral("inspectionPixelWidth *")) ||
	      !studio_workspace_source.contains(QStringLiteral("function onDisplayPreviewNativeViewportFullResolutionChanged()")) ||
		      !studio_workspace_source.contains(QStringLiteral("readonly property bool highCostInspectionOverlaysVisible: inspectionDisplayReady")) ||
			      !studio_workspace_source.contains(QStringLiteral("pixel-inspection-performance:v5|full-resolution-visible-tile-required:true|proxy-display-while-inspecting:false|viewport-publisher-service:true|signature-dedup:true|event-loop-coalesced:true|source-patch-refresh:immediate")) ||
			      !studio_workspace_source.contains(QStringLiteral("pixel-grid-render:v6|scenegraph-lines:true|viewport-bounded:true|visible-lines-only:true|no-raster-canvas:true|content-aligned:true|source-backed-required:true|visible-region-coverage-required:true|crop-output-coordinates:true")) ||
			      !studio_workspace_source.contains(QStringLiteral("readonly property bool gridGeometryValid:")) ||
			      studio_workspace_source.contains(QStringLiteral("id: pixelGridPaintTimer")) ||
		      !studio_workspace_source.contains(QStringLiteral("(zoom <= 1.01 && !pixelInspectionRequested)")) ||
	      !studio_workspace_source.contains(QStringLiteral("previewContext.schedulePreviewViewportPublication(")) ||
		      studio_workspace_source.contains(QStringLiteral("publishedPreviewViewportSettleTimer")) ||
		      studio_workspace_source.contains(QStringLiteral("lastPublishedPreviewViewportSignature")) ||
		      !controller_header_source.contains(QStringLiteral("Q_INVOKABLE void schedulePreviewViewportPublication(")) ||
			      !studio_workspace_source.contains(QStringLiteral("readonly property real basePaintedContentX: contentMargin")) ||
			      !studio_workspace_source.contains(QStringLiteral("contentX: viewport.basePaintedContentX + viewport.panX")) ||
			      !studio_workspace_source.contains(QStringLiteral("contentWidth: viewport.basePaintedContentWidth")) ||
				      !studio_workspace_source.contains(QStringLiteral("contentScale: viewport.zoom")) ||
			      !studio_workspace_source.contains(QStringLiteral("onPixelInspectionRequestedChanged: schedulePublishedPreviewViewportUpdate(true)")) ||
				      !studio_workspace_source.contains(QStringLiteral("function schedulePreviewChangedViewportUpdate()")) ||
	      !studio_workspace_source.contains(QStringLiteral("if (pixelInspectionRequested)")) ||
			      !studio_workspace_source.contains(QStringLiteral("function onPreviewChanged() { viewport.schedulePreviewChangedViewportUpdate() }")) ||
			      !controller_source.contains(QStringLiteral("preview.render.visible_tile_result_skipped")) ||
		      controller_source.contains(QStringLiteral("render_complete_to_handle_ms >= 500")) ||
		      !controller_source.contains(QStringLiteral("pending_visible_viewport_request")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("deep-zoom-canvas:v2|item:viewport-bounded|coordinates:window-translated|hidden:skip-paint")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("readonly property rect visibleCanvasWindow:")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("x: root.visibleCanvasWindow.x")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("firstViewPoint.x - resolvedOffsetX")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("readonly property bool deepZoomCanvasMode:")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("function boundedCanvasCoordinate(")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("!root.deepZoomCanvasMode")) ||
		      !shared_mask_overlay_source.contains(QStringLiteral("readonly property int maximumCanvasBackingEdge: 2048")) ||
		      !crop_overlay_source.contains(QStringLiteral("crop-canvas-memory:v1|max-backing-edge:2048|hidden-or-oversized:1x1|repaint:visible-safe-only")) ||
		      !crop_overlay_source.contains(QStringLiteral("width: backingAllowed ? parent.width : 1")) ||
		      !crop_overlay_source.contains(QStringLiteral("function scheduleSafePaint()")) ||
		      !mask_canvas_overlay_source.contains(QStringLiteral("mask-canvas-memory:v1|max-backing-edge:2048|hidden-or-oversized:1x1|repaint:visible-safe-only")) ||
		      !mask_canvas_overlay_source.contains(QStringLiteral("function canvasBackingExtent(requestedExtent)")) ||
		      !mask_canvas_overlay_source.contains(QStringLiteral("width: root.canvasBackingExtent(parent.width)")) ||
		      shared_mask_overlay_source.contains(QStringLiteral("ctx.translate(-x, -y)")) ||
		      shared_mask_overlay_source.contains(QStringLiteral("canvasWindow: root.visibleCanvasWindow")) ||
	      !studio_workspace_source.contains(QStringLiteral("loupeVisible: workspaceContext.loupeEnabled && viewport.loupeEnabled &&\n                      viewport.highCostInspectionOverlaysVisible")) ||
	      !studio_workspace_source.contains(QStringLiteral("focusPeakingEnabled: workspaceContext.focusPeakingOverlayEnabled && viewport.focusPeakingOverlayEnabled")) ||
	      !studio_workspace_source.contains(QStringLiteral("function startPixelBrushSource(operationName)")) ||
	      !studio_workspace_source.contains(QStringLiteral("function pixelBrushToolReady()")) ||
	      !studio_workspace_source.contains(QStringLiteral("function pixelExactToolReady()")) ||
	      !studio_workspace_source.contains(QStringLiteral("label: \"Pixel Brush Add\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSourcePixelBrushAddButton\"")) ||
		      !studio_workspace_source.contains(QStringLiteral("id: pixelBrushSnappedCursor")) ||
		      !studio_workspace_source.contains(QStringLiteral("pixel-brush-cursor:v2|source-cell:floor|screen-rect:mapper|visible:pixel-grid|source-backed:pixelGridFullResolutionBacked")) ||
		      !studio_workspace_source.contains(QStringLiteral("readonly property real sourcePixelInspectionZoom: inspectionPixelWidth > 0")) ||
		      !studio_workspace_source.contains(QStringLiteral("readonly property real maxInspectionZoom: Math.max(")) ||
		      !studio_workspace_source.contains(QStringLiteral("sourcePixelInspectionZoom * deepInspectionZoomMultiplier")) ||
		      !studio_workspace_source.contains(QStringLiteral("function wheelZoomFactor(wheel)")) ||
		      !studio_workspace_source.contains(QStringLiteral("viewport.setZoom(viewport.zoom * zoomFactor, wheel.x, wheel.y)")) ||
		      !studio_workspace_source.contains(QStringLiteral("adaptive-source-pixel|max:source-pixel-4x-min-6400")) ||
		      !studio_workspace_source.contains(QStringLiteral("Math.floor(point.x * sourceWidth)")) ||
	      !studio_workspace_source.contains(QStringLiteral("Math.floor(point.y * sourceHeight)")) ||
	      !studio_workspace_source.contains(QStringLiteral("pixelBrushVisible: workspaceContext.pixelExactToolReady() && viewport.pixelGridVisible")) ||
	      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(bool displayPreviewNativeFullResolution READ displayPreviewNativeFullResolution NOTIFY previewChanged)")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(bool displayPreviewNativeViewportFullResolution READ displayPreviewNativeViewportFullResolution NOTIFY displayPreviewNativeViewportFullResolutionChanged)")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(double activeMaskBrushFlow READ activeMaskBrushFlow WRITE setActiveMaskBrushFlow NOTIFY maskStateChanged)")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(double activeMaskBrushHardness READ activeMaskBrushHardness WRITE setActiveMaskBrushHardness NOTIFY maskStateChanged)")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(double activeMaskBrushPressureSize READ activeMaskBrushPressureSize WRITE setActiveMaskBrushPressureSize NOTIFY controlsChanged)")) ||
      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(bool activeMaskBrushAutoMask READ activeMaskBrushAutoMask WRITE setActiveMaskBrushAutoMask NOTIFY controlsChanged)")) ||
      !studio_workspace_source.contains(QStringLiteral("function setBrushPaintMode(operationName)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.startSelectionBrush(operation === \"subtract\")")) ||
      !studio_workspace_source.contains(QStringLiteral("return \"brush-defaults:v1|size:6|flow:100|hardness:45|density:100|opacity:100|spacing:75|smoothing:18|pressure:0/0/0|reset:true|operation-preserved:true\"")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string brushInspectorOwnershipContract: \"brush-inspector-controls:v1|visible-during-brush:true|owns:size-flow-hardness|parent-controls:density-opacity|more:presets-pressure-reset|hud:quick-only\"")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.activeMaskBrushFlow = root.clamp(value / 100.0, 0.05, 1.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.activeMaskBrushHardness = root.clamp(value / 100.0, 0.0, 1.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.activeMaskDensity = root.clamp(editorPreview.activeMaskDensity + delta, 0.0, 1.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("editorPreview.activeMaskOpacity = root.clamp(editorPreview.activeMaskOpacity + delta, 0.0, 1.0)")) ||
      !studio_workspace_source.contains(QStringLiteral("const pressure = point && point.pressure !== undefined ? point.pressure : 1.0")) ||
      !mask_inspector_source.contains(QStringLiteral("text: editorPreview.activeMaskBrushAutoMask ? \"Edge Aware\" : \"Freehand\"")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.activeMaskBrushAutoMask = !editorPreview.activeMaskBrushAutoMask")) ||
      !mask_inspector_source.contains(QStringLiteral("editorPreview.activeMaskBrushAutoMaskTolerance = value")) ||
      !controller_source.contains(QStringLiteral("visible_render_tiles_for_request(")) ||
	      !controller_source.contains(QStringLiteral("request->tile_visible_region_active && request->render_mode == QStringLiteral(\"max_preview_visible_tile\")")) ||
	      !controller_source.contains(QStringLiteral("qimage_patch_from_working_image_region(")) ||
	      !controller_source.contains(QStringLiteral("request->tile_visible_region")) ||
	      !controller_source.contains(QStringLiteral("visible-exact-region")) ||
	      !controller_source.contains(QStringLiteral("display_preview_texture_->ensureNativeTexture(")) ||
	      !controller_source.contains(QStringLiteral("preview_viewport_patch_max_edge_ > 0")) ||
	      !controller_source.contains(QStringLiteral("source_pixel_inspection_request")) ||
	      controller_source.contains(QStringLiteral("preview.render.visible_overlay_skipped")) ||
	      !controller_source.contains(QStringLiteral("render_complete_to_handle_ms")) ||
	      !controller_source.contains(QStringLiteral("const auto can_render_initial_visible_tile =")) ||
	      !controller_source.contains(QStringLiteral("queued_request.visible_tile_only")) ||
	      !texture_header_source.contains(QStringLiteral("virtual bool ensureNativeTexture(const QSize& size, QImage::Format format) = 0"))) {
	    return fail("Pixel Brush should stay separate from the normal soft Photo Brush and full-resolution pixel grid path");
	  }

  QImage source_dimension_fixture(32, 32, QImage::Format_ARGB32);
  source_dimension_fixture.fill(QColor(28, 34, 42));
  const auto source_dimension_path =
      runtime_root.filePath(QStringLiteral("source-dimension-fixture.png"));
  if (!source_dimension_fixture.save(source_dimension_path)) {
    return fail("source dimension fixture should be writable");
  }
  photo_editor::desktop::EditorPreviewController source_dimension_controller;
  source_dimension_controller.loadAsset(
      QStringLiteral("source-dimensions"),
      source_dimension_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(source_dimension_controller) ||
      source_dimension_controller.sourceImageWidth() != source_dimension_fixture.width() ||
      source_dimension_controller.sourceImageHeight() != source_dimension_fixture.height()) {
    return fail("source dimension controller should publish the fixture source size");
  }
  photo_editor::desktop::RenderResult poisoned_source_size_result;
  poisoned_source_size_result.success = true;
  poisoned_source_size_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          source_dimension_controller);
  poisoned_source_size_result.render_mode = QStringLiteral("interactive_preview");
  poisoned_source_size_result.preview_image = QImage(64, 64, QImage::Format_ARGB32);
  poisoned_source_size_result.preview_image.fill(QColor(84, 96, 128));
  poisoned_source_size_result.source_width = 1024;
  poisoned_source_size_result.source_height = 1024;
  poisoned_source_size_result.output_width = poisoned_source_size_result.preview_image.width();
  poisoned_source_size_result.output_height = poisoned_source_size_result.preview_image.height();
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      source_dimension_controller,
      false,
      poisoned_source_size_result);
  if (source_dimension_controller.sourceImageWidth() != source_dimension_fixture.width() ||
      source_dimension_controller.sourceImageHeight() != source_dimension_fixture.height()) {
    return fail("pixel grid source dimensions should come from the current asset, not stale preview render metadata");
  }

  const auto dimension_probes =
      photo_editor::desktop::EditorPreviewControllerTestAccess::source_size_cache_misses(
          source_dimension_controller);
  for (int read = 0; read < 100; ++read) {
    static_cast<void>(source_dimension_controller.sourceImageWidth());
    static_cast<void>(source_dimension_controller.sourceImageHeight());
    static_cast<void>(source_dimension_controller.cropRectWidth());
    static_cast<void>(source_dimension_controller.cropRectHeight());
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::source_size_cache_misses(
          source_dimension_controller) != dimension_probes) {
    return fail("repeated QML geometry reads must not reopen the source decoder");
  }
  QImage replacement_dimensions(64, 16, QImage::Format_ARGB32);
  replacement_dimensions.fill(QColor(28, 34, 42));
  if (!replacement_dimensions.save(source_dimension_path))
    return fail("replacement dimension fixture should be writable");
  QFile replaced_source(source_dimension_path);
  if (!replaced_source.open(QIODevice::ReadWrite) ||
      !replaced_source.setFileTime(QDateTime::currentDateTimeUtc().addSecs(2),
                                   QFileDevice::FileModificationTime)) {
    return fail("replacement source must have a distinct file revision");
  }
  replaced_source.close();
  if (source_dimension_controller.sourceImageWidth() != 64 ||
      source_dimension_controller.sourceImageHeight() != 16 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::source_size_cache_misses(
          source_dimension_controller) != dimension_probes + 1) {
    return fail("source dimension cache must invalidate when the original file changes");
  }
  const auto moved_source = source_dimension_path + QStringLiteral(".temporarily-moved");
  if (!QFile::rename(source_dimension_path, moved_source))
    return fail("temporary source move should succeed");
  static_cast<void>(source_dimension_controller.sourceImageWidth());
  if (!QFile::rename(moved_source, source_dimension_path) ||
      source_dimension_controller.sourceImageWidth() != 64 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::source_size_cache_misses(
          source_dimension_controller) != dimension_probes + 3) {
    return fail("a missing source must not leave a stale or permanent negative dimension cache");
  }

	  photo_editor::desktop::EditorPreviewController photo_brush_controller;
  QImage photo_brush_source(8, 8, QImage::Format_ARGB32);
  photo_brush_source.fill(QColor(88, 112, 140));
  const auto photo_brush_source_path =
      runtime_root.filePath(QStringLiteral("photo-brush-source.png"));
  if (!photo_brush_source.save(photo_brush_source_path)) {
    return fail("photo brush source image should be writable");
  }
  photo_brush_controller.loadAsset(
      QStringLiteral("photo-brush-controls"),
      photo_brush_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(photo_brush_controller)) {
    return fail("photo brush controller should finish its initial preview");
  }
  photo_brush_controller.startSelectionBrush(false);
  photo_brush_controller.setActiveMaskBrushSize(0.12);
  photo_brush_controller.setActiveMaskBrushFlow(0.35);
  photo_brush_controller.setActiveMaskBrushHardness(0.42);
  photo_brush_controller.setActiveMaskDensity(0.77);
  photo_brush_controller.setActiveMaskOpacity(0.66);
  photo_brush_controller.setActiveMaskFeather(0.08);
  photo_brush_controller.setActiveMaskBrushPressureSize(0.5);
  photo_brush_controller.setActiveMaskBrushPressureFlow(0.25);
  photo_brush_controller.setActiveMaskBrushPressureOpacity(0.125);
  photo_brush_controller.setActiveMaskBrushAutoMask(true);
  photo_brush_controller.setActiveMaskBrushAutoMaskTolerance(0.22);
  photo_brush_controller.setActiveMaskBrushAutoMaskContinuousSampling(true);
  photo_brush_controller.beginActiveBrushStroke();
  photo_brush_controller.addBrushPointWithPressure(0.5, 0.5, 0.41);
  const auto photo_brush_stack = photo_brush_controller.maskStack();
  const auto photo_brush_mask =
      photo_brush_stack.at(photo_brush_controller.activeMaskIndex()).toMap();
  const auto photo_brush_points = photo_brush_controller.activeMaskLiveBrushPoints();
  const auto photo_brush_point =
      photo_brush_points.isEmpty() ? QVariantMap{} : photo_brush_points.first().toMap();
  if (photo_brush_controller.activeMaskKind() != QStringLiteral("brush") ||
      photo_brush_mask.value(QStringLiteral("kind")).toString() != QStringLiteral("brush") ||
      photo_brush_mask.value(QStringLiteral("pixelSelection")).toBool() ||
      std::abs(photo_brush_controller.activeMaskBrushSize() - 0.12) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskBrushFlow() - 0.35) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskBrushHardness() - 0.42) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskDensity() - 0.77) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskOpacity() - 0.66) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskFeather() - 0.08) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskBrushPressureSize() - 0.5) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskBrushPressureFlow() - 0.25) > 1e-9 ||
      std::abs(photo_brush_controller.activeMaskBrushPressureOpacity() - 0.125) > 1e-9 ||
      !photo_brush_controller.activeMaskBrushAutoMask() ||
      std::abs(photo_brush_controller.activeMaskBrushAutoMaskTolerance() - 0.22) > 1e-9 ||
      !photo_brush_controller.activeMaskBrushAutoMaskContinuousSampling() ||
      std::abs(photo_brush_point.value(QStringLiteral("pressure")).toDouble() - 0.41) > 1e-9) {
    std::cerr << "kind=" << photo_brush_controller.activeMaskKind().toStdString()
              << " maskKind=" << photo_brush_mask.value(QStringLiteral("kind")).toString().toStdString()
              << " pixelSelection=" << photo_brush_mask.value(QStringLiteral("pixelSelection")).toBool()
              << " size=" << photo_brush_controller.activeMaskBrushSize()
              << " flow=" << photo_brush_controller.activeMaskBrushFlow()
              << " hardness=" << photo_brush_controller.activeMaskBrushHardness()
              << " density=" << photo_brush_controller.activeMaskDensity()
              << " opacity=" << photo_brush_controller.activeMaskOpacity()
              << " feather=" << photo_brush_controller.activeMaskFeather()
              << " pressureSize=" << photo_brush_controller.activeMaskBrushPressureSize()
              << " pressureFlow=" << photo_brush_controller.activeMaskBrushPressureFlow()
              << " pressureOpacity=" << photo_brush_controller.activeMaskBrushPressureOpacity()
              << " autoMask=" << photo_brush_controller.activeMaskBrushAutoMask()
              << " autoMaskTolerance=" << photo_brush_controller.activeMaskBrushAutoMaskTolerance()
              << " autoMaskContinuous=" << photo_brush_controller.activeMaskBrushAutoMaskContinuousSampling()
              << " pointCount=" << photo_brush_points.size()
              << " pointPressure=" << photo_brush_point.value(QStringLiteral("pressure")).toDouble()
              << '\n';
    return fail("normal Photo Brush should keep soft brush controls, pressure, and auto-mask settings separate from Pixel Brush exact-alpha edits");
  }

  QImage viewport_source(1024, 768, QImage::Format_ARGB32);
  viewport_source.fill(QColor(92, 116, 140));
  const auto viewport_source_path =
      runtime_root.filePath(QStringLiteral("pixel-viewport-source.png"));
  if (!viewport_source.save(viewport_source_path)) {
    return fail("pixel viewport source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController viewport_controller;
  viewport_controller.loadAsset(
      QStringLiteral("pixel-viewport"),
      viewport_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(viewport_controller)) {
    return fail("pixel viewport controller should finish its initial preview");
  }
  const auto schedule_count_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          viewport_max_preview_schedule_count(viewport_controller);
  viewport_controller.setPreviewViewport(0.25, 0.25, 0.125, 0.125, 128, true);
  const auto scheduled_visible_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          pending_full_render_request(viewport_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              viewport_max_preview_schedule_count(viewport_controller) <=
          schedule_count_before ||
      (!photo_editor::desktop::EditorPreviewControllerTestAccess::
           full_render_pending(viewport_controller) &&
       !photo_editor::desktop::EditorPreviewControllerTestAccess::
           full_render_running(viewport_controller)) ||
      !scheduled_visible_request.visible_tile_only ||
      !scheduled_visible_request.tile_visible_region_active ||
      scheduled_visible_request.render_reason != QStringLiteral("viewport_change") ||
	      scheduled_visible_request.dirty_domain != QStringLiteral("publish_texture") ||
	      scheduled_visible_request.planned_visible_tile_count <= 0 ||
	      scheduled_visible_request.planned_background_tile_count < 0 ||
	      photo_editor::desktop::EditorPreviewControllerTestAccess::
	          overlay_render_pending(viewport_controller) ||
	      photo_editor::desktop::EditorPreviewControllerTestAccess::
	          overlay_render_running(viewport_controller)) {
	    return fail("pixel inspection viewport should schedule a source-resolution visible-tile max preview request");
	  }

  const QRectF latest_viewport_rect(0.75, 0.75, 0.125, 0.125);
  viewport_controller.setPreviewViewport(
      latest_viewport_rect.x(),
      latest_viewport_rect.y(),
      latest_viewport_rect.width(),
      latest_viewport_rect.height(),
      128,
      true);
  const auto latest_visible_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          pending_full_render_request(viewport_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::
              viewport_max_preview_schedule_count(viewport_controller) !=
          schedule_count_before + 2 ||
      latest_visible_request.tile_visible_region != latest_viewport_rect ||
      (!photo_editor::desktop::EditorPreviewControllerTestAccess::
           full_render_pending(viewport_controller) &&
       !photo_editor::desktop::EditorPreviewControllerTestAccess::
           full_render_running(viewport_controller))) {
    return fail("deep zoom should retain the latest visible-tile request while an older viewport tile is rendering");
  }
  if (!wait_for_preview_idle(viewport_controller)) {
    return fail("queued deep-zoom visible-tile requests should settle");
  }

  photo_editor::desktop::EditorPreviewController native_patch_controller;
  auto native_patch_texture =
      std::make_unique<photo_editor::desktop::FakeEditorPreviewTexture>();
  auto* native_patch_texture_ptr = native_patch_texture.get();
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      native_patch_controller,
      std::move(native_patch_texture));
  QImage visible_patch(128, 96, QImage::Format_ARGB32);
  visible_patch.fill(QColor(180, 84, 52));
  photo_editor::desktop::RenderResult visible_patch_result;
  visible_patch_result.success = true;
  visible_patch_result.full_resolution = true;
  visible_patch_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          native_patch_controller);
  visible_patch_result.render_mode = QStringLiteral("max_preview_visible_tile");
  visible_patch_result.render_reason = QStringLiteral("viewport_change");
  visible_patch_result.dirty_domain = QStringLiteral("publish_texture");
  visible_patch_result.texture_upload_image = visible_patch;
  visible_patch_result.texture_upload_rect = QRect(256, 192, 128, 96);
  visible_patch_result.partial_texture_upload_candidate = true;
  visible_patch_result.output_width = viewport_source.width();
  visible_patch_result.output_height = viewport_source.height();
  visible_patch_result.source_width = viewport_source.width();
  visible_patch_result.source_height = viewport_source.height();
  const QRectF visible_patch_viewport(0.25, 0.25, 0.125, 0.125);
  visible_patch_result.tile_visible_region_active = true;
  visible_patch_result.tile_visible_region = visible_patch_viewport;
  visible_patch_result.render_completed_at_msecs =
      QDateTime::currentMSecsSinceEpoch() - 1000;
  native_patch_controller.setPreviewViewport(
      visible_patch_viewport.x(),
      visible_patch_viewport.y(),
      visible_patch_viewport.width(),
      visible_patch_viewport.height(),
      128,
      true);
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      native_patch_controller,
      true,
      visible_patch_result);
  if (!native_patch_controller.displayPreviewNativeAvailable() ||
      !native_patch_controller.displayPreviewNativeFullResolution() ||
      !native_patch_controller.displayPreviewNativeViewportFullResolution() ||
      native_patch_controller.displayPreviewNativeSize() != viewport_source.size() ||
      native_patch_texture_ptr->ensureTextureCount() != 1 ||
      native_patch_texture_ptr->patchUploadCount() != 1 ||
      native_patch_texture_ptr->uploadCount() != 0 ||
      native_patch_texture_ptr->lastPatchUploadRect() !=
          visible_patch_result.texture_upload_rect) {
    return fail("visible source-resolution tile uploads should allocate a full-size native backing and patch only the visible rect");
  }

  const QRectF uncovered_viewport(0.75, 0.75, 0.125, 0.125);
  native_patch_controller.setPreviewViewport(0.30, 0.25, 0.125, 0.125, 128, true);
  if (native_patch_controller.displayPreviewNativeViewportFullResolution()) {
    return fail("partially overlapping rendered pixels must not qualify an expanded view as fully rendered");
  }
  native_patch_controller.setPreviewViewport(
      uncovered_viewport.x(),
      uncovered_viewport.y(),
      uncovered_viewport.width(),
      uncovered_viewport.height(),
      128,
      true);
  if (native_patch_controller.displayPreviewNativeViewportFullResolution()) {
    return fail("panning beyond rendered full-resolution coverage must stop exposing the pixel grid until the new source tile arrives");
  }

  QImage brush_metadata_source(512, 384, QImage::Format_ARGB32);
  brush_metadata_source.fill(QColor(48, 64, 96));
  const auto brush_metadata_path =
      runtime_root.filePath(QStringLiteral("brush-metadata-source.png"));
  if (!brush_metadata_source.save(brush_metadata_path)) {
    return fail("brush metadata source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController brush_metadata_controller;
  brush_metadata_controller.loadAsset(
      QStringLiteral("brush-metadata"),
      brush_metadata_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(brush_metadata_controller)) {
    return fail("brush metadata controller should finish its initial preview");
  }
  brush_metadata_controller.addMask(QStringLiteral("brush"));
  if (!wait_for_preview_idle(brush_metadata_controller)) {
    return fail("brush metadata controller should settle after adding a brush mask");
  }
  brush_metadata_controller.setActiveMaskBrushSize(0.05);
  brush_metadata_controller.setActiveMaskBrushSpacing(0.20);
  if (!wait_for_preview_idle(brush_metadata_controller)) {
    return fail("brush metadata controller should settle after brush setup");
  }
  brush_metadata_controller.beginActiveBrushStroke();
  brush_metadata_controller.addBrushPoint(0.20, 0.30);
  brush_metadata_controller.addBrushPoint(0.26, 0.34);
  if (!finish_brush_stroke(brush_metadata_controller)) {
    return fail("brush metadata stroke should commit and report raster telemetry");
  }
  const auto brush_metadata_mask =
      brush_metadata_controller.maskStack()
          .at(brush_metadata_controller.activeMaskIndex())
          .toMap();
  const auto brush_metadata_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          brush_metadata_controller);
  const auto brush_metadata_data =
      brush_metadata_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (brush_metadata_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      brush_metadata_mask.value(QStringLiteral("segmentationWidth")).toInt() !=
          brush_metadata_source.width() ||
      brush_metadata_mask.value(QStringLiteral("segmentationHeight")).toInt() !=
          brush_metadata_source.height() ||
      brush_metadata_data.size() != brush_metadata_source.width() * brush_metadata_source.height() ||
      brush_metadata_telemetry.value(QStringLiteral("brush_commit_final_raster_width")).toInt() !=
          brush_metadata_mask.value(QStringLiteral("segmentationWidth")).toInt() ||
      brush_metadata_telemetry.value(QStringLiteral("brush_commit_final_raster_height")).toInt() !=
          brush_metadata_mask.value(QStringLiteral("segmentationHeight")).toInt() ||
      brush_metadata_telemetry.value(QStringLiteral("raster_dirty_x")).toInt() < 0 ||
      brush_metadata_telemetry.value(QStringLiteral("raster_dirty_y")).toInt() < 0 ||
      brush_metadata_telemetry.value(QStringLiteral("brush_commit_dirty_rect_width")).toInt() <= 0 ||
      brush_metadata_telemetry.value(QStringLiteral("brush_commit_dirty_rect_height")).toInt() <= 0 ||
      brush_metadata_telemetry.value(QStringLiteral("raster_dirty_x")).toInt() +
              brush_metadata_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() >
          brush_metadata_source.width() ||
      brush_metadata_telemetry.value(QStringLiteral("raster_dirty_y")).toInt() +
              brush_metadata_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() >
          brush_metadata_source.height() ||
      brush_metadata_telemetry.value(QStringLiteral("brush_commit_dirty_rect_width")).toInt() !=
          brush_metadata_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() ||
      brush_metadata_telemetry.value(QStringLiteral("brush_commit_dirty_rect_height")).toInt() !=
          brush_metadata_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() ||
      !brush_metadata_telemetry.value(QStringLiteral("brush_commit_source_revision_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      brush_metadata_telemetry.value(QStringLiteral("brush_commit_source_of_truth")).toString() !=
          QStringLiteral("brushStrokes") ||
      brush_metadata_mask.value(QStringLiteral("brushAlphaSourceOfTruth")).toString() !=
          QStringLiteral("brushStrokes")) {
    return fail("brush commit metadata should report final raster size, dirty rect, source revision, and source-of-truth mode");
  }

  brush_metadata_controller.beginActiveBrushStroke();
  brush_metadata_controller.addBrushPoint(0.34, 0.42);
  brush_metadata_controller.addBrushPoint(0.38, 0.45);
  if (!finish_brush_stroke(brush_metadata_controller)) {
    return fail("source-free brush stroke should commit and report raster telemetry");
  }
  const auto source_free_brush_mask =
      brush_metadata_controller.maskStack()
          .at(brush_metadata_controller.activeMaskIndex())
          .toMap();
  const auto source_free_brush_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          brush_metadata_controller);
  const auto source_free_width =
      source_free_brush_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto source_free_height =
      source_free_brush_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto source_free_data =
      source_free_brush_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (source_free_brush_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      !source_free_brush_telemetry.value(QStringLiteral("incremental")).toBool() ||
      source_free_brush_telemetry.value(QStringLiteral("source_decoded")).toBool() ||
      source_free_width != brush_metadata_source.width() ||
      source_free_height != brush_metadata_source.height() ||
      source_free_data.size() != source_free_width * source_free_height ||
      !source_free_brush_telemetry.value(QStringLiteral("dirty_region_valid")).toBool() ||
      source_free_brush_telemetry.value(QStringLiteral("dirty_region_area")).toDouble() <= 0.0 ||
      source_free_brush_telemetry.value(QStringLiteral("dirty_region_area")).toDouble() >= 0.25 ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_x")).toInt() < 0 ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_y")).toInt() < 0 ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() <= 0 ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() <= 0 ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() >=
          source_free_width ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() >=
          source_free_height ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_x")).toInt() +
              source_free_brush_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() >
          source_free_width ||
      source_free_brush_telemetry.value(QStringLiteral("raster_dirty_y")).toInt() +
              source_free_brush_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() >
          source_free_height ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_final_raster_width")).toInt() !=
          source_free_width ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_final_raster_height")).toInt() !=
          source_free_height ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_dirty_rect_width")).toInt() !=
          source_free_brush_telemetry.value(QStringLiteral("raster_dirty_width")).toInt() ||
      source_free_brush_telemetry.value(QStringLiteral("brush_commit_dirty_rect_height")).toInt() !=
          source_free_brush_telemetry.value(QStringLiteral("raster_dirty_height")).toInt() ||
      source_free_brush_mask.value(QStringLiteral("brushAlphaSourceOfTruth")).toString() !=
          QStringLiteral("brushStrokes")) {
    return fail("source-free manual brush commits should avoid full source decode and report bounded dirty raster metadata");
  }

  QImage auto_mask_source(1536, 1024, QImage::Format_ARGB32);
  for (int y = 0; y < auto_mask_source.height(); ++y) {
    for (int x = 0; x < auto_mask_source.width(); ++x) {
      auto_mask_source.setPixelColor(
          x,
          y,
          x < auto_mask_source.width() / 2
              ? QColor(216, 56 + (y % 16), 42)
              : QColor(48, 96, 210 - (x % 16)));
    }
  }
  const auto auto_mask_source_path =
      runtime_root.filePath(QStringLiteral("auto-mask-footprint-source.png"));
  if (!auto_mask_source.save(auto_mask_source_path)) {
    return fail("auto-mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController auto_mask_controller;
  auto_mask_controller.loadAsset(
      QStringLiteral("auto-mask-footprint"),
      auto_mask_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(auto_mask_controller)) {
    return fail("auto-mask controller should finish its initial preview");
  }
  auto_mask_controller.addMask(QStringLiteral("brush"));
  auto_mask_controller.setActiveMaskBrushSize(0.025);
  auto_mask_controller.setActiveMaskBrushSpacing(0.20);
  auto_mask_controller.setActiveMaskBrushAutoMask(true);
  auto_mask_controller.setActiveMaskBrushAutoMaskTolerance(0.18);
  auto_mask_controller.beginActiveBrushStroke();
  auto_mask_controller.addBrushPoint(0.48, 0.50);
  auto_mask_controller.addBrushPoint(0.50, 0.50);
  if (!finish_brush_stroke(auto_mask_controller)) {
    return fail("auto-mask brush stroke should commit and report decode telemetry");
  }
  const auto auto_mask_telemetry =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_mask_work_telemetry(
          auto_mask_controller);
  const auto auto_mask =
      auto_mask_controller.maskStack().at(auto_mask_controller.activeMaskIndex()).toMap();
  if (auto_mask_telemetry.value(QStringLiteral("operation")).toString() !=
          QStringLiteral("brush_rasterization") ||
      !auto_mask_telemetry.value(QStringLiteral("source_decoded")).toBool() ||
      auto_mask_telemetry.value(QStringLiteral("source_decode_edge")).toInt() != 512 ||
      !auto_mask_telemetry.value(QStringLiteral("source_decode_bounded")).toBool() ||
      auto_mask_telemetry.value(QStringLiteral("source_decode_width")).toInt() <= 0 ||
      auto_mask_telemetry.value(QStringLiteral("source_decode_height")).toInt() <= 0 ||
      auto_mask_telemetry.value(QStringLiteral("source_decode_width")).toInt() >
          auto_mask_telemetry.value(QStringLiteral("source_decode_edge")).toInt() ||
      auto_mask_telemetry.value(QStringLiteral("source_decode_height")).toInt() >
          auto_mask_telemetry.value(QStringLiteral("source_decode_edge")).toInt() ||
      auto_mask_telemetry.value(QStringLiteral("auto_mask_candidate_pixels")).toLongLong() <= 0 ||
      auto_mask_telemetry.value(QStringLiteral("auto_mask_edge_evaluation_pixels")).toLongLong() <=
          0 ||
      auto_mask_telemetry.value(QStringLiteral("brush_commit_source_of_truth")).toString() !=
          QStringLiteral("brushStrokes") ||
      auto_mask.value(QStringLiteral("brushAlphaSourceOfTruth")).toString() !=
          QStringLiteral("brushStrokes")) {
    return fail("auto-mask brush commits should use a bounded source-footprint decode and report lower-resolution assist telemetry");
  }

  const auto committed_brush_data =
      source_free_brush_mask.value(QStringLiteral("segmentationData")).toByteArray();
  photo_editor::desktop::EditorPreviewControllerTestAccess::clear_scaled_mask_matte_cache();
  const auto preview_scaled_brush_mask =
      photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask(
          source_free_brush_mask,
          QSize(96, 72),
          QStringLiteral("preview-display"));
  const auto preview_scaled_data =
      preview_scaled_brush_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (preview_scaled_brush_mask.value(QStringLiteral("segmentationWidth")).toInt() != 96 ||
      preview_scaled_brush_mask.value(QStringLiteral("segmentationHeight")).toInt() != 72 ||
      preview_scaled_brush_mask.value(QStringLiteral("scaledMattePurpose")).toString() !=
          QStringLiteral("preview-display") ||
      preview_scaled_brush_mask.value(QStringLiteral("scaledMatteSourceWidth")).toInt() !=
          source_free_width ||
      preview_scaled_brush_mask.value(QStringLiteral("scaledMatteSourceHeight")).toInt() !=
          source_free_height ||
      !preview_scaled_brush_mask.value(QStringLiteral("scaledMatteRebuiltFromBrushStrokes")).toBool() ||
      preview_scaled_brush_mask.value(QStringLiteral("scaledMatteBrushReplayPointCount")).toInt() <= 0 ||
      preview_scaled_data.size() != 96 * 72 ||
      preview_scaled_data == committed_brush_data ||
      source_free_brush_mask.value(QStringLiteral("segmentationWidth")).toInt() != source_free_width ||
      source_free_brush_mask.value(QStringLiteral("segmentationHeight")).toInt() != source_free_height ||
      source_free_brush_mask.value(QStringLiteral("segmentationData")).toByteArray() !=
          committed_brush_data ||
      source_free_brush_mask.value(QStringLiteral("brushAlphaSourceOfTruth")).toString() !=
          QStringLiteral("brushStrokes") ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask_matte_cache_entries() != 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask_matte_cache_hits() != 0 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::scaled_mask_matte_cache_misses() != 0) {
    return fail("preview brush mattes should be display-only stroke replays and must not replace the committed final alpha payload");
  }

  QImage preview_image(4, 3, QImage::Format_ARGB32);
  QImage reference_image(4, 3, QImage::Format_ARGB32);
  for (int y = 0; y < preview_image.height(); ++y) {
    for (int x = 0; x < preview_image.width(); ++x) {
      preview_image.setPixelColor(
          x,
          y,
          QColor(20 + x * 30, 40 + y * 20, 70 + x + y));
      reference_image.setPixelColor(
          x,
          y,
          QColor(80 + x * 10, 90 + y * 15, 100 + x * 2 + y));
    }
  }

  photo_editor::desktop::RenderResult pixel_result;
  pixel_result.success = true;
  pixel_result.full_resolution = false;
  pixel_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
  pixel_result.render_mode = QStringLiteral("settled_preview");
  pixel_result.render_reason = QStringLiteral("pixel_inspection_test");
  pixel_result.preview_image = preview_image;
  pixel_result.reference_preview_image = reference_image;
  pixel_result.output_width = preview_image.width();
  pixel_result.output_height = preview_image.height();
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      controller,
      false,
      pixel_result);

  controller.updatePixelInspector(1.0, 1.0);
  const auto inspector = controller.pixelInspector();
  const auto after = inspector.value(QStringLiteral("after")).toMap();
  const auto before = inspector.value(QStringLiteral("before")).toMap();
  const auto expected_after = preview_image.pixelColor(3, 2);
  const auto expected_before = reference_image.pixelColor(3, 2);
  if (!inspector.value(QStringLiteral("active")).toBool() ||
      !inspector.value(QStringLiteral("hasAfter")).toBool() ||
      !inspector.value(QStringLiteral("hasBefore")).toBool() ||
      inspector.value(QStringLiteral("pixelX")).toInt() != 3 ||
      inspector.value(QStringLiteral("pixelY")).toInt() != 2 ||
      after.value(QStringLiteral("red")).toInt() != expected_after.red() ||
      after.value(QStringLiteral("green")).toInt() != expected_after.green() ||
      after.value(QStringLiteral("blue")).toInt() != expected_after.blue() ||
      before.value(QStringLiteral("red")).toInt() != expected_before.red() ||
      before.value(QStringLiteral("green")).toInt() != expected_before.green() ||
      before.value(QStringLiteral("blue")).toInt() != expected_before.blue()) {
    return fail("pixel inspector should report exact source pixel coordinates and before/after RGB values");
  }

  controller.clearPixelInspector();
  if (!controller.pixelInspector().isEmpty()) {
    return fail("clearing the pixel inspector should remove transient coordinate and color state");
  }

  QImage pixel_selection_source(5, 4, QImage::Format_ARGB32);
  pixel_selection_source.fill(QColor(18, 24, 32));
  const auto pixel_selection_source_path =
      runtime_root.filePath(QStringLiteral("pixel-selection-source.png"));
  if (!pixel_selection_source.save(pixel_selection_source_path)) {
    return fail("pixel selection source image should be writable");
  }

  photo_editor::desktop::EditorPreviewController pixel_controller;
  pixel_controller.loadAsset(
      QStringLiteral("pixel-selection"),
      pixel_selection_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!pixel_controller.applyActiveSelectionPixel(0.5, 1.0 / 3.0, QStringLiteral("add"))) {
    return fail("pixel add should create a source-resolution alpha selection component");
  }
  auto stack = pixel_controller.maskStack();
  if (stack.size() != 1) {
    return fail("first pixel add should create exactly one pixel selection component");
  }
  auto add_mask = stack.at(0).toMap();
  auto add_data = add_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (add_mask.value(QStringLiteral("kind")).toString() != QStringLiteral("alpha") ||
      add_mask.value(QStringLiteral("operation")).toString() != QStringLiteral("add") ||
      !add_mask.value(QStringLiteral("pixelSelection")).toBool() ||
      add_mask.value(QStringLiteral("pixelSelectionContract")).toString() !=
          QStringLiteral("pixel-selection-alpha:v1|source-resolution:true|exact-byte:true|dirty-rect:true") ||
      add_mask.value(QStringLiteral("segmentationWidth")).toInt() != pixel_selection_source.width() ||
      add_mask.value(QStringLiteral("segmentationHeight")).toInt() != pixel_selection_source.height() ||
      selected_pixel_count(add_data) != 1 ||
      alpha_at(add_mask, 2, 1) != 255U ||
      add_mask.value(QStringLiteral("pixelDirtyX")).toInt() != 2 ||
      add_mask.value(QStringLiteral("pixelDirtyY")).toInt() != 1 ||
      add_mask.value(QStringLiteral("pixelDirtyWidth")).toInt() != 1 ||
      add_mask.value(QStringLiteral("pixelDirtyHeight")).toInt() != 1) {
    std::cerr << "kind=" << add_mask.value(QStringLiteral("kind")).toString().toStdString()
              << " op=" << add_mask.value(QStringLiteral("operation")).toString().toStdString()
              << " pixelSelection=" << add_mask.value(QStringLiteral("pixelSelection")).toBool()
              << " size=" << add_mask.value(QStringLiteral("segmentationWidth")).toInt()
              << "x" << add_mask.value(QStringLiteral("segmentationHeight")).toInt()
              << " count=" << selected_pixel_count(add_data)
              << " alpha(2,1)=" << static_cast<int>(alpha_at(add_mask, 2, 1))
              << " dirty=" << add_mask.value(QStringLiteral("pixelDirtyX")).toInt()
              << "," << add_mask.value(QStringLiteral("pixelDirtyY")).toInt()
              << " " << add_mask.value(QStringLiteral("pixelDirtyWidth")).toInt()
              << "x" << add_mask.value(QStringLiteral("pixelDirtyHeight")).toInt()
              << '\n';
    return fail("pixel add should change exactly the addressed source pixel and record a one-pixel dirty rect");
  }

  if (!pixel_controller.applyActiveSelectionPixel(1.0, 1.0, QStringLiteral("add"))) {
    return fail("second pixel add should update the active pixel alpha component");
  }
  stack = pixel_controller.maskStack();
  add_mask = stack.at(0).toMap();
  add_data = add_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (stack.size() != 1 ||
      selected_pixel_count(add_data) != 2 ||
      alpha_at(add_mask, 2, 1) != 255U ||
      alpha_at(add_mask, 4, 3) != 255U) {
    return fail("pixel add should update the active alpha payload without neighbor bleed");
  }

  photo_editor::desktop::EditorPreviewController cropped_pixel_controller;
  cropped_pixel_controller.loadAsset(
      QStringLiteral("cropped-pixel-selection"),
      pixel_selection_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  cropped_pixel_controller.updateCropRect(0.2, 0.25, 0.4, 0.5);
  if (!cropped_pixel_controller.applyActiveSelectionPixel(
          0.0,
          0.0,
          QStringLiteral("add"))) {
    return fail("cropped pixel selection should map the displayed output cell back to its exact source pixel");
  }
  const auto cropped_pixel_mask =
      cropped_pixel_controller.maskStack().constFirst().toMap();
  if (!pixel_mask_matches(
          cropped_pixel_mask,
          QStringLiteral("add"),
          cropped_pixel_mask.value(QStringLiteral("maskSetId")).toString(),
          pixel_selection_source.width(),
          pixel_selection_source.height(),
          1,
          1)) {
    return fail("the top-left cropped output pixel should address source pixel 1,1 rather than uncropped pixel 0,0");
  }
  const auto pixel_outline_trace = pixel_controller.activeMaskOutlineTrace();
  if (pixel_outline_trace.value(QStringLiteral("outline_simplification_contract")).toString() !=
          QStringLiteral("disabled-for-pixel-brush-inspection:v1") ||
      pixel_outline_trace.value(QStringLiteral("outline_simplification_applied")).toBool() ||
      pixel_outline_trace.value(QStringLiteral("outline_simplification_disabled_reason")).toString() !=
          QStringLiteral("pixel-brush-inspection") ||
      pixel_outline_trace.value(QStringLiteral("outline_simplification_tolerance_pixels")).toDouble() !=
          0.0 ||
      pixel_outline_trace.value(QStringLiteral("outline_simplification_max_error_pixels")).toDouble() !=
          0.0 ||
      !pixel_outline_trace.value(QStringLiteral("outline_simplification_within_tolerance")).toBool()) {
    return fail("pixel brush inspection outlines should bypass simplification and report the disabled contract");
  }

  pixel_controller.undo();
  stack = pixel_controller.maskStack();
  add_mask = stack.at(0).toMap();
  add_data = add_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (stack.size() != 1 ||
      selected_pixel_count(add_data) != 1 ||
      alpha_at(add_mask, 2, 1) != 255U ||
      alpha_at(add_mask, 4, 3) != 0U) {
    return fail("undo should restore exact alpha bytes before the second pixel add");
  }
  pixel_controller.redo();
  stack = pixel_controller.maskStack();
  add_mask = stack.at(0).toMap();
  add_data = add_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (stack.size() != 1 ||
      selected_pixel_count(add_data) != 2 ||
      alpha_at(add_mask, 2, 1) != 255U ||
      alpha_at(add_mask, 4, 3) != 255U) {
    return fail("redo should restore exact alpha bytes after the second pixel add");
  }

  if (!pixel_controller.applyActiveSelectionPixel(0.25, 2.0 / 3.0, QStringLiteral("deselect"))) {
    return fail("pixel deselect should create a subtract component in the active selection group");
  }
  stack = pixel_controller.maskStack();
  if (stack.size() != 2) {
    return fail("pixel deselect should add one subtract component to the active selection group");
  }
  add_mask = stack.at(0).toMap();
  const auto subtract_mask = stack.at(1).toMap();
  const auto subtract_data = subtract_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (subtract_mask.value(QStringLiteral("kind")).toString() != QStringLiteral("alpha") ||
      subtract_mask.value(QStringLiteral("operation")).toString() != QStringLiteral("subtract") ||
      subtract_mask.value(QStringLiteral("maskSetId")).toString() !=
          add_mask.value(QStringLiteral("maskSetId")).toString() ||
      selected_pixel_count(subtract_data) != 1 ||
      alpha_at(subtract_mask, 1, 2) != 255U ||
      alpha_at(subtract_mask, 0, 0) != 0U) {
    return fail("pixel deselect should write one subtract alpha byte in the same selection group");
  }

  pixel_controller.setActiveMaskIndex(0);
  if (!pixel_controller.applyActiveSelectionPixel(0.5, 1.0 / 3.0, QStringLiteral("clear"))) {
    return fail("pixel clear should remove an exact byte from the active pixel component");
  }
  stack = pixel_controller.maskStack();
  add_mask = stack.at(0).toMap();
  add_data = add_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (selected_pixel_count(add_data) != 1 ||
      alpha_at(add_mask, 2, 1) != 0U ||
      alpha_at(add_mask, 4, 3) != 255U) {
    return fail("pixel clear should only clear the addressed source pixel");
  }

  if (!pixel_controller.applyActiveSelectionPixel(0.0, 0.0, QStringLiteral("replace"))) {
    return fail("pixel replace should create an exact replace component in the active selection group");
  }
  stack = pixel_controller.maskStack();
  if (stack.size() != 3) {
    return fail("pixel replace should add one replace component alongside add/subtract pixel edits");
  }
  add_mask = stack.at(0).toMap();
  const auto replace_mask = stack.at(1).toMap();
  const auto replace_data = replace_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (replace_mask.value(QStringLiteral("kind")).toString() != QStringLiteral("alpha") ||
      replace_mask.value(QStringLiteral("operation")).toString() != QStringLiteral("replace") ||
      replace_mask.value(QStringLiteral("maskSetId")).toString() !=
          add_mask.value(QStringLiteral("maskSetId")).toString() ||
      selected_pixel_count(replace_data) != 1 ||
      alpha_at(replace_mask, 0, 0) != 255U ||
      alpha_at(replace_mask, 1, 0) != 0U) {
    return fail("pixel replace should write one exact alpha byte in the active selection group");
  }

  photo_editor::desktop::EditorPreviewController scoped_layer_controller;
  scoped_layer_controller.loadAsset(
      QStringLiteral("pixel-selection-layer-scope"),
      pixel_selection_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  scoped_layer_controller.addMask(QStringLiteral("linear"));
  if (scoped_layer_controller.maskCount() != 1 ||
      scoped_layer_controller.photoLayerCount() != 1) {
    return fail("selection-scoped adjustment layer fixture should start with one mask and one layer");
  }
  const auto scoped_base_mask = scoped_layer_controller.maskStack().at(0).toMap();
  const auto scoped_set_id = scoped_base_mask.value(QStringLiteral("maskSetId")).toString();
  const auto scoped_layer_before = scoped_layer_controller.photoLayers().at(0).toMap();
  if (scoped_set_id.isEmpty() ||
      !scoped_layer_before.value(QStringLiteral("selectionScoped")).toBool() ||
      scoped_layer_before.value(QStringLiteral("selectionMaskSetId")).toString() != scoped_set_id ||
      scoped_layer_before.value(QStringLiteral("selectionMaskSetIds")).toList().size() != 1 ||
      scoped_layer_before.value(QStringLiteral("selectionMaskSetIds")).toList().constFirst().toString() !=
          scoped_set_id) {
    return fail("selection-scoped adjustment layer fixture should be linked to the active mask set");
  }
  if (!scoped_layer_controller.applyActiveSelectionPixel(0.5, 0.5, QStringLiteral("add")) ||
      !scoped_layer_controller.applyActiveSelectionPixel(1.0, 1.0, QStringLiteral("deselect"))) {
    return fail("pixel edits should apply while a selection-scoped adjustment layer is linked");
  }
  const auto scoped_stack_after = scoped_layer_controller.maskStack();
  const auto scoped_layer_after = scoped_layer_controller.photoLayers().at(0).toMap();
  if (scoped_layer_controller.photoLayerCount() != 1 ||
      scoped_stack_after.size() != 3 ||
      !scoped_layer_after.value(QStringLiteral("selectionScoped")).toBool() ||
      scoped_layer_after.value(QStringLiteral("selectionMaskSetId")).toString() != scoped_set_id ||
      scoped_layer_after.value(QStringLiteral("selectionMaskSetIds")).toList().size() != 1 ||
      scoped_layer_after.value(QStringLiteral("selectionMaskSetIds")).toList().constFirst().toString() !=
          scoped_set_id ||
      !scoped_layer_after.value(QStringLiteral("selectionMaskSetSignature")).toString().contains(scoped_set_id) ||
      scoped_stack_after.at(0).toMap().value(QStringLiteral("maskSetId")).toString() != scoped_set_id ||
      scoped_stack_after.at(1).toMap().value(QStringLiteral("maskSetId")).toString() != scoped_set_id ||
      scoped_stack_after.at(2).toMap().value(QStringLiteral("maskSetId")).toString() != scoped_set_id ||
      scoped_stack_after.at(1).toMap().value(QStringLiteral("operation")).toString() != QStringLiteral("add") ||
      scoped_stack_after.at(2).toMap().value(QStringLiteral("operation")).toString() != QStringLiteral("subtract")) {
    return fail("pixel selection edits should keep adjustment layers scoped to the intended selection group");
  }

  photo_editor::desktop::EditorPreviewController brush_compose_controller;
  brush_compose_controller.loadAsset(
      QStringLiteral("pixel-compose-brush"),
      pixel_selection_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  brush_compose_controller.startSelectionBrush(false);
  auto compose_stack = brush_compose_controller.maskStack();
  if (compose_stack.size() != 1 ||
      brush_compose_controller.activeMaskKind() != QStringLiteral("brush")) {
    return fail("pixel compose brush fixture should start with one active brush selection component");
  }
  const auto brush_mask_before = compose_stack.at(0).toMap();
  const auto brush_set_id = brush_mask_before.value(QStringLiteral("maskSetId")).toString();
  if (brush_set_id.isEmpty() ||
      !brush_compose_controller.applyActiveSelectionPixel(0.5, 1.0 / 3.0, QStringLiteral("add"))) {
    return fail("pixel edit should apply on top of an active brush selection component");
  }
  compose_stack = brush_compose_controller.maskStack();
  if (compose_stack.size() != 2 ||
      compose_stack.at(0).toMap().value(QStringLiteral("id")).toString() !=
          brush_mask_before.value(QStringLiteral("id")).toString() ||
      !pixel_mask_matches(
          compose_stack.at(1).toMap(),
          QStringLiteral("add"),
          brush_set_id,
          pixel_selection_source.width(),
          pixel_selection_source.height(),
          2,
          1)) {
    return fail("pixel edit should compose with brush selections as a same-group alpha component");
  }

  photo_editor::desktop::EditorPreviewController lasso_compose_controller;
  lasso_compose_controller.loadAsset(
      QStringLiteral("pixel-compose-lasso"),
      pixel_selection_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  lasso_compose_controller.startLassoSelection(false);
  const QVariantList lasso_points{
      normalized_point(0.20, 0.20),
      normalized_point(0.80, 0.24),
      normalized_point(0.70, 0.78),
      normalized_point(0.24, 0.72),
  };
  for (const auto& point_value : lasso_points) {
    const auto point = point_value.toMap();
    lasso_compose_controller.appendActivePolygonPoint(
        point.value(QStringLiteral("x")).toDouble(),
        point.value(QStringLiteral("y")).toDouble());
  }
  if (!lasso_compose_controller.closeActiveLassoPath()) {
    return fail("pixel compose lasso fixture should close before adding a pixel component");
  }
  compose_stack = lasso_compose_controller.maskStack();
  if (compose_stack.size() != 1 ||
      lasso_compose_controller.activeMaskKind() != QStringLiteral("lasso")) {
    return fail("pixel compose lasso fixture should start with one active lasso component");
  }
  const auto lasso_mask_before = compose_stack.at(0).toMap();
  const auto lasso_set_id = lasso_mask_before.value(QStringLiteral("maskSetId")).toString();
  if (lasso_set_id.isEmpty() ||
      !lasso_compose_controller.applyActiveSelectionPixel(1.0, 1.0, QStringLiteral("deselect"))) {
    return fail("pixel edit should apply on top of an active lasso selection component");
  }
  compose_stack = lasso_compose_controller.maskStack();
  if (compose_stack.size() != 2 ||
      compose_stack.at(0).toMap().value(QStringLiteral("id")).toString() !=
          lasso_mask_before.value(QStringLiteral("id")).toString() ||
      compose_stack.at(0).toMap().value(QStringLiteral("lassoRawPointCount")).toInt() !=
          lasso_points.size() ||
      !pixel_mask_matches(
          compose_stack.at(1).toMap(),
          QStringLiteral("subtract"),
          lasso_set_id,
          pixel_selection_source.width(),
          pixel_selection_source.height(),
          4,
          3)) {
    return fail("pixel edit should compose with lasso selections as a same-group subtract alpha component");
  }

  const auto range_compose_source_path =
      runtime_root.filePath(QStringLiteral("pixel-compose-range-source.png"));
  QImage range_compose_source(8, 8, QImage::Format_ARGB32);
  for (int y = 0; y < range_compose_source.height(); ++y) {
    for (int x = 0; x < range_compose_source.width(); ++x) {
      range_compose_source.setPixelColor(
          x,
          y,
          x < 4 ? QColor(210, 42, 42 + y * 4) : QColor(42 + y * 4, 90, 210));
    }
  }
  if (!range_compose_source.save(range_compose_source_path)) {
    return fail("pixel compose range source image should be writable");
  }

  photo_editor::desktop::EditorPreviewController color_compose_controller;
  color_compose_controller.loadAsset(
      QStringLiteral("pixel-compose-color-range"),
      range_compose_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  color_compose_controller.addMask(QStringLiteral("color"));
  if (!color_compose_controller.sampleActiveColorRangeAt(0.25, 0.25)) {
    return fail("pixel compose color fixture should sample a color range before adding a pixel component");
  }
  compose_stack = color_compose_controller.maskStack();
  if (compose_stack.size() != 1 ||
      color_compose_controller.activeMaskKind() != QStringLiteral("color") ||
      compose_stack.at(0).toMap().value(QStringLiteral("colorSamples")).toList().isEmpty()) {
    return fail("pixel compose color fixture should start with one sampled color range component");
  }
  const auto color_mask_before = compose_stack.at(0).toMap();
  const auto color_set_id = color_mask_before.value(QStringLiteral("maskSetId")).toString();
  if (color_set_id.isEmpty() ||
      !color_compose_controller.applyActiveSelectionPixel(0.5, 0.5, QStringLiteral("add"))) {
    return fail("pixel edit should apply on top of an active color range component");
  }
  compose_stack = color_compose_controller.maskStack();
  if (compose_stack.size() != 2 ||
      compose_stack.at(0).toMap().value(QStringLiteral("id")).toString() !=
          color_mask_before.value(QStringLiteral("id")).toString() ||
      compose_stack.at(0).toMap().value(QStringLiteral("colorSamples")).toList().isEmpty() ||
      !pixel_mask_matches(
          compose_stack.at(1).toMap(),
          QStringLiteral("add"),
          color_set_id,
          range_compose_source.width(),
          range_compose_source.height(),
          4,
          4)) {
    return fail("pixel edit should compose with color range selections as a same-group alpha component");
  }

  photo_editor::desktop::EditorPreviewController luminance_compose_controller;
  luminance_compose_controller.loadAsset(
      QStringLiteral("pixel-compose-luminance-range"),
      range_compose_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  luminance_compose_controller.addMask(QStringLiteral("luminance"));
  if (!luminance_compose_controller.beginActiveLuminanceRangeSample(0.12, 0.12) ||
      !luminance_compose_controller.commitActiveLuminanceRangeSample(0.42, 0.42)) {
    return fail("pixel compose luminance fixture should sample a luminance patch before adding a pixel component");
  }
  compose_stack = luminance_compose_controller.maskStack();
  if (compose_stack.size() != 1 ||
      luminance_compose_controller.activeMaskKind() != QStringLiteral("luminance") ||
      compose_stack.at(0).toMap().value(QStringLiteral("luminanceSamples")).toList().isEmpty()) {
    return fail("pixel compose luminance fixture should start with one sampled luminance range component");
  }
  const auto luminance_mask_before = compose_stack.at(0).toMap();
  const auto luminance_set_id = luminance_mask_before.value(QStringLiteral("maskSetId")).toString();
  if (luminance_set_id.isEmpty() ||
      !luminance_compose_controller.applyActiveSelectionPixel(0.25, 0.75, QStringLiteral("deselect"))) {
    return fail("pixel edit should apply on top of an active luminance range component");
  }
  compose_stack = luminance_compose_controller.maskStack();
  if (compose_stack.size() != 2 ||
      compose_stack.at(0).toMap().value(QStringLiteral("id")).toString() !=
          luminance_mask_before.value(QStringLiteral("id")).toString() ||
      compose_stack.at(0).toMap().value(QStringLiteral("luminanceSamples")).toList().isEmpty() ||
      !pixel_mask_matches(
          compose_stack.at(1).toMap(),
          QStringLiteral("subtract"),
          luminance_set_id,
          range_compose_source.width(),
          range_compose_source.height(),
          2,
          6)) {
    return fail("pixel edit should compose with luminance range selections as a same-group subtract alpha component");
  }

  const auto smart_compose_source_path =
      runtime_root.filePath(QStringLiteral("pixel-compose-smart-source.png"));
  QImage smart_compose_source(32, 32, QImage::Format_ARGB32);
  smart_compose_source.fill(Qt::white);
  for (int y = 10; y <= 21; ++y) {
    for (int x = 5; x <= 14; ++x) {
      smart_compose_source.setPixelColor(x, y, QColor(210, 78, 78));
    }
    for (int x = 18; x <= 27; ++x) {
      smart_compose_source.setPixelColor(x, y, QColor(74, 118, 212));
    }
  }
  if (!smart_compose_source.save(smart_compose_source_path)) {
    return fail("pixel compose smart source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController smart_compose_controller;
  smart_compose_controller.loadAsset(
      QStringLiteral("pixel-compose-smart"),
      smart_compose_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  smart_compose_controller.smartSelectObjectAt(0.25, 0.50, true);
  if (!wait_for_smart_commit(smart_compose_controller, 1)) {
    return fail("pixel compose smart fixture should commit before adding a pixel component");
  }
  compose_stack = smart_compose_controller.maskStack();
  if (compose_stack.size() != 1 ||
      smart_compose_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("pixel compose smart fixture should start with one active object component");
  }
  const auto object_mask_before = compose_stack.at(0).toMap();
  const auto object_set_id = object_mask_before.value(QStringLiteral("maskSetId")).toString();
  if (object_set_id.isEmpty() ||
      !smart_compose_controller.applyActiveSelectionPixel(0.5, 0.5, QStringLiteral("add"))) {
    return fail("pixel edit should apply on top of an active smart/object component");
  }
  compose_stack = smart_compose_controller.maskStack();
  if (compose_stack.size() != 2 ||
      compose_stack.at(0).toMap().value(QStringLiteral("id")).toString() !=
          object_mask_before.value(QStringLiteral("id")).toString() ||
      compose_stack.at(0).toMap().value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      !pixel_mask_matches(
          compose_stack.at(1).toMap(),
          QStringLiteral("add"),
          object_set_id,
          smart_compose_source.width(),
          smart_compose_source.height(),
          16,
          16)) {
    return fail("pixel edit should compose with smart/object selections as a same-group alpha component");
  }

  const auto generated_compose_source_path =
      runtime_root.filePath(QStringLiteral("pixel-compose-generated-source.png"));
  QImage generated_compose_source(24, 24, QImage::Format_ARGB32);
  generated_compose_source.fill(QColor(160, 190, 230));
  if (!generated_compose_source.save(generated_compose_source_path)) {
    return fail("pixel compose generated source image should be writable");
  }
  auto generated_mask = photo_editor::desktop::default_mask_definition(QStringLiteral("sky"), 1);
  generated_mask.insert(QStringLiteral("id"), QStringLiteral("generated-sky-fixture"));
  generated_mask.insert(QStringLiteral("name"), QStringLiteral("Generated Sky Fixture"));
  generated_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("generated-sky-set"));
  generated_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Generated Sky"));
  generated_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  generated_mask.insert(QStringLiteral("segmentationData"), QByteArray(8 * 8, '\xff'));
  generated_mask.insert(QStringLiteral("segmentationWidth"), 8);
  generated_mask.insert(QStringLiteral("segmentationHeight"), 8);
  generated_mask.insert(QStringLiteral("generatedMaskMetadata"), QVariantMap{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("rasterSchemaVersion"), 1},
      {QStringLiteral("generatedOutputSchemaVersion"), 1},
      {QStringLiteral("modelName"), QStringLiteral("test-generated-mask")},
      {QStringLiteral("modelVersion"), QStringLiteral("semantic-mask-v1")},
      {QStringLiteral("promptType"), QStringLiteral("semantic:sky")},
      {QStringLiteral("sourceWidth"), generated_compose_source.width()},
      {QStringLiteral("sourceHeight"), generated_compose_source.height()},
      {QStringLiteral("sourceResolution"), QStringLiteral("24x24")},
      {QStringLiteral("sourceImageVersion"), QStringLiteral("test-source-version")},
  });
  generated_mask.insert(
      QStringLiteral("generatedAlphaSourceContract"),
      QStringLiteral("generated-alpha-plus-metadata-v1"));
  generated_mask.insert(QStringLiteral("generatedAlphaModelName"), QStringLiteral("test-generated-mask"));
  generated_mask.insert(QStringLiteral("generatedAlphaModelVersion"), QStringLiteral("semantic-mask-v1"));
  generated_mask.insert(QStringLiteral("generatedAlphaTargetType"), QStringLiteral("sky"));
  generated_mask.insert(QStringLiteral("generatedAlphaPromptType"), QStringLiteral("semantic:sky"));
  generated_mask.insert(QStringLiteral("generatedAlphaSourceImageVersion"), QStringLiteral("test-source-version"));
  generated_mask.insert(QStringLiteral("generatedAlphaWidth"), 8);
  generated_mask.insert(QStringLiteral("generatedAlphaHeight"), 8);
  generated_mask.insert(QStringLiteral("generatedAlphaDownscaleFactor"), 3.0);
  generated_mask.insert(QStringLiteral("generatedAlphaConfidenceAvailable"), false);
  generated_mask.insert(QStringLiteral("generatedAlphaRefinementStateSignature"), QStringLiteral("sha1:test-refine"));
  generated_mask.insert(QStringLiteral("generatedAlphaSourceSignature"), QStringLiteral("sha1:test-alpha"));
  photo_editor::desktop::EditState generated_state;
  generated_state.mask_stack = QVariantList{generated_mask};
  const auto generated_recipe_json =
      photo_editor::desktop::serialize_edit_recipe_json(generated_state);

  photo_editor::desktop::EditorPreviewController generated_compose_controller;
  generated_compose_controller.loadAsset(
      QStringLiteral("pixel-compose-generated"),
      generated_compose_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      generated_recipe_json);
  generated_compose_controller.setActiveMaskIndex(0);
  compose_stack = generated_compose_controller.maskStack();
  if (compose_stack.size() != 1 ||
      generated_compose_controller.activeMaskKind() != QStringLiteral("sky")) {
    return fail("pixel compose generated fixture should start with one generated sky component");
  }
  const auto generated_mask_before = compose_stack.at(0).toMap();
  const auto generated_set_id = generated_mask_before.value(QStringLiteral("maskSetId")).toString();
  if (generated_set_id.isEmpty() ||
      generated_mask_before.value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      !generated_compose_controller.applyActiveSelectionPixel(0.0, 0.0, QStringLiteral("add"))) {
    return fail("pixel edit should apply on top of an active generated mask component");
  }
  compose_stack = generated_compose_controller.maskStack();
  if (compose_stack.size() != 2 ||
      compose_stack.at(0).toMap().value(QStringLiteral("id")).toString() !=
          generated_mask_before.value(QStringLiteral("id")).toString() ||
      compose_stack.at(0).toMap().value(QStringLiteral("generatedAlphaSourceContract")).toString() !=
          QStringLiteral("generated-alpha-plus-metadata-v1") ||
      !pixel_mask_matches(
          compose_stack.at(1).toMap(),
          QStringLiteral("add"),
          generated_set_id,
          generated_compose_source.width(),
          generated_compose_source.height(),
          0,
          0)) {
    return fail("pixel edit should compose with generated masks as a same-group alpha component");
  }

  return EXIT_SUCCESS;
}
