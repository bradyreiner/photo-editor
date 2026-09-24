#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include "editor_preview_controller.h"
#include "editor_preview_texture.h"
#include "editor_preview_image_sink.h"

namespace photo_editor::desktop {

class FeedbackImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString&, const QImage&) override {}
  void clearImage(const QString&) override {}
};

RenderResult render_preview_request_for_test(const RenderRequest& request);

struct EditorPreviewControllerTestAccess {
  static RenderRequest interactive_render_request(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false);
  }

  static RenderRequest full_render_request(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(true);
  }

  static void set_next_interactive_render_draft(
      EditorPreviewController& controller,
      bool draft) {
    controller.next_interactive_render_draft_ = draft;
  }

  static RenderResult render_preview_request_sync(const RenderRequest& request) {
    return render_preview_request_for_test(request);
  }

  static bool interactive_render_timer_active(const EditorPreviewController& controller) {
    return controller.render_scheduler_.interactiveActive();
  }

  static int interactive_render_timer_interval(const EditorPreviewController& controller) {
    return controller.render_scheduler_.interactiveDelayMs();
  }

  static bool full_render_timer_active(const EditorPreviewController& controller) {
    return controller.render_scheduler_.fullActive();
  }

  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }

  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }

  static bool interactive_render_running(const EditorPreviewController& controller) {
    return controller.interactive_render_watcher_.isRunning();
  }

  static bool interactive_render_pending(const EditorPreviewController& controller) {
    return controller.interactive_request_pending_;
  }

  static bool photo_layer_adjustment_render_timer_active(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.isActive();
  }

  static int photo_layer_adjustment_render_timer_interval(
      const EditorPreviewController& controller) {
    return controller.photo_layer_adjustment_render_timer_.interval();
  }

  static int render_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.generation();
  }

  static int last_full_preview_generation(const EditorPreviewController& controller) {
    return controller.preview_session_.lastFullGeneration();
  }

  static void handle_render_result(
      EditorPreviewController& controller,
      bool full_resolution,
      const RenderResult& result) {
    controller.handleRenderResult(full_resolution, result);
  }

  static void set_display_preview_texture(
      EditorPreviewController& controller,
      std::unique_ptr<EditorPreviewTexture> texture) {
    controller.display_preview_texture_ = std::move(texture);
    controller.display_preview_native_revision_ = 0;
    controller.display_preview_native_rendered_cache_key_.clear();
  }
};

class FeedbackTestTexture final : public EditorPreviewTexture {
 public:
  [[nodiscard]] bool available() const override { return true; }
  [[nodiscard]] const char* backendName() const override { return "feedback-test"; }

  bool ensureNativeTexture(const QSize& size, QImage::Format format) override {
    if (!size.isValid()) {
      return false;
    }
    size_ = size;
    format_ = format;
    return true;
  }

  bool upload(const QImage& image) override {
    if (image.isNull()) {
      return false;
    }
    size_ = image.size();
    format_ = image.format();
    ++upload_count_;
    return true;
  }

  bool uploadRegion(const QImage& image, const QRect& region) override {
    return !image.isNull() && !region.isEmpty() && image.size() == size_;
  }

  bool uploadPatch(const QImage& image, const QRect& destination_region) override {
    return !image.isNull() && !destination_region.isEmpty() &&
           image.size() == destination_region.size() && size_.isValid();
  }

  void clear() override { size_ = {}; }
  [[nodiscard]] bool hasNativeTexture() const override { return size_.isValid(); }
  [[nodiscard]] quint64 nativeTextureHandle() const override { return hasNativeTexture() ? 1 : 0; }
  [[nodiscard]] QSize nativeTextureSize() const override { return size_; }
  [[nodiscard]] const char* nativeTextureFormatName() const override {
    return format_ == QImage::Format_RGBA16FPx4 ? "rgba16float" : "bgra8unorm";
  }
  [[nodiscard]] int nativeTextureBytesPerPixel() const override {
    return format_ == QImage::Format_RGBA16FPx4 ? 8 : 4;
  }
  [[nodiscard]] int uploadCount() const { return upload_count_; }

 private:
  QSize size_;
  QImage::Format format_ = QImage::Format_Invalid;
  int upload_count_ = 0;
};

}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms = 7000) {
  auto elapsed_ms = 0;
  while (elapsed_ms < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller.processing() &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
            controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
            controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
            controller) &&
        !photo_editor::desktop::EditorPreviewControllerTestAccess::
            photo_layer_adjustment_render_timer_active(controller)) {
      return true;
    }
    QThread::msleep(20);
    elapsed_ms += 20;
  }
  return false;
}

bool within_first_frame_budget(const std::function<void()>& action) {
  QElapsedTimer timer;
  timer.start();
  action();
  return timer.elapsed() <= 33;
}

int maximum_channel_delta(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) {
    return 255;
  }
  auto maximum = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto left_pixel = left.pixelColor(x, y);
      const auto right_pixel = right.pixelColor(x, y);
      maximum = std::max(
          maximum,
          std::max({
              std::abs(left_pixel.red() - right_pixel.red()),
              std::abs(left_pixel.green() - right_pixel.green()),
              std::abs(left_pixel.blue() - right_pixel.blue()),
          }));
    }
  }
  return maximum;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("preview feedback proof should create isolated runtime storage");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());

  QImage source(640, 480, QImage::Format_ARGB32);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixel(x, y, qRgba(24 + x % 180, 36 + y % 160, 92 + (x + y) % 140, 255));
    }
  }
  const auto source_path = runtime_root.filePath(QStringLiteral("preview-feedback-source.png"));
  if (!source.save(source_path)) {
    return fail("preview feedback source fixture should be writable");
  }

  photo_editor::desktop::FeedbackImageSink image_sink;
  photo_editor::desktop::EditorPreviewController controller;
  controller.setImageProvider(&image_sink);
  controller.loadAsset("unsupported", runtime_root.filePath("unsupported.xyz"), {}, {}, {}, {}, {});
  if (controller.previewFailureStage() != QStringLiteral("source_format")) {
    return fail("an unsupported selected source should retain its format failure");
  }
  controller.loadAsset({}, {}, {}, {}, {}, {}, {});
  if (controller.hasAsset() || !controller.previewFailureStage().isEmpty() ||
      !controller.previewFailureDetail().isEmpty() || controller.processing()) {
    return fail("an empty selection should clear failures and stay idle");
  }
  controller.setPreviewPresentationMetrics(
      1000.0, 800.0, 2.0, false);
  if (controller.previewTargetEdge() != 4000) {
    return fail(
        "controller should derive ordinary preview quality from presentation facts");
  }
  controller.setPreviewPresentationMetrics(
      320.0, 200.0, 2.0, true);
  if (controller.previewTargetEdge() != 3072) {
    return fail(
        "controller should request the settled mask quality floor through shared render policy");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      controller,
      std::make_unique<photo_editor::desktop::FeedbackTestTexture>());
  controller.loadAsset(
      QStringLiteral("preview-feedback-asset"),
      source_path,
      QString(),
      QStringLiteral("Feedback Camera"),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(controller)) {
    return fail("preview feedback controller should settle its initial full-resolution preview");
  }

  QImage fidelity_source(256, 192, QImage::Format_ARGB32);
  for (int y = 0; y < fidelity_source.height(); ++y) {
    for (int x = 0; x < fidelity_source.width(); ++x) {
      fidelity_source.setPixel(
          x,
          y,
          qRgba(
              18 + (x * 5 + y * 2) % 210,
              22 + (x * 2 + y * 7) % 205,
              28 + (x * 3 + y * 3) % 200,
              255));
    }
  }
  const auto fidelity_source_path =
      runtime_root.filePath(QStringLiteral("preview-color-fidelity-source.png"));
  if (!fidelity_source.save(fidelity_source_path)) {
    return fail("preview color-fidelity source fixture should be writable");
  }
  auto fidelity_state = photo_editor::desktop::EditRecipeState{};
  fidelity_state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  fidelity_state.texture = 0.36;
  fidelity_state.clarity = 0.42;
  fidelity_state.dehaze = 0.31;
  fidelity_state.temperature = 18.0;
  fidelity_state.tint = -7.0;
  fidelity_state.vibrance = 22.0;
  photo_editor::desktop::EditorPreviewController fidelity_controller;
  fidelity_controller.loadAsset(
      QStringLiteral("preview-color-fidelity-asset"),
      fidelity_source_path,
      QString(),
      QStringLiteral("Fidelity Camera"),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(fidelity_state));
  if (!wait_for_preview_idle(fidelity_controller)) {
    return fail("preview color-fidelity controller should settle its initial render");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::
      set_next_interactive_render_draft(fidelity_controller, true);
  auto fidelity_draft_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          interactive_render_request(fidelity_controller);
  auto fidelity_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          full_render_request(fidelity_controller);
  // Compare renderer output directly rather than a previously encoded JPEG
  // disk-cache artifact.
  fidelity_draft_request.use_in_memory_preview = true;
  fidelity_full_request.use_in_memory_preview = true;
  const auto fidelity_draft_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          render_preview_request_sync(fidelity_draft_request);
  const auto fidelity_full_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::
          render_preview_request_sync(fidelity_full_request);
  const auto fidelity_maximum_delta = maximum_channel_delta(
      fidelity_draft_result.preview_image,
      fidelity_full_result.preview_image);
  if (!fidelity_draft_result.success || !fidelity_full_result.success ||
      fidelity_draft_result.preview_image.size() != fidelity_source.size() ||
      fidelity_full_result.preview_image.size() != fidelity_source.size() ||
      fidelity_maximum_delta > 1) {
    std::cerr << "draft_success=" << fidelity_draft_result.success
              << " full_success=" << fidelity_full_result.success
              << " draft_size=" << fidelity_draft_result.preview_image.width()
              << 'x' << fidelity_draft_result.preview_image.height()
              << " full_size=" << fidelity_full_result.preview_image.width()
              << 'x' << fidelity_full_result.preview_image.height()
              << " maximum_channel_delta=" << fidelity_maximum_delta << '\n';
    return fail(
        "draft and full previews must share the same global adjustment and display-color pipeline");
  }

  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (!within_first_frame_budget([&]() { controller.setExposure(0.18); }) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_active(
          controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_interval(
          controller) != 0 ||
      (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
           controller) &&
       !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
           controller))) {
    return fail("slider edits should publish immediate draft feedback and schedule settled full resolution");
  }
  const auto slider_interactive_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_request(controller);
  const auto slider_full_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(controller);
  if (slider_interactive_request.full_resolution ||
      !slider_full_request.use_in_memory_preview ||
      slider_interactive_request.render_latency_budget_ms > 1000 ||
      !slider_full_request.full_resolution ||
      slider_full_request.render_latency_budget_ms > 6000) {
    return fail("slider preview requests should expose bounded interactive and settled latency budgets");
  }
  if (!wait_for_preview_idle(controller)) {
    return fail("slider preview should settle to full resolution");
  }
  if (!controller.displayPreviewNativeFullResolution() ||
      controller.displayPreviewNativeSize() != source.size()) {
    return fail("settled slider feedback should restore source-resolution native backing");
  }

  controller.updateCropRect(0.1, 0.1, 0.5, 0.5);
  if (!wait_for_preview_idle(controller) ||
      !controller.displayPreviewNativeFullResolution() ||
      controller.displayPreviewNativeSize() != QSize(320, 240)) {
    return fail("a settled cropped output should be recognized as full resolution for its recipe geometry");
  }
  controller.resetCropRect();
  if (!wait_for_preview_idle(controller) ||
      !controller.displayPreviewNativeFullResolution() ||
      controller.displayPreviewNativeSize() != source.size()) {
    return fail("resetting crop should restore source-resolution native backing");
  }

  controller.addMask(QStringLiteral("linear"));
  if (!wait_for_preview_idle(controller)) {
    return fail("mask setup should settle before immediate feedback checks");
  }
  if (!within_first_frame_budget([&]() { controller.setActiveMaskExposure(0.31); }) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_active(
          controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_timer_interval(
          controller) != 0 ||
      (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          controller) &&
       !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(controller))) {
    return fail("mask edits should post immediate feedback and retain full-quality restoration work");
  }
  if (!wait_for_preview_idle(controller)) {
    return fail("mask preview should settle");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_preview_generation(controller) !=
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller)) {
    return fail("mask color edits must restore the current full-resolution generation");
  }

  if (controller.photoLayerCount() < 1 ||
      !within_first_frame_budget([&]() {
        static_cast<void>(controller.setPhotoLayerAdjustmentValue(
            0,
            QStringLiteral("exposure"),
            0.44));
      }) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::
          photo_layer_adjustment_render_timer_active(controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::
              photo_layer_adjustment_render_timer_interval(controller) != 0) {
    return fail("layer edits should publish visible feedback on the first interaction frame");
  }
  if (!wait_for_preview_idle(controller)) {
    std::cerr
        << QStringLiteral(
               "layer settle diagnostics: status=%1 processing=%2 interactiveTimer=%3 interactiveRunning=%4 interactivePending=%5 fullTimer=%6 fullRunning=%7 fullPending=%8 layerTimer=%9 generation=%10 lastFull=%11 failureStage=%12 failureDetail=%13\n")
               .arg(controller.status())
               .arg(controller.processing())
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        interactive_render_timer_active(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        interactive_render_running(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        interactive_render_pending(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        full_render_timer_active(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        full_render_running(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        full_render_pending(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        photo_layer_adjustment_render_timer_active(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        render_generation(controller))
               .arg(photo_editor::desktop::EditorPreviewControllerTestAccess::
                        last_full_preview_generation(controller))
               .arg(controller.previewFailureStage(), controller.previewFailureDetail())
               .toStdString();
    return fail("layer preview should settle");
  }

  controller.startSelectionBrush(false);
  controller.beginActiveBrushStroke();
  if (!within_first_frame_budget([&]() { controller.addBrushPointWithPressure(0.45, 0.52, 0.72); }) ||
      controller.activeMaskLiveBrushPoints().isEmpty() ||
      !controller.activeMaskLiveBrushPreviewWithinLatencyBudget()) {
    return fail("brush input should expose a live point and preview inside the first-frame latency budget");
  }
  controller.endActiveBrushStroke();
  if (!wait_for_preview_idle(controller)) {
    return fail("brush preview should settle without losing the committed stroke");
  }

  const auto crop_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
  if (!within_first_frame_budget([&]() {
        controller.setCropEditing(true);
        controller.setCropZoom(1.12);
      }) ||
      !controller.cropEditing() ||
      controller.cropZoom() != 1.12 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller) <=
          crop_generation_before) {
    return fail("crop geometry should update synchronously before its preview render settles");
  }
  if (!wait_for_preview_idle(controller)) {
    return fail("crop preview should settle");
  }

  const auto compare_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
  if (!within_first_frame_budget([&]() { controller.setCompareMode(true); }) ||
      !controller.compareMode() || controller.cropEditing() ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller) <=
          compare_generation_before) {
    return fail("compare should switch immediately, exit crop safely, and schedule current preview work");
  }
  if (!wait_for_preview_idle(controller)) {
    return fail("compare preview should settle");
  }

  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  controller.setExposure(0.23);
  const auto stale_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_request(controller);
  const auto stale_result =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_preview_request_sync(
          stale_request);
  if (!stale_result.success || stale_result.async_job.job_id.isEmpty()) {
    return fail("stale-publication proof should render one identified full-resolution candidate");
  }
  const auto full_generation_before_stale =
      photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_preview_generation(
          controller);
  controller.setExposure(0.29);
  auto forged_stale_result = stale_result;
  forged_stale_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      controller,
      true,
      forged_stale_result);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::last_full_preview_generation(
          controller) != full_generation_before_stale) {
    return fail("an older full-resolution result should never publish over a newer edit");
  }
  if (!wait_for_preview_idle(controller)) {
    return fail("newer edit should settle after stale publication rejection");
  }

  photo_editor::desktop::EditorPreviewController preservation_controller;
  auto preservation_texture =
      std::make_unique<photo_editor::desktop::FeedbackTestTexture>();
  auto* preservation_texture_ptr = preservation_texture.get();
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_display_preview_texture(
      preservation_controller,
      std::move(preservation_texture));
  QImage full_preview(4000, 3000, QImage::Format_ARGB32);
  full_preview.fill(QColor(180, 72, 44));
  photo_editor::desktop::RenderResult full_result;
  full_result.success = true;
  full_result.full_resolution = true;
  full_result.generation = 0;
  full_result.render_mode = QStringLiteral("max_preview");
  full_result.render_reason = QStringLiteral("edit_change");
  full_result.preview_image = full_preview;
  full_result.source_width = full_preview.width();
  full_result.source_height = full_preview.height();
  full_result.output_width = full_preview.width();
  full_result.output_height = full_preview.height();
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      preservation_controller,
      true,
      full_result);
  const auto full_revision = preservation_controller.displayPreviewNativeRevision();
  const auto full_upload_count = preservation_texture_ptr->uploadCount();

  QImage view_preview(1200, 900, QImage::Format_ARGB32);
  view_preview.fill(QColor(24, 44, 68));
  photo_editor::desktop::RenderResult view_result;
  view_result.success = true;
  view_result.full_resolution = false;
  view_result.generation = 0;
  view_result.render_mode = QStringLiteral("settled_preview");
  view_result.render_reason = QStringLiteral("viewport_change");
  view_result.preview_image = view_preview;
  view_result.source_width = full_preview.width();
  view_result.source_height = full_preview.height();
  view_result.output_width = view_preview.width();
  view_result.output_height = view_preview.height();
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      preservation_controller,
      false,
      view_result);
  if (!preservation_controller.displayPreviewNativeFullResolution() ||
      preservation_controller.displayPreviewNativeSize() != full_preview.size() ||
      preservation_controller.displayPreviewNativeRevision() != full_revision ||
      preservation_texture_ptr->uploadCount() != full_upload_count) {
    return fail("view-only zoom and pan work should retain the valid higher-resolution backing");
  }

  auto content_result = view_result;
  content_result.render_reason = QStringLiteral("photo_layer_change");
  content_result.dirty_domain = QStringLiteral("layers_composite");
  content_result.dirty_stages = {QStringLiteral("layers_composite")};
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      preservation_controller,
      false,
      content_result);
  if (preservation_controller.displayPreviewNativeFullResolution() ||
      preservation_controller.displayPreviewNativeSize() != view_preview.size() ||
      preservation_texture_ptr->uploadCount() != full_upload_count + 1) {
    return fail("content edits should publish a correct draft instead of holding a stale full-resolution image");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
