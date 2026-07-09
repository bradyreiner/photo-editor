#include <cstdlib>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QColor>
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

#include "edit_recipe_state.h"
#include "editor_preview_controller.h"
#include "editor_preview_texture.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static int render_generation(const EditorPreviewController& controller) {
    return controller.render_generation_;
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
  }

  static QVariantMap last_mask_work_telemetry(const EditorPreviewController& controller) {
    return controller.last_mask_work_telemetry_;
  }

  static QVariantMap scaled_mask(
      const QVariantMap& mask,
      const QSize& target_size,
      const QString& purpose) {
    return EditorPreviewController::scaledMaskForTesting(mask, target_size, purpose);
  }

  static void clear_scaled_mask_matte_cache() {
    EditorPreviewController::clearScaledMaskMatteCacheForTesting();
  }

  static int scaled_mask_matte_cache_entries() {
    return EditorPreviewController::scaledMaskMatteCacheEntryCountForTesting();
  }

  static int scaled_mask_matte_cache_hits() {
    return EditorPreviewController::scaledMaskMatteCacheHitCountForTesting();
  }

  static int scaled_mask_matte_cache_misses() {
    return EditorPreviewController::scaledMaskMatteCacheMissCountForTesting();
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

  photo_editor::desktop::EditorPreviewController controller;

  const auto inactive_edge = controller.previewViewportPatchEdgeForInspection(
      512.0,
      384.0,
      64.0,
      48.0,
      1.0,
      false,
      false);
  if (inactive_edge != 64) {
    return fail("non-inspection viewport requests should follow display-pixel demand");
  }

  const auto source_pixel_edge = controller.previewViewportPatchEdgeForInspection(
      512.0,
      384.0,
      64.0,
      48.0,
      1.0,
      false,
      true);
  if (source_pixel_edge != 512) {
    return fail("pixel inspection should request at least the visible source-pixel edge for 1:1 inspection");
  }

  const auto high_dpi_edge = controller.previewViewportPatchEdgeForInspection(
      80.0,
      60.0,
      120.0,
      90.0,
      2.0,
      false,
      true);
  if (high_dpi_edge != 240) {
    return fail("pixel inspection should preserve higher device-pixel display demand when it exceeds source pixels");
  }

  const auto mask_work_edge = controller.previewViewportPatchEdgeForInspection(
      128.0,
      128.0,
      220.0,
      180.0,
      1.0,
      true,
      true);
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
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  const auto controller_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.cpp"));
  const auto controller_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.h"));
  const auto texture_header_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_texture.h"));
  const auto mask_inspector_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskInspectorSections.qml"));
	  if (studio_workspace_source.isEmpty() ||
	      controller_source.isEmpty() ||
	      controller_header_source.isEmpty() ||
	      texture_header_source.isEmpty() ||
	      mask_inspector_source.isEmpty() ||
	      !controller_source.contains(QStringLiteral("const auto source_size = sourceImageSize();\n  if (source_size.width() > 0)")) ||
	      !controller_source.contains(QStringLiteral("const auto source_size = sourceImageSize();\n  if (source_size.height() > 0)")) ||
	      !studio_workspace_source.contains(QStringLiteral("readonly property bool pixelGridFullResolutionBacked: (nativePreviewActive &&")) ||
	      !studio_workspace_source.contains(QStringLiteral("editorPreview.displayPreviewNativeFullResolution")) ||
	      !studio_workspace_source.contains(QStringLiteral("Number(editorPreview.sourceImageWidth || 0) <= editorPreview.previewTargetEdge")) ||
	      !studio_workspace_source.contains(QStringLiteral("Number(editorPreview.sourceImageHeight || 0) <= editorPreview.previewTargetEdge")) ||
		      !studio_workspace_source.contains(QStringLiteral("readonly property bool pixelGridVisible: pixelInspectionActive")) ||
			      !studio_workspace_source.contains(QStringLiteral("readonly property bool highCostInspectionOverlaysVisible: true")) ||
			      !studio_workspace_source.contains(QStringLiteral("pixel-inspection-performance:v3|allow-loupe-and-analysis-overlays-while-source-pixels-visible:true|viewport-publish-signature-dedup:true|callLater-coalesced:true|deferred-source-patch-refresh:520ms")) ||
			      !studio_workspace_source.contains(QStringLiteral("pixel-grid-render:v4|canvas:viewport-bounded|visible-lines-only:true|repaint:coalesced|content-aligned:true|immediate-grid:true|source-backed-flag:true|full-resolution-backed:pixelGridFullResolutionBacked")) ||
		      !studio_workspace_source.contains(QStringLiteral("(zoom <= 1.01 && !pixelInspectionActive)")) ||
		      !studio_workspace_source.contains(QStringLiteral("property string lastPublishedPreviewViewportSignature: \"\"")) ||
		      !studio_workspace_source.contains(QStringLiteral("function publishedPreviewViewportSignature(")) ||
		      !studio_workspace_source.contains(QStringLiteral("if (nextSignature === lastPublishedPreviewViewportSignature)")) ||
			      !studio_workspace_source.contains(QStringLiteral("property bool publishedPreviewViewportUpdateQueued: false")) ||
			      !studio_workspace_source.contains(QStringLiteral("if (publishedPreviewViewportUpdateQueued)")) ||
			      !studio_workspace_source.contains(QStringLiteral("readonly property real basePaintedContentX: contentMargin")) ||
			      !studio_workspace_source.contains(QStringLiteral("x: viewport.basePaintedContentX + viewport.panX")) ||
			      !studio_workspace_source.contains(QStringLiteral("width: viewport.basePaintedContentWidth")) ||
				      !studio_workspace_source.contains(QStringLiteral("scale: viewport.zoom")) ||
				      !studio_workspace_source.contains(QStringLiteral("onPixelInspectionActiveChanged: schedulePublishedPreviewViewportUpdate(false)")) ||
				      !studio_workspace_source.contains(QStringLiteral("function schedulePreviewChangedViewportUpdate()")) ||
				      !studio_workspace_source.contains(QStringLiteral("if (pixelInspectionActive)\n                return")) ||
			      !studio_workspace_source.contains(QStringLiteral("function onPreviewChanged() { viewport.schedulePreviewChangedViewportUpdate() }")) ||
			      !controller_source.contains(QStringLiteral("preview.render.visible_tile_result_skipped")) ||
			      !controller_source.contains(QStringLiteral("render_complete_to_handle_ms >= 500")) ||
			      !studio_workspace_source.contains(QStringLiteral("visible: root.loupeEnabled && viewport.loupeEnabled &&\n                     viewport.highCostInspectionOverlaysVisible")) ||
		      !studio_workspace_source.contains(QStringLiteral("visible: root.focusPeakingOverlayEnabled && viewport.focusPeakingOverlayEnabled &&\n                     viewport.highCostInspectionOverlaysVisible")) ||
	      !studio_workspace_source.contains(QStringLiteral("function startPixelBrushSource(operationName)")) ||
	      !studio_workspace_source.contains(QStringLiteral("function pixelBrushToolReady()")) ||
	      !studio_workspace_source.contains(QStringLiteral("function pixelExactToolReady()")) ||
	      !studio_workspace_source.contains(QStringLiteral("label: \"Pixel Brush Add\"")) ||
	      !studio_workspace_source.contains(QStringLiteral("objectName: \"maskSourcePixelBrushAddButton\"")) ||
		      !studio_workspace_source.contains(QStringLiteral("id: pixelBrushSnappedCursor")) ||
		      !studio_workspace_source.contains(QStringLiteral("pixel-brush-cursor:v2|source-cell:floor|screen-rect:mapper|visible:pixel-grid|source-backed:pixelGridFullResolutionBacked")) ||
		      !studio_workspace_source.contains(QStringLiteral("readonly property real sourcePixelInspectionZoom: Number(editorPreview.sourceImageWidth || 0) > 0")) ||
		      !studio_workspace_source.contains(QStringLiteral("readonly property real maxInspectionZoom: Math.max(")) ||
		      !studio_workspace_source.contains(QStringLiteral("sourcePixelInspectionZoom * deepInspectionZoomMultiplier")) ||
		      !studio_workspace_source.contains(QStringLiteral("function wheelZoomFactor(wheel)")) ||
		      !studio_workspace_source.contains(QStringLiteral("viewport.setZoom(viewport.zoom * zoomFactor, wheel.x, wheel.y)")) ||
		      !studio_workspace_source.contains(QStringLiteral("adaptive-source-pixel|max:source-pixel-4x-min-6400")) ||
		      !studio_workspace_source.contains(QStringLiteral("Math.floor(point.x * sourceWidth)")) ||
	      !studio_workspace_source.contains(QStringLiteral("Math.floor(point.y * sourceHeight)")) ||
	      !studio_workspace_source.contains(QStringLiteral("visible: root.pixelExactToolReady() && viewport.pixelGridVisible && cell.valid")) ||
	      !controller_header_source.contains(QStringLiteral("Q_PROPERTY(bool displayPreviewNativeFullResolution READ displayPreviewNativeFullResolution NOTIFY previewChanged)")) ||
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
	      !controller_source.contains(QStringLiteral("request->visible_tile_only && request->tile_visible_region_active")) ||
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
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      native_patch_controller,
      true,
      visible_patch_result);
  if (!native_patch_controller.displayPreviewNativeAvailable() ||
      !native_patch_controller.displayPreviewNativeFullResolution() ||
      native_patch_controller.displayPreviewNativeSize() != viewport_source.size() ||
      native_patch_texture_ptr->ensureTextureCount() != 1 ||
      native_patch_texture_ptr->patchUploadCount() != 1 ||
      native_patch_texture_ptr->uploadCount() != 0 ||
      native_patch_texture_ptr->lastPatchUploadRect() !=
          visible_patch_result.texture_upload_rect) {
    return fail("visible source-resolution tile uploads should allocate a full-size native backing and patch only the visible rect");
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
          5)) {
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
