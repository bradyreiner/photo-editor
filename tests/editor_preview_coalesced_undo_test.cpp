#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QString>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include "photo_render_utils.h"

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static bool section_comparison_isolation() {
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    QImage fixture(16, 16, QImage::Format_RGBA8888);
    fixture.setColorSpace(QColorSpace::SRgb);
    fixture.fill(QColor(96, 133, 77));
    const auto fixture_path = directory.filePath(QStringLiteral("source.png"));
    if (!fixture.save(fixture_path)) return false;
    EditorPreviewController controller;
    controller.loadAsset(QStringLiteral("section-comparison"), fixture_path, {}, {}, {}, {}, {});
    controller.setExposure(0.75);
    controller.setTemperature(30);
    controller.setTint(-12);
    controller.setGradingShadowsSaturation(24);
    controller.setHueAdjustment(18);
    controller.setSaturation(9);
    const QVariantList curve{QVariantMap{{"x", 0.0}, {"y", 0.0}},
                             QVariantMap{{"x", 0.5}, {"y", 0.6}},
                             QVariantMap{{"x", 1.0}, {"y", 1.0}}};
    controller.setActiveCurvePoints(curve);
    const auto saved = serialize_edit_recipe_json(controller.currentState());
    const auto undo_count = controller.undoStackForAsset(controller.asset_id_).size();
    const auto baseline = controller.currentRenderRequest(false).state;
    if (!controller.setSectionComparison(QStringLiteral("white-balance"))) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    auto preview = controller.currentRenderRequest(false).state;
    if (preview.temperature != 0 || preview.tint != 0 || preview.exposure != 0.75 ||
        preview.grading_shadows_saturation != 24 || preview.master_curve_points != curve)
      { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    const auto render = [&](const EditRecipeState& state) {
      RenderBackendTelemetry telemetry;
      auto result = render_output_image_with_backend(fixture, state, QColorSpace(QColorSpace::SRgb),
                                                     QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly,
                                                     nullptr, &telemetry);
      if (result.isNull())
        std::cerr << "Section render failed: " << telemetry.canonical_plan_error.toStdString()
                  << " / " << telemetry.fallback_reason.toStdString() << '\n';
      return result;
    };
    const auto original_pixels = render(baseline);
    const auto compared_pixels = render(preview);
    if (!original_pixels.isNull() && original_pixels == compared_pixels)
      std::cerr << "Comparison produced identical pixels, temperature=" << baseline.temperature
                << "/" << preview.temperature << " existingLayers=" << baseline.layers.size() << " red=" << original_pixels.pixelColor(0, 0).red() << '\n';
    if (original_pixels.isNull() || compared_pixels == original_pixels) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    if (!controller.setSectionComparison(QStringLiteral("curves"))) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    preview = controller.currentRenderRequest(false).state;
    if (!preview.master_curve_points.isEmpty() || preview.exposure != 0.75 || preview.temperature != 30 ||
        preview.grading_shadows_saturation != 24) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    if (!controller.setSectionComparison(QStringLiteral("grading"))) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    preview = controller.currentRenderRequest(false).state;
    if (preview.grading_shadows_saturation != 0 || preview.master_curve_points != curve || preview.temperature != 30)
      { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    controller.setSectionComparison(QStringLiteral("color-mix"));
    preview = controller.currentRenderRequest(false).state;
    if (preview.hue_adjustment != 0 || preview.saturation != 9 || preview.temperature != 30 ||
        preview.grading_shadows_saturation != 24 || preview.master_curve_points != curve)
      { std::cerr << "Color Mix comparison altered another section\n"; return false; }
    if (preview != controller.currentRenderRequest(true).state)
      { std::cerr << "Settled and interactive section scopes disagree\n"; return false; }
    controller.setSectionComparison(QStringLiteral("light"));
    if (!controller.beginSectionComparisonHold(QStringLiteral("curves"))) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    preview = controller.currentRenderRequest(false).state;
    if (!preview.master_curve_points.isEmpty() || preview.exposure != 0.75) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    controller.endSectionComparisonHold();
    preview = controller.currentRenderRequest(false).state;
    if (preview.exposure != 0 || preview.master_curve_points != curve) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    controller.beginSectionComparisonHold(QStringLiteral("curves"));
    controller.setSectionComparison(QStringLiteral("grading"));
    controller.endSectionComparisonHold();
    if (controller.sectionComparisonScope() != QStringLiteral("grading"))
      { std::cerr << "Releasing an obsolete hold replaced a newer comparison\n"; return false; }
    controller.clearSectionComparison();
    if (serialize_edit_recipe_json(controller.currentState()) != saved ||
        controller.undoStackForAsset(controller.asset_id_).size() != undo_count ||
        render(controller.currentRenderRequest(false).state) != original_pixels)
      { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    if (controller.setSectionComparison(QStringLiteral("invalid")) ||
        controller.setSectionComparison(QStringLiteral("white-balance"), 99)) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }

    if (!controller.createAdjustmentLayerFromCurrentGlobalEdits(QStringLiteral("Compared layer"))) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    controller.setTemperature(-5);
    controller.setExposure(0.25);
    const auto layer_id = controller.photoLayers().front().toMap().value(QStringLiteral("id")).toString();
    const auto layer_saved = serialize_edit_recipe_json(controller.currentState());
    if (!controller.setSectionComparison(QStringLiteral("white-balance"), 0)) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    preview = controller.currentRenderRequest(false).state;
    const auto compared = preview.layers.front().toMap();
    if (preview.temperature != -5 || preview.exposure != 0.25 ||
        compared.value(QStringLiteral("temperature")).toDouble() != 0 ||
        compared.value(QStringLiteral("exposure")).toDouble() != 0.75 ||
        compared.value(QStringLiteral("masterCurvePoints")).toList() != curve ||
        compared.value(QStringLiteral("id")).toString() != layer_id)
      { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    if (serialize_edit_recipe_json(controller.currentState()) != layer_saved) { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    controller.setSectionComparison(QStringLiteral("grading"), 0);
    preview = controller.currentRenderRequest(false).state;
    const auto grading = preview.layers.front().toMap().value(QStringLiteral("adjustments")).toMap()
                             .value(QStringLiteral("colorGrading")).toMap();
    if (grading.value(QStringLiteral("shadows")).toMap().value(QStringLiteral("saturation")).toDouble() != 0 ||
        preview.layers.front().toMap().value(QStringLiteral("temperature")).toDouble() != 30)
      { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    // Index movement cannot redirect a preview to a different layer.
    controller.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Other layer"));
    preview = controller.currentRenderRequest(false).state;
    if (controller.sectionComparisonLayerId() != layer_id || preview.layers.size() != 2 ||
        preview.layers.back().toMap().value(QStringLiteral("id")).toString() != layer_id)
      { std::cerr << "section comparison assertion at line " << __LINE__ << '\n'; return false; }
    controller.beginSectionComparisonHold(QStringLiteral("white-balance"), 1);
    controller.loadAsset(QStringLiteral("another-photo"), {}, {}, {}, {}, {}, {});
    controller.endSectionComparisonHold();
    return controller.sectionComparisonScope().isEmpty();
  }
  static bool extended_exposure_roundtrip() {
    QTemporaryDir directory;
    if (!directory.isValid()) return false;
    QImage fixture(8, 8, QImage::Format_RGBA8888);
    fixture.setColorSpace(QColorSpace::SRgb);
    fixture.fill(QColor(35, 60, 90));
    for (const double ev : {-5.0, -3.0, 3.0, 5.0, 0.01, -0.01}) {
      EditorPreviewController controller;
      controller.loadAsset(QStringLiteral("exposure-range"), {}, {}, {}, {}, {}, {});
      controller.setExposure(ev);
      if (std::abs(controller.exposure() - ev) > 1e-8) return false;
      controller.undo();
      if (controller.exposure() != 0) { std::cerr << "undo failed\n"; return false; }
      controller.redo();
      if (std::abs(controller.exposure() - ev) > 1e-8) return false;
      if (!controller.createAdjustmentLayerFromCurrentGlobalEdits(QStringLiteral("Local exposure"))) { std::cerr << "add layer failed\n"; return false; }
      // Creating an adjustment layer moves existing global edits into it.
      // Exercise a real change on that layer, then restore the global exposure.
      controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0);
      if (!controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), ev)) return false;
      controller.setExposure(ev);
      const auto layer_id = controller.photoLayers().front().toMap().value(QStringLiteral("id")).toString();
      controller.addMask(QStringLiteral("linear"));
      controller.setActiveMaskExposure(ev);
      if (std::abs(controller.activeMaskExposure() - ev) > 1e-8) { std::cerr << "mask failed " << controller.activeMaskExposure() << '\n'; return false; }
      const auto original_state = controller.currentState();
      const auto encoded = serialize_edit_recipe_json(original_state).toUtf8();
      QSaveFile recipe_file(directory.filePath(QStringLiteral("recipe.json")));
      if (!recipe_file.open(QIODevice::WriteOnly) || recipe_file.write(encoded) != encoded.size() ||
          !recipe_file.commit()) return false;
      QFile saved_recipe(recipe_file.fileName());
      if (!saved_recipe.open(QIODevice::ReadOnly)) return false;
      EditorPreviewController reopened;
      reopened.loadAsset(QStringLiteral("exposure-reopened"), {}, {}, {}, {}, {},
                         QString::fromUtf8(saved_recipe.readAll()));
      QVariantMap restored_layer;
      for (const auto& entry : reopened.photoLayers()) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == layer_id)
          restored_layer = entry.toMap();
      }
      if (std::abs(reopened.exposure() - ev) > 1e-8 || reopened.photoLayerCount() != controller.photoLayerCount() ||
          std::abs(restored_layer.value(QStringLiteral("exposure")).toDouble() - ev) > 1e-8 ||
          reopened.maskStack().isEmpty() ||
          std::abs(reopened.maskStack().front().toMap().value(QStringLiteral("exposure")).toDouble() - ev) > 1e-8)
        return false;
      const auto render = [&](const EditRecipeState& state) {
        return render_output_image_with_backend(fixture, state, QColorSpace(QColorSpace::SRgb),
                                                QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
      };
      const auto before = render(original_state);
      const auto after = render(reopened.currentState());
      if (before.isNull() || before != after) {
        std::cerr << "Export renderer changed after exposure reopen at " << ev << '\n';
        return false;
      }
      const auto output_path = directory.filePath(QStringLiteral("exposure.png"));
      if (!after.save(output_path, "PNG") || QImage(output_path).convertToFormat(after.format()) != after)
        return false;
    }
    return true;
  }
  static bool drafts_use_bounded_pipeline() {
    class Sink final : public EditorPreviewImageSink {
      void setImage(const QString&, const QImage&) override {}
      void clearImage(const QString&) override {}
    } sink;
    EditorPreviewController controller;
    controller.image_provider_ = &sink;
    controller.preview_viewport_active_ = true;
    controller.preview_viewport_rect_ = QRectF(0, 0, 1, 1);
    controller.preview_viewport_patch_max_edge_ = 4096;
    controller.file_path_ = QStringLiteral("/nonexistent/draft.png");
    controller.next_interactive_render_draft_ = true;
    const auto draft = controller.currentRenderRequest(false);
    controller.next_interactive_render_draft_ = false;
    const auto qt_inspection = controller.currentRenderRequest(false);
    controller.file_path_ = QStringLiteral("/nonexistent/draft.CR2");
    const auto raw_inspection = controller.currentRenderRequest(false);
    controller.image_provider_ = nullptr;
    return draft.draft_interactive && !draft.overlay_only && draft.max_edge > 0 &&
        !draft.preview_overlay_rect.isValid() && draft.preview_overlay_max_edge == 0 &&
        qt_inspection.preview_overlay_rect.isValid() &&
        !raw_inspection.overlay_only && !raw_inspection.preview_overlay_rect.isValid();
  }
  static bool pending_overlay_preserves_latest_drag() {
    EditorPreviewController controller;
    controller.beginCoalescedEdit();
    controller.edit_transaction_interactive_preview_request_count_ = 3;
    controller.edit_transaction_interactive_preview_reason_ = QStringLiteral("edit_change");
    controller.overlay_request_pending_ = true;
    const auto generation = controller.preview_session_.generation();
    controller.flushEditTransactionInteractivePreview();
    const auto retained = controller.preview_session_.generation() == generation &&
        controller.edit_transaction_interactive_preview_request_count_ == 3 &&
        controller.edit_transaction_interactive_preview_timer_.isActive();
    controller.overlay_request_pending_ = false;
    controller.flushEditTransactionInteractivePreview();
    const auto dispatched = controller.preview_session_.generation() == generation + 1 &&
        controller.edit_transaction_interactive_preview_request_count_ == 0;
    controller.endCoalescedEdit();
    return retained && dispatched &&
        !controller.edit_transaction_interactive_preview_timer_.isActive();
  }
};
}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  // Controllers recover their journal on construction. Isolate this process so
  // a prior test's opaque fill layer cannot cover the next run's image fixture.
  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) return fail("Could not create isolated test runtime");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::section_comparison_isolation())
    return fail("Section comparison must isolate the selected panel and stable target without changing saved edits");
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::extended_exposure_roundtrip())
    return fail("Extended global, layer and mask exposure must survive undo/redo and serialized reopen");
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::drafts_use_bounded_pipeline())
    return fail("Drafts and RAW must use the shared preview pipeline instead of a full-region Qt overlay");
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::pending_overlay_preserves_latest_drag())
    return fail("An in-flight overlay must retain the latest coalesced edit without invalidating its frame");

  photo_editor::desktop::EditorPreviewController global_controller;
  global_controller.loadAsset(
      QStringLiteral("coalesced-global-edit"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  global_controller.beginCoalescedEdit();
  global_controller.setExposure(0.2);
  global_controller.setExposure(0.5);
  global_controller.setContrast(10.0);
  global_controller.endCoalescedEdit();
  if (!almost_equal(global_controller.exposure(), 0.5) ||
      !almost_equal(global_controller.contrast(), 10.0) ||
      !global_controller.canUndo()) {
    return fail("coalesced global slider edits should preserve final values and create one undo step");
  }
  global_controller.undo();
  if (!almost_equal(global_controller.exposure(), 0.0) ||
      !almost_equal(global_controller.contrast(), 0.0) ||
      !global_controller.canRedo()) {
    return fail("one undo should restore the pre-drag global edit state");
  }
  global_controller.redo();
  if (!almost_equal(global_controller.exposure(), 0.5) ||
      !almost_equal(global_controller.contrast(), 10.0)) {
    return fail("redo should restore all coalesced global edit values together");
  }

  // The two-axis pad rolls back its captured values before ending the same
  // transaction. Cancellation must not add undo or consume an existing redo tail.
  photo_editor::desktop::EditorPreviewController pad_controller;
  pad_controller.loadAsset(QStringLiteral("coalesced-pad-edit"), QString(), QString(),
                           QString(), QString(), QString(), QString());
  pad_controller.beginCoalescedEdit();
  pad_controller.setTemperature(12.5);
  pad_controller.setTint(-8.25);
  pad_controller.endCoalescedEdit();
  pad_controller.undo();
  if (pad_controller.canUndo() || !pad_controller.canRedo() ||
      !almost_equal(pad_controller.temperature(), 0) || !almost_equal(pad_controller.tint(), 0))
    return fail("Two-axis pad must produce exactly one undo step for both parameters");
  pad_controller.beginCoalescedEdit();
  pad_controller.setTemperature(30);
  pad_controller.setTint(20);
  pad_controller.setTemperature(0);
  pad_controller.setTint(0);
  pad_controller.endCoalescedEdit();
  if (pad_controller.canUndo() || !pad_controller.canRedo())
    return fail("Canceled pad transaction must preserve history and the existing redo tail");
  pad_controller.redo();
  if (!almost_equal(pad_controller.temperature(), 12.5) ||
      !almost_equal(pad_controller.tint(), -8.25))
    return fail("Redo after pad cancellation must restore the original completed gesture");

  photo_editor::desktop::EditorPreviewController curve_controller;
  curve_controller.loadAsset(QStringLiteral("coalesced-curve-edit"), QString(), QString(),
                             QString(), QString(), QString(), QString());
  const auto original_points = curve_controller.activeCurvePoints();
  auto moved_points = original_points;
  auto endpoint = moved_points.front().toMap();
  endpoint.insert(QStringLiteral("y"), 3.0 / 255.0);
  moved_points.front() = endpoint;
  curve_controller.beginCoalescedEdit();
  curve_controller.setActiveCurvePoints(moved_points);
  endpoint.insert(QStringLiteral("y"), 4.0 / 255.0);
  moved_points.front() = endpoint;
  curve_controller.setActiveCurvePoints(moved_points);
  curve_controller.endCoalescedEdit();
  curve_controller.undo();
  if (curve_controller.canUndo() || !curve_controller.canRedo() ||
      curve_controller.activeCurvePoints() != original_points)
    return fail("One curve undo must restore all movements within the completed drag");
  curve_controller.beginCoalescedEdit();
  curve_controller.setActiveCurvePoints(moved_points);
  curve_controller.setActiveCurvePoints(original_points);
  curve_controller.endCoalescedEdit();
  if (curve_controller.canUndo() || !curve_controller.canRedo())
    return fail("Curve cancellation must not create history or discard redo");

  photo_editor::desktop::EditorPreviewController geometry_controller;
  geometry_controller.loadAsset(
      QStringLiteral("coalesced-geometry-edit"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  geometry_controller.beginCoalescedEdit();
  geometry_controller.setPerspectiveVertical(-14.0);
  geometry_controller.setTransformRotation(7.5);
  geometry_controller.setTransformScale(1.35);
  geometry_controller.setLiquifyCenterX(0.42);
  geometry_controller.setLiquifyCenterY(0.63);
  geometry_controller.setLiquifyRadius(0.31);
  geometry_controller.setLiquifyStrength(28.0);
  geometry_controller.endCoalescedEdit();
  if (!almost_equal(geometry_controller.perspectiveVertical(), -14.0) ||
      !almost_equal(geometry_controller.transformRotation(), 7.5) ||
      !almost_equal(geometry_controller.transformScale(), 1.35) ||
      !almost_equal(geometry_controller.liquifyStrength(), 28.0) ||
      !geometry_controller.canUndo()) {
    return fail("perspective, transform, and liquify drags should preserve final values in one edit");
  }
  geometry_controller.undo();
  if (!almost_equal(geometry_controller.perspectiveVertical(), 0.0) ||
      !almost_equal(geometry_controller.transformRotation(), 0.0) ||
      !almost_equal(geometry_controller.transformScale(), 1.0) ||
      !almost_equal(geometry_controller.liquifyCenterX(), 0.5) ||
      !almost_equal(geometry_controller.liquifyCenterY(), 0.5) ||
      !almost_equal(geometry_controller.liquifyRadius(), 0.25) ||
      !almost_equal(geometry_controller.liquifyStrength(), 0.0)) {
    return fail("one undo should restore the complete pre-drag geometry and liquify state");
  }

  photo_editor::desktop::EditorPreviewController layer_controller;
  layer_controller.loadAsset(
      QStringLiteral("coalesced-layer-edit"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!layer_controller.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Fill"))) {
    return fail("coalesced layer edit proof should create a layer target");
  }
  layer_controller.beginCoalescedEdit();
  if (!layer_controller.setPhotoLayerOpacity(0, 0.82) ||
      !layer_controller.setPhotoLayerOpacity(0, 0.64)) {
    return fail("coalesced layer edit proof should update layer opacity during a drag");
  }
  layer_controller.endCoalescedEdit();
  auto layer_after_drag = layer_controller.photoLayers().value(0).toMap();
  if (!almost_equal(layer_after_drag.value(QStringLiteral("opacity")).toDouble(), 0.64) ||
      !layer_controller.canUndo()) {
    return fail("coalesced layer opacity drag should keep only the final opacity value");
  }
  layer_controller.undo();
  auto layer_after_undo = layer_controller.photoLayers().value(0).toMap();
  if (layer_controller.photoLayerCount() != 1 ||
      !almost_equal(layer_after_undo.value(QStringLiteral("opacity")).toDouble(), 1.0) ||
      !layer_controller.canRedo()) {
    return fail("one undo should restore layer opacity without removing the layer target");
  }
  layer_controller.redo();
  auto layer_after_redo = layer_controller.photoLayers().value(0).toMap();
  if (layer_controller.photoLayerCount() != 1 ||
      !almost_equal(layer_after_redo.value(QStringLiteral("opacity")).toDouble(), 0.64)) {
    return fail("redo should restore the coalesced layer opacity drag as one edit");
  }

  photo_editor::desktop::EditorPreviewController text_controller;
  text_controller.loadAsset(
      QStringLiteral("coalesced-text-edit"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  if (!text_controller.addTextLayer(QStringLiteral("Text"), QStringLiteral("Caption"))) {
    return fail("coalesced text edit proof should create a text layer target");
  }
  text_controller.beginCoalescedEdit();
  if (!text_controller.setPhotoLayerText(0, QString()) ||
      !text_controller.setPhotoLayerText(0, QStringLiteral("Fast caption")) ||
      !text_controller.setPhotoLayerTextStyle(
          0,
          QStringLiteral("Avenir"),
          72,
          QStringLiteral("#33ccaa"),
          QStringLiteral("bold"),
          QStringLiteral("center")) ||
      !text_controller.setPhotoLayerTextBox(0, 720, 240) ||
      !text_controller.setPhotoLayerTransform(0, 120, 80, 1.4, 12.0)) {
    return fail("coalesced text edit proof should apply live content, typography, box, and transform changes");
  }
  text_controller.endCoalescedEdit();
  const auto text_after_edit = text_controller.photoLayers().value(0).toMap();
  if (text_after_edit.value(QStringLiteral("text")).toString() != QStringLiteral("Fast caption") ||
      text_after_edit.value(QStringLiteral("fontSize")).toInt() != 72 ||
      text_after_edit.value(QStringLiteral("pixelWidth")).toInt() != 720 ||
      text_after_edit.value(QStringLiteral("x")).toInt() != 120 ||
      !text_controller.canUndo()) {
    return fail("coalesced text edit should preserve the final live values and create one undo step");
  }
  text_controller.undo();
  const auto text_after_undo = text_controller.photoLayers().value(0).toMap();
  if (text_controller.photoLayerCount() != 1 ||
      text_after_undo.value(QStringLiteral("text")).toString() != QStringLiteral("Text") ||
      text_after_undo.value(QStringLiteral("fontSize")).toInt() != 48 ||
      text_after_undo.value(QStringLiteral("pixelWidth")).toInt() != 512 ||
      text_after_undo.value(QStringLiteral("x")).toInt() != 64) {
    return fail("one undo should restore the complete pre-interaction text layer state");
  }

  const auto before_typography = text_controller.photoLayers();
  if (!text_controller.setPhotoLayerTypography(0, {{"italic", true}, {"size", 27.375}, {"tracking", 1.5}}) ||
      text_controller.photoLayerTypography(0).value("size").toDouble() != 27.375 ||
      !text_controller.photoLayerTypography(0).value("italic").toBool())
    return fail("Typography command must apply fractional metrics and styling");
  const auto styled_layers = text_controller.photoLayers();
  if (text_controller.setPhotoLayerTypography(0, {{"size", -4}}) ||
      text_controller.photoLayers() != styled_layers)
    return fail("Invalid typography must leave the layer unchanged");
  const auto style_before_alignment = text_controller.photoLayerTypography(0);
  const auto row_before_alignment = text_controller.photoLayers().at(0).toMap();
  if (!text_controller.setPhotoLayerTextStyle(0, row_before_alignment.value("fontFamily").toString(),
      row_before_alignment.value("fontSize").toDouble(), row_before_alignment.value("textColor").toString(),
      row_before_alignment.value("fontWeight").toString(), QStringLiteral("right")) ||
      text_controller.photoLayerTypography(0) != style_before_alignment)
    return fail("Alignment changes must preserve precise versioned typography metrics");
  text_controller.undo();
  text_controller.undo();
  if (text_controller.photoLayers() != before_typography)
    return fail("Typography undo must restore the legacy layer without upgrading its layout");

  const auto before_creation = text_controller.photoLayers();
  if (!text_controller.addParagraphTextLayer(QString(), 120, 80, 300, 160, 800, 600))
    return fail("Paragraph creation must accept a document-space box");
  const auto paragraph = text_controller.photoLayers().first().toMap();
  if (!paragraph.value("text").toString().isEmpty() || paragraph.value("x").toInt() != 120 ||
      paragraph.value("pixelWidth").toInt() != 300 || paragraph.value("canvasWidth").toInt() != 800 ||
      !paragraph.contains("textTypography"))
    return fail("Paragraph creation must preserve its empty draft, geometry and versioned typography");
  if (text_controller.addParagraphTextLayer("Invalid", 0, 0, 0, 160, 800, 600))
    return fail("Invalid paragraph geometry must be rejected before history changes");
  text_controller.undo();
  if (text_controller.photoLayers() != before_creation)
    return fail("One undo must remove the entire newly created paragraph layer");

  photo_editor::desktop::EditorPreviewController range_controller;
  range_controller.loadAsset(
      QStringLiteral("coalesced-range-mask-edit"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  range_controller.addMask(QStringLiteral("luminance"));
  range_controller.beginCoalescedEdit();
  range_controller.setActiveMaskLuminanceMin(0.34);
  range_controller.setActiveMaskLuminanceMax(0.91);
  range_controller.setActiveMaskRangeFeather(0.06);
  range_controller.endCoalescedEdit();
  if (!almost_equal(range_controller.activeMaskLuminanceMin(), 0.34) ||
      !almost_equal(range_controller.activeMaskLuminanceMax(), 0.91) ||
      !almost_equal(range_controller.activeMaskRangeFeather(), 0.06) ||
      !range_controller.canUndo()) {
    return fail("range-mask slider drags should preserve final values in one edit");
  }
  range_controller.undo();
  if (!almost_equal(range_controller.activeMaskLuminanceMin(), 0.2) ||
      !almost_equal(range_controller.activeMaskLuminanceMax(), 0.8) ||
      !almost_equal(range_controller.activeMaskRangeFeather(), 0.1)) {
    return fail("one undo should restore all range-mask values without removing the mask");
  }

  return EXIT_SUCCESS;
}
