#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QColor>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_render_utils.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "raw_image_decoder.h"
#include "working_image.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

double curve_point_coordinate(const QVariant& point, const char* key) {
  return point.toMap().value(QString::fromLatin1(key)).toDouble();
}

int max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return 255;
  }

  auto max_delta = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto lhs = left.pixelColor(x, y);
      const auto rhs = right.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
    }
  }
  return max_delta;
}

unsigned char mask_value_at(
    const QByteArray& mask_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (mask_data.isEmpty() || width <= 0 || height <= 0) {
    return 0;
  }

  const auto x = std::clamp(
      static_cast<int>(std::lround(normalized_x * static_cast<double>(std::max(width - 1, 0)))),
      0,
      std::max(width - 1, 0));
  const auto y = std::clamp(
      static_cast<int>(std::lround(normalized_y * static_cast<double>(std::max(height - 1, 0)))),
      0,
      std::max(height - 1, 0));
  return static_cast<unsigned char>(mask_data.at(y * width + x));
}

}  // namespace

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static QString cache_path_for_selection(
      const EditorPreviewController& controller,
      bool edited,
      bool full_resolution) {
    return controller.cachePathForSelection(edited, full_resolution);
  }
  static void set_raw_as_shot_white_balance(
      EditorPreviewController& controller,
      double temperature,
      double tint) {
    controller.setRawAsShotWhiteBalance(temperature, tint);
  }
  static void update_raw_clipping_warnings(EditorPreviewController& controller, const QImage& image) {
    controller.updateRawClippingWarnings(image);
  }
  static QSize active_brush_live_preview_size(const EditorPreviewController& controller) {
    return controller.activeBrushLivePreviewSize();
  }
  static EditState interactive_render_state(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false).state;
  }
  static QStringList interactive_compare_adjustment_groups(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false).compare_adjustment_groups;
  }
  static bool full_render_timer_active(const EditorPreviewController& controller) {
    return controller.full_render_timer_.isActive();
  }
  static bool full_render_pending(const EditorPreviewController& controller) {
    return controller.full_request_pending_;
  }
  static bool full_render_running(const EditorPreviewController& controller) {
    return controller.full_render_watcher_.isRunning();
  }
  static EditState pending_full_render_state(const EditorPreviewController& controller) {
    return controller.pending_full_request_.state;
  }
  static int pending_full_render_generation(const EditorPreviewController& controller) {
    return controller.pending_full_request_.generation;
  }
  static bool interactive_render_draft(const EditorPreviewController& controller) {
    return controller.currentRenderRequest(false).draft_interactive;
  }
  static void begin_edit_transaction(EditorPreviewController& controller) {
    controller.beginEditTransaction();
  }
  static void end_edit_transaction(EditorPreviewController& controller) {
    controller.endEditTransaction();
  }
  static int render_generation(const EditorPreviewController& controller) {
    return controller.render_generation_;
  }
  static void start_histogram_update(
      EditorPreviewController& controller,
      const QImage& image,
      int generation) {
    controller.startHistogramUpdate(
        image,
        generation,
        false,
        0,
        0,
        0,
        QStringLiteral("test_histogram_async"),
        QStringLiteral("test"));
  }
};

class TestImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override { images_.insert(key, image); }
  void clearImage(const QString& key) override { images_.remove(key); }

  [[nodiscard]] QImage image(const QString& key) const { return images_.value(key); }

 private:
  QHash<QString, QImage> images_;
};

}  // namespace photo_editor::desktop

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("EditorPreviewControllerTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");

  photo_editor::desktop::EditorPreviewController controller;
  const auto wait_for_preview_idle = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                        int timeout_ms = 2000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      if (!preview_controller.processing() &&
          !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
              preview_controller) &&
          !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
              preview_controller) &&
          !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
              preview_controller)) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return !preview_controller.processing() &&
           !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
               preview_controller) &&
           !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
               preview_controller) &&
            !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
                preview_controller);
  };

  auto wait_for_histogram_idle = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                    int timeout_ms = 2000) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      if (!preview_controller.histogramUpdateActive()) {
        return true;
      }
      QThread::msleep(25);
      elapsed_ms += 25;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return !preview_controller.histogramUpdateActive();
  };

  photo_editor::desktop::EditorPreviewController histogram_async_controller;
  QImage histogram_async_source(320, 240, QImage::Format_ARGB32);
  for (int y = 0; y < histogram_async_source.height(); ++y) {
    for (int x = 0; x < histogram_async_source.width(); ++x) {
      histogram_async_source.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              (x * 3) % 256,
              (y * 5) % 256,
              ((x + y) * 7) % 256));
    }
  }
  const auto histogram_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          histogram_async_controller);
  QElapsedTimer histogram_queue_timer;
  histogram_queue_timer.start();
  photo_editor::desktop::EditorPreviewControllerTestAccess::start_histogram_update(
      histogram_async_controller,
      histogram_async_source,
      histogram_generation);
  if (histogram_queue_timer.elapsed() > 100 || !histogram_async_controller.histogramUpdateActive()) {
    return fail("histogram and scope updates should queue asynchronously without blocking the UI thread");
  }
  if (!wait_for_histogram_idle(histogram_async_controller) ||
      histogram_async_controller.histogramSampleCount() <= 0 ||
      histogram_async_controller.histogramRed().isEmpty() ||
      histogram_async_controller.scopeWaveformLuminance().isEmpty() ||
      histogram_async_controller.scopeVectorscopePoints().isEmpty()) {
    return fail("asynchronous histogram and scope updates should publish completed scope data");
  }

  QImage rapid_slider_source(16, 16, QImage::Format_ARGB32);
  rapid_slider_source.fill(QColor::fromRgb(96, 104, 112));
  const auto rapid_slider_path = runtime_root.filePath(QStringLiteral("rapid-slider.png"));
  if (!rapid_slider_source.save(rapid_slider_path)) {
    return fail("rapid slider source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController rapid_slider_controller;
  rapid_slider_controller.loadAsset(
      QStringLiteral("rapid-slider"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(rapid_slider_controller)) {
    return fail("rapid slider controller should finish its initial preview");
  }
  QThread::msleep(220);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  rapid_slider_controller.setExposure(0.10);
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          rapid_slider_controller)) {
    return fail("the first slider edit should schedule a settled full-resolution render");
  }
  const auto first_slider_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rapid_slider_controller);
  rapid_slider_controller.setContrast(12.0);
  rapid_slider_controller.setHighlights(-18.0);
  rapid_slider_controller.setShadows(24.0);
  rapid_slider_controller.setWhites(8.0);
  rapid_slider_controller.setBlacks(-10.0);
  rapid_slider_controller.setBrightness(9.0);
  rapid_slider_controller.setVibrance(33.0);
  rapid_slider_controller.setSaturation(-22.0);
  rapid_slider_controller.setTexture(14.0);
  rapid_slider_controller.setClarity(-11.0);
  rapid_slider_controller.setDehaze(7.0);
  rapid_slider_controller.setTemperature(-30.0);
  rapid_slider_controller.setTint(15.0);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          rapid_slider_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_running(
          rapid_slider_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          rapid_slider_controller)) {
    return fail(
        "rapid slider edits should keep full-resolution work deferred behind interactive preview");
  }
  const auto rapid_slider_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(
          rapid_slider_controller);
  if (rapid_slider_generation != first_slider_generation + 13 ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_generation(
          rapid_slider_controller) != rapid_slider_generation ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          rapid_slider_controller)) {
    return fail("rapid slider edits should coalesce into one latest draft/full render pair");
  }
  const auto rapid_pending_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::pending_full_render_state(
          rapid_slider_controller);
  if (!almost_equal(rapid_pending_state.exposure, 0.10) ||
      !almost_equal(rapid_pending_state.contrast, 12.0) ||
      !almost_equal(rapid_pending_state.highlights, -18.0) ||
      !almost_equal(rapid_pending_state.shadows, 24.0) ||
      !almost_equal(rapid_pending_state.whites, 8.0) ||
      !almost_equal(rapid_pending_state.blacks, -10.0) ||
      !almost_equal(rapid_pending_state.brightness, 9.0) ||
      !almost_equal(rapid_pending_state.vibrance, 33.0) ||
      !almost_equal(rapid_pending_state.saturation, -22.0) ||
      !almost_equal(rapid_pending_state.texture, 14.0) ||
      !almost_equal(rapid_pending_state.clarity, -11.0) ||
      !almost_equal(rapid_pending_state.dehaze, 7.0) ||
      !almost_equal(rapid_pending_state.temperature, -30.0) ||
      !almost_equal(rapid_pending_state.tint, 15.0)) {
    return fail("rapid slider full-resolution render should use the latest adjustment state");
  }
  if (!wait_for_preview_idle(rapid_slider_controller)) {
    return fail("rapid slider controller should settle after deferred full render");
  }

  photo_editor::desktop::EditorPreviewController mask_preview_controller;
  mask_preview_controller.loadAsset(
      QStringLiteral("mask-preview-draft"),
      rapid_slider_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(mask_preview_controller)) {
    return fail("mask preview controller should finish its initial preview");
  }
  mask_preview_controller.addMask(QStringLiteral("linear"));
  if (mask_preview_controller.activeMaskPreviewSource().isEmpty() ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_draft(
          mask_preview_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          mask_preview_controller) ||
      !photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          mask_preview_controller)) {
    return fail("mask edits should expose the matte immediately and defer full render behind a draft preview");
  }
  if (!wait_for_preview_idle(mask_preview_controller)) {
    return fail("mask preview controller should settle after deferred mask render");
  }

  photo_editor::desktop::EditorPreviewController lens_profile_controller;
  lens_profile_controller.loadAsset(
      QStringLiteral("lens-profile-auto"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QStringLiteral("RF 28-70mm F2L USM"),
      QString());
  if (!almost_equal(lens_profile_controller.lensDistortion(), 12.0) ||
      !almost_equal(lens_profile_controller.lensVignette(), 18.0) ||
      !almost_equal(lens_profile_controller.chromaticAberration(), 22.0)) {
    return fail("new assets should automatically match camera and lens correction profiles");
  }

  const auto explicit_neutral_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(photo_editor::desktop::EditRecipeState{});
  lens_profile_controller.loadAsset(
      QStringLiteral("lens-profile-explicit-recipe"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QStringLiteral("RF 28-70mm F2L USM"),
      explicit_neutral_recipe);
  if (!almost_equal(lens_profile_controller.lensDistortion(), 0.0) ||
      !almost_equal(lens_profile_controller.lensVignette(), 0.0) ||
      !almost_equal(lens_profile_controller.chromaticAberration(), 0.0)) {
    return fail("explicit recipes should not be overwritten by automatic lens profile matching");
  }

  QImage live_preview_source(3000, 1500, QImage::Format_ARGB32);
  live_preview_source.fill(QColor::fromRgb(96, 104, 112));
  const auto live_preview_path = runtime_root.filePath(QStringLiteral("brush-live-preview.png"));
  if (!live_preview_source.save(live_preview_path)) {
    return fail("live brush preview source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController live_preview_controller;
  live_preview_controller.loadAsset(
      QStringLiteral("brush-live-preview"),
      live_preview_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  live_preview_controller.setPreviewTargetEdge(960);
  const auto live_preview_size =
      photo_editor::desktop::EditorPreviewControllerTestAccess::active_brush_live_preview_size(
          live_preview_controller);
  if (live_preview_size.width() != 2048 || live_preview_size.height() != 1024) {
    return fail("live brush stroke previews should use a high-resolution overlay target");
  }

  controller.loadAsset(
      QStringLiteral("asset-a"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  if (controller.canUndo() || controller.canRedo()) {
    return fail("freshly loaded assets should start with empty undo history");
  }

  photo_editor::desktop::EditorPreviewController liquify_controller;
  liquify_controller.loadAsset(
      QStringLiteral("liquify-controls"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  liquify_controller.setLiquifyCenterX(-0.5);
  liquify_controller.setLiquifyCenterY(1.5);
  liquify_controller.setLiquifyRadius(0.001);
  liquify_controller.setLiquifyStrength(140.0);
  if (!almost_equal(liquify_controller.liquifyCenterX(), 0.0) ||
      !almost_equal(liquify_controller.liquifyCenterY(), 1.0) ||
      !almost_equal(liquify_controller.liquifyRadius(), 0.02) ||
      !almost_equal(liquify_controller.liquifyStrength(), 100.0)) {
    return fail("liquify controls should clamp to supported center, radius, and strength ranges");
  }
  const auto liquify_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(liquify_controller);
  if (!almost_equal(liquify_render_state.liquify_center_x, 0.0) ||
      !almost_equal(liquify_render_state.liquify_center_y, 1.0) ||
      !almost_equal(liquify_render_state.liquify_radius, 0.02) ||
      !almost_equal(liquify_render_state.liquify_strength, 100.0)) {
    return fail("controller render requests should carry liquify geometry into the pipeline state");
  }

  QImage compare_source_image(24, 16, QImage::Format_ARGB32);
  compare_source_image.fill(QColor::fromRgb(24, 48, 96));
  const auto compare_source_path = runtime_root.filePath(QStringLiteral("compare-source.png"));
  if (!compare_source_image.save(compare_source_path)) {
    return fail("compare source image should be writable");
  }
  QImage reference_image(18, 12, QImage::Format_ARGB32);
  reference_image.fill(QColor::fromRgb(180, 140, 80));
  const auto reference_image_path = runtime_root.filePath(QStringLiteral("reference-image.png"));
  if (!reference_image.save(reference_image_path)) {
    return fail("reference image should be writable");
  }
  photo_editor::desktop::EditorPreviewController reference_controller;
  reference_controller.loadAsset(
      QStringLiteral("reference-compare"),
      compare_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!reference_controller.setReferenceImage(QUrl::fromLocalFile(reference_image_path).toString()) ||
      !reference_controller.hasReferenceImage() ||
      reference_controller.referenceImageName() != QStringLiteral("reference-image.png")) {
    return fail("external reference images should be accepted from local file URLs");
  }
  reference_controller.setCompareMode(true);
  if (!wait_for_preview_idle(reference_controller) ||
      reference_controller.referencePreviewSource().isEmpty() ||
      reference_controller.displayReferencePreviewSource().isEmpty()) {
    return fail("compare mode should render the selected external reference image");
  }
  reference_controller.updatePixelInspector(0.5, 0.5);
  const auto pixel_inspector = reference_controller.pixelInspector();
  const auto pixel_after = pixel_inspector.value(QStringLiteral("after")).toMap();
  const auto pixel_before = pixel_inspector.value(QStringLiteral("before")).toMap();
  if (!pixel_inspector.value(QStringLiteral("active")).toBool() ||
      !pixel_inspector.value(QStringLiteral("hasAfter")).toBool() ||
      !pixel_inspector.value(QStringLiteral("hasBefore")).toBool() ||
      pixel_after.value(QStringLiteral("red")).toInt() != 24 ||
      pixel_after.value(QStringLiteral("green")).toInt() != 48 ||
      pixel_after.value(QStringLiteral("blue")).toInt() != 96 ||
      pixel_before.value(QStringLiteral("red")).toInt() != 180 ||
      pixel_before.value(QStringLiteral("green")).toInt() != 140 ||
      pixel_before.value(QStringLiteral("blue")).toInt() != 80 ||
      pixel_inspector.value(QStringLiteral("deltaRed")).toInt() != -156) {
    return fail("pixel inspector should report before and after preview RGB values at the sampled point");
  }
  reference_controller.clearPixelInspector();
  if (!reference_controller.pixelInspector().isEmpty()) {
    return fail("clearing the pixel inspector should remove transient sample state");
  }
  reference_controller.clearReferenceImage();
  if (reference_controller.hasReferenceImage() || !reference_controller.referenceImagePath().isEmpty()) {
    return fail("clearing the reference image should restore before/after compare mode");
  }

  photo_editor::desktop::EditorPreviewController adjustment_layer_controller;
  adjustment_layer_controller.loadAsset(
      QStringLiteral("adjustment-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  adjustment_layer_controller.setExposure(0.5);
  adjustment_layer_controller.setContrast(18.0);
  adjustment_layer_controller.setTemperature(65.0);
  adjustment_layer_controller.setVibrance(22.0);
  if (!adjustment_layer_controller.addAdjustmentLayer(QStringLiteral("Grade Pass")) ||
      adjustment_layer_controller.photoLayerCount() != 1) {
    return fail("current tone and color edits should be movable into a photo adjustment layer");
  }
  const auto adjustment_layers = adjustment_layer_controller.photoLayers();
  const auto adjustment_layer = adjustment_layers.at(0).toMap();
  if (adjustment_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      adjustment_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Grade Pass") ||
      !almost_equal(adjustment_layer.value(QStringLiteral("exposure")).toDouble(), 0.5) ||
      !almost_equal(adjustment_layer.value(QStringLiteral("contrast")).toDouble(), 18.0) ||
      !almost_equal(adjustment_layer.value(QStringLiteral("temperature")).toDouble(), 65.0) ||
      !almost_equal(adjustment_layer.value(QStringLiteral("vibrance")).toDouble(), 22.0) ||
      !adjustment_layer.value(QStringLiteral("enabled")).toBool() ||
      !almost_equal(adjustment_layer.value(QStringLiteral("opacity")).toDouble(), 1.0)) {
    return fail("adjustment layer should preserve captured edit values and normalized layer metadata");
  }
  if (!almost_equal(adjustment_layer_controller.exposure(), 0.0) ||
      !almost_equal(adjustment_layer_controller.contrast(), 0.0) ||
      !almost_equal(adjustment_layer_controller.temperature(), 0.0) ||
      !almost_equal(adjustment_layer_controller.vibrance(), 0.0)) {
    return fail("captured base adjustments should reset after creating an adjustment layer");
  }
  const auto adjustment_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          adjustment_layer_controller);
  if (adjustment_layer_render_state.layers.size() != 1 ||
      adjustment_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("adjustment")) {
    return fail("controller render requests should carry photo adjustment layers into the pipeline state");
  }
  adjustment_layer_controller.undo();
  if (adjustment_layer_controller.photoLayerCount() != 0 ||
      !almost_equal(adjustment_layer_controller.exposure(), 0.5)) {
    return fail("undo should restore edits that were moved into an adjustment layer");
  }
  adjustment_layer_controller.redo();
  if (adjustment_layer_controller.photoLayerCount() != 1 ||
      !almost_equal(adjustment_layer_controller.exposure(), 0.0)) {
    return fail("redo should recreate the adjustment layer and keep captured edits off the base layer");
  }

  photo_editor::desktop::EditorPreviewController fill_layer_controller;
  fill_layer_controller.loadAsset(
      QStringLiteral("fill-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!fill_layer_controller.addFillLayer(QStringLiteral("#336699"), QStringLiteral("Slate Fill")) ||
      fill_layer_controller.photoLayerCount() != 1) {
    return fail("fill layers should be creatable through the editor preview controller");
  }
  const auto fill_layer = fill_layer_controller.photoLayers().at(0).toMap();
  if (fill_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("fill") ||
      fill_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Slate Fill") ||
      fill_layer.value(QStringLiteral("fillColor")).toString() != QStringLiteral("#336699") ||
      !almost_equal(fill_layer.value(QStringLiteral("fillAlpha")).toDouble(), 1.0)) {
    return fail("fill layer controller state should store normalized solid color metadata");
  }
  const auto fill_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          fill_layer_controller);
  if (fill_layer_render_state.layers.size() != 1 ||
      fill_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("fill")) {
    return fail("controller render requests should carry fill layers into the pipeline state");
  }
  fill_layer_controller.undo();
  if (fill_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added fill layer");
  }

  photo_editor::desktop::EditorPreviewController text_layer_controller;
  text_layer_controller.loadAsset(
      QStringLiteral("text-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!text_layer_controller.addTextLayer(QStringLiteral("Caption"), QStringLiteral("Caption Layer")) ||
      text_layer_controller.photoLayerCount() != 1) {
    return fail("text layers should be creatable through the editor preview controller");
  }
  const auto text_layer = text_layer_controller.photoLayers().at(0).toMap();
  if (text_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("text") ||
      text_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Caption Layer") ||
      text_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Caption") ||
      text_layer.value(QStringLiteral("textColor")).toString() != QStringLiteral("#ffffff") ||
      text_layer.value(QStringLiteral("pixelWidth")).toInt() <= 0 ||
      text_layer.value(QStringLiteral("pixelHeight")).toInt() <= 0) {
    return fail("text layer controller state should store normalized raster text metadata");
  }
  const auto text_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          text_layer_controller);
  if (text_layer_render_state.layers.size() != 1 ||
      text_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("text")) {
    return fail("controller render requests should carry text layers into the pipeline state");
  }
  text_layer_controller.undo();
  if (text_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added text layer");
  }

  QImage source_layer_image(3, 2, QImage::Format_ARGB32);
  source_layer_image.fill(QColor::fromRgb(32, 128, 220));
  const auto source_layer_path = runtime_root.filePath(QStringLiteral("source-layer.png"));
  if (!source_layer_image.save(source_layer_path)) {
    return fail("source layer fixture image should be writable");
  }
  photo_editor::desktop::EditorPreviewController source_layer_controller;
  source_layer_controller.loadAsset(
      QStringLiteral("source-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!source_layer_controller.addSourceLayer(source_layer_path, QStringLiteral("Linked Source")) ||
      source_layer_controller.photoLayerCount() != 1) {
    return fail("source layers should be creatable through the editor preview controller");
  }
  const auto source_layer = source_layer_controller.photoLayers().at(0).toMap();
  if (source_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("source") ||
      source_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Linked Source") ||
      source_layer.value(QStringLiteral("sourcePath")).toString() != source_layer_path ||
      source_layer.value(QStringLiteral("pixelWidth")).toInt() != 3 ||
      source_layer.value(QStringLiteral("pixelHeight")).toInt() != 2) {
    return fail("source layer controller state should retain linked image metadata and dimensions");
  }
  const auto source_layer_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          source_layer_controller);
  if (source_layer_render_state.layers.size() != 1 ||
      source_layer_render_state.layers.at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("source")) {
    return fail("controller render requests should carry source layers into the pipeline state");
  }
  source_layer_controller.undo();
  if (source_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added source layer");
  }

  photo_editor::desktop::EditorPreviewController group_layer_controller;
  group_layer_controller.loadAsset(
      QStringLiteral("group-layer-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!group_layer_controller.addLayerGroup(QStringLiteral("Retouch Group")) ||
      group_layer_controller.photoLayerCount() != 1) {
    return fail("layer groups should be creatable through the editor preview controller");
  }
  const auto group_layer = group_layer_controller.photoLayers().at(0).toMap();
  if (group_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("group") ||
      group_layer.value(QStringLiteral("name")).toString() != QStringLiteral("Retouch Group") ||
      group_layer.value(QStringLiteral("groupId")).toString().isEmpty() ||
      !group_layer.value(QStringLiteral("expanded")).toBool()) {
    return fail("layer group controller state should store normalized organizational metadata");
  }
  group_layer_controller.undo();
  if (group_layer_controller.photoLayerCount() != 0) {
    return fail("undo should remove a newly added layer group");
  }

  photo_editor::desktop::EditorPreviewController layer_ops_controller;
  layer_ops_controller.loadAsset(
      QStringLiteral("layer-ops-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_ops_controller.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Fill"));
  layer_ops_controller.addTextLayer(QStringLiteral("Title"), QStringLiteral("Title"));
  if (layer_ops_controller.photoLayerCount() != 2 ||
      !layer_ops_controller.renamePhotoLayer(0, QStringLiteral("Headline"))) {
    return fail("photo layers should support rename operations");
  }
  auto readable_layer = layer_ops_controller.photoLayers().at(0).toMap();
  if (readable_layer.value(QStringLiteral("flatIndex")).toInt() != 0 ||
      readable_layer.value(QStringLiteral("kindLabel")).toString() != QStringLiteral("Text") ||
      readable_layer.value(QStringLiteral("opacityPercent")).toInt() != 100 ||
      !readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("visible")) ||
      !readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("normal"))) {
    std::cerr << "Layer row metadata: flatIndex="
              << readable_layer.value(QStringLiteral("flatIndex")).toInt()
              << " kindLabel="
              << readable_layer.value(QStringLiteral("kindLabel")).toString().toStdString()
              << " opacityPercent="
              << readable_layer.value(QStringLiteral("opacityPercent")).toInt()
              << " audit="
              << readable_layer.value(QStringLiteral("layerAuditLabel")).toString().toStdString()
              << "\\n";
    return fail("photo layer stack rows should expose readable kind, opacity, state, and blend metadata");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, false) ||
      layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("photo layers should support hide/show operations");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, true) ||
      !layer_ops_controller.setPhotoLayerOpacity(0, 0.42) ||
      !layer_ops_controller.setPhotoLayerBlendMode(0, QStringLiteral("multiply")) ||
      !layer_ops_controller.setPhotoLayerClippingMask(0, true) ||
      !layer_ops_controller.setPhotoLayerMaskEnabled(0, true)) {
    return fail("photo layers should support direct opacity, blend, clipping, and layer mask controls");
  }
  readable_layer = layer_ops_controller.photoLayers().at(0).toMap();
  const auto layer_audit_label = readable_layer.value(QStringLiteral("layerAuditLabel")).toString();
  if (!almost_equal(readable_layer.value(QStringLiteral("opacity")).toDouble(), 0.42) ||
      readable_layer.value(QStringLiteral("opacityPercent")).toInt() != 42 ||
      readable_layer.value(QStringLiteral("blendMode")).toString() != QStringLiteral("multiply") ||
      !readable_layer.value(QStringLiteral("clippingMask")).toBool() ||
      !readable_layer.value(QStringLiteral("hasLayerMask")).toBool() ||
      readable_layer.value(QStringLiteral("layerMaskLabel")).toString() != QStringLiteral("Layer mask") ||
      readable_layer.value(QStringLiteral("dependencyLabel")).toString() !=
          QStringLiteral("Clips to layer below") ||
      !layer_audit_label.contains(QStringLiteral("42%")) ||
      !layer_audit_label.contains(QStringLiteral("multiply")) ||
      !layer_audit_label.contains(QStringLiteral("clips below")) ||
      !layer_audit_label.contains(QStringLiteral("mask"))) {
    return fail("photo layer rows should expose opacity, blend, clipping, and layer mask metadata");
  }
  const auto layer_stack_render_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          layer_ops_controller);
  if (layer_stack_render_state.layers.size() != 2 ||
      layer_stack_render_state.layers.at(0).toMap().value(QStringLiteral("blendMode")).toString() !=
          QStringLiteral("multiply") ||
      !almost_equal(
          layer_stack_render_state.layers.at(0).toMap().value(QStringLiteral("opacity")).toDouble(),
          0.42)) {
    return fail("draft render requests should carry live layer opacity and blend metadata");
  }
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_timer_active(
          layer_ops_controller) ||
      photo_editor::desktop::EditorPreviewControllerTestAccess::full_render_pending(
          layer_ops_controller)) {
    return fail("photo layer blend and opacity changes should preview at draft quality without scheduling full renders");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, false) ||
      layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("photo layers should support hide/show operations after direct layer controls");
  }
  if (!layer_ops_controller.setPhotoLayerSolo(0, true) ||
      !layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("solo")).toBool()) {
    return fail("photo layers should support solo operations");
  }
  readable_layer = layer_ops_controller.photoLayers().at(0).toMap();
  if (!readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("hidden")) ||
      !readable_layer.value(QStringLiteral("layerAuditLabel")).toString().contains(QStringLiteral("solo"))) {
    return fail("photo layer audit metadata should reflect hidden and solo layer states");
  }
  if (!layer_ops_controller.photoLayerSoloActive() || !layer_ops_controller.clearPhotoLayerSolo() ||
      layer_ops_controller.photoLayerSoloActive()) {
    return fail("photo layer isolation should expose and clear active solo state");
  }
  if (!layer_ops_controller.isolatePhotoLayer(1) ||
      layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("solo")).toBool() ||
      !layer_ops_controller.photoLayers().at(1).toMap().value(QStringLiteral("solo")).toBool()) {
    return fail("photo layer isolate should solo exactly one layer at a time");
  }
  if (!layer_ops_controller.clearPhotoLayerSolo()) {
    return fail("photo layer isolate workflow should return to the full stack");
  }
  if (!layer_ops_controller.savePhotoLayerVisibilityState(QStringLiteral("Hidden Headline")) ||
      layer_ops_controller.photoLayerVisibilityStateCount() != 1) {
    return fail("photo layer stack should save named visibility states");
  }
  const auto visibility_state = layer_ops_controller.photoLayerVisibilityStates().at(0).toMap();
  if (visibility_state.value(QStringLiteral("name")).toString() != QStringLiteral("Hidden Headline") ||
      visibility_state.value(QStringLiteral("visibleCount")).toInt() != 1 ||
      !visibility_state.value(QStringLiteral("visibilitySummary")).toString().contains(QStringLiteral("1/2"))) {
    return fail("photo layer visibility states should summarize saved layer visibility");
  }
  if (!layer_ops_controller.setPhotoLayerEnabled(0, true) ||
      !layer_ops_controller.isolatePhotoLayer(1) ||
      !layer_ops_controller.applyPhotoLayerVisibilityState(0)) {
    return fail("photo layer stack should restore a saved visibility state");
  }
  if (layer_ops_controller.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool() ||
      layer_ops_controller.photoLayerSoloActive()) {
    return fail("saved layer visibility state should restore enabled and solo flags by layer id");
  }
  if (!layer_ops_controller.deletePhotoLayerVisibilityState(0) ||
      layer_ops_controller.photoLayerVisibilityStateCount() != 0) {
    return fail("photo layer visibility states should be removable");
  }
  if (!layer_ops_controller.setPhotoLayerTransform(0, 11, 13, 1.5, 12.0)) {
    return fail("photo layers should support non-destructive transform metadata edits");
  }
  const auto transformed_layer = layer_ops_controller.photoLayers().at(0).toMap();
  if (transformed_layer.value(QStringLiteral("x")).toInt() != 11 ||
      transformed_layer.value(QStringLiteral("y")).toInt() != 13 ||
      !almost_equal(transformed_layer.value(QStringLiteral("scale")).toDouble(), 1.5) ||
      !almost_equal(transformed_layer.value(QStringLiteral("rotation")).toDouble(), 12.0)) {
    return fail("photo layer transforms should be normalized into controller state");
  }
  if (!layer_ops_controller.setPhotoLayerLocked(0, true) ||
      layer_ops_controller.renamePhotoLayer(0, QStringLiteral("Blocked Rename")) ||
      layer_ops_controller.setPhotoLayerOpacity(0, 0.75) ||
      layer_ops_controller.setPhotoLayerBlendMode(0, QStringLiteral("screen")) ||
      layer_ops_controller.setPhotoLayerClippingMask(0, false) ||
      layer_ops_controller.setPhotoLayerMaskEnabled(0, false) ||
      layer_ops_controller.setPhotoLayerTransform(0, 4, 4, 2.0, 45.0) ||
      layer_ops_controller.deletePhotoLayer(0)) {
    return fail("locked photo layers should block destructive stack operations");
  }
  if (!layer_ops_controller.setPhotoLayerLocked(0, false) ||
      !layer_ops_controller.duplicatePhotoLayer(0) ||
      layer_ops_controller.photoLayerCount() != 3) {
    return fail("photo layers should support duplicate operations");
  }
  const auto duplicated_name = layer_ops_controller.photoLayers().at(1).toMap().value(QStringLiteral("name")).toString();
  if (!duplicated_name.endsWith(QStringLiteral("Copy"))) {
    return fail("duplicated photo layers should receive copy metadata");
  }
  if (!layer_ops_controller.movePhotoLayer(1, 1) ||
      layer_ops_controller.photoLayers().at(2).toMap().value(QStringLiteral("name")).toString() != duplicated_name) {
    return fail("photo layers should support reorder operations");
  }
  if (!layer_ops_controller.deletePhotoLayer(2) || layer_ops_controller.photoLayerCount() != 2) {
    return fail("photo layers should support delete operations");
  }

  photo_editor::desktop::EditorPreviewController layer_geometry_controller;
  layer_geometry_controller.loadAsset(
      QStringLiteral("layer-geometry-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  layer_geometry_controller.addTextLayer(QStringLiteral("A"), QStringLiteral("Layer A"));
  layer_geometry_controller.addTextLayer(QStringLiteral("B"), QStringLiteral("Layer B"));
  layer_geometry_controller.addTextLayer(QStringLiteral("C"), QStringLiteral("Layer C"));
  if (!layer_geometry_controller.setPhotoLayerTransform(0, 300, 20, 1.0, 0.0) ||
      !layer_geometry_controller.setPhotoLayerTransform(1, 100, 70, 1.0, 0.0) ||
      !layer_geometry_controller.setPhotoLayerTransform(2, 500, 30, 1.0, 0.0)) {
    return fail("photo layer geometry setup should support transform positioning");
  }
  const auto initial_bounds = layer_geometry_controller.photoLayerBounds(0);
  if (!almost_equal(initial_bounds.value(QStringLiteral("x")).toDouble(), 300.0) ||
      !almost_equal(initial_bounds.value(QStringLiteral("width")).toDouble(), 512.0) ||
      !almost_equal(initial_bounds.value(QStringLiteral("height")).toDouble(), 128.0)) {
    return fail("photo layer bounds should expose transformed layer geometry");
  }
  if (!layer_geometry_controller.snapPhotoLayerToCanvas(0, QStringLiteral("bottom-right"), 1000, 800)) {
    return fail("photo layers should snap to canvas anchors");
  }
  auto snapped_layer = layer_geometry_controller.photoLayers().at(0).toMap();
  if (snapped_layer.value(QStringLiteral("x")).toInt() != 488 ||
      snapped_layer.value(QStringLiteral("y")).toInt() != 672) {
    return fail("canvas snapping should update layer position from layer bounds");
  }
  QVariantList geometry_indices;
  geometry_indices << 0 << 1 << 2;
  if (!layer_geometry_controller.alignPhotoLayers(
          geometry_indices,
          QStringLiteral("vertical-center"),
          1000,
          800)) {
    return fail("photo layers should align against a canvas reference");
  }
  for (int index = 0; index < 3; ++index) {
    if (layer_geometry_controller.photoLayers().at(index).toMap().value(QStringLiteral("y")).toInt() != 336) {
      return fail("vertical-center alignment should place selected layers on the canvas center line");
    }
  }
  layer_geometry_controller.setPhotoLayerTransform(0, 0, 0, 1.0, 0.0);
  layer_geometry_controller.setPhotoLayerTransform(1, 50, 0, 1.0, 0.0);
  layer_geometry_controller.setPhotoLayerTransform(2, 400, 0, 1.0, 0.0);
  if (!layer_geometry_controller.distributePhotoLayers(geometry_indices, QStringLiteral("horizontal")) ||
      layer_geometry_controller.photoLayers().at(1).toMap().value(QStringLiteral("x")).toInt() != 200) {
    return fail("photo layers should distribute selected layer centers along an axis");
  }

  QImage flattened_source(8, 4, QImage::Format_ARGB32);
  flattened_source.fill(QColor::fromRgb(24, 48, 220));
  const auto flattened_source_path = runtime_root.filePath(QStringLiteral("flattened-source.png"));
  if (!flattened_source.save(flattened_source_path)) {
    return fail("flattened export source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController flattened_export_controller;
  flattened_export_controller.loadAsset(
      QStringLiteral("flattened-export-asset"),
      flattened_source_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!flattened_export_controller.addFillLayer(QStringLiteral("#00ff00"), QStringLiteral("Green Overlay")) ||
      flattened_export_controller.photoLayerCount() != 1) {
    return fail("flattened export setup should create a visible fill layer");
  }
  const auto layer_export_path = runtime_root.filePath(QStringLiteral("fill-layer-export.png"));
  if (!flattened_export_controller.exportPhotoLayer(0, layer_export_path)) {
    return fail("photo layers should export individual layer rasters");
  }
  const QImage layer_export(layer_export_path);
  if (layer_export.isNull() || layer_export.size() != flattened_source.size()) {
    return fail("exported fill layer rasters should use the source canvas dimensions");
  }
  const auto layer_sample = layer_export.pixelColor(4, 2);
  if (!(layer_sample.green() > layer_sample.blue() && layer_sample.green() > layer_sample.red())) {
    return fail("exported fill layer rasters should preserve layer color");
  }
  const auto flattened_export_path = runtime_root.filePath(QStringLiteral("flattened-photo-export.png"));
  if (!flattened_export_controller.exportFlattenedPhoto(flattened_export_path)) {
    return fail("photo layer stacks should export as flattened rendered photos");
  }
  const QImage flattened_export(flattened_export_path);
  if (flattened_export.isNull() || flattened_export.size() != flattened_source.size()) {
    return fail("flattened photo export should preserve rendered image dimensions");
  }
  const auto flattened_sample = flattened_export.pixelColor(4, 2);
  if (!(flattened_sample.green() > flattened_sample.blue() &&
        flattened_sample.green() > flattened_sample.red())) {
    return fail("flattened photo export should composite visible photo layers into the output image");
  }

  photo_editor::desktop::EditorPreviewController panel_reset_controller;
  panel_reset_controller.loadAsset(
      QStringLiteral("panel-reset-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  panel_reset_controller.setTemperature(18.0);
  panel_reset_controller.setTint(-9.0);
  panel_reset_controller.setExposure(0.65);
  panel_reset_controller.setHighlights(-22.0);
  panel_reset_controller.setVibrance(31.0);
  panel_reset_controller.setTexture(44.0);
  if (!panel_reset_controller.resetAdjustmentGroup(QStringLiteral("whiteBalance")) ||
      !almost_equal(panel_reset_controller.temperature(), 0.0) ||
      !almost_equal(panel_reset_controller.tint(), 0.0) ||
      !almost_equal(panel_reset_controller.exposure(), 0.65) ||
      !almost_equal(panel_reset_controller.vibrance(), 31.0) ||
      !almost_equal(panel_reset_controller.texture(), 44.0)) {
    return fail("white balance panel reset should reset only temperature and tint");
  }
  panel_reset_controller.undo();
  if (!almost_equal(panel_reset_controller.temperature(), 18.0) ||
      !almost_equal(panel_reset_controller.tint(), -9.0) ||
      !almost_equal(panel_reset_controller.exposure(), 0.65)) {
    return fail("panel reset should be undoable as a single edit state");
  }
  if (!panel_reset_controller.resetAdjustmentGroup(QStringLiteral("light")) ||
      !almost_equal(panel_reset_controller.exposure(), 0.0) ||
      !almost_equal(panel_reset_controller.highlights(), 0.0) ||
      !almost_equal(panel_reset_controller.temperature(), 18.0) ||
      !almost_equal(panel_reset_controller.vibrance(), 31.0) ||
      !almost_equal(panel_reset_controller.texture(), 44.0)) {
    return fail("light panel reset should not reset white balance, color, or presence controls");
  }
  if (!panel_reset_controller.resetAdjustmentGroup(QStringLiteral("color")) ||
      !almost_equal(panel_reset_controller.vibrance(), 0.0) ||
      !almost_equal(panel_reset_controller.saturation(), 0.0) ||
      !almost_equal(panel_reset_controller.temperature(), 18.0) ||
      !almost_equal(panel_reset_controller.texture(), 44.0)) {
    return fail("color panel reset should only reset basic color controls");
  }
  if (panel_reset_controller.resetAdjustmentGroup(QStringLiteral("missing-panel"))) {
    return fail("unknown panel reset groups should be rejected");
  }

  photo_editor::desktop::EditorPreviewController transaction_controller;
  transaction_controller.loadAsset(
      QStringLiteral("transaction-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto transaction_generation_before =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(transaction_controller);
  photo_editor::desktop::EditorPreviewControllerTestAccess::begin_edit_transaction(transaction_controller);
  transaction_controller.setExposure(0.5);
  transaction_controller.setContrast(10.0);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(transaction_controller) !=
      transaction_generation_before) {
    return fail("edit transactions should defer render generation invalidation while controls are changing");
  }
  photo_editor::desktop::EditorPreviewControllerTestAccess::end_edit_transaction(transaction_controller);
  if (photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(transaction_controller) !=
      transaction_generation_before + 1) {
    return fail("edit transactions should collapse multiple control changes into one render request");
  }
  if (!almost_equal(transaction_controller.exposure(), 0.5) ||
      !almost_equal(transaction_controller.contrast(), 10.0)) {
    return fail("edit transactions should preserve every control value changed inside the transaction");
  }

  controller.setExposure(0.75);
  controller.setContrast(12.0);

  if (!controller.canUndo()) {
    return fail("changing editor controls should enable undo");
  }

  controller.undo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("undo should restore the previous edit state");
  }
  if (!controller.canRedo()) {
    return fail("undo should enable redo");
  }

  controller.undo();
  if (!almost_equal(controller.exposure(), 0.0) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("multiple undo steps should walk back through the edit history");
  }

  controller.redo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("redo should restore the most recently undone state");
  }

  controller.redo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 12.0)) {
    return fail("redo should restore the latest edit state");
  }
  if (controller.canRedo()) {
    return fail("redo history should be exhausted after replaying every state");
  }

  controller.loadAsset(
      QStringLiteral("asset-b"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.setExposure(-0.5);

  controller.loadAsset(
      QStringLiteral("asset-a"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 12.0)) {
    return fail("reloading an asset should restore its current edit state");
  }

  controller.undo();
  if (!almost_equal(controller.exposure(), 0.75) || !almost_equal(controller.contrast(), 0.0)) {
    return fail("each asset should preserve its own undo stack");
  }

  controller.loadAsset(
      QStringLiteral("asset-b"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  if (!almost_equal(controller.exposure(), -0.5)) {
    return fail("switching assets should preserve independent edit states");
  }
  if (!controller.canUndo()) {
    return fail("switching back to a previously edited asset should restore its undo history");
  }

  photo_editor::desktop::EditorPreviewController curve_controller;
  curve_controller.loadAsset(
      QStringLiteral("curve-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  curve_controller.setCurveChannel(QStringLiteral("Master"));
  curve_controller.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.12}},
      QVariantMap{{QStringLiteral("x"), 0.45}, {QStringLiteral("y"), 0.58}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.91}},
  });

  const auto curve_points = curve_controller.activeCurvePoints();
  if (curve_points.size() != 3) {
    return fail("custom tone curves should preserve endpoint controls");
  }
  if (!almost_equal(curve_point_coordinate(curve_points[0], "x"), 0.0) ||
      !almost_equal(curve_point_coordinate(curve_points[0], "y"), 0.12) ||
      !almost_equal(curve_point_coordinate(curve_points[1], "x"), 0.45) ||
      !almost_equal(curve_point_coordinate(curve_points[1], "y"), 0.58) ||
      !almost_equal(curve_point_coordinate(curve_points[2], "x"), 1.0) ||
      !almost_equal(curve_point_coordinate(curve_points[2], "y"), 0.91)) {
    return fail("tone curve endpoints should remain editable for black and white point adjustment");
  }
  curve_controller.setCurveChannel(QStringLiteral("Luma"));
  curve_controller.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.68}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  const auto luma_curve_points = curve_controller.activeCurvePoints();
  if (curve_controller.curveChannel() != QStringLiteral("Luma") ||
      luma_curve_points.size() != 3 ||
      !almost_equal(curve_point_coordinate(luma_curve_points[1], "y"), 0.68)) {
    return fail("luma curve channel should preserve independent point controls");
  }
  if (!curve_controller.curvePresetNames().contains(QStringLiteral("Filmic S"))) {
    return fail("built-in curve presets should be exposed to the UI");
  }
  curve_controller.setCurveChannel(QStringLiteral("RGB"));
  curve_controller.setCurveShadows(22.0);
  if (curve_controller.activeCurveMode() != QStringLiteral("Parametric")) {
    return fail("RGB curve sliders should expose parametric curve mode");
  }
  if (!curve_controller.applyCurvePreset(QStringLiteral("Matte Lift"))) {
    return fail("built-in curve presets should apply to the active curve channel");
  }
  const auto matte_curve_points = curve_controller.activeCurvePoints();
  if (matte_curve_points.size() != 5 ||
      !almost_equal(curve_point_coordinate(matte_curve_points[0], "y"), 0.08) ||
      !almost_equal(curve_controller.curveShadows(), 0.0) ||
      curve_controller.activeCurveMode() != QStringLiteral("Point")) {
    return fail("curve presets should install point curves and clear RGB parametric controls");
  }
  curve_controller.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.45}, {QStringLiteral("y"), 0.54}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  const auto before_curve_preview = curve_controller.activeCurvePoints();
  const auto filmic_preview_points = curve_controller.curvePresetPreviewPoints(QStringLiteral("Filmic S"));
  if (filmic_preview_points.size() != 5 || curve_controller.activeCurvePoints() != before_curve_preview) {
    return fail("curve preset preview points should be readable without mutating the active curve");
  }
  curve_controller.setCurveChannel(QStringLiteral("Luma"));
  if (!curve_controller.applyCurvePreset(QStringLiteral("Highlight Roll-off"))) {
    return fail("built-in curve presets should apply to luma curves");
  }
  const auto rolloff_curve_points = curve_controller.activeCurvePoints();
  if (rolloff_curve_points.size() != 4 ||
      !almost_equal(curve_point_coordinate(rolloff_curve_points[3], "y"), 0.92)) {
    return fail("curve presets should preserve endpoint edits for non-RGB channels");
  }
  if (!curve_controller.presetNames().contains(QStringLiteral("Clean Portrait")) ||
      !curve_controller.presetNames().contains(QStringLiteral("Landscape Pop")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("Portrait Essentials")) ||
      !curve_controller.presetPackNames().contains(QStringLiteral("Landscape Core")) ||
      curve_controller.presetPackName(QStringLiteral("Clean Portrait")) != QStringLiteral("Portrait Essentials")) {
    return fail("built-in preset packs should be exposed to the UI");
  }
  if (curve_controller.presetRecipeJson(QStringLiteral("Landscape Pop")).isEmpty()) {
    return fail("built-in preset packs should expose recipe payloads");
  }
  if (!curve_controller.presetCategoryNames().contains(QStringLiteral("Portrait")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("Landscape")) ||
      !curve_controller.presetCategoryNames().contains(QStringLiteral("Black and White")) ||
      curve_controller.presetCategoryName(QStringLiteral("Landscape Pop")) != QStringLiteral("Landscape")) {
    return fail("built-in presets should expose categories for browsing");
  }
  const auto landscape_groups = curve_controller.presetAdjustmentGroups(QStringLiteral("Landscape Pop"));
  if (landscape_groups.size() != 3 ||
      !landscape_groups.contains(QStringLiteral("tone")) ||
      !landscape_groups.contains(QStringLiteral("color")) ||
      !landscape_groups.contains(QStringLiteral("detail"))) {
    return fail("built-in presets should declare their touched adjustment groups");
  }
  if (!curve_controller.searchPresetNames(QStringLiteral("landscape")).contains(QStringLiteral("Landscape Pop")) ||
      !curve_controller.searchPresetNames(QStringLiteral("Landscape Core"))
           .contains(QStringLiteral("Golden Hour Landscape"))) {
    return fail("preset search should match names, packs, and categories");
  }
  if (!curve_controller.presetThumbnailSource(QStringLiteral("Landscape Pop"))
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      curve_controller.presetThumbnailSource(QStringLiteral("Landscape Pop")) ==
          curve_controller.presetThumbnailSource(QStringLiteral("Clean Black and White"))) {
    return fail("preset thumbnails should be generated as distinct PNG data URLs");
  }
  curve_controller.setPresetFavorite(QStringLiteral("Landscape Pop"), true);
  if (!curve_controller.isPresetFavorite(QStringLiteral("Landscape Pop")) ||
      !curve_controller.favoritePresetNames().contains(QStringLiteral("Landscape Pop"))) {
    return fail("preset favorites should track built-in presets");
  }
  if (!curve_controller.applyPreset(QStringLiteral("Landscape Pop"))) {
    return fail("built-in preset packs should apply like user presets");
  }
  if (!almost_equal(curve_controller.texture(), 18.0) ||
      !almost_equal(curve_controller.clarity(), 20.0) ||
      !almost_equal(curve_controller.dehaze(), 14.0) ||
      !almost_equal(curve_controller.vibrance(), 22.0)) {
    return fail("built-in landscape preset should apply its tone and detail recipe");
  }
  if (!curve_controller.canUndo() ||
      !curve_controller.status().contains(QStringLiteral("Applied preset 'Landscape Pop'"))) {
    return fail("preset application should be recorded through status and undo history");
  }
  if (curve_controller.deletePreset(QStringLiteral("Landscape Pop"))) {
    return fail("built-in preset packs should not be deleted as user presets");
  }
  photo_editor::desktop::EditorPreviewController adaptive_controller;
  adaptive_controller.loadAsset(
      QStringLiteral("adaptive-preset-asset"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QStringLiteral("RF 28-70mm F2L USM"),
      QString());
  if (!adaptive_controller.isPresetAdaptive(QStringLiteral("Camera Aware Portrait")) ||
      adaptive_controller.isPresetAdaptive(QStringLiteral("Clean Portrait"))) {
    return fail("adaptive presets should declare adaptive metadata rules");
  }
  if (!adaptive_controller.applyPreset(QStringLiteral("Camera Aware Portrait")) ||
      !almost_equal(adaptive_controller.exposure(), 0.18) ||
      !almost_equal(adaptive_controller.temperature(), 2.0) ||
      !almost_equal(adaptive_controller.texture(), -10.0) ||
      !almost_equal(adaptive_controller.clarity(), -4.0)) {
    return fail("adaptive presets should vary by camera and lens metadata");
  }
  photo_editor::desktop::EditorPreviewController changed_controls_controller;
  changed_controls_controller.loadAsset(
      QStringLiteral("changed-controls-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto changed_controls =
      changed_controls_controller.presetChangedControls(QStringLiteral("Landscape Pop"));
  if (!changed_controls.contains(QStringLiteral("Texture")) ||
      !changed_controls.contains(QStringLiteral("Clarity")) ||
      !changed_controls.contains(QStringLiteral("Vibrance"))) {
    return fail("preset changed-control preview should list controls before applying");
  }
  changed_controls_controller.setPresetIntensity(0.0);
  if (!changed_controls_controller.presetChangedControls(QStringLiteral("Landscape Pop")).isEmpty()) {
    return fail("preset changed-control preview should respect preset intensity");
  }
  changed_controls_controller.setCompareAdjustmentGroups(
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("unsupported")});
  changed_controls_controller.setCompareMode(true);
  const auto selected_compare_edits =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_compare_adjustment_groups(
          changed_controls_controller);
  if (!selected_compare_edits.contains(QStringLiteral("tone")) ||
      !selected_compare_edits.contains(QStringLiteral("color")) ||
      selected_compare_edits.contains(QStringLiteral("unsupported"))) {
    return fail("compare selected edits should carry normalized adjustment groups into render requests");
  }
  photo_editor::desktop::EditorPreviewController intensity_controller;
  intensity_controller.loadAsset(
      QStringLiteral("preset-intensity-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!intensity_controller.applyPresetWithIntensity(QStringLiteral("Landscape Pop"), 0.5) ||
      !almost_equal(intensity_controller.texture(), 9.0) ||
      !almost_equal(intensity_controller.clarity(), 10.0) ||
      !almost_equal(intensity_controller.dehaze(), 7.0) ||
      !almost_equal(intensity_controller.vibrance(), 11.0)) {
    return fail("preset intensity should scale preset adjustment deltas");
  }
  intensity_controller.resetAdjustments();
  intensity_controller.setPresetIntensity(0.25);
  if (!almost_equal(intensity_controller.presetIntensity(), 0.25) ||
      !intensity_controller.applyPreset(QStringLiteral("Landscape Pop")) ||
      !almost_equal(intensity_controller.texture(), 4.5) ||
      !almost_equal(intensity_controller.clarity(), 5.0)) {
    return fail("preset intensity property should drive regular preset application");
  }
  intensity_controller.resetAdjustments();
  if (!intensity_controller.applyPresetWithIntensity(
          QStringLiteral("Landscape Pop"),
          0.5,
          {QStringLiteral("detail")}) ||
      !almost_equal(intensity_controller.texture(), 9.0) ||
      !almost_equal(intensity_controller.contrast(), 0.0)) {
    return fail("preset intensity should respect partial adjustment groups");
  }
  intensity_controller.resetAdjustments();
  const auto preview_recipe_json = intensity_controller.presetPreviewRecipeJson(
      QStringLiteral("Landscape Pop"),
      {QStringLiteral("detail")},
      0.5);
  const auto preview_recipe_state = photo_editor::desktop::parse_edit_recipe_json(preview_recipe_json);
  if (preview_recipe_json.isEmpty() ||
      !almost_equal(preview_recipe_state.texture, 9.0) ||
      !almost_equal(preview_recipe_state.clarity, 10.0) ||
      !almost_equal(intensity_controller.texture(), 0.0) ||
      !almost_equal(intensity_controller.clarity(), 0.0)) {
    return fail("preset preview recipe should honor intensity and groups without mutating the current recipe");
  }
  photo_editor::desktop::EditorPreviewController partial_preset_controller;
  partial_preset_controller.loadAsset(
      QStringLiteral("partial-preset-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  partial_preset_controller.setExposure(0.64);
  partial_preset_controller.setContrast(12.0);
  if (!partial_preset_controller.savePreset(QStringLiteral("Tone Only"), true)) {
    return fail("saving a preset should record declared adjustment groups");
  }
  const auto tone_only_groups = partial_preset_controller.presetAdjustmentGroups(QStringLiteral("Tone Only"));
  if (tone_only_groups.size() != 1 || !tone_only_groups.contains(QStringLiteral("tone"))) {
    return fail("user presets should declare exactly the groups they touch");
  }
  partial_preset_controller.setExposure(0.0);
  partial_preset_controller.setContrast(0.0);
  partial_preset_controller.setTemperature(18.0);
  if (!partial_preset_controller.applyPreset(QStringLiteral("Tone Only")) ||
      !almost_equal(partial_preset_controller.exposure(), 0.64) ||
      !almost_equal(partial_preset_controller.contrast(), 12.0) ||
      !almost_equal(partial_preset_controller.temperature(), 18.0)) {
    return fail("preset declarations should be the default scope when applying presets");
  }
  photo_editor::desktop::EditorPreviewController safety_controller;
  safety_controller.loadAsset(
      QStringLiteral("preset-safety-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  safety_controller.setCropPreset(QStringLiteral("4x5"));
  safety_controller.addMask(QStringLiteral("linear"));
  safety_controller.setOutputColorSpace(QStringLiteral("Display P3"));
  if (!safety_controller.savePreset(QStringLiteral("Crop Mask Output"), true)) {
    return fail("saving a safety preset should work");
  }
  const auto safety_warnings = safety_controller.presetSafetyWarnings(QStringLiteral("Crop Mask Output"));
  const auto has_warning = [&safety_warnings](const QString& text) {
    return std::any_of(safety_warnings.cbegin(), safety_warnings.cend(), [&text](const QString& warning) {
      return warning.contains(text);
    });
  };
  if (!has_warning(QStringLiteral("crop")) ||
      !has_warning(QStringLiteral("masks")) ||
      !has_warning(QStringLiteral("output"))) {
    return fail("preset safety checks should warn for crop, masks, and output settings");
  }
  if (!safety_controller.presetSafetyWarnings(
                            QStringLiteral("Crop Mask Output"),
                            {QStringLiteral("tone")})
           .isEmpty()) {
    return fail("preset safety checks should respect explicit adjustment groups");
  }
  curve_controller.setColorMixerModel(QStringLiteral("HSV"));
  if (curve_controller.colorMixerModel() != QStringLiteral("HSV")) {
    return fail("color mixer model should expose HSV selection");
  }
  curve_controller.setColorMixerModel(QStringLiteral("LCH"));
  if (curve_controller.colorMixerModel() != QStringLiteral("LCH")) {
    return fail("color mixer model should expose LCH selection");
  }
  curve_controller.setColorMixerModel(QStringLiteral("OKLab"));
  if (curve_controller.colorMixerModel() != QStringLiteral("OKLab")) {
    return fail("color mixer model should expose OKLab selection");
  }
  curve_controller.setHslChannel(QStringLiteral("Reds"));
  curve_controller.setSelectiveCyan(24.0);
  curve_controller.setSelectiveMagenta(-12.0);
  curve_controller.setSelectiveYellow(9.0);
  curve_controller.setSelectiveBlack(15.0);
  if (!almost_equal(curve_controller.selectiveCyan(), 24.0) ||
      !almost_equal(curve_controller.selectiveMagenta(), -12.0) ||
      !almost_equal(curve_controller.selectiveYellow(), 9.0) ||
      !almost_equal(curve_controller.selectiveBlack(), 15.0)) {
    return fail("selective color controls should preserve CMYK edits for the active channel");
  }
  curve_controller.setHslChannel(QStringLiteral("Blues"));
  if (!almost_equal(curve_controller.selectiveCyan(), 0.0)) {
    return fail("selective color controls should be stored per color channel");
  }
  curve_controller.setHslChannel(QStringLiteral("Reds"));
  if (!almost_equal(curve_controller.selectiveBlack(), 15.0)) {
    return fail("selective color controls should restore the previous channel values");
  }
  curve_controller.setChannelMixerRedFromGreen(34.0);
  curve_controller.setChannelMixerGreenFromBlue(-16.0);
  curve_controller.setChannelMixerBlueFromRed(21.0);
  curve_controller.setChannelMixerRedFromRed(118.0);
  if (!almost_equal(curve_controller.channelMixerRedFromGreen(), 34.0) ||
      !almost_equal(curve_controller.channelMixerGreenFromBlue(), -16.0) ||
      !almost_equal(curve_controller.channelMixerBlueFromRed(), 21.0) ||
      !almost_equal(curve_controller.channelMixerRedFromRed(), 118.0)) {
    return fail("channel mixer controls should expose the RGB contribution matrix");
  }
  curve_controller.setBlackAndWhiteEnabled(true);
  curve_controller.setBlackAndWhiteReds(28.0);
  curve_controller.setBlackAndWhiteBlues(-22.0);
  if (!curve_controller.blackAndWhiteEnabled() ||
      !almost_equal(curve_controller.blackAndWhiteReds(), 28.0) ||
      !almost_equal(curve_controller.blackAndWhiteBlues(), -22.0)) {
    return fail("black-and-white mixer controls should expose monochrome channel weights");
  }
  curve_controller.setColorReplacementEnabled(true);
  curve_controller.setColorReplacementSourceHue(0.02);
  curve_controller.setColorReplacementTargetHue(0.36);
  curve_controller.setColorReplacementRange(48.0);
  curve_controller.setColorReplacementStrength(82.0);
  if (!curve_controller.colorReplacementEnabled() ||
      !almost_equal(curve_controller.colorReplacementSourceHue(), 0.02) ||
      !almost_equal(curve_controller.colorReplacementTargetHue(), 0.36) ||
      !almost_equal(curve_controller.colorReplacementRange(), 48.0) ||
      !almost_equal(curve_controller.colorReplacementStrength(), 82.0)) {
    return fail("color replacement controls should expose source hue, target hue, range, and strength");
  }
  curve_controller.setSkinToneProtectionEnabled(true);
  curve_controller.setSkinToneLineHue(0.09);
  curve_controller.setSkinToneProtectionRange(44.0);
  curve_controller.setSkinToneProtectionStrength(91.0);
  if (!curve_controller.skinToneProtectionEnabled() ||
      !almost_equal(curve_controller.skinToneLineHue(), 0.09) ||
      !almost_equal(curve_controller.skinToneLineAngle(), 32.4) ||
      !almost_equal(curve_controller.skinToneProtectionRange(), 44.0) ||
      !almost_equal(curve_controller.skinToneProtectionStrength(), 91.0)) {
    return fail("skin tone controls should expose line angle, protected range, and protection strength");
  }
  curve_controller.setColorHarmonyEnabled(true);
  curve_controller.setColorHarmonyMode(QStringLiteral("Triadic"));
  curve_controller.setColorHarmonyBaseHue(0.04);
  curve_controller.setColorHarmonyStrength(68.0);
  if (!curve_controller.colorHarmonyEnabled() ||
      curve_controller.colorHarmonyMode() != QStringLiteral("Triadic") ||
      !almost_equal(curve_controller.colorHarmonyBaseHue(), 0.04) ||
      !almost_equal(curve_controller.colorHarmonyStrength(), 68.0)) {
    return fail("color harmony controls should expose mode, base hue, and strength");
  }

  controller.addMask(QStringLiteral("linear"));
  if (controller.maskCount() != 1 || !controller.hasActiveMask() ||
      controller.activeMaskKind() != QStringLiteral("linear")) {
    return fail("adding a mask should create and select the requested stack entry");
  }

  controller.setActiveMaskName(QStringLiteral("Subject Sweep"));
  controller.setActiveMaskExposure(0.75);
  controller.setActiveMaskContrast(18.0);
  controller.setActiveMaskHighlights(-22.0);
  controller.setActiveMaskShadows(31.0);
  controller.setActiveMaskWhites(12.0);
  controller.setActiveMaskBlacks(-14.0);
  controller.setActiveMaskPosition(0.61);
  controller.setActiveMaskRotation(14.0);
  if (!almost_equal(controller.linearMaskExposure(), 0.75) ||
      !almost_equal(controller.linearMaskPosition(), 0.61)) {
    return fail("active mask edits should synchronize legacy compatibility fields");
  }
  if (!almost_equal(controller.activeMaskContrast(), 18.0) ||
      !almost_equal(controller.activeMaskHighlights(), -22.0) ||
      !almost_equal(controller.activeMaskShadows(), 31.0) ||
      !almost_equal(controller.activeMaskWhites(), 12.0) ||
      !almost_equal(controller.activeMaskBlacks(), -14.0)) {
    return fail("selection masks should expose local tone controls");
  }
  controller.setActiveMaskRefineLuminanceMin(0.18);
  controller.setActiveMaskRefineLuminanceMax(0.82);
  controller.setActiveMaskRefineHueCenter(1.16);
  controller.setActiveMaskRefineHueRange(0.22);
  controller.setActiveMaskRefineSaturationMin(0.25);
  controller.setActiveMaskRefineSaturationMax(0.88);
  controller.setActiveMaskRefineDepthMin(0.12);
  controller.setActiveMaskRefineDepthMax(0.76);
  controller.setActiveMaskRefineEdgeConfidenceMin(0.31);
  controller.setActiveMaskRefineRangeFeather(0.07);
  if (!almost_equal(controller.activeMaskRefineLuminanceMin(), 0.18) ||
      !almost_equal(controller.activeMaskRefineLuminanceMax(), 0.82) ||
      !almost_equal(controller.activeMaskRefineHueCenter(), 0.16) ||
      !almost_equal(controller.activeMaskRefineHueRange(), 0.22) ||
      !almost_equal(controller.activeMaskRefineSaturationMin(), 0.25) ||
      !almost_equal(controller.activeMaskRefineSaturationMax(), 0.88) ||
      !almost_equal(controller.activeMaskRefineDepthMin(), 0.12) ||
      !almost_equal(controller.activeMaskRefineDepthMax(), 0.76) ||
      !almost_equal(controller.activeMaskRefineEdgeConfidenceMin(), 0.31) ||
      !almost_equal(controller.activeMaskRefineRangeFeather(), 0.07)) {
    return fail("selection masks should expose range refinement controls");
  }

  controller.addMask(QStringLiteral("radial"));
  controller.setActiveMaskOperation(QStringLiteral("subtract"));
  controller.setActiveMaskName(QStringLiteral("Radial Cut"));
  controller.setActiveMaskCenterX(0.43);
  controller.setActiveMaskCenterY(0.57);
  controller.setActiveMaskRadiusX(0.18);
  controller.setActiveMaskRadiusY(0.22);
  if (controller.maskCount() != 2 ||
      controller.activeMaskOperation() != QStringLiteral("subtract")) {
    return fail("stack masks should retain per-entry operations");
  }
  controller.setActiveMaskIndex(0);
  if (controller.activeMaskName() != QStringLiteral("Subject Sweep")) {
    return fail("active mask access should refresh when switching the selected mask");
  }
  controller.setActiveMaskIndex(1);
  if (controller.activeMaskName() != QStringLiteral("Radial Cut")) {
    return fail("active mask cache should not return stale data after switching masks");
  }
  controller.setActiveMaskSolo(true);
  controller.setActiveMaskLocked(true);
  controller.setActiveMaskName(QStringLiteral("Locked Rename"));
  controller.setActiveMaskEnabled(false);
  if (!controller.activeMaskSolo() || !controller.activeMaskLocked() ||
      controller.activeMaskName() != QStringLiteral("Radial Cut") ||
      !controller.activeMaskEnabled()) {
    return fail("locked masks should keep solo state while rejecting ordinary edits");
  }
  controller.removeActiveMask();
  if (controller.maskCount() != 2) {
    return fail("locked masks should not be deleted");
  }
  controller.setActiveMaskLocked(false);
  controller.setActiveMaskSolo(false);
  if (controller.activeMaskLocked() || controller.activeMaskSolo()) {
    return fail("mask lock and solo toggles should be reversible");
  }

  controller.moveActiveMask(-1);
  if (controller.activeMaskIndex() != 0 ||
      controller.maskStack().at(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial")) {
    return fail("moving the active mask should reorder the stack");
  }

  controller.duplicateActiveMask();
  if (controller.maskCount() != 3 ||
      controller.maskStack().at(1).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial")) {
    return fail("duplicating the active mask should insert a cloned entry after it");
  }

  controller.removeActiveMask();
  if (controller.maskCount() != 2) {
    return fail("removing the active mask should shrink the stack");
  }

  controller.addMask(QStringLiteral("brush"));
  if (controller.activeMaskKind() != QStringLiteral("brush")) {
    return fail("adding a brush mask should select the brush entry");
  }
  if (!controller.brushPresetNames().contains(QStringLiteral("Edge Aware")) ||
      controller.activeMaskBrushPreset() != QStringLiteral("Soft Round")) {
    return fail("brush masks should expose built-in brush presets");
  }
  if (!controller.applyActiveMaskBrushPreset(QStringLiteral("Edge Aware")) ||
      controller.activeMaskBrushPreset() != QStringLiteral("Edge Aware") ||
      !almost_equal(controller.activeMaskBrushSize(), 0.026) ||
      !almost_equal(controller.activeMaskBrushHardness(), 0.66) ||
      !almost_equal(controller.activeMaskBrushSpacing(), 0.42) ||
      !almost_equal(controller.activeMaskBrushSmoothing(), 0.34) ||
      !controller.activeMaskBrushAutoMask() ||
      !almost_equal(controller.activeMaskBrushAutoMaskTolerance(), 0.14)) {
    return fail("applying a brush preset should update brush shape and auto-mask settings");
  }
  controller.setActiveMaskBrushSize(0.14);
  controller.setActiveMaskBrushFlow(0.72);
  controller.setActiveMaskBrushHardness(0.61);
  controller.setActiveMaskBrushSpacing(0.32);
  controller.setActiveMaskBrushSmoothing(0.28);
  controller.setActiveMaskBrushBuildup(false);
  controller.setActiveMaskBrushEraseMode(true);
  controller.setActiveMaskBrushAutoMask(true);
  controller.setActiveMaskBrushAutoMaskTolerance(0.11);
  if (!almost_equal(controller.activeMaskBrushHardness(), 0.61) ||
      !almost_equal(controller.activeMaskBrushSpacing(), 0.32) ||
      !almost_equal(controller.activeMaskBrushSmoothing(), 0.28) ||
      controller.activeMaskBrushBuildup() ||
      !controller.activeMaskBrushEraseMode() ||
      !controller.activeMaskBrushAutoMask() ||
      !almost_equal(controller.activeMaskBrushAutoMaskTolerance(), 0.11) ||
      controller.activeMaskBrushPreset() != QStringLiteral("Custom")) {
    return fail("brush masks should expose editable hardness, spacing, smoothing, buildup, erase mode, auto-mask, and preset controls");
  }
  controller.setActiveMaskBrushBuildup(true);
  controller.addBrushPointWithTablet(0.35, 0.44, 0.5, 0.6, -0.2);
  controller.addBrushPoint(0.41, 0.51);
  if (controller.activeMaskBrushPointCount() < 2) {
    return fail("brush masks should track painted samples");
  }
  const auto pressure_point =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushPoints")).toList().first().toMap();
  if (!almost_equal(pressure_point.value(QStringLiteral("pressure")).toDouble(), 0.5) ||
      !almost_equal(pressure_point.value(QStringLiteral("tiltX")).toDouble(), 0.6) ||
      !almost_equal(pressure_point.value(QStringLiteral("tiltY")).toDouble(), -0.2) ||
      !pressure_point.value(QStringLiteral("erase")).toBool() ||
      pressure_point.value(QStringLiteral("size")).toDouble() >= 0.14 ||
      pressure_point.value(QStringLiteral("flow")).toDouble() >= 0.72) {
    return fail("brush points should store and apply stylus pressure, tilt, and erase mode");
  }
  controller.setActiveMaskBrushEraseMode(false);
  controller.clearActiveBrushPoints();
  if (controller.activeMaskBrushPointCount() != 0) {
    return fail("clearing brush points should reset the active brush mask");
  }
  controller.addBrushPointWithTabletEraser(0.45, 0.45, 0.64, 0.20, -0.10, true);
  const auto eraser_point =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushPoints")).toList().first().toMap();
  if (!eraser_point.value(QStringLiteral("erase")).toBool() ||
      !almost_equal(eraser_point.value(QStringLiteral("pressure")).toDouble(), 0.64) ||
      !almost_equal(eraser_point.value(QStringLiteral("tiltX")).toDouble(), 0.20) ||
      !almost_equal(eraser_point.value(QStringLiteral("tiltY")).toDouble(), -0.10)) {
    return fail("tablet eraser strokes should preserve pressure, tilt, and erase intent");
  }
  controller.clearActiveBrushPoints();
  controller.setActiveMaskBrushSize(0.06);
  controller.setActiveMaskBrushSpacing(0.10);
  controller.addBrushPoint(0.10, 0.20);
  controller.addBrushPoint(0.90, 0.20);
  const auto dense_spacing_point_count = controller.activeMaskBrushPointCount();
  controller.clearActiveBrushPoints();
  controller.setActiveMaskBrushSpacing(2.0);
  controller.addBrushPoint(0.10, 0.20);
  controller.addBrushPoint(0.90, 0.20);
  if (dense_spacing_point_count <= controller.activeMaskBrushPointCount()) {
    return fail("lower brush spacing should sample more brush dabs along a stroke");
  }
  controller.clearActiveBrushPoints();
  controller.setActiveMaskBrushSpacing(2.0);
  controller.setActiveMaskBrushSmoothing(1.0);
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.10, 0.40);
  controller.addBrushPoint(0.90, 0.40);
  const auto smoothed_live_points = controller.activeMaskLiveBrushPoints();
  if (smoothed_live_points.size() < 4) {
    return fail("stroke smoothing should keep damped intermediate samples before the live endpoint");
  }
  const auto smoothed_intermediate_point =
      smoothed_live_points.at(smoothed_live_points.size() - 2).toMap();
  const auto smoothed_live_endpoint = smoothed_live_points.last().toMap();
  if (!(smoothed_intermediate_point.value(QStringLiteral("x")).toDouble() < 0.50) ||
      !almost_equal(smoothed_live_endpoint.value(QStringLiteral("x")).toDouble(), 0.90) ||
      !almost_equal(smoothed_live_endpoint.value(QStringLiteral("y")).toDouble(), 0.40)) {
    return fail("stroke smoothing should damp intermediate samples without lagging behind the cursor");
  }
  controller.endActiveBrushStroke();
  const auto smoothed_committed_endpoint =
      controller.maskStack().at(controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("brushPoints")).toList().last().toMap();
  if (!almost_equal(smoothed_committed_endpoint.value(QStringLiteral("x")).toDouble(), 0.90) ||
      !almost_equal(smoothed_committed_endpoint.value(QStringLiteral("y")).toDouble(), 0.40)) {
    return fail("ending a smoothed stroke should commit the same cursor endpoint shown live");
  }
  controller.clearActiveBrushPoints();

  QImage auto_mask_source(32, 8, QImage::Format_ARGB32);
  for (int y = 0; y < auto_mask_source.height(); ++y) {
    for (int x = 0; x < auto_mask_source.width(); ++x) {
      auto_mask_source.setPixelColor(
          x,
          y,
          x < auto_mask_source.width() / 2 ? QColor::fromRgb(28, 28, 28)
                                           : QColor::fromRgb(235, 235, 235));
    }
  }
  const auto auto_mask_path = runtime_root.filePath(QStringLiteral("brush-auto-mask.png"));
  if (!auto_mask_source.save(auto_mask_path)) {
    return fail("auto-mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController auto_mask_controller;
  auto_mask_controller.loadAsset(
      QStringLiteral("brush-auto-mask"),
      auto_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  auto_mask_controller.addMask(QStringLiteral("brush"));
  auto_mask_controller.setActiveMaskBrushSize(0.45);
  auto_mask_controller.setActiveMaskBrushFlow(1.0);
  auto_mask_controller.setActiveMaskBrushAutoMask(true);
  auto_mask_controller.setActiveMaskBrushAutoMaskTolerance(0.05);
  auto_mask_controller.addBrushPoint(0.35, 0.5);
  const auto auto_mask = auto_mask_controller.maskStack().at(auto_mask_controller.activeMaskIndex()).toMap();
  const auto auto_mask_data = auto_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto auto_mask_width = auto_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto auto_mask_height = auto_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(auto_mask_data, auto_mask_width, auto_mask_height, 0.35, 0.5) < 128 ||
      mask_value_at(auto_mask_data, auto_mask_width, auto_mask_height, 0.70, 0.5) > 48) {
    return fail("brush auto-mask should stop painted coverage at a strong image edge");
  }

  controller.addMask(QStringLiteral("luminance"));
  controller.setActiveMaskLuminanceMin(0.64);
  controller.setActiveMaskLuminanceMax(0.91);
  controller.setActiveMaskRangeFeather(0.06);
  if (!almost_equal(controller.activeMaskLuminanceMin(), 0.64) ||
      !almost_equal(controller.activeMaskLuminanceMax(), 0.91) ||
      !almost_equal(controller.activeMaskRangeFeather(), 0.06)) {
    return fail("luminance range masks should expose editable constraint controls");
  }

  controller.addMask(QStringLiteral("color"));
  controller.setActiveMaskTemperature(14.0);
  controller.setActiveMaskTint(-11.0);
  controller.setActiveMaskVibrance(26.0);
  controller.setActiveMaskSaturation(-9.0);
  controller.setActiveMaskHueCenter(0.12);
  controller.setActiveMaskHueRange(0.18);
  controller.setActiveMaskSaturationMin(0.24);
  controller.setActiveMaskSaturationMax(0.77);
  controller.setActiveMaskRemoveStrength(63.0);
  if (!almost_equal(controller.activeMaskTemperature(), 14.0) ||
      !almost_equal(controller.activeMaskTint(), -11.0) ||
      !almost_equal(controller.activeMaskVibrance(), 26.0) ||
      !almost_equal(controller.activeMaskSaturation(), -9.0)) {
    return fail("selection masks should expose local color controls");
  }
  if (!almost_equal(controller.activeMaskHueCenter(), 0.12) ||
      !almost_equal(controller.activeMaskHueRange(), 0.18) ||
      !almost_equal(controller.activeMaskSaturationMin(), 0.24) ||
      !almost_equal(controller.activeMaskSaturationMax(), 0.77) ||
      !almost_equal(controller.activeMaskRemoveStrength(), 63.0)) {
    return fail("color range masks should expose editable hue and saturation constraints");
  }

  controller.addMask(QStringLiteral("depth"));
  controller.setActiveMaskDepthMin(0.28);
  controller.setActiveMaskDepthMax(0.74);
  controller.setActiveMaskRangeFeather(0.09);
  if (controller.activeMaskKind() != QStringLiteral("depth") ||
      !almost_equal(controller.activeMaskDepthMin(), 0.28) ||
      !almost_equal(controller.activeMaskDepthMax(), 0.74) ||
      !almost_equal(controller.activeMaskRangeFeather(), 0.09)) {
    return fail("depth range masks should expose editable depth constraints");
  }

  controller.startLassoSelection(false);
  controller.appendActivePolygonPoint(0.18, 0.18);
  controller.appendActivePolygonPoint(0.70, 0.22);
  controller.appendActivePolygonPoint(0.64, 0.72);
  if (controller.activeMaskKind() != QStringLiteral("lasso") ||
      controller.activeMaskPolygonPointCount() != 3) {
    return fail("lasso selections should create a freehand polygon-backed mask");
  }

  const auto ai_mask_path = runtime_root.filePath(QStringLiteral("ai-mask-source.png"));
  QImage ai_mask_source(24, 24, QImage::Format_ARGB32);
  ai_mask_source.fill(QColor(160, 190, 230));
  if (!ai_mask_source.save(ai_mask_path)) {
    return fail("AI mask source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController ai_mask_controller;
  ai_mask_controller.loadAsset(
      QStringLiteral("ai-mask-source"),
      ai_mask_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  ai_mask_controller.addAIMask(QStringLiteral("sky"));
  auto active_mask = ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("sky") ||
      active_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty()) {
    return fail("AI sky masks should create a semantic raster mask");
  }
  ai_mask_controller.addAIMask(QStringLiteral("background"));
  active_mask = ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("background") ||
      active_mask.value(QStringLiteral("segmentationWidth")).toInt() <= 0) {
    return fail("AI background masks should create an inverted semantic raster mask");
  }
  ai_mask_controller.addAIMask(QStringLiteral("hair"));
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("hair") ||
      !almost_equal(ai_mask_controller.activeMaskFeather(), 0.5) ||
      !almost_equal(ai_mask_controller.activeMaskSelectionFineEdge(), 65.0)) {
    return fail("AI hair masks should create editable semantic mask entries");
  }
  ai_mask_controller.startAIMaskRefineBrush(false);
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("brush") ||
      ai_mask_controller.activeMaskOperation() != QStringLiteral("add") ||
      ai_mask_controller.activeMaskBrushPreset() != QStringLiteral("Edge Aware") ||
      !ai_mask_controller.activeMaskBrushAutoMask() ||
      !almost_equal(ai_mask_controller.activeMaskBrushSize(), 0.026)) {
    return fail("AI mask refine brush should create an edge-aware additive brush source");
  }
  ai_mask_controller.addAIMask(QStringLiteral("person"));
  if (ai_mask_controller.activeMaskKind() != QStringLiteral("people")) {
    return fail("AI person masks should normalize to the people semantic target");
  }

  const auto alpha_mask_path = runtime_root.filePath(QStringLiteral("imported-alpha-mask.png"));
  QImage alpha_mask_source(4, 2, QImage::Format_ARGB32);
  alpha_mask_source.fill(QColor(255, 255, 255, 0));
  alpha_mask_source.setPixelColor(1, 0, QColor(255, 255, 255, 128));
  alpha_mask_source.setPixelColor(2, 0, QColor(255, 255, 255, 255));
  if (!alpha_mask_source.save(alpha_mask_path)) {
    return fail("alpha mask source image should be writable");
  }
  if (!ai_mask_controller.addAlphaImageMask(QUrl::fromLocalFile(alpha_mask_path).toString()) ||
      ai_mask_controller.activeMaskKind() != QStringLiteral("alpha")) {
    return fail("alpha image masks should import from local image files");
  }
  active_mask = ai_mask_controller.maskStack().at(ai_mask_controller.activeMaskIndex()).toMap();
  if (active_mask.value(QStringLiteral("segmentationData")).toByteArray().size() != 8 ||
      active_mask.value(QStringLiteral("segmentationWidth")).toInt() != 4 ||
      active_mask.value(QStringLiteral("segmentationHeight")).toInt() != 2 ||
      active_mask.value(QStringLiteral("alphaSourcePath")).toString() != alpha_mask_path) {
    return fail("alpha image masks should store imported matte dimensions and source path");
  }

  controller.setSharpening(28.0);
  controller.setSharpenMasking(41.0);
  controller.setLuminanceNr(19.0);
  controller.setColorNr(24.0);
  controller.setLensDistortion(-16.0);
  controller.setLensVignette(22.0);
  controller.setChromaticAberration(37.0);
  controller.setPerspectiveVertical(-29.0);
  controller.setPerspectiveHorizontal(18.0);
  if (!almost_equal(controller.sharpening(), 28.0) ||
      !almost_equal(controller.sharpenMasking(), 41.0) ||
      !almost_equal(controller.luminanceNr(), 19.0) ||
      !almost_equal(controller.colorNr(), 24.0) ||
      !almost_equal(controller.lensDistortion(), -16.0) ||
      !almost_equal(controller.lensVignette(), 22.0) ||
      !almost_equal(controller.chromaticAberration(), 37.0) ||
      !almost_equal(controller.perspectiveVertical(), -29.0) ||
      !almost_equal(controller.perspectiveHorizontal(), 18.0)) {
    return fail("detail, optics, and perspective controls should retain editable preview state");
  }

  controller.setUprightMode(QStringLiteral("Guided"));
  controller.setUprightVerticalGuideAngle(12.0);
  controller.setUprightHorizontalGuideAngle(-8.0);
  if (controller.uprightMode() != QStringLiteral("Guided") ||
      !almost_equal(controller.uprightVerticalGuideAngle(), 12.0) ||
      !almost_equal(controller.uprightHorizontalGuideAngle(), -8.0) ||
      !almost_equal(controller.perspectiveVertical(), -30.0) ||
      !almost_equal(controller.perspectiveHorizontal(), 20.0)) {
    return fail("guided upright should persist guide angles and derive perspective corrections");
  }

  controller.setTransformRotation(4.5);
  controller.setTransformScale(1.35);
  controller.setTransformSkewX(-22.0);
  controller.setTransformSkewY(14.0);
  controller.setContentAwareCropFill(true);
  if (!almost_equal(controller.transformRotation(), 4.5) ||
      !almost_equal(controller.transformScale(), 1.35) ||
      !almost_equal(controller.transformSkewX(), -22.0) ||
      !almost_equal(controller.transformSkewY(), 14.0) ||
      !controller.contentAwareCropFill()) {
    return fail("transform controls should retain rotate, scale, skew, and crop-fill preview state");
  }

  const auto crop_precision_path = runtime_root.filePath(QStringLiteral("crop-transform-precision.png"));
  QImage crop_precision_source(32, 20, QImage::Format_ARGB32);
  crop_precision_source.fill(QColor::fromRgb(112, 128, 144));
  if (!crop_precision_source.save(crop_precision_path)) {
    return fail("crop precision source image should be writable");
  }
  photo_editor::desktop::EditorPreviewController crop_precision_controller;
  crop_precision_controller.loadAsset(
      QStringLiteral("crop-transform-precision"),
      crop_precision_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  crop_precision_controller.updateCropRect(0.12, 0.14, 0.52, 0.62);
  crop_precision_controller.setPerspectiveVertical(-18.0);
  crop_precision_controller.setPerspectiveHorizontal(12.0);
  crop_precision_controller.setTransformRotation(4.5);
  crop_precision_controller.setTransformScale(1.35);
  crop_precision_controller.setTransformSkewX(-22.0);
  crop_precision_controller.setTransformSkewY(14.0);
  crop_precision_controller.setLiquifyRadius(0.42);
  crop_precision_controller.setLiquifyStrength(36.0);
  crop_precision_controller.setContentAwareCropFill(true);
  crop_precision_controller.resetCropRect();
  if (!almost_equal(crop_precision_controller.cropRectX(), 0.0) ||
      !almost_equal(crop_precision_controller.cropRectY(), 0.0) ||
      !almost_equal(crop_precision_controller.cropRectWidth(), 1.0) ||
      !almost_equal(crop_precision_controller.cropRectHeight(), 1.0) ||
      !almost_equal(crop_precision_controller.transformRotation(), 4.5) ||
      !almost_equal(crop_precision_controller.transformScale(), 1.35) ||
      !almost_equal(crop_precision_controller.transformSkewX(), -22.0) ||
      !almost_equal(crop_precision_controller.transformSkewY(), 14.0) ||
      !almost_equal(crop_precision_controller.perspectiveVertical(), -18.0) ||
      !almost_equal(crop_precision_controller.perspectiveHorizontal(), 12.0) ||
      !almost_equal(crop_precision_controller.liquifyRadius(), 0.42) ||
      !almost_equal(crop_precision_controller.liquifyStrength(), 36.0) ||
      !crop_precision_controller.contentAwareCropFill()) {
    return fail("reset crop should preserve transform, perspective, liquify, and crop-fill state");
  }
  crop_precision_controller.updateCropRect(0.10, 0.20, 0.50, 0.60);
  if (!crop_precision_controller.resetTransform() ||
      !almost_equal(crop_precision_controller.cropRectX(), 0.10) ||
      !almost_equal(crop_precision_controller.cropRectY(), 0.20) ||
      !almost_equal(crop_precision_controller.cropRectWidth(), 0.50) ||
      !almost_equal(crop_precision_controller.cropRectHeight(), 0.60) ||
      !almost_equal(crop_precision_controller.transformRotation(), 0.0) ||
      !almost_equal(crop_precision_controller.transformScale(), 1.0) ||
      !almost_equal(crop_precision_controller.transformSkewX(), 0.0) ||
      !almost_equal(crop_precision_controller.transformSkewY(), 0.0) ||
      !almost_equal(crop_precision_controller.perspectiveVertical(), 0.0) ||
      !almost_equal(crop_precision_controller.perspectiveHorizontal(), 0.0) ||
      !almost_equal(crop_precision_controller.liquifyRadius(), 0.25) ||
      !almost_equal(crop_precision_controller.liquifyStrength(), 0.0) ||
      crop_precision_controller.contentAwareCropFill()) {
    return fail("reset transform should preserve explicit crop geometry while clearing transform state");
  }
  if (crop_precision_controller.resetTransform()) {
    return fail("reset transform should report false when transform state is already neutral");
  }

  controller.deselectActiveMask();
  if (controller.hasActiveMask() || controller.activeMaskIndex() != -1) {
    return fail("deselect should clear the active mask focus without deleting masks");
  }

  controller.setTemperature(22.0);
  if (!controller.copyCurrentSettings() || !controller.hasCopiedSettings()) {
    return fail("copying settings should populate the edit clipboard");
  }

  controller.setExposure(0.0);
  controller.setTemperature(0.0);
  if (!controller.pasteCopiedSettings({QStringLiteral("color")})) {
    return fail("pasting copied settings should support partial adjustment groups");
  }
  if (!almost_equal(controller.exposure(), 0.0)) {
    std::cerr << "exposure after color paste: " << controller.exposure() << '\n';
    return fail("partial paste should preserve excluded tone controls");
  }
  if (!almost_equal(controller.temperature(), 22.0)) {
    std::cerr << "temperature after color paste: " << controller.temperature() << '\n';
    return fail("partial paste should apply included color controls");
  }

  controller.setRawExposure(1.25);
  controller.setRawBoost(0.55);
  controller.setRawShadowBoost(1.4);
  controller.setRawChromaNoiseReduction(0.62);
  controller.setRawMoireReduction(0.37);
  controller.setRawHotDeadPixelCleanup(0.41);
  controller.setRawFalseColorSuppression(0.29);
  controller.setRawDemosaicQuality(QStringLiteral("Full"));
  controller.setRawCameraMatchingLook(QStringLiteral("Portrait"));
  controller.setRawWhiteBalanceOverride(true);
  controller.setRawWhiteBalanceTemperature(6400.0);
  controller.setRawWhiteBalanceTint(-12.0);
  controller.setRawBlackLevel(0.016);
  controller.setRawWhiteLevel(1.16);
  controller.setRawCalibrationHue(-11.0);
  controller.setRawCalibrationSaturation(21.0);
  controller.setRawRedPrimaryHue(7.0);
  controller.setRawRedPrimarySaturation(18.0);
  controller.setRawGreenPrimaryHue(-9.0);
  controller.setRawGreenPrimarySaturation(-12.0);
  controller.setRawBluePrimaryHue(14.0);
  controller.setRawBluePrimarySaturation(16.0);
  if (!controller.copyCurrentSettings()) {
    return fail("copying settings should retain RAW decode controls");
  }

  controller.setRawExposure(0.0);
  controller.setRawBoost(0.0);
  controller.setRawShadowBoost(1.0);
  controller.setRawChromaNoiseReduction(0.0);
  controller.setRawMoireReduction(0.0);
  controller.setRawHotDeadPixelCleanup(0.0);
  controller.setRawFalseColorSuppression(0.0);
  controller.setRawDemosaicQuality(QStringLiteral("Balanced"));
  controller.setRawCameraMatchingLook(QStringLiteral("Standard"));
  controller.setRawWhiteBalanceOverride(false);
  controller.setRawWhiteBalanceTemperature(5200.0);
  controller.setRawWhiteBalanceTint(0.0);
  controller.setRawBlackLevel(0.0);
  controller.setRawWhiteLevel(1.0);
  controller.setRawCalibrationHue(0.0);
  controller.setRawCalibrationSaturation(0.0);
  controller.setRawRedPrimaryHue(0.0);
  controller.setRawRedPrimarySaturation(0.0);
  controller.setRawGreenPrimaryHue(0.0);
  controller.setRawGreenPrimarySaturation(0.0);
  controller.setRawBluePrimaryHue(0.0);
  controller.setRawBluePrimarySaturation(0.0);
  controller.setExposure(-0.25);
  if (!controller.pasteCopiedSettings({QStringLiteral("raw")})) {
    return fail("pasting copied settings should support RAW-only adjustment groups");
  }
  if (!almost_equal(controller.rawExposure(), 1.25) ||
      !almost_equal(controller.rawBoost(), 0.55) ||
      !almost_equal(controller.rawShadowBoost(), 1.4) ||
      !almost_equal(controller.rawChromaNoiseReduction(), 0.62) ||
      !almost_equal(controller.rawMoireReduction(), 0.37) ||
      !almost_equal(controller.rawHotDeadPixelCleanup(), 0.41) ||
      !almost_equal(controller.rawFalseColorSuppression(), 0.29) ||
      controller.rawDemosaicQuality() != QStringLiteral("Full") ||
      controller.rawCameraMatchingLook() != QStringLiteral("Portrait") ||
      !controller.rawWhiteBalanceOverride() ||
      !almost_equal(controller.rawWhiteBalanceTemperature(), 6400.0) ||
      !almost_equal(controller.rawWhiteBalanceTint(), -12.0) ||
      !almost_equal(controller.rawBlackLevel(), 0.016) ||
      !almost_equal(controller.rawWhiteLevel(), 1.16) ||
      !almost_equal(controller.rawCalibrationHue(), -11.0) ||
      !almost_equal(controller.rawCalibrationSaturation(), 21.0) ||
      !almost_equal(controller.rawRedPrimaryHue(), 7.0) ||
      !almost_equal(controller.rawRedPrimarySaturation(), 18.0) ||
      !almost_equal(controller.rawGreenPrimaryHue(), -9.0) ||
      !almost_equal(controller.rawGreenPrimarySaturation(), -12.0) ||
      !almost_equal(controller.rawBluePrimaryHue(), 14.0) ||
      !almost_equal(controller.rawBluePrimarySaturation(), 16.0) ||
      !almost_equal(controller.exposure(), -0.25)) {
    return fail("raw-only paste should restore RAW decode controls without touching tone controls");
  }

  controller.setTexture(18.0);
  controller.setDehaze(11.0);
  controller.setRawDetail(0.72);
  controller.setRawChromaNoiseReduction(0.48);
  controller.setRawMoireReduction(0.26);
  controller.setRawHotDeadPixelCleanup(0.34);
  controller.setRawFalseColorSuppression(0.39);
  controller.setRawCalibrationHue(13.0);
  controller.setRawCalibrationSaturation(-17.0);
  controller.setRawRedPrimaryHue(-6.0);
  controller.setRawRedPrimarySaturation(22.0);
  controller.setRawGreenPrimaryHue(8.0);
  controller.setRawGreenPrimarySaturation(-11.0);
  controller.setRawBluePrimaryHue(10.0);
  controller.setRawBluePrimarySaturation(15.0);
  controller.setRawDemosaicQuality(QStringLiteral("Preview"));
  controller.setRawCameraMatchingLook(QStringLiteral("Neutral"));
  controller.setRawLensCorrection(false);
  if (!controller.savePreset(QStringLiteral("Portrait Base"), true) ||
      !controller.presetNames().contains(QStringLiteral("Portrait Base"))) {
    return fail("saving a preset should persist it in the controller");
  }
  if (!controller.savePresetToPack(QStringLiteral("Client Warmth"), QStringLiteral("Client Pack"), true) ||
      !controller.presetNames().contains(QStringLiteral("Client Warmth")) ||
      !controller.presetPackNames().contains(QStringLiteral("Client Pack")) ||
      controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      !controller.searchPresetNames(QStringLiteral("Client Pack")).contains(QStringLiteral("Client Warmth")) ||
      !controller.presetThumbnailSource(QStringLiteral("Client Warmth"))
           .startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("saving a user preset should support custom preset packs");
  }
  if (!controller.savePresetToPack(QStringLiteral("Client Organized"), QStringLiteral("Working Pack"), true) ||
      !controller.isUserPreset(QStringLiteral("Client Organized"))) {
    return fail("saved presets should be identified as user-editable presets");
  }
  controller.setPresetFavorite(QStringLiteral("Client Organized"), true);
  if (!controller.renamePreset(QStringLiteral("Client Organized"), QStringLiteral("Client Organized Final")) ||
      controller.presetNames().contains(QStringLiteral("Client Organized")) ||
      !controller.presetNames().contains(QStringLiteral("Client Organized Final")) ||
      !controller.isPresetFavorite(QStringLiteral("Client Organized Final")) ||
      controller.renamePreset(QStringLiteral("Client Organized Final"), QStringLiteral("Portrait Base")) ||
      controller.renamePreset(QStringLiteral("Landscape Pop"), QStringLiteral("Landscape Copy"))) {
    return fail("user presets should be renameable without clobbering other presets or built-ins");
  }
  if (!controller.movePresetToPack(QStringLiteral("Client Organized Final"), QStringLiteral("Delivered Pack")) ||
      controller.presetPackName(QStringLiteral("Client Organized Final")) != QStringLiteral("Delivered Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Organized Final")) != QStringLiteral("Delivered Pack") ||
      !controller.searchPresetNames(QStringLiteral("Delivered Pack")).contains(QStringLiteral("Client Organized Final")) ||
      controller.movePresetToPack(QStringLiteral("Landscape Pop"), QStringLiteral("Delivered Pack"))) {
    return fail("user presets should be movable between organizational packs");
  }
  if (!controller.deletePreset(QStringLiteral("Client Organized Final")) ||
      controller.presetNames().contains(QStringLiteral("Client Organized Final"))) {
    return fail("organized user presets should remain deletable after rename and move");
  }
  const auto exported_pack_path = runtime_root.filePath(QStringLiteral("client-pack.photoeditor-presets.json"));
  if (!controller.exportPresetPack(exported_pack_path, QStringLiteral("Client Pack")) ||
      !QFileInfo::exists(exported_pack_path)) {
    return fail("user preset packs should export to a portable preset pack file");
  }
  if (!controller.deletePreset(QStringLiteral("Client Warmth")) ||
      controller.presetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("user presets should be removable before pack import");
  }
  if (!controller.importPresetPack(exported_pack_path, true) ||
      !controller.presetNames().contains(QStringLiteral("Client Warmth")) ||
      controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack")) {
    return fail("user preset packs should import from exported preset pack files");
  }
  controller.setPresetFavorite(QStringLiteral("Client Warmth"), true);
  if (!controller.isPresetFavorite(QStringLiteral("Client Warmth")) ||
      !controller.favoritePresetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("user preset favorites should be saved with preset metadata");
  }
  const auto sync_folder_path = runtime_root.filePath(QStringLiteral("preset-sync"));
  if (!controller.presetSyncManifestJson().contains(QStringLiteral("photo-editor-preset-sync")) ||
      !controller.exportPresetSyncBundle(sync_folder_path)) {
    return fail("preset sync hooks should export a portable sync manifest");
  }
  if (!controller.deletePreset(QStringLiteral("Client Warmth")) ||
      controller.presetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("sync import test should be able to remove the local preset first");
  }
  if (!controller.importPresetSyncBundle(sync_folder_path, true) ||
      controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      !controller.isPresetFavorite(QStringLiteral("Client Warmth"))) {
    return fail("preset sync hooks should restore presets and metadata from a sync folder");
  }

  controller.setTexture(0.0);
  controller.setDehaze(0.0);
  controller.setRawDetail(0.0);
  controller.setRawChromaNoiseReduction(0.0);
  controller.setRawMoireReduction(0.0);
  controller.setRawHotDeadPixelCleanup(0.0);
  controller.setRawFalseColorSuppression(0.0);
  controller.setRawCalibrationHue(0.0);
  controller.setRawCalibrationSaturation(0.0);
  controller.setRawRedPrimaryHue(0.0);
  controller.setRawRedPrimarySaturation(0.0);
  controller.setRawGreenPrimaryHue(0.0);
  controller.setRawGreenPrimarySaturation(0.0);
  controller.setRawBluePrimaryHue(0.0);
  controller.setRawBluePrimarySaturation(0.0);
  controller.setRawDemosaicQuality(QStringLiteral("Balanced"));
  controller.setRawCameraMatchingLook(QStringLiteral("Standard"));
  controller.setRawLensCorrection(true);
  controller.setExposure(-0.5);
  controller.setTemperature(0.0);
  if (!controller.applyPreset(QStringLiteral("Portrait Base"), {QStringLiteral("detail")})) {
    return fail("preset application should support partial adjustment groups");
  }
  if (!almost_equal(controller.texture(), 18.0) || !almost_equal(controller.dehaze(), 11.0)) {
    return fail("detail-only preset application should restore the saved detail controls");
  }
  if (!almost_equal(controller.temperature(), 0.0)) {
    return fail("partial preset application should not touch excluded color controls");
  }

  if (!controller.applyPreset(QStringLiteral("Portrait Base"), {QStringLiteral("raw")})) {
    return fail("preset application should support RAW-only adjustment groups");
  }
  if (!almost_equal(controller.rawDetail(), 0.72) ||
      !almost_equal(controller.rawChromaNoiseReduction(), 0.48) ||
      !almost_equal(controller.rawMoireReduction(), 0.26) ||
      !almost_equal(controller.rawHotDeadPixelCleanup(), 0.34) ||
      !almost_equal(controller.rawFalseColorSuppression(), 0.39) ||
      !almost_equal(controller.rawCalibrationHue(), 13.0) ||
      !almost_equal(controller.rawCalibrationSaturation(), -17.0) ||
      !almost_equal(controller.rawRedPrimaryHue(), -6.0) ||
      !almost_equal(controller.rawRedPrimarySaturation(), 22.0) ||
      !almost_equal(controller.rawGreenPrimaryHue(), 8.0) ||
      !almost_equal(controller.rawGreenPrimarySaturation(), -11.0) ||
      !almost_equal(controller.rawBluePrimaryHue(), 10.0) ||
      !almost_equal(controller.rawBluePrimarySaturation(), 15.0) ||
      controller.rawDemosaicQuality() != QStringLiteral("Preview") ||
      controller.rawCameraMatchingLook() != QStringLiteral("Neutral") ||
      controller.rawLensCorrection() ||
      !almost_equal(controller.texture(), 18.0) ||
      !almost_equal(controller.dehaze(), 11.0)) {
    return fail("raw-only preset application should restore RAW decode controls without touching detail controls");
  }

  controller.setPreviewTargetEdge(1400);
  const auto reference_cache_before_exposure =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          false,
          false);
  controller.setExposure(0.4);
  const auto reference_cache_after_exposure =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          false,
          false);
  if (reference_cache_before_exposure != reference_cache_after_exposure) {
    return fail("reference preview cache should stay stable across non-RAW, non-crop adjustment changes");
  }

  const auto edited_cache_before_crop =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          true,
          false);
  controller.setCropPreset(QStringLiteral("4x5"));
  const auto reference_cache_after_crop =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          false,
          false);
  const auto edited_cache_after_crop =
      photo_editor::desktop::EditorPreviewControllerTestAccess::cache_path_for_selection(
          controller,
          true,
          false);
  if (reference_cache_after_crop == reference_cache_after_exposure ||
      edited_cache_after_crop == edited_cache_before_crop) {
    return fail("preview cache paths should still change when crop-dependent content changes");
  }
  controller.setCropPreset(QStringLiteral("1:1"));
  if (!almost_equal(controller.cropAspectRatio(), 1.0)) {
    return fail("square aspect presets should support 1:1 ratio labels");
  }
  controller.setCropPreset(QStringLiteral("9:16"));
  if (!almost_equal(controller.cropAspectRatio(), 9.0 / 16.0)) {
    return fail("portrait aspect presets should parse colon-delimited ratio labels");
  }
  const auto available_crop_presets = controller.cropAspectPresets();
  if (!available_crop_presets.contains(QStringLiteral("9:16")) ||
      !available_crop_presets.contains(QStringLiteral("5:4"))) {
    return fail("crop aspect presets should expose common landscape and portrait ratios");
  }
  if (!available_crop_presets.contains(QStringLiteral("8x10 in")) ||
      !available_crop_presets.contains(QStringLiteral("A4 Landscape"))) {
    return fail("crop aspect presets should expose common print sizes");
  }
  photo_editor::desktop::EditRecipeState portrait_crop_state;
  portrait_crop_state.crop_preset = QStringLiteral("9:16");
  const auto portrait_crop_rect =
      photo_editor::desktop::crop_rect_for_image(QSize(1600, 1000), portrait_crop_state);
  if (portrait_crop_rect.width() < 560 || portrait_crop_rect.width() > 565 ||
      portrait_crop_rect.height() != 1000) {
    return fail("render crop utilities should apply expanded aspect presets consistently");
  }
  controller.setCropPreset(QStringLiteral("8x10 in"));
  if (!almost_equal(controller.cropAspectRatio(), 0.8)) {
    return fail("print crop presets should parse inch-based portrait ratios");
  }
  controller.setCropPreset(QStringLiteral("A4 Landscape"));
  if (!almost_equal(controller.cropAspectRatio(), 297.0 / 210.0)) {
    return fail("print crop presets should parse ISO paper landscape ratios");
  }
  photo_editor::desktop::EditRecipeState print_crop_state;
  print_crop_state.crop_preset = QStringLiteral("8x10 in");
  const auto print_crop_rect =
      photo_editor::desktop::crop_rect_for_image(QSize(1600, 1000), print_crop_state);
  if (print_crop_rect.width() != 800 || print_crop_rect.height() != 1000) {
    return fail("render crop utilities should apply print size crops consistently");
  }
  if (!controller.addCustomCropAspectPreset(QStringLiteral("2.39:1")) ||
      controller.addCustomCropAspectPreset(QStringLiteral("2.39x1"))) {
    return fail("custom crop aspect presets should be addable without duplicate aliases");
  }
  if (!controller.cropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      !controller.customCropAspectPresets().contains(QStringLiteral("2.39:1"))) {
    return fail("custom crop aspect presets should appear in the merged crop preset list");
  }
  controller.setCropPreset(QStringLiteral("2.39:1"));
  if (!almost_equal(controller.cropAspectRatio(), 2.39)) {
    return fail("custom crop aspect presets should drive crop aspect ratio parsing");
  }
  const auto crop_preset_bundle_path = runtime_root.filePath(QStringLiteral("crop-presets.json"));
  if (!controller.exportCustomCropAspectPresets(crop_preset_bundle_path)) {
    return fail("custom crop aspect presets should export to a reusable preset bundle");
  }
  photo_editor::desktop::EditorPreviewController persisted_crop_controller;
  if (!persisted_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      !persisted_crop_controller.removeCustomCropAspectPreset(QStringLiteral("2.39x1"))) {
    return fail("custom crop aspect presets should persist and be removable by ratio alias");
  }
  photo_editor::desktop::EditorPreviewController removed_crop_controller;
  if (removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1"))) {
    return fail("removed custom crop aspect presets should not reload from disk");
  }
  if (removed_crop_controller.importCustomCropAspectPresets(crop_preset_bundle_path) != 1 ||
      !removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      removed_crop_controller.importCustomCropAspectPresets(crop_preset_bundle_path) != 0) {
    return fail("custom crop aspect preset imports should restore exported presets without duplicates");
  }
  if (!removed_crop_controller.addCustomCropAspectPreset(QStringLiteral("7:5")) ||
      removed_crop_controller.importCustomCropAspectPresets(crop_preset_bundle_path, true) != 1 ||
      !removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("2.39:1")) ||
      removed_crop_controller.customCropAspectPresets().contains(QStringLiteral("7:5"))) {
    return fail("custom crop aspect preset imports should optionally replace existing custom presets");
  }
  photo_editor::desktop::EditorPreviewController horizon_controller;
  horizon_controller.loadAsset(
      QStringLiteral("horizon-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  constexpr auto kPi = 3.14159265358979323846;
  const auto horizon_angle = std::atan2(0.2, 0.8) * 180.0 / kPi;
  if (!horizon_controller.straightenFromHorizon(0.1, 0.3, 0.9, 0.5) ||
      !almost_equal(horizon_controller.transformRotation(), -horizon_angle, 1e-5) ||
      !almost_equal(horizon_controller.uprightHorizontalGuideAngle(), horizon_angle, 1e-5)) {
    return fail("straighten from horizon should convert a marked horizon line into a rotation correction");
  }
  horizon_controller.undo();
  if (!almost_equal(horizon_controller.transformRotation(), 0.0) ||
      !almost_equal(horizon_controller.uprightHorizontalGuideAngle(), 0.0)) {
    return fail("straighten from horizon should be undoable");
  }
  if (!horizon_controller.straightenFromHorizon(0.9, 0.5, 0.1, 0.3) ||
      !almost_equal(horizon_controller.transformRotation(), -horizon_angle, 1e-5)) {
    return fail("straighten from horizon should treat reversed guide endpoints consistently");
  }
  const auto rotation_before_invalid_horizon = horizon_controller.transformRotation();
  if (horizon_controller.straightenFromHorizon(0.4, 0.4, 0.4, 0.4) ||
      !almost_equal(horizon_controller.transformRotation(), rotation_before_invalid_horizon)) {
    return fail("straighten from horizon should reject zero-length horizon guides");
  }

  const auto wb_histogram_path = runtime_root.filePath(QStringLiteral("wb-histogram-source.png"));
  QImage wb_histogram_source(48, 48, QImage::Format_ARGB32);
  wb_histogram_source.fill(QColor(122, 122, 122));
  for (int y = 0; y < wb_histogram_source.height(); ++y) {
    for (int x = 0; x < 8; ++x) {
      wb_histogram_source.setPixelColor(x, y, Qt::black);
    }
    for (int x = wb_histogram_source.width() - 8; x < wb_histogram_source.width(); ++x) {
      wb_histogram_source.setPixelColor(x, y, Qt::white);
    }
  }
  for (int y = 20; y <= 28; ++y) {
    for (int x = 20; x <= 28; ++x) {
      wb_histogram_source.setPixelColor(x, y, QColor(230, 210, 180));
    }
  }
  wb_histogram_source.setPixelColor(24, 24, QColor(90, 120, 255));
  if (!wb_histogram_source.save(wb_histogram_path)) {
    return fail("wb histogram source image should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-wb-histogram"),
      wb_histogram_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(controller)) {
    return fail("wb histogram preview should finish rendering");
  }
  if (controller.histogramSampleCount() <= 0 ||
      controller.histogramShadowClipCount() <= 0 ||
      controller.histogramHighlightClipCount() <= 0) {
    return fail("preview histogram should report sample count and clipping diagnostics");
  }
  controller.setHighlightReconstructionMode(QStringLiteral("Camera Default"));
  if (controller.highlightReconstructionMode() != QStringLiteral("Camera Default") ||
      !controller.rawHighlightRecovery()) {
    return fail("controller should expose camera-default highlight reconstruction mode");
  }
  controller.setRawHighlightRecovery(false);
  if (controller.highlightReconstructionMode() != QStringLiteral("Disabled")) {
    return fail("legacy RAW highlight recovery toggle should select the disabled reconstruction mode");
  }
  controller.setRawHighlightRecovery(true);
  if (controller.highlightReconstructionMode() != QStringLiteral("Enabled")) {
    return fail("legacy RAW highlight recovery toggle should restore the enabled reconstruction mode");
  }

  photo_editor::desktop::RawDecodeSettings preview_demosaic_settings;
  preview_demosaic_settings.demosaic_quality = QStringLiteral("Preview");
  photo_editor::desktop::RawDecodeSettings balanced_demosaic_settings;
  balanced_demosaic_settings.demosaic_quality = QStringLiteral("Balanced");
  photo_editor::desktop::RawDecodeSettings full_demosaic_settings;
  full_demosaic_settings.demosaic_quality = QStringLiteral("Full");
  full_demosaic_settings.chroma_noise_reduction_amount = 1.8;
  full_demosaic_settings.moire_reduction_amount = -0.5;
  full_demosaic_settings.hot_dead_pixel_cleanup_amount = 1.2;
  full_demosaic_settings.false_color_suppression_amount = -0.8;
  if (!photo_editor::desktop::raw_decode_uses_draft_demosaic(preview_demosaic_settings, false) ||
      !photo_editor::desktop::raw_decode_uses_draft_demosaic(balanced_demosaic_settings, true) ||
      photo_editor::desktop::raw_decode_uses_draft_demosaic(full_demosaic_settings, true) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_chroma_noise_reduction_amount(full_demosaic_settings),
          1.0) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_moire_reduction_amount(full_demosaic_settings),
          0.0) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_hot_dead_pixel_cleanup_amount(full_demosaic_settings),
          1.0) ||
      !almost_equal(
          photo_editor::desktop::raw_decode_false_color_suppression_amount(full_demosaic_settings),
          0.0)) {
    return fail("RAW demosaic quality modes should select draft/full decode behavior");
  }

  QImage hot_pixel_test(5, 5, QImage::Format_RGBA64);
  hot_pixel_test.fill(QColor::fromRgbF(0.22F, 0.22F, 0.22F));
  hot_pixel_test.setPixelColor(2, 2, QColor::fromRgbF(1.0F, 1.0F, 1.0F));
  if (!photo_editor::desktop::apply_raw_hot_dead_pixel_cleanup(&hot_pixel_test, 1.0) ||
      hot_pixel_test.pixelColor(2, 2).redF() > 0.35) {
    return fail("RAW hot/dead pixel cleanup should repair isolated bright pixels");
  }

  QImage dead_pixel_test(5, 5, QImage::Format_RGBA64);
  dead_pixel_test.fill(QColor::fromRgbF(0.72F, 0.72F, 0.72F));
  dead_pixel_test.setPixelColor(2, 2, QColor::fromRgbF(0.0F, 0.0F, 0.0F));
  if (!photo_editor::desktop::apply_raw_hot_dead_pixel_cleanup(&dead_pixel_test, 1.0) ||
      dead_pixel_test.pixelColor(2, 2).redF() < 0.55) {
    return fail("RAW hot/dead pixel cleanup should repair isolated dark pixels");
  }

  QImage normal_pixel_test(5, 5, QImage::Format_RGBA64);
  normal_pixel_test.fill(QColor::fromRgbF(0.22F, 0.22F, 0.22F));
  normal_pixel_test.setPixelColor(2, 2, QColor::fromRgbF(0.24F, 0.24F, 0.24F));
  if (photo_editor::desktop::apply_raw_hot_dead_pixel_cleanup(&normal_pixel_test, 1.0) ||
      normal_pixel_test.pixelColor(2, 2).redF() < 0.23) {
    return fail("RAW hot/dead pixel cleanup should preserve normal local texture");
  }

  QImage false_color_test(5, 5, QImage::Format_RGBA64);
  false_color_test.fill(QColor::fromRgbF(0.48F, 0.48F, 0.48F));
  false_color_test.setPixelColor(2, 2, QColor::fromRgbF(0.95F, 0.05F, 0.05F));
  if (!photo_editor::desktop::apply_raw_false_color_suppression(&false_color_test, 1.0) ||
      false_color_test.pixelColor(2, 2).saturationF() > 0.25F) {
    return fail("RAW false-color suppression should neutralize isolated chroma outliers");
  }

  QImage neutral_detail_test(5, 5, QImage::Format_RGBA64);
  neutral_detail_test.fill(QColor::fromRgbF(0.48F, 0.48F, 0.48F));
  neutral_detail_test.setPixelColor(2, 2, QColor::fromRgbF(0.55F, 0.55F, 0.55F));
  if (photo_editor::desktop::apply_raw_false_color_suppression(&neutral_detail_test, 1.0) ||
      neutral_detail_test.pixelColor(2, 2).redF() < 0.54F) {
    return fail("RAW false-color suppression should preserve neutral luminance detail");
  }

  photo_editor::desktop::EditorPreviewControllerTestAccess::set_raw_as_shot_white_balance(
      controller,
      7350.0,
      -21.0);
  if (!almost_equal(controller.rawAsShotWhiteBalanceTemperature(), 7350.0) ||
      !almost_equal(controller.rawAsShotWhiteBalanceTint(), -21.0)) {
    return fail("controller should expose decoded RAW as-shot white balance metadata");
  }
  controller.setRawWhiteBalanceOverride(false);
  controller.setRawWhiteBalanceTemperature(5200.0);
  controller.setRawWhiteBalanceTint(0.0);
  controller.useAsShotRawWhiteBalance();
  if (!controller.rawWhiteBalanceOverride() ||
      !almost_equal(controller.rawWhiteBalanceTemperature(), 7350.0) ||
      !almost_equal(controller.rawWhiteBalanceTint(), -21.0)) {
    return fail("as-shot RAW white balance action should enable the override and copy decoded metadata");
  }

  QImage raw_clipping_source(12, 10, QImage::Format_ARGB32);
  raw_clipping_source.fill(QColor(128, 128, 128));
  raw_clipping_source.setPixelColor(1, 1, Qt::black);
  raw_clipping_source.setPixelColor(2, 2, Qt::white);
  photo_editor::desktop::EditorPreviewControllerTestAccess::update_raw_clipping_warnings(
      controller,
      raw_clipping_source);
  if (controller.rawClipSampleCount() != raw_clipping_source.width() * raw_clipping_source.height() ||
      controller.rawShadowClipCount() != 1 ||
      controller.rawHighlightClipCount() != 1) {
    return fail("RAW clipping warnings should report source shadow and highlight clipping separately");
  }

  controller.setWhiteBalanceSampling(true);
  controller.sampleWhiteBalanceAt(0.5, 0.5);
  if (controller.whiteBalanceSampling()) {
    return fail("white balance sampling should exit sampling mode after applying a patch sample");
  }
  if (!(controller.temperature() < -1.0)) {
    return fail("white balance sampling should average a local patch instead of following a noisy center pixel outlier");
  }

  photo_editor::desktop::TestImageSink image_sink;
  controller.setImageProvider(&image_sink);

  const auto color_space_path = runtime_root.filePath(QStringLiteral("preview-color-space.png"));
  QImage color_space_source(24, 16, QImage::Format_ARGB32);
  for (int y = 0; y < color_space_source.height(); ++y) {
    for (int x = 0; x < color_space_source.width(); ++x) {
      color_space_source.setPixelColor(
          x,
          y,
          QColor(
              32 + ((x * 200) / std::max(1, color_space_source.width() - 1)),
              48 + ((y * 160) / std::max(1, color_space_source.height() - 1)),
              96));
    }
  }
  color_space_source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  if (!color_space_source.save(color_space_path)) {
    return fail("preview color space source image should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-color-space"),
      color_space_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!wait_for_preview_idle(controller)) {
    return fail("preview color space render should finish rendering");
  }

  const auto preview_image = image_sink.image(QStringLiteral("edited"));
  const auto expected_srgb = QColorSpace(QColorSpace::SRgb);
  if (preview_image.isNull()) {
    return fail("image sink should receive the rendered in-memory preview");
  }
  if (!preview_image.colorSpace().isValid() || preview_image.colorSpace() != expected_srgb) {
    return fail("rendered preview should normalize decoded images into the sRGB working space");
  }

  controller.setOutputColorSpace(QStringLiteral("Display P3"));
  if (!wait_for_preview_idle(controller)) {
    return fail("display-p3 preview render should finish rendering");
  }
  const auto display_p3_preview_image = image_sink.image(QStringLiteral("edited"));
  const auto expected_display_p3 = QColorSpace(QColorSpace::DisplayP3);
  if (controller.outputColorSpace() != QStringLiteral("Display P3")) {
    return fail("controller should expose the selected output color space");
  }
  if (display_p3_preview_image.isNull() ||
      !display_p3_preview_image.colorSpace().isValid() ||
      display_p3_preview_image.colorSpace() != expected_display_p3) {
    return fail("rendered preview should apply the selected output color space transform");
  }
  controller.setInputColorProfile(QStringLiteral("/profiles/Canon EOS R5 Adobe Standard.dcp"));
  if (controller.inputColorProfile() != QStringLiteral("Camera DCP: Canon EOS R5 Adobe Standard.dcp")) {
    return fail("controller should expose normalized DCP camera input profiles");
  }
  const auto dcp_recipe = QJsonDocument::fromJson(controller.currentRecipeJson().toUtf8()).object();
  if (dcp_recipe.value(QStringLiteral("colorPipeline"))
          .toObject()
          .value(QStringLiteral("inputProfile"))
          .toString() != QStringLiteral("Camera DCP: Canon EOS R5 Adobe Standard.dcp")) {
    return fail("controller recipes should persist DCP camera input profiles");
  }
  controller.setInputColorProfile(QStringLiteral("ICC: Manufacturer Camera Profile"));
  if (controller.inputColorProfile() != QStringLiteral("Camera ICC: Manufacturer Camera Profile")) {
    return fail("controller should expose normalized ICC camera input profiles");
  }
  controller.setRawCameraMatchingLook(QStringLiteral("Vivid"));
  if (controller.rawCameraMatchingLook() != QStringLiteral("Vivid")) {
    return fail("controller should expose selected camera matching looks");
  }
  const auto look_recipe = QJsonDocument::fromJson(controller.currentRecipeJson().toUtf8()).object();
  if (look_recipe.value(QStringLiteral("rawAdjustments"))
          .toObject()
          .value(QStringLiteral("cameraMatchingLook"))
          .toString() != QStringLiteral("Vivid")) {
    return fail("controller recipes should persist camera matching looks");
  }
  controller.setRawCameraMatchingLook(QStringLiteral("Standard"));
  controller.setInputColorProfile(QStringLiteral("Embedded"));
  controller.setMonitorColorProfile(QStringLiteral("sRGB"));
  if (!wait_for_preview_idle(controller)) {
    return fail("monitor-profile preview render should finish rendering");
  }
  const auto monitor_srgb_preview_image = image_sink.image(QStringLiteral("edited"));
  if (controller.outputColorSpace() != QStringLiteral("Display P3") ||
      controller.monitorColorProfile() != QStringLiteral("sRGB")) {
    return fail("monitor profile selection should not replace the export output profile");
  }
  if (monitor_srgb_preview_image.isNull() ||
      !monitor_srgb_preview_image.colorSpace().isValid() ||
      monitor_srgb_preview_image.colorSpace() != expected_srgb) {
    return fail("rendered preview should target the selected monitor profile");
  }

  const auto p3_boundary_path = runtime_root.filePath(QStringLiteral("preview-display-p3-boundary.png"));
  QImage p3_boundary_source(18, 12, QImage::Format_ARGB32);
  p3_boundary_source.fill(QColor(0, 255, 0));
  p3_boundary_source.setColorSpace(expected_display_p3);
  if (!p3_boundary_source.save(p3_boundary_path)) {
    return fail("Display P3 boundary source image should be writable");
  }
  controller.loadAsset(
      QStringLiteral("asset-display-p3-boundary"),
      p3_boundary_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.setOutputColorSpace(QStringLiteral("Display P3"));
  controller.setMonitorColorProfile(QStringLiteral("Display P3"));
  controller.setExposure(0.0);
  controller.setTemperature(0.0);
  controller.setTint(0.0);
  controller.setSaturation(1.0);
  if (!wait_for_preview_idle(controller)) {
    return fail("Display P3 boundary preview render should finish rendering");
  }
  const auto p3_boundary_preview = image_sink.image(QStringLiteral("edited"));
  if (p3_boundary_preview.isNull() || p3_boundary_preview.colorSpace() != expected_display_p3) {
    return fail("Display P3 boundary preview should keep the selected output profile");
  }
  const auto p3_boundary_argb = p3_boundary_preview.convertToFormat(QImage::Format_ARGB32);
  const auto p3_boundary_sample =
      p3_boundary_argb.pixel(p3_boundary_argb.width() / 2, p3_boundary_argb.height() / 2);
  if (qRed(p3_boundary_sample) > 8 || qGreen(p3_boundary_sample) < 248 ||
      qBlue(p3_boundary_sample) > 8) {
    return fail("edited Display P3 previews should not be routed through a clipped sRGB image");
  }
  controller.setSoftProofProfile(QStringLiteral("sRGB"));
  controller.setSoftProofEnabled(true);
  if (!wait_for_preview_idle(controller)) {
    return fail("soft-proof preview render should finish rendering");
  }
  const auto soft_proof_preview = image_sink.image(QStringLiteral("edited"));
  if (!controller.softProofEnabled() || controller.softProofProfile() != QStringLiteral("sRGB")) {
    return fail("controller should expose the selected soft proof profile");
  }
  if (soft_proof_preview.isNull() || soft_proof_preview.colorSpace() != expected_display_p3) {
    return fail("soft proof preview should still target the monitor display profile");
  }
  const auto soft_proof_argb = soft_proof_preview.convertToFormat(QImage::Format_ARGB32);
  const auto soft_proof_sample =
      soft_proof_argb.pixel(soft_proof_argb.width() / 2, soft_proof_argb.height() / 2);
  if (qRed(soft_proof_sample) <= qRed(p3_boundary_sample) + 16 ||
      qBlue(soft_proof_sample) <= qBlue(p3_boundary_sample) + 16) {
    return fail("soft proofing through sRGB should simulate the proof profile before monitor display");
  }
  controller.setSoftProofEnabled(false);

  controller.setOutputColorSpace(QStringLiteral("sRGB"));
  controller.setMonitorColorProfile(QStringLiteral("sRGB"));
  const auto hdr_ramp_path = runtime_root.filePath(QStringLiteral("preview-hdr-ramp.tiff"));
  QImage hdr_ramp_source(512, 24, QImage::Format_RGBA64);
  hdr_ramp_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < hdr_ramp_source.height(); ++y) {
    for (int x = 0; x < hdr_ramp_source.width(); ++x) {
      const auto t = static_cast<double>(x) / static_cast<double>(std::max(1, hdr_ramp_source.width() - 1));
      const auto value = 0.84 + t * 0.10;
      hdr_ramp_source.setPixelColor(
          x,
          y,
          QColor::fromRgbF(
              static_cast<float>(value),
              static_cast<float>(value),
              static_cast<float>(value),
              1.0F));
    }
  }
  if (!hdr_ramp_source.save(hdr_ramp_path)) {
    return fail("16-bit preview ramp source should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-hdr-preview-ramp"),
      hdr_ramp_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.setExposure(0.9);
  controller.setContrast(65.0);
  controller.setBrightness(24.0);
  controller.setHdrLocalToneMapping(42.0);
  controller.setFilmicShoulder(31.0);
  controller.setFilmicToe(-27.0);
  controller.setAdaptiveContrast(36.0);
  controller.setLevelsBlackPoint(18.0);
  controller.setLevelsGamma(22.0);
  controller.setLevelsWhitePoint(14.0);
  controller.setZoneDeepShadows(-11.0);
  controller.setZoneShadows(12.0);
  controller.setZoneMidtones(13.0);
  controller.setZoneLights(-14.0);
  controller.setZoneHighlights(15.0);
  if (!almost_equal(controller.brightness(), 24.0)) {
    return fail("brightness control should be exposed on the preview controller");
  }
  if (!almost_equal(controller.hdrLocalToneMapping(), 42.0)) {
    return fail("HDR local tone mapping control should be exposed on the preview controller");
  }
  if (!almost_equal(controller.filmicShoulder(), 31.0) ||
      !almost_equal(controller.filmicToe(), -27.0)) {
    return fail("filmic shoulder and toe controls should be exposed on the preview controller");
  }
  if (!almost_equal(controller.adaptiveContrast(), 36.0)) {
    return fail("adaptive contrast control should be exposed on the preview controller");
  }
  if (!almost_equal(controller.levelsBlackPoint(), 18.0) ||
      !almost_equal(controller.levelsGamma(), 22.0) ||
      !almost_equal(controller.levelsWhitePoint(), 14.0)) {
    return fail("levels controls should be exposed on the preview controller");
  }
  if (!almost_equal(controller.zoneDeepShadows(), -11.0) ||
      !almost_equal(controller.zoneShadows(), 12.0) ||
      !almost_equal(controller.zoneMidtones(), 13.0) ||
      !almost_equal(controller.zoneLights(), -14.0) ||
      !almost_equal(controller.zoneHighlights(), 15.0)) {
    return fail("zone controls should be exposed on the preview controller");
  }
  if (!wait_for_preview_idle(controller)) {
    return fail("16-bit preview ramp render should finish rendering");
  }
  const auto hdr_preview_image = image_sink.image(QStringLiteral("edited"));
  if (hdr_preview_image.isNull()) {
    return fail("16-bit preview ramp should produce an in-memory preview image");
  }

  QImage hdr_linear_decode;
  if (!photo_editor::desktop::decode_native_linear_image(hdr_ramp_path, &hdr_linear_decode, 0)) {
    return fail("16-bit preview ramp should decode through the native linear path");
  }
  auto expected_working = photo_editor::desktop::working_image_from_qimage(hdr_linear_decode);
  if (expected_working.isNull()) {
    return fail("native linear preview ramp decode should produce a working image");
  }
  photo_editor::pipeline::ColorAdjustmentState expected_state;
  expected_state.exposure = 0.9;
  expected_state.contrast = 65.0;
  expected_state.brightness = 24.0;
  expected_state.hdr_local_tone_mapping = 42.0;
  expected_state.filmic_shoulder = 31.0;
  expected_state.filmic_toe = -27.0;
  expected_state.adaptive_contrast = 36.0;
  expected_state.levels_black_point = 18.0;
  expected_state.levels_gamma = 22.0;
  expected_state.levels_white_point = 14.0;
  expected_state.zone_deep_shadows = -11.0;
  expected_state.zone_shadows = 12.0;
  expected_state.zone_midtones = 13.0;
  expected_state.zone_lights = -14.0;
  expected_state.zone_highlights = 15.0;
  photo_editor::pipeline::apply_color_adjustments(
      &expected_working.pixels,
      expected_working.width,
      expected_working.height,
      expected_state);
  const auto expected_preview_image = photo_editor::desktop::qimage_from_working_image(
      expected_working,
      QColorSpace(QColorSpace::SRgb));
  if (expected_preview_image.isNull()) {
    return fail("preview reference image should render from the linear working buffer");
  }
  const auto hdr_preview_delta = max_channel_delta(hdr_preview_image, expected_preview_image);
  if (hdr_preview_delta > 6) {
    std::cerr << "16-bit preview max channel delta: " << hdr_preview_delta << '\n';
    std::cerr << "16-bit preview size: " << hdr_preview_image.width() << "x" << hdr_preview_image.height()
              << " expected: " << expected_preview_image.width() << "x" << expected_preview_image.height() << '\n';
    return fail("preview rendering should match the linear reference path for 16-bit sources under strong edits");
  }

  QImage hdr_chroma_source(4, 1, QImage::Format_RGBA16FPx4);
  if (hdr_chroma_source.isNull()) {
    return fail("half-float chroma source should allocate");
  }
  hdr_chroma_source.setColorSpace(photo_editor::desktop::working_color_space());
  auto* hdr_chroma_scanline = reinterpret_cast<QRgbaFloat16*>(hdr_chroma_source.scanLine(0));
  hdr_chroma_scanline[0] = QRgbaFloat16{qfloat16(0.95F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};
  hdr_chroma_scanline[1] = QRgbaFloat16{qfloat16(1.05F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};
  hdr_chroma_scanline[2] = QRgbaFloat16{qfloat16(1.20F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};
  hdr_chroma_scanline[3] = QRgbaFloat16{qfloat16(1.35F), qfloat16(0.52F), qfloat16(0.24F), qfloat16(1.0F)};

  photo_editor::desktop::RenderEditState hdr_chroma_state;
  hdr_chroma_state.exposure = 1.0;
  hdr_chroma_state.contrast = 10.0;

  auto hdr_chroma_preview = hdr_chroma_source;
  photo_editor::desktop::apply_color_adjustments(&hdr_chroma_preview, hdr_chroma_state);
  if (hdr_chroma_preview.isNull()) {
    return fail("desktop render helper should render over-range chroma previews");
  }

  auto hdr_chroma_expected = photo_editor::desktop::working_image_from_qimage(hdr_chroma_source);
  if (hdr_chroma_expected.isNull()) {
    return fail("half-float chroma source should convert to a working image");
  }
  photo_editor::pipeline::ColorAdjustmentState hdr_chroma_pipeline_state;
  hdr_chroma_pipeline_state.exposure = 1.0;
  hdr_chroma_pipeline_state.contrast = 10.0;
  photo_editor::pipeline::apply_color_adjustments(
      &hdr_chroma_expected.pixels,
      hdr_chroma_expected.width,
      hdr_chroma_expected.height,
      hdr_chroma_pipeline_state);
  const auto hdr_chroma_expected_preview = photo_editor::desktop::qimage_from_working_image(
      hdr_chroma_expected,
      QColorSpace(QColorSpace::SRgb));
  if (hdr_chroma_expected_preview.isNull()) {
    return fail("desktop render helper reference image should be valid");
  }
  if (max_channel_delta(hdr_chroma_preview, hdr_chroma_expected_preview) > 3) {
    return fail("desktop render helper should match the direct path for over-range chroma highlights");
  }

  const auto smart_select_path = runtime_root.filePath(QStringLiteral("smart-select-source.png"));
  QImage smart_select_source(96, 96, QImage::Format_ARGB32);
  smart_select_source.fill(Qt::white);
  for (int y = 38; y <= 57; ++y) {
    for (int x = 10; x <= 25; ++x) {
      smart_select_source.setPixelColor(x, y, QColor(214, 78, 78));
    }
    for (int x = 40; x <= 55; ++x) {
      smart_select_source.setPixelColor(x, y, QColor(74, 118, 212));
    }
    for (int x = 70; x <= 85; ++x) {
      smart_select_source.setPixelColor(x, y, QColor(78, 176, 112));
    }
  }
  if (!smart_select_source.save(smart_select_path)) {
    return fail("smart select source image should be writable");
  }

  const auto tiny_object_path = runtime_root.filePath(QStringLiteral("tiny-object-source.png"));
  QImage tiny_object_source(96, 96, QImage::Format_ARGB32);
  tiny_object_source.fill(Qt::white);
  for (int y = 46; y <= 51; ++y) {
    for (int x = 14; x <= 19; ++x) {
      tiny_object_source.setPixelColor(x, y, QColor(214, 78, 78));
    }
    for (int x = 45; x <= 50; ++x) {
      tiny_object_source.setPixelColor(x, y, QColor(74, 118, 212));
    }
    for (int x = 76; x <= 81; ++x) {
      tiny_object_source.setPixelColor(x, y, QColor(78, 176, 112));
    }
  }
  if (!tiny_object_source.save(tiny_object_path)) {
    return fail("tiny object source image should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-smart"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto smart_select_point = [](double x, double y) {
    QVariantMap point;
    point.insert(QStringLiteral("x"), x);
    point.insert(QStringLiteral("y"), y);
    return point;
  };
  const auto wait_for_smart_preview = [](photo_editor::desktop::EditorPreviewController& preview_controller,
                                         int timeout_ms = 500) {
    auto elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
      if (preview_controller.smartSelectPreviewActive()) {
        return true;
      }
      QThread::msleep(25);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      elapsed_ms += 25;
    }
    return preview_controller.smartSelectPreviewActive();
  };
  controller.updateSmartSelectPreview(0.18, 0.5);
  if (!controller.smartSelectPreviewActive() ||
      controller.smartSelectPreviewSource().isEmpty() ||
      controller.smartSelectPreviewOutlinePoints().size() < 4) {
    return fail("smart select should expose a realtime hover preview before committing the selection");
  }
  const auto smart_preview_overlay = controller.smartSelectPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("white-on-black"));
  if (controller.maskPreviewMode() != QStringLiteral("white-on-black") ||
      controller.smartSelectPreviewSource().isEmpty() ||
      controller.smartSelectPreviewSource() == smart_preview_overlay) {
    return fail("smart select preview should follow the selected mask preview mode");
  }
  controller.updateSmartSelectPreview(4.0 / 7.0, 1.0 / 7.0);
  const auto first_candidate_preview = controller.smartSelectPreviewSource();
  if (!controller.smartSelectCanCycleCandidates() ||
      controller.smartSelectCandidateCount() < 2 ||
      controller.smartSelectCandidateIndex() != 0 ||
      first_candidate_preview.isEmpty()) {
    return fail("smart select should expose multiple candidate objects near ambiguous clicks");
  }
  controller.cycleSmartSelectCandidate(1);
  if (controller.smartSelectCandidateIndex() != 1 ||
      controller.smartSelectPreviewSource().isEmpty() ||
      controller.smartSelectPreviewSource() == first_candidate_preview) {
    return fail("smart select candidate cycling should update the active preview candidate");
  }
  controller.cycleSmartSelectCandidate(-1);
  if (controller.smartSelectCandidateIndex() != 0) {
    return fail("smart select candidate cycling should wrap backward to the previous candidate");
  }
  controller.smartSelectObjectAt(0.18, 0.5, true);
  if (controller.maskCount() != 1 ||
      controller.activeMaskKind() != QStringLiteral("object") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      controller.activeMaskPreviewSource().isEmpty() ||
      !controller.activeMaskCommitted()) {
    return fail("smart select should create an object selection mask");
  }
  const auto object_preview_white_on_black = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("overlay"));
  if (controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskPreviewSource() == object_preview_white_on_black) {
    return fail("active mask preview should re-render when the preview mode changes");
  }
  controller.setMaskPreviewMode(QStringLiteral("marching ants"));
  if (controller.maskPreviewMode() != QStringLiteral("marching-ants") ||
      controller.activeMaskOutlinePaths().isEmpty()) {
    return fail("mask preview should support marching ants outline mode");
  }
  controller.setMaskPreviewMode(QStringLiteral("edge-view"));
  const auto object_preview_edge = controller.activeMaskPreviewSource();
  controller.setMaskPreviewMode(QStringLiteral("affected-pixels"));
  if (controller.maskPreviewMode() != QStringLiteral("affected") ||
      object_preview_edge.isEmpty() ||
      controller.activeMaskPreviewSource().isEmpty() ||
      controller.activeMaskPreviewSource() == object_preview_edge) {
    return fail("mask preview should support edge and affected-pixel modes");
  }
  auto smart_mask = controller.maskStack().at(0).toMap();
  if (smart_mask.value(QStringLiteral("name")).toString() != QStringLiteral("Smart Selection 1")) {
    return fail("smart select should label newly created smart selections");
  }
  if (controller.maskSetCount() != 1 ||
      controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList().size() != 1) {
    return fail("selection masks should surface as grouped mask sets");
  }
  const auto first_mask_set = controller.maskSets().at(0).toMap();
  const auto first_mask_set_thumbnail =
      first_mask_set.value(QStringLiteral("thumbnailSource")).toString();
  const auto first_mask_source_thumbnail =
      first_mask_set.value(QStringLiteral("sources")).toList().at(0).toMap()
          .value(QStringLiteral("thumbnailSource"))
          .toString();
  if (!first_mask_set_thumbnail.startsWith(QStringLiteral("data:image/png;base64,")) ||
      !first_mask_source_thumbnail.startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("mask sets and sources should expose real matte thumbnails");
  }
  auto merged = smart_mask.value(QStringLiteral("segmentationData")).toByteArray();
  auto merged_width = smart_mask.value(QStringLiteral("segmentationWidth")).toInt();
  auto merged_height = smart_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(merged, merged_width, merged_height, 0.18, 0.5) < 128 ||
      mask_value_at(merged, merged_width, merged_height, 0.48, 0.5) > 24 ||
      mask_value_at(merged, merged_width, merged_height, 0.02, 0.02) > 24) {
    return fail("smart select should create a localized mask around the clicked region");
  }

  controller.smartSelectObjectAt(0.48, 0.5, false);
  if (controller.maskCount() != 1) {
    return fail("smart select should expand the active object mask instead of adding a new one");
  }
  smart_mask = controller.maskStack().at(0).toMap();
  merged = smart_mask.value(QStringLiteral("segmentationData")).toByteArray();
  merged_width = smart_mask.value(QStringLiteral("segmentationWidth")).toInt();
  merged_height = smart_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(merged, merged_width, merged_height, 0.18, 0.5) < 128 ||
      mask_value_at(merged, merged_width, merged_height, 0.48, 0.5) < 128 ||
      mask_value_at(merged, merged_width, merged_height, 0.80, 0.5) > 24) {
    return fail("smart select should union repeated object picks into one selection");
  }
  if (controller.maskSets().at(0).toMap().value(QStringLiteral("thumbnailSource")).toString() ==
      first_mask_set_thumbnail) {
    return fail("mask set thumbnails should reflect matte changes after mask edits");
  }
  if (controller.activeMaskOutlinePoints().size() < 4) {
    return fail("smart select should expose a traced outline for the active object selection");
  }

  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  photo_editor::desktop::EditorPreviewController scheduled_preview_controller;
  scheduled_preview_controller.loadAsset(
      QStringLiteral("asset-scheduled-preview"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  scheduled_preview_controller.scheduleSmartSelectPreview(0.18, 0.5);
  if (!wait_for_smart_preview(scheduled_preview_controller) ||
      scheduled_preview_controller.smartSelectPreviewSource().isEmpty() ||
      scheduled_preview_controller.smartSelectPreviewOutlinePoints().size() < 4) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("scheduled smart select preview should resolve through the debounced fallback path");
  }

  photo_editor::desktop::EditorPreviewController fallback_tiny_object_controller;
  fallback_tiny_object_controller.loadAsset(
      QStringLiteral("asset-fallback-tiny-object"),
      tiny_object_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  fallback_tiny_object_controller.smartSelectObjectAt(0.18, 0.5, true);
  if (fallback_tiny_object_controller.maskCount() != 1 ||
      fallback_tiny_object_controller.activeMaskKind() != QStringLiteral("object")) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("single-click object picks should still work without the instance-map path");
  }
  const auto fallback_tiny_object_mask =
      fallback_tiny_object_controller.maskStack().at(0).toMap();
  const auto fallback_tiny_object_data =
      fallback_tiny_object_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto fallback_tiny_object_width =
      fallback_tiny_object_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto fallback_tiny_object_height =
      fallback_tiny_object_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto fallback_tiny_center =
      mask_value_at(
          fallback_tiny_object_data,
          fallback_tiny_object_width,
          fallback_tiny_object_height,
          0.18,
          0.5);
  const auto fallback_tiny_nearby =
      mask_value_at(
          fallback_tiny_object_data,
          fallback_tiny_object_width,
          fallback_tiny_object_height,
          0.28,
          0.5);
  const auto fallback_tiny_other =
      mask_value_at(
          fallback_tiny_object_data,
          fallback_tiny_object_width,
          fallback_tiny_object_height,
          0.48,
          0.5);
  if (fallback_tiny_center < 128 || fallback_tiny_nearby > 64 || fallback_tiny_other > 24) {
    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
    return fail("single-click object fallback should prefer the tighter localized object mask");
  }
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");

  photo_editor::desktop::EditorPreviewController stroke_controller;
  stroke_controller.loadAsset(
      QStringLiteral("asset-stroke"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const QVariantList stroke_points{
      smart_select_point(0.18, 0.5),
      smart_select_point(0.48, 0.5),
  };
  stroke_controller.updateSmartSelectPreviewPath(stroke_points);
  if (!stroke_controller.smartSelectPreviewActive() ||
      stroke_controller.smartSelectPreviewOutlinePoints().size() < 4) {
    return fail("smart select strokes should preview the combined selection footprint before commit");
  }
  const auto stroke_pick =
      stroke_controller.pickMaskPath(stroke_points, QStringLiteral("selection"), true);
  if (stroke_pick != QStringLiteral("selection-ai") ||
      stroke_controller.maskCount() != 1 ||
      stroke_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("smart select strokes should create one object source from a dragged quick-selection pass");
  }
  const auto stroke_mask = stroke_controller.maskStack().at(0).toMap();
  const auto stroke_data = stroke_mask.value(
      QStringLiteral("segmentationData")).toByteArray();
  const auto stroke_width = stroke_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto stroke_height = stroke_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(stroke_data, stroke_width, stroke_height, 0.18, 0.5) < 128 ||
      mask_value_at(stroke_data, stroke_width, stroke_height, 0.48, 0.5) < 128 ||
      mask_value_at(stroke_data, stroke_width, stroke_height, 0.80, 0.5) > 24) {
    return fail("smart select strokes should union every object candidate touched by the drag path");
  }

  photo_editor::desktop::EditorPreviewController sparse_stroke_controller;
  sparse_stroke_controller.loadAsset(
      QStringLiteral("asset-sparse-stroke"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const QVariantList sparse_stroke_points{
      smart_select_point(0.18, 0.5),
      smart_select_point(0.80, 0.5),
  };
  const auto sparse_stroke_pick =
      sparse_stroke_controller.pickMaskPath(sparse_stroke_points, QStringLiteral("selection"), true);
  if (sparse_stroke_pick != QStringLiteral("selection-ai") ||
      sparse_stroke_controller.maskCount() != 1 ||
      sparse_stroke_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("smart select drags should still create one object source from sparse drag samples");
  }
  const auto sparse_stroke_mask = sparse_stroke_controller.maskStack().at(0).toMap();
  const auto sparse_stroke_data =
      sparse_stroke_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto sparse_stroke_width =
      sparse_stroke_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto sparse_stroke_height =
      sparse_stroke_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(sparse_stroke_data, sparse_stroke_width, sparse_stroke_height, 0.18, 0.5) < 128 ||
      mask_value_at(sparse_stroke_data, sparse_stroke_width, sparse_stroke_height, 0.48, 0.5) < 128 ||
      mask_value_at(sparse_stroke_data, sparse_stroke_width, sparse_stroke_height, 0.80, 0.5) < 128) {
    return fail("smart select drags should sample along the full path so intermediate objects are not skipped");
  }

  controller.smartSelectObjectAt(0.80, 0.5, true);
  if (controller.maskCount() != 2 ||
      controller.activeMaskIndex() != 1) {
    return fail("starting a new smart selection should create a separate object mask");
  }
  controller.clearSmartSelectPreview();
  if (controller.smartSelectPreviewActive()) {
    return fail("smart select preview should clear when the hover state is reset");
  }

  photo_editor::desktop::EditorPreviewController subtract_controller;
  subtract_controller.loadAsset(
      QStringLiteral("asset-subtract"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  subtract_controller.smartSelectAddAt(0.18, 0.5, true);
  subtract_controller.smartSelectSubtractAt(0.48, 0.5, false);
  if (subtract_controller.maskCount() != 2 ||
      subtract_controller.activeMaskOperation() != QStringLiteral("subtract") ||
      subtract_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      subtract_controller.maskSetCount() != 1 ||
      subtract_controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList().size() != 2) {
    return fail("smart select should support positive and negative object clicks inside the current mask set");
  }
  const auto subtract_mask = subtract_controller.maskStack().at(1).toMap();
  if (mask_value_at(
          subtract_mask.value(QStringLiteral("segmentationData")).toByteArray(),
          subtract_mask.value(QStringLiteral("segmentationWidth")).toInt(),
          subtract_mask.value(QStringLiteral("segmentationHeight")).toInt(),
          0.48,
          0.5) < 128) {
    return fail("subtract smart select should rasterize the subtracted region");
  }
  subtract_controller.setActiveMaskIndex(0);
  const auto subtract_set_preview_a = subtract_controller.activeMaskPreviewSource();
  subtract_controller.setActiveMaskIndex(1);
  const auto subtract_set_preview_b = subtract_controller.activeMaskPreviewSource();
  if (subtract_set_preview_a.isEmpty() ||
      subtract_set_preview_b.isEmpty() ||
      subtract_set_preview_a != subtract_set_preview_b) {
    return fail("active mask preview should show the full set result, not the selected source only");
  }

  photo_editor::desktop::EditorPreviewController tiny_object_controller;
  tiny_object_controller.loadAsset(
      QStringLiteral("asset-tiny-object"),
      tiny_object_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  tiny_object_controller.smartSelectObjectAt(0.18, 0.5, true);
  if (tiny_object_controller.maskCount() != 1 ||
      tiny_object_controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("single-click object picks should still create an object selection for tiny targets");
  }
  const auto tiny_object_mask = tiny_object_controller.maskStack().at(0).toMap();
  const auto tiny_object_data =
      tiny_object_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto tiny_object_width =
      tiny_object_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto tiny_object_height =
      tiny_object_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto tiny_center =
      mask_value_at(tiny_object_data, tiny_object_width, tiny_object_height, 0.18, 0.5);
  const auto tiny_nearby =
      mask_value_at(tiny_object_data, tiny_object_width, tiny_object_height, 0.28, 0.5);
  const auto tiny_other =
      mask_value_at(tiny_object_data, tiny_object_width, tiny_object_height, 0.48, 0.5);
  if (tiny_center < 128 || tiny_nearby > 64 || tiny_other > 24) {
    return fail("single-click object picks should prefer the tighter prompt-refined mask for tiny objects");
  }

  photo_editor::desktop::EditorPreviewController range_controller;
  range_controller.loadAsset(
      QStringLiteral("asset-range"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  range_controller.addMask(QStringLiteral("luminance"));
  range_controller.setActiveMaskLuminanceMin(0.1);
  range_controller.setActiveMaskLuminanceMax(0.85);
  if (range_controller.activeMaskKind() != QStringLiteral("luminance") ||
      range_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("luminance range masks should expose a raster preview matte");
  }
  range_controller.addMask(QStringLiteral("color"));
  range_controller.setActiveMaskHueCenter(0.0);
  range_controller.setActiveMaskHueRange(0.12);
  range_controller.setActiveMaskSaturationMin(0.2);
  range_controller.setActiveMaskSaturationMax(1.0);
  if (range_controller.activeMaskKind() != QStringLiteral("color") ||
      range_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("color range masks should expose a raster preview matte");
  }
  const auto range_overlay_preview = range_controller.activeMaskPreviewSource();
  range_controller.setMaskPreviewMode(QStringLiteral("black-on-white"));
  if (range_controller.maskPreviewMode() != QStringLiteral("black-on-white") ||
      range_controller.activeMaskPreviewSource().isEmpty() ||
      range_controller.activeMaskPreviewSource() == range_overlay_preview) {
    return fail("range mask previews should support the dedicated neutral preview modes");
  }

  photo_editor::desktop::EditorPreviewController missed_pick_controller;
  missed_pick_controller.loadAsset(
      QStringLiteral("asset-missed-pick"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto missed_selection =
      missed_pick_controller.pickMaskAt(0.02, 0.02, QStringLiteral("selection"), false);
  if (missed_selection != QStringLiteral("selection-missed") ||
      missed_pick_controller.maskCount() != 0 ||
      missed_pick_controller.hasActiveMask()) {
    return fail("missed smart selection should not create a polygon fallback mask");
  }

  const auto missed_remove =
      missed_pick_controller.pickMaskAt(0.02, 0.02, QStringLiteral("remove"), false);
  if (missed_remove != QStringLiteral("remove-missed") ||
      missed_pick_controller.maskCount() != 0 ||
      missed_pick_controller.hasActiveMask()) {
    return fail("missed object remove should not create an implicit remove brush fallback");
  }

  photo_editor::desktop::EditorPreviewController remove_controller;
  remove_controller.loadAsset(
      QStringLiteral("asset-remove"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  remove_controller.objectRemoveAt(0.18, 0.5, true);
  if (remove_controller.maskCount() != 1 ||
      remove_controller.activeMaskKind() != QStringLiteral("object") ||
      remove_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      remove_controller.activeMaskRetouchMode() != QStringLiteral("remove") ||
      remove_controller.activeMaskCommitted()) {
    return fail("object remove should create a staged retouch object mask");
  }
  remove_controller.startRemoveBrush();
  if (remove_controller.activeMaskKind() != QStringLiteral("brush") ||
      remove_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      remove_controller.activeMaskRetouchMode() != QStringLiteral("remove") ||
      remove_controller.activeMaskCommitted()) {
    return fail("remove brush should join the staged retouch workflow");
  }
  remove_controller.beginActiveBrushStroke();
  remove_controller.addBrushPoint(0.22, 0.52);
  remove_controller.endActiveBrushStroke();
  if (remove_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("remove brush should show a live highlighted matte before the retouch is applied");
  }
  qputenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS", "1");
  remove_controller.applyActiveRetouch();
  if (!remove_controller.maskStack().at(0).toMap().value(QStringLiteral("committed")).toBool() ||
      !remove_controller.maskStack().at(1).toMap().value(QStringLiteral("committed")).toBool()) {
    return fail("applying a retouch should commit every pending retouch mask in the session");
  }
  if (remove_controller.maskStack().at(0).toMap().value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      remove_controller.maskStack().at(0).toMap().value(QStringLiteral("inpaintPatchWidth")).toInt() <= 0 ||
      remove_controller.maskStack().at(1).toMap().value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty()) {
    return fail("applying a remove retouch with AI inpaint available should bake patch data into committed masks");
  }
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");

  QImage inpaint_texture_source(96, 96, QImage::Format_ARGB32);
  for (int y = 0; y < inpaint_texture_source.height(); ++y) {
    for (int x = 0; x < inpaint_texture_source.width(); ++x) {
      const auto grain = ((x * 17 + y * 29) % 11) - 5;
      inpaint_texture_source.setPixelColor(
          x,
          y,
          QColor::fromRgb(
              std::clamp(110 + x + grain, 0, 255),
              std::clamp(116 + y + grain, 0, 255),
              std::clamp(128 + grain, 0, 255)));
    }
  }
  const auto inpaint_texture_path = runtime_root.filePath(QStringLiteral("cpu-inpaint-texture.png"));
  if (!inpaint_texture_source.save(inpaint_texture_path)) {
    return fail("CPU inpaint texture test image should be writable");
  }

  photo_editor::desktop::EditorPreviewController fallback_inpaint_controller;
  fallback_inpaint_controller.loadAsset(
      QStringLiteral("asset-cpu-inpaint"),
      inpaint_texture_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  fallback_inpaint_controller.setInpaintPrompt(QStringLiteral("preserve fine grain texture"));
  fallback_inpaint_controller.setInpaintSeed(42);
  fallback_inpaint_controller.regenerateInpaintSeed();
  if (fallback_inpaint_controller.inpaintSeed() != 43) {
    return fail("regenerating inpaint should advance the seed control");
  }
  fallback_inpaint_controller.setInpaintBlendAmount(0.42);
  fallback_inpaint_controller.setInpaintQualityMode(QStringLiteral("final"));
  fallback_inpaint_controller.startRemoveBrush();
  fallback_inpaint_controller.beginActiveBrushStroke();
  fallback_inpaint_controller.addBrushPoint(0.48, 0.5);
  fallback_inpaint_controller.endActiveBrushStroke();
  fallback_inpaint_controller.applyActiveRetouch();
  const auto fallback_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  if (fallback_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() ||
      !fallback_mask.value(QStringLiteral("inpaintFallback")).toBool() ||
      fallback_mask.value(QStringLiteral("inpaintModelName")).toString() !=
          QStringLiteral("cpu-diffusion-inpaint-v1") ||
      fallback_mask.value(QStringLiteral("inpaintPrompt")).toString() !=
          QStringLiteral("preserve fine grain texture") ||
      fallback_mask.value(QStringLiteral("inpaintSeed")).toInt() != 43 ||
      std::abs(fallback_mask.value(QStringLiteral("inpaintBlendAmount")).toDouble() - 0.42) > 1e-6 ||
      fallback_mask.value(QStringLiteral("inpaintQualityMode")).toString() != QStringLiteral("final") ||
      fallback_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray().isEmpty() ||
      fallback_mask.value(QStringLiteral("inpaintSourceMaskWidth")).toInt() !=
          inpaint_texture_source.width() ||
      fallback_mask.value(QStringLiteral("inpaintSourceMaskHeight")).toInt() !=
          inpaint_texture_source.height() ||
      fallback_mask.value(QStringLiteral("inpaintCandidateCount")).toInt() < 3 ||
      fallback_mask.value(QStringLiteral("inpaintCandidates")).toList().size() < 3 ||
      fallback_mask.value(QStringLiteral("inpaintRegionKind")).toString().isEmpty() ||
      !fallback_mask.value(QStringLiteral("inpaintEdgeContinuation")).toBool() ||
      !fallback_mask.value(QStringLiteral("inpaintTextureSynthesis")).toBool() ||
      fallback_mask.value(QStringLiteral("inpaintTextureStrength")).toDouble() <= 0.0 ||
      fallback_mask.value(QStringLiteral("inpaintRegionCoverage")).toDouble() <= 0.0 ||
      fallback_mask.value(QStringLiteral("inpaintRegionWidth")).toInt() <= 0 ||
      fallback_mask.value(QStringLiteral("inpaintRegionHeight")).toInt() <= 0) {
    std::cerr << "fallback inpaint patch bytes="
              << fallback_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().size()
              << " fallback=" << fallback_mask.value(QStringLiteral("inpaintFallback")).toBool()
              << " model="
              << fallback_mask.value(QStringLiteral("inpaintModelName")).toString().toStdString()
              << " prompt="
              << fallback_mask.value(QStringLiteral("inpaintPrompt")).toString().toStdString()
              << " seed=" << fallback_mask.value(QStringLiteral("inpaintSeed")).toInt()
              << " blend=" << fallback_mask.value(QStringLiteral("inpaintBlendAmount")).toDouble()
              << " quality="
              << fallback_mask.value(QStringLiteral("inpaintQualityMode")).toString().toStdString()
              << " sourceMask="
              << fallback_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray().size()
              << " candidates=" << fallback_mask.value(QStringLiteral("inpaintCandidateCount")).toInt()
              << " region="
              << fallback_mask.value(QStringLiteral("inpaintRegionKind")).toString().toStdString()
              << " edge=" << fallback_mask.value(QStringLiteral("inpaintEdgeContinuation")).toBool()
              << " texture=" << fallback_mask.value(QStringLiteral("inpaintTextureSynthesis")).toBool()
              << " textureStrength="
              << fallback_mask.value(QStringLiteral("inpaintTextureStrength")).toDouble()
              << " coverage="
              << fallback_mask.value(QStringLiteral("inpaintRegionCoverage")).toDouble()
              << " status=" << fallback_inpaint_controller.status().toStdString()
              << "\n";
    return fail("remove retouch should bake a deterministic CPU inpaint patch when the AI model is unavailable");
  }
  if (fallback_inpaint_controller.retouchBeforeAfterEnabled()) {
    return fail("retouch before/after should default to the after view");
  }
  fallback_inpaint_controller.setRetouchBeforeAfterEnabled(true);
  if (!fallback_inpaint_controller.retouchBeforeAfterEnabled()) {
    return fail("retouch before/after toggle should enter the before view");
  }
  const auto before_request_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          fallback_inpaint_controller);
  if (before_request_state.mask_stack.isEmpty() ||
      before_request_state.mask_stack.at(0)
          .toMap()
          .value(QStringLiteral("enabled"), true)
          .toBool()) {
    return fail("retouch before view should bypass retouch and inpaint masks in preview render state");
  }
  if (!fallback_inpaint_controller.maskStack().at(0).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("retouch before view should not mutate the committed retouch mask");
  }
  fallback_inpaint_controller.setRetouchBeforeAfterEnabled(false);
  const auto after_request_state =
      photo_editor::desktop::EditorPreviewControllerTestAccess::interactive_render_state(
          fallback_inpaint_controller);
  if (after_request_state.mask_stack.isEmpty() ||
      !after_request_state.mask_stack.at(0).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("retouch after view should restore retouch masks in preview render state");
  }
  const auto patch_bounds = fallback_inpaint_controller.activeInpaintPatchBounds();
  if (!patch_bounds.isValid() || patch_bounds.width() <= 0.0 || patch_bounds.height() <= 0.0 ||
      patch_bounds.x() < 0.0 || patch_bounds.y() < 0.0 ||
      patch_bounds.right() > 1.0 || patch_bounds.bottom() > 1.0) {
    return fail("inpaint patch diagnostics should expose normalized active patch bounds");
  }
  if (fallback_inpaint_controller.inpaintPatchDiagnosticsEnabled()) {
    return fail("inpaint patch diagnostics should default off");
  }
  fallback_inpaint_controller.setInpaintPatchDiagnosticsEnabled(true);
  if (!fallback_inpaint_controller.inpaintPatchDiagnosticsEnabled()) {
    return fail("inpaint patch diagnostics toggle should enable visible patch bounds");
  }
  const auto committed_inpaint_patch =
      fallback_inpaint_controller.maskStack().at(0).toMap().value(QStringLiteral("inpaintPatchData")).toByteArray();
  fallback_inpaint_controller.undo();
  if (!fallback_inpaint_controller.canRedo() ||
      fallback_inpaint_controller.maskStack().isEmpty() ||
      fallback_inpaint_controller.maskStack().at(0).toMap().value(QStringLiteral("committed")).toBool() ||
      !fallback_inpaint_controller.maskStack()
           .at(0)
           .toMap()
           .value(QStringLiteral("inpaintPatchData"))
           .toByteArray()
           .isEmpty()) {
    return fail("undo should restore a pending retouch mask before the baked inpaint operation");
  }
  fallback_inpaint_controller.redo();
  const auto redone_inpaint_mask = fallback_inpaint_controller.maskStack().at(0).toMap();
  if (!redone_inpaint_mask.value(QStringLiteral("committed")).toBool() ||
      redone_inpaint_mask.value(QStringLiteral("inpaintPatchData")).toByteArray() !=
          committed_inpaint_patch ||
      redone_inpaint_mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray().isEmpty() ||
      redone_inpaint_mask.value(QStringLiteral("inpaintCandidates")).toList().size() < 3 ||
      redone_inpaint_mask.value(QStringLiteral("inpaintQualityMode")).toString() !=
          QStringLiteral("final")) {
    return fail("redo should restore the full baked inpaint operation payload");
  }

  const auto committed_count = remove_controller.maskCount();
  remove_controller.objectRemoveAt(0.85, 0.5, true);
  if (remove_controller.maskCount() != committed_count + 1 ||
      remove_controller.activeMaskCommitted()) {
    return fail("starting another object remove should stage a fresh pending retouch");
  }
  remove_controller.cancelActiveRetouch();
  if (remove_controller.maskCount() != committed_count) {
    return fail("cancel should drop pending retouch masks without removing committed edits");
  }

  QImage dust_source(96, 96, QImage::Format_ARGB32);
  dust_source.fill(QColor::fromRgb(180, 184, 188));
  const auto paint_dust = [&](int center_x, int center_y) {
    for (int y = center_y - 1; y <= center_y + 1; ++y) {
      for (int x = center_x - 1; x <= center_x + 1; ++x) {
        dust_source.setPixelColor(x, y, QColor::fromRgb(82, 84, 86));
      }
    }
  };
  paint_dust(30, 32);
  paint_dust(70, 62);
  const auto dust_path = runtime_root.filePath(QStringLiteral("dust-source.png"));
  if (!dust_source.save(dust_path)) {
    return fail("dust removal test image should be writable");
  }
  photo_editor::desktop::EditorPreviewController dust_controller;
  const auto dust_camera = QStringLiteral("DustCam 1");
  const auto dust_lens = QStringLiteral("Dust 50mm");
  dust_controller.loadAsset(
      QStringLiteral("dust-removal"),
      dust_path,
      QString(),
      dust_camera,
      QString(),
      dust_lens,
      QString());
  dust_controller.removeDustSpots();
  if (dust_controller.maskCount() != 1 ||
      dust_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      dust_controller.activeMaskRetouchMode() != QStringLiteral("dust") ||
      dust_controller.activeMaskCommitted() ||
      !almost_equal(dust_controller.activeMaskRemoveStrength(), 74.0)) {
    return fail("dust removal should create a staged retouch mask for review");
  }
  if (!dust_controller.status().contains(QStringLiteral("staged")) ||
      dust_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("dust removal should communicate review state and expose a preview matte before commit");
  }
  const auto dust_mask = dust_controller.maskStack().at(0).toMap();
  const auto dust_data = dust_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto dust_width = dust_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto dust_height = dust_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto first_dust_value = mask_value_at(dust_data, dust_width, dust_height, 30.0 / 95.0, 32.0 / 95.0);
  const auto second_dust_value = mask_value_at(dust_data, dust_width, dust_height, 70.0 / 95.0, 62.0 / 95.0);
  const auto background_dust_value = mask_value_at(dust_data, dust_width, dust_height, 0.10, 0.10);
  if (first_dust_value < 128 || second_dust_value < 128 || background_dust_value > 24) {
    auto max_dust_value = 0;
    auto max_dust_x = 0;
    auto max_dust_y = 0;
    for (int y = 0; y < dust_height; ++y) {
      for (int x = 0; x < dust_width; ++x) {
        const auto value = static_cast<int>(static_cast<unsigned char>(dust_data.at(y * dust_width + x)));
        if (value > max_dust_value) {
          max_dust_value = value;
          max_dust_x = x;
          max_dust_y = y;
        }
      }
    }
    std::cerr << "dust mask values first=" << static_cast<int>(first_dust_value)
              << " second=" << static_cast<int>(second_dust_value)
              << " background=" << static_cast<int>(background_dust_value)
              << " max=" << max_dust_value << " at " << max_dust_x << "," << max_dust_y << '\n';
    return fail("dust removal should target small dark spots without filling smooth background");
  }
  dust_controller.saveActiveDustMap();
  if (!dust_controller.hasSensorDustMap()) {
    return fail("saving dust removal should create a reusable sensor dust map for the camera and lens");
  }
  dust_controller.applyActiveRetouch();
  if (!dust_controller.activeMaskCommitted()) {
    return fail("dust removal should commit only after the review apply action");
  }
  photo_editor::desktop::EditorPreviewController dust_cancel_controller;
  dust_cancel_controller.loadAsset(
      QStringLiteral("dust-removal-cancel"),
      dust_path,
      QString(),
      dust_camera,
      QString(),
      dust_lens,
      QString());
  dust_cancel_controller.removeDustSpots();
  dust_cancel_controller.cancelActiveRetouch();
  if (dust_cancel_controller.maskCount() != 0 || dust_cancel_controller.hasActiveMask()) {
    return fail("dust removal review should discard the staged retouch before commit");
  }

  QImage clean_sensor_source(96, 96, QImage::Format_ARGB32);
  clean_sensor_source.fill(QColor::fromRgb(180, 184, 188));
  const auto clean_sensor_path = runtime_root.filePath(QStringLiteral("sensor-dust-clean-source.png"));
  if (!clean_sensor_source.save(clean_sensor_path)) {
    return fail("sensor dust map target image should be writable");
  }
  photo_editor::desktop::EditorPreviewController sensor_map_controller;
  sensor_map_controller.loadAsset(
      QStringLiteral("sensor-dust-map"),
      clean_sensor_path,
      QString(),
      dust_camera,
      QString(),
      dust_lens,
      QString());
  if (!sensor_map_controller.hasSensorDustMap()) {
    return fail("sensor dust maps should be discovered for matching camera and lens metadata");
  }
  sensor_map_controller.applySensorDustMap();
  if (sensor_map_controller.maskCount() != 1 ||
      sensor_map_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      sensor_map_controller.activeMaskRetouchMode() != QStringLiteral("dust") ||
      sensor_map_controller.activeMaskCommitted()) {
    return fail("applying a sensor dust map should stage a dust retouch mask for review");
  }
  const auto sensor_map_mask = sensor_map_controller.maskStack().at(0).toMap();
  const auto sensor_map_data = sensor_map_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto sensor_map_width = sensor_map_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto sensor_map_height = sensor_map_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(sensor_map_data, sensor_map_width, sensor_map_height, 30.0 / 95.0, 32.0 / 95.0) < 128 ||
      mask_value_at(sensor_map_data, sensor_map_width, sensor_map_height, 70.0 / 95.0, 62.0 / 95.0) < 128 ||
      mask_value_at(sensor_map_data, sensor_map_width, sensor_map_height, 0.10, 0.10) > 24) {
    return fail("sensor dust maps should reapply the saved matte at the same sensor coordinates");
  }
  sensor_map_controller.applyActiveRetouch();
  if (!sensor_map_controller.activeMaskCommitted()) {
    return fail("sensor dust map review should commit after apply");
  }

  QImage red_eye_source(96, 96, QImage::Format_ARGB32);
  red_eye_source.fill(QColor::fromRgb(74, 64, 58));
  const auto paint_red_eye = [&](int center_x, int center_y) {
    for (int y = center_y - 1; y <= center_y + 1; ++y) {
      for (int x = center_x - 1; x <= center_x + 1; ++x) {
        red_eye_source.setPixelColor(x, y, QColor::fromRgb(235, 24, 20));
      }
    }
  };
  paint_red_eye(34, 42);
  paint_red_eye(62, 42);
  const auto red_eye_path = runtime_root.filePath(QStringLiteral("red-eye-source.png"));
  if (!red_eye_source.save(red_eye_path)) {
    return fail("red-eye removal test image should be writable");
  }
  photo_editor::desktop::EditorPreviewController red_eye_controller;
  red_eye_controller.loadAsset(
      QStringLiteral("red-eye-removal"),
      red_eye_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  red_eye_controller.removeRedEye();
  if (red_eye_controller.maskCount() != 1 ||
      red_eye_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      red_eye_controller.activeMaskRetouchMode() != QStringLiteral("red-eye") ||
      red_eye_controller.activeMaskCommitted()) {
    return fail("red-eye removal should create a staged retouch mask for review");
  }
  if (!red_eye_controller.status().contains(QStringLiteral("staged")) ||
      red_eye_controller.activeMaskPreviewSource().isEmpty()) {
    return fail("red-eye removal should communicate review state and expose a preview matte before commit");
  }
  const auto red_eye_mask = red_eye_controller.maskStack().at(0).toMap();
  const auto red_eye_data = red_eye_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto red_eye_width = red_eye_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto red_eye_height = red_eye_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(red_eye_data, red_eye_width, red_eye_height, 34.0 / 95.0, 42.0 / 95.0) < 128 ||
      mask_value_at(red_eye_data, red_eye_width, red_eye_height, 62.0 / 95.0, 42.0 / 95.0) < 128 ||
      mask_value_at(red_eye_data, red_eye_width, red_eye_height, 0.10, 0.10) > 24) {
    return fail("red-eye removal should target red pupil highlights without masking neutral skin");
  }
  red_eye_controller.applyActiveRetouch();
  if (!red_eye_controller.activeMaskCommitted()) {
    return fail("red-eye removal should commit only after the review apply action");
  }
  photo_editor::desktop::EditorPreviewController red_eye_cancel_controller;
  red_eye_cancel_controller.loadAsset(
      QStringLiteral("red-eye-removal-cancel"),
      red_eye_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  red_eye_cancel_controller.removeRedEye();
  red_eye_cancel_controller.cancelActiveRetouch();
  if (red_eye_cancel_controller.maskCount() != 0 || red_eye_cancel_controller.hasActiveMask()) {
    return fail("red-eye removal review should discard the staged retouch before commit");
  }

  QImage skin_source(96, 96, QImage::Format_ARGB32);
  skin_source.fill(QColor::fromRgb(64, 72, 82));
  for (int y = 20; y <= 72; ++y) {
    for (int x = 18; x <= 78; ++x) {
      skin_source.setPixelColor(x, y, QColor::fromRgb(210, 152, 124));
    }
  }
  skin_source.setPixelColor(48, 46, QColor::fromRgb(118, 72, 62));
  const auto skin_path = runtime_root.filePath(QStringLiteral("skin-cleanup-source.png"));
  if (!skin_source.save(skin_path)) {
    return fail("skin cleanup test image should be writable");
  }
  photo_editor::desktop::EditorPreviewController skin_controller;
  skin_controller.loadAsset(
      QStringLiteral("skin-cleanup"),
      skin_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  skin_controller.cleanupSkin();
  if (skin_controller.maskCount() != 1 ||
      skin_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      skin_controller.activeMaskRetouchMode() != QStringLiteral("skin-cleanup") ||
      !skin_controller.activeMaskCommitted() ||
      !almost_equal(skin_controller.activeMaskRemoveStrength(), 46.0)) {
    return fail("skin cleanup should create a committed skin retouch mask");
  }
  const auto skin_mask = skin_controller.maskStack().at(0).toMap();
  const auto skin_data = skin_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto skin_width = skin_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto skin_height = skin_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(skin_data, skin_width, skin_height, 48.0 / 95.0, 46.0 / 95.0) < 128 ||
      mask_value_at(skin_data, skin_width, skin_height, 0.08, 0.08) > 24) {
    return fail("skin cleanup should mask skin-toned regions without selecting neutral background");
  }

  photo_editor::desktop::EditorPreviewController frequency_controller;
  frequency_controller.loadAsset(
      QStringLiteral("frequency-smoothing"),
      skin_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  frequency_controller.smoothSkinFrequency();
  if (frequency_controller.maskCount() != 1 ||
      frequency_controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      frequency_controller.activeMaskRetouchMode() != QStringLiteral("frequency-smoothing") ||
      !frequency_controller.activeMaskCommitted() ||
      !almost_equal(frequency_controller.activeMaskRemoveStrength(), 58.0)) {
    return fail("frequency smoothing should create a committed skin smoothing retouch mask");
  }

  photo_editor::desktop::EditorPreviewController retouch_list_controller;
  retouch_list_controller.loadAsset(
      QStringLiteral("retouch-operation-list"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  retouch_list_controller.startRemoveBrush();
  retouch_list_controller.startHealBrush();
  if (retouch_list_controller.retouchCount() != 2) {
    return fail("retouch operation list should expose staged retouch operations");
  }
  const auto initial_retouch_list = retouch_list_controller.retouchStack();
  const auto staged_heal_row = initial_retouch_list.at(1).toMap();
  if (staged_heal_row.value(QStringLiteral("retouchModeLabel")).toString() != QStringLiteral("Heal") ||
      staged_heal_row.value(QStringLiteral("retouchStatus")).toString() != QStringLiteral("staged") ||
      staged_heal_row.value(QStringLiteral("retouchPointCount")).toInt() != 0 ||
      !staged_heal_row.value(QStringLiteral("retouchAuditLabel")).toString().contains(QStringLiteral("no strokes"))) {
    return fail("retouch operation list should expose audit labels, status, and stroke counts");
  }
  retouch_list_controller.setActiveMaskName(QStringLiteral("Blemish pass"));
  if (retouch_list_controller.activeMaskName() != QStringLiteral("Blemish pass")) {
    return fail("retouch operation list should support renaming the active operation");
  }
  retouch_list_controller.setActiveMaskEnabled(false);
  if (retouch_list_controller.maskStack().at(retouch_list_controller.activeMaskIndex()).toMap()
          .value(QStringLiteral("enabled")).toBool()) {
    return fail("retouch operation list should support disabling an operation");
  }
  retouch_list_controller.moveActiveMask(-1);
  if (retouch_list_controller.activeMaskIndex() != 0 ||
      retouch_list_controller.maskStack().at(0).toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Blemish pass")) {
    return fail("retouch operation list should support reordering operations");
  }
  retouch_list_controller.removeActiveMask();
  if (retouch_list_controller.retouchCount() != 1 ||
      retouch_list_controller.maskStack().at(0).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("remove")) {
    return fail("retouch operation list should support deleting operations");
  }

  controller.startSelectionBrush(false);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("add") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted()) {
    return fail("refine add should create an additive brush mask");
  }
  controller.beginActiveBrushStroke();
  if (!controller.brushStrokeActive()) {
    return fail("beginning a brush stroke should expose active stroke state for the live canvas overlay");
  }
  controller.addBrushPoint(0.34, 0.48);
  controller.addBrushPoint(0.88, 0.82);
  if (controller.activeMaskLiveBrushPoints().size() <= 2) {
    return fail("active brush strokes should expose interpolated live brush samples before commit");
  }
  controller.endActiveBrushStroke();
  if (controller.brushStrokeActive()) {
    return fail("ending a brush stroke should clear the active stroke state");
  }
  if (!controller.activeMaskLiveBrushPoints().isEmpty()) {
    return fail("ending a brush stroke should clear the transient live brush sample buffer");
  }
  if (controller.activeMaskBrushPointCount() <= 2) {
    return fail("selection brush strokes should interpolate samples along long drags");
  }
  if (controller.activeMaskPreviewSource().isEmpty()) {
    return fail("selection brush should build a live raster matte while painting");
  }
  const auto selection_brush_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto selection_brush_data =
      selection_brush_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto selection_brush_width =
      selection_brush_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto selection_brush_height =
      selection_brush_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(selection_brush_data, selection_brush_width, selection_brush_height, 0.34, 0.48) < 96 ||
      mask_value_at(selection_brush_data, selection_brush_width, selection_brush_height, 0.61, 0.65) < 96 ||
      mask_value_at(selection_brush_data, selection_brush_width, selection_brush_height, 0.08, 0.08) > 24) {
    return fail("selection brush should paint a continuous raster mask along the dragged path");
  }
  const auto grouped_mask_sets = controller.maskSets();
  if (grouped_mask_sets.size() != 2 ||
      grouped_mask_sets.at(1).toMap().value(QStringLiteral("sources")).toList().size() != 2) {
    return fail("refine brushes should join the active selection mask set instead of creating a new set");
  }
  const auto refine_set = grouped_mask_sets.at(1).toMap();
  const auto refine_sources = refine_set.value(QStringLiteral("sources")).toList();
  if (!refine_set.value(QStringLiteral("thumbnailSource")).toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      !refine_sources.at(0).toMap()
           .value(QStringLiteral("thumbnailSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,")) ||
      !refine_sources.at(1).toMap()
           .value(QStringLiteral("thumbnailSource"))
           .toString()
           .startsWith(QStringLiteral("data:image/png;base64,"))) {
    return fail("grouped refine masks should expose matte thumbnails for the set and each source");
  }

  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.18, 0.22);
  controller.endActiveBrushStroke();
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.82, 0.78);
  controller.endActiveBrushStroke();
  const auto separated_stroke_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  const auto separated_stroke_data =
      separated_stroke_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto separated_stroke_width =
      separated_stroke_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto separated_stroke_height =
      separated_stroke_mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (mask_value_at(separated_stroke_data, separated_stroke_width, separated_stroke_height, 0.5, 0.5) > 24) {
    return fail("separate brush strokes should not interpolate a bridge through the gap between strokes");
  }
  const auto brush_points_before_undo_probe = controller.activeMaskBrushPointCount();
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.48, 0.18);
  controller.endActiveBrushStroke();
  if (controller.activeMaskBrushPointCount() <= brush_points_before_undo_probe) {
    return fail("a completed brush stroke should add committed brush samples");
  }
  controller.undo();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskBrushPointCount() != brush_points_before_undo_probe) {
    return fail("undo should remove the last completed brush stroke without removing the active mask");
  }

  controller.startSelectionBrush(true);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("subtract") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted()) {
    return fail("refine subtract should create a subtractive brush mask");
  }
  if (controller.activeMaskOutlinePoints().isEmpty()) {
    return fail("switching to an erase brush should keep outlining the current selection set");
  }
  const auto grouped_mask_sets_after_subtract = controller.maskSets();
  if (grouped_mask_sets_after_subtract.size() != 2 ||
      grouped_mask_sets_after_subtract.at(1).toMap().value(QStringLiteral("sources")).toList().size() != 3) {
    return fail("grouped selection mask views should refresh after stack edits");
  }

  photo_editor::desktop::EditRecipeState dependency_state;
  QVariantMap reusable_mask;
  reusable_mask.insert(QStringLiteral("id"), QStringLiteral("linear-base"));
  reusable_mask.insert(QStringLiteral("name"), QStringLiteral("Reusable Base"));
  reusable_mask.insert(QStringLiteral("kind"), QStringLiteral("linear"));
  reusable_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("dependency-set"));
  reusable_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Dependency Set"));
  reusable_mask.insert(QStringLiteral("outputId"), QStringLiteral("base-alpha"));
  reusable_mask.insert(QStringLiteral("reusableOutput"), true);
  QVariantMap dependent_mask;
  dependent_mask.insert(QStringLiteral("id"), QStringLiteral("radial-child"));
  dependent_mask.insert(QStringLiteral("name"), QStringLiteral("Dependent Radial"));
  dependent_mask.insert(QStringLiteral("kind"), QStringLiteral("radial"));
  dependent_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("dependency-set"));
  dependent_mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Dependency Set"));
  dependent_mask.insert(QStringLiteral("parentId"), QStringLiteral("linear-base"));
  dependent_mask.insert(QStringLiteral("operation"), QStringLiteral("subtract"));
  dependency_state.mask_stack = {reusable_mask, dependent_mask};
  photo_editor::desktop::EditorPreviewController dependency_controller;
  dependency_controller.loadAsset(
      QStringLiteral("mask-dependency-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(dependency_state));
  const auto dependency_outputs = dependency_controller.reusableMaskOutputs();
  const auto dependency_sources =
      dependency_controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList();
  if (dependency_outputs.size() != 1 ||
      dependency_outputs.at(0).toMap().value(QStringLiteral("dependencyLabel")).toString() !=
          QStringLiteral("Reusable matte: base-alpha") ||
      !dependency_sources.at(0)
           .toMap()
           .value(QStringLiteral("dependencyLabel"))
           .toString()
           .contains(QStringLiteral("Reusable output: base-alpha")) ||
      dependency_sources.at(1).toMap().value(QStringLiteral("dependencyLabel")).toString() !=
          QStringLiteral("Depends on Reusable Base")) {
    return fail("mask sets should expose reusable outputs and source dependency labels");
  }

  controller.startSelectionEdgeBrush(false);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("add") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted() ||
      !almost_equal(controller.activeMaskBrushSize(), 0.026) ||
      !almost_equal(controller.activeMaskFeather(), 0.34)) {
    return fail("edge add should create a smaller direct-paint brush for boundary cleanup");
  }

  controller.startSelectionEdgeBrush(true);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("subtract") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted() ||
      !almost_equal(controller.activeMaskBrushSize(), 0.026) ||
      !almost_equal(controller.activeMaskFeather(), 0.34)) {
    return fail("edge erase should create a smaller subtractive brush for boundary cleanup");
  }

  controller.startRemoveBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      !almost_equal(controller.activeMaskRemoveStrength(), 100.0)) {
    return fail("remove brush should create a brush mask configured for object removal");
  }
  if (controller.retouchCount() != 1 ||
      controller.retouchStack().size() != 1) {
    return fail("retouch grouped views should refresh after stack edits");
  }

  controller.startHealBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      controller.activeMaskRetouchMode() != QStringLiteral("heal") ||
      controller.activeMaskRemoveStrength() <= 0.0) {
    return fail("heal brush should create a dedicated retouch brush");
  }

  controller.startPatchBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      controller.activeMaskRetouchMode() != QStringLiteral("patch") ||
      !almost_equal(controller.activeMaskRemoveStrength(), 88.0) ||
      !controller.activeMaskBrushAutoMask()) {
    return fail("patch brush should create an auto-masked retouch brush");
  }
  controller.beginActiveBrushStroke();
  controller.addBrushPoint(0.42, 0.52);
  controller.endActiveBrushStroke();
  qputenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS", "1");
  controller.applyActiveRetouch();
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");
  const auto patch_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!patch_mask.value(QStringLiteral("committed")).toBool() ||
      patch_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty()) {
    return fail("patch brush should bake an inpaint patch when applied");
  }

  controller.startCloneBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskToolRole() != QStringLiteral("retouch") ||
      controller.activeMaskCommitted() ||
      controller.activeMaskRetouchMode() != QStringLiteral("clone") ||
      controller.activeMaskCloneSourceSet()) {
    return fail("clone stamp should start without a source point");
  }
  controller.setActiveMaskCloneSource(0.22, 0.34);
  if (!controller.activeMaskCloneSourceSet() ||
      !almost_equal(controller.activeMaskCloneSourceX(), 0.22) ||
      !almost_equal(controller.activeMaskCloneSourceY(), 0.34)) {
    return fail("clone stamp should store the chosen source point");
  }
  controller.setActiveMaskCloneScale(1.4);
  controller.setActiveMaskCloneRotation(18.0);
  if (!almost_equal(controller.activeMaskCloneScale(), 1.4) ||
      !almost_equal(controller.activeMaskCloneRotation(), 18.0)) {
    return fail("clone stamp should expose scale and rotation controls");
  }
  controller.addBrushPoint(0.48, 0.52);
  const auto clone_mask = controller.maskStack().at(controller.activeMaskIndex()).toMap();
  if (!almost_equal(clone_mask.value(QStringLiteral("cloneOffsetX")).toDouble(), -0.26) ||
      !almost_equal(clone_mask.value(QStringLiteral("cloneOffsetY")).toDouble(), -0.18) ||
      !almost_equal(clone_mask.value(QStringLiteral("cloneScale")).toDouble(), 1.4) ||
      !almost_equal(clone_mask.value(QStringLiteral("cloneRotation")).toDouble(), 18.0)) {
    return fail("clone stamp should derive a stable source offset from the first painted point");
  }

  controller.setActiveMaskSelectionExpand(22.0);
  if (!almost_equal(controller.activeMaskSelectionExpand(), 22.0)) {
    return fail("selection refine controls should expose expand and contract on the active mask");
  }
  controller.setActiveMaskSelectionSmooth(31.0);
  if (!almost_equal(controller.activeMaskSelectionSmooth(), 31.0)) {
    return fail("selection refine controls should expose smoothing on the active mask");
  }
  controller.setActiveMaskSelectionFineEdge(73.0);
  if (!almost_equal(controller.activeMaskSelectionFineEdge(), 73.0)) {
    return fail("selection refine controls should expose fine-edge recovery on the active mask");
  }
  controller.setActiveMaskSelectionEdgeContrast(64.0);
  if (!almost_equal(controller.activeMaskSelectionEdgeContrast(), 64.0)) {
    return fail("selection refine controls should expose edge contrast on the active mask");
  }
  controller.setActiveMaskSelectionDecontaminate(42.0);
  if (!almost_equal(controller.activeMaskSelectionDecontaminate(), 42.0)) {
    return fail("selection refine controls should expose edge decontamination on the active mask");
  }
  controller.setActiveMaskSelectionInverted(true);
  if (!controller.activeMaskSelectionInverted()) {
    return fail("selection refine controls should allow inverting the active selection");
  }

  controller.startPolygonSelection();
  controller.appendActivePolygonPoint(0.15, 0.15);
  controller.appendActivePolygonPoint(0.82, 0.18);
  controller.appendActivePolygonPoint(0.76, 0.84);
  if (controller.activeMaskKind() != QStringLiteral("polygon") ||
      controller.activeMaskPolygonPointCount() != 3 ||
      controller.activeMaskOutlinePoints().size() != 3) {
    return fail("polygon selection should track drawn points and expose an outline");
  }
  controller.removeLastActivePolygonPoint();
  if (controller.activeMaskPolygonPointCount() != 2) {
    return fail("polygon selection should support removing the last point");
  }
  controller.clearActivePolygonPoints();
  if (controller.activeMaskPolygonPointCount() != 0) {
    return fail("polygon selection should support clearing all points");
  }

  photo_editor::desktop::EditorPreviewController camera_default_controller;
  camera_default_controller.loadAsset(
      QStringLiteral("camera-default-source"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QString(),
      QString());
  camera_default_controller.setRawCalibrationHue(17.0);
  camera_default_controller.setRawRedPrimarySaturation(28.0);
  camera_default_controller.setRawCameraMatchingLook(QStringLiteral("Landscape"));
  camera_default_controller.setExposure(0.42);
  camera_default_controller.setCropPreset(QStringLiteral("4x5"));
  camera_default_controller.addMask(QStringLiteral("linear"));
  if (!camera_default_controller.saveCameraDefault() || !camera_default_controller.hasCameraDefault()) {
    return fail("saving camera defaults should persist defaults for the active camera");
  }

  camera_default_controller.loadAsset(
      QStringLiteral("camera-default-target"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QString(),
      QString());
  if (!almost_equal(camera_default_controller.rawCalibrationHue(), 17.0) ||
      !almost_equal(camera_default_controller.rawRedPrimarySaturation(), 28.0) ||
      camera_default_controller.rawCameraMatchingLook() != QStringLiteral("Landscape") ||
      !almost_equal(camera_default_controller.exposure(), 0.42) ||
      camera_default_controller.cropPreset() != QStringLiteral("Original") ||
      camera_default_controller.maskCount() != 0) {
    return fail("new assets from a camera should inherit sanitized per-camera defaults");
  }

  camera_default_controller.loadAsset(
      QStringLiteral("camera-default-other"),
      QString(),
      QString(),
      QStringLiteral("Nikon Z8"),
      QString(),
      QString(),
      QString());
  if (camera_default_controller.hasCameraDefault() ||
      !almost_equal(camera_default_controller.rawCalibrationHue(), 0.0) ||
      camera_default_controller.rawCameraMatchingLook() != QStringLiteral("Standard") ||
      !almost_equal(camera_default_controller.exposure(), 0.0)) {
    return fail("camera defaults should not bleed into other camera models");
  }

  photo_editor::desktop::EditorPreviewController reloaded_controller;
  if (!reloaded_controller.presetNames().contains(QStringLiteral("Portrait Base"))) {
    return fail("saved presets should reload from app data");
  }
  if (reloaded_controller.presetRecipeJson(QStringLiteral("Portrait Base")).isEmpty()) {
    return fail("reloaded presets should retain their recipe payload");
  }
  if (!reloaded_controller.presetPackNames().contains(QStringLiteral("Client Pack")) ||
      reloaded_controller.presetPackName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack") ||
      reloaded_controller.presetCategoryName(QStringLiteral("Client Warmth")) != QStringLiteral("Client Pack")) {
    return fail("saved user preset packs should reload from app data");
  }
  if (!reloaded_controller.isPresetFavorite(QStringLiteral("Client Warmth")) ||
      !reloaded_controller.favoritePresetNames().contains(QStringLiteral("Client Warmth"))) {
    return fail("saved preset favorites should reload from app data");
  }
  reloaded_controller.loadAsset(
      QStringLiteral("camera-default-reloaded"),
      QString(),
      QString(),
      QStringLiteral("Canon EOS R5"),
      QString(),
      QString(),
      QString());
  if (!reloaded_controller.hasCameraDefault() ||
      !almost_equal(reloaded_controller.rawCalibrationHue(), 17.0) ||
      !almost_equal(reloaded_controller.rawRedPrimarySaturation(), 28.0) ||
      reloaded_controller.rawCameraMatchingLook() != QStringLiteral("Landscape")) {
    return fail("saved camera defaults should reload from app data");
  }
  if (!reloaded_controller.clearCameraDefault() || reloaded_controller.hasCameraDefault()) {
    return fail("camera defaults should be clearable for the active camera");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  return EXIT_SUCCESS;
}
