#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include "editor_preview_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
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
  controller.setActiveMaskBrushSize(0.14);
  controller.setActiveMaskBrushFlow(0.72);
  controller.addBrushPoint(0.35, 0.44);
  controller.addBrushPoint(0.41, 0.51);
  if (controller.activeMaskBrushPointCount() < 2) {
    return fail("brush masks should track painted samples");
  }
  controller.clearActiveBrushPoints();
  if (controller.activeMaskBrushPointCount() != 0) {
    return fail("clearing brush points should reset the active brush mask");
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

  controller.setSharpening(28.0);
  controller.setSharpenMasking(41.0);
  controller.setLuminanceNr(19.0);
  controller.setColorNr(24.0);
  controller.setLensDistortion(-16.0);
  controller.setLensVignette(22.0);
  controller.setChromaticAberration(37.0);
  if (!almost_equal(controller.sharpening(), 28.0) ||
      !almost_equal(controller.sharpenMasking(), 41.0) ||
      !almost_equal(controller.luminanceNr(), 19.0) ||
      !almost_equal(controller.colorNr(), 24.0) ||
      !almost_equal(controller.lensDistortion(), -16.0) ||
      !almost_equal(controller.lensVignette(), 22.0) ||
      !almost_equal(controller.chromaticAberration(), 37.0)) {
    return fail("detail and optics controls should retain editable preview state");
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
  controller.setRawWhiteBalanceOverride(true);
  controller.setRawWhiteBalanceTemperature(6400.0);
  controller.setRawWhiteBalanceTint(-12.0);
  if (!controller.copyCurrentSettings()) {
    return fail("copying settings should retain RAW decode controls");
  }

  controller.setRawExposure(0.0);
  controller.setRawBoost(0.0);
  controller.setRawShadowBoost(1.0);
  controller.setRawWhiteBalanceOverride(false);
  controller.setRawWhiteBalanceTemperature(5200.0);
  controller.setRawWhiteBalanceTint(0.0);
  controller.setExposure(-0.25);
  if (!controller.pasteCopiedSettings({QStringLiteral("raw")})) {
    return fail("pasting copied settings should support RAW-only adjustment groups");
  }
  if (!almost_equal(controller.rawExposure(), 1.25) ||
      !almost_equal(controller.rawBoost(), 0.55) ||
      !almost_equal(controller.rawShadowBoost(), 1.4) ||
      !controller.rawWhiteBalanceOverride() ||
      !almost_equal(controller.rawWhiteBalanceTemperature(), 6400.0) ||
      !almost_equal(controller.rawWhiteBalanceTint(), -12.0) ||
      !almost_equal(controller.exposure(), -0.25)) {
    return fail("raw-only paste should restore RAW decode controls without touching tone controls");
  }

  controller.setTexture(18.0);
  controller.setDehaze(11.0);
  controller.setRawDetail(0.72);
  controller.setRawLensCorrection(false);
  if (!controller.savePreset(QStringLiteral("Portrait Base"), true) ||
      !controller.presetNames().contains(QStringLiteral("Portrait Base"))) {
    return fail("saving a preset should persist it in the controller");
  }

  controller.setTexture(0.0);
  controller.setDehaze(0.0);
  controller.setRawDetail(0.0);
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
  auto smart_mask = controller.maskStack().at(0).toMap();
  if (smart_mask.value(QStringLiteral("name")).toString() != QStringLiteral("Smart Selection 1")) {
    return fail("smart select should label newly created smart selections");
  }
  if (controller.maskSetCount() != 1 ||
      controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList().size() != 1) {
    return fail("selection masks should surface as grouped mask sets");
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
  subtract_controller.smartSelectObjectAt(0.18, 0.5, true);
  const auto subtract_pick =
      subtract_controller.pickMaskAt(0.48, 0.5, QStringLiteral("selection-subtract"), false);
  if (subtract_pick != QStringLiteral("selection-ai") ||
      subtract_controller.maskCount() != 2 ||
      subtract_controller.activeMaskOperation() != QStringLiteral("subtract") ||
      subtract_controller.activeMaskToolRole() != QStringLiteral("selection") ||
      subtract_controller.maskSetCount() != 1 ||
      subtract_controller.maskSets().at(0).toMap().value(QStringLiteral("sources")).toList().size() != 2) {
    return fail("smart select should support subtractive object prompts inside the current mask set");
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

  photo_editor::desktop::EditorPreviewController fallback_controller;
  fallback_controller.loadAsset(
      QStringLiteral("asset-fallback"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  const auto selection_fallback =
      fallback_controller.pickMaskAt(0.02, 0.02, QStringLiteral("selection"), false);
  if (selection_fallback != QStringLiteral("selection-fallback-polygon") ||
      fallback_controller.activeMaskKind() != QStringLiteral("polygon") ||
      fallback_controller.activeMaskPolygonPointCount() != 1) {
    return fail("selection fallback should seed a polygon mask when AI cannot isolate the click");
  }
  const auto fallback_polygon_points = fallback_controller.activeMaskPolygonPoints();
  if (fallback_polygon_points.size() != 1 ||
      !almost_equal(fallback_polygon_points.at(0).toMap().value(QStringLiteral("x")).toDouble(), 0.02) ||
      !almost_equal(fallback_polygon_points.at(0).toMap().value(QStringLiteral("y")).toDouble(), 0.02)) {
    return fail("selection fallback should seed the polygon at the clicked point");
  }

  fallback_controller.removeActiveMask();
  const auto remove_fallback =
      fallback_controller.pickMaskAt(0.02, 0.02, QStringLiteral("remove"), false);
  if (remove_fallback != QStringLiteral("remove-fallback-brush") ||
      fallback_controller.activeMaskKind() != QStringLiteral("brush") ||
      fallback_controller.activeMaskRetouchMode() != QStringLiteral("remove") ||
      fallback_controller.activeMaskCommitted() ||
      fallback_controller.activeMaskBrushPointCount() != 1) {
    return fail("remove fallback should start a staged remove brush when AI cannot isolate the click");
  }
  const auto fallback_brush_points =
      fallback_controller.maskStack().at(fallback_controller.activeMaskIndex()).toMap().value(
          QStringLiteral("brushPoints")).toList();
  if (fallback_brush_points.size() != 1 ||
      !almost_equal(fallback_brush_points.at(0).toMap().value(QStringLiteral("x")).toDouble(), 0.02) ||
      !almost_equal(fallback_brush_points.at(0).toMap().value(QStringLiteral("y")).toDouble(), 0.02)) {
    return fail("remove fallback should seed the first brush point at the clicked point");
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
  remove_controller.applyActiveRetouch();
  if (!remove_controller.maskStack().at(0).toMap().value(QStringLiteral("committed")).toBool() ||
      !remove_controller.maskStack().at(1).toMap().value(QStringLiteral("committed")).toBool()) {
    return fail("applying a retouch should commit every pending retouch mask in the session");
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

  controller.startSelectionBrush(true);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("subtract") ||
      controller.activeMaskToolRole() != QStringLiteral("selection") ||
      !controller.activeMaskCommitted()) {
    return fail("refine subtract should create a subtractive brush mask");
  }
  const auto grouped_mask_sets_after_subtract = controller.maskSets();
  if (grouped_mask_sets_after_subtract.size() != 2 ||
      grouped_mask_sets_after_subtract.at(1).toMap().value(QStringLiteral("sources")).toList().size() != 3) {
    return fail("grouped selection mask views should refresh after stack edits");
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

  photo_editor::desktop::EditorPreviewController reloaded_controller;
  if (!reloaded_controller.presetNames().contains(QStringLiteral("Portrait Base"))) {
    return fail("saved presets should reload from app data");
  }
  if (reloaded_controller.presetRecipeJson(QStringLiteral("Portrait Base")).isEmpty()) {
    return fail("reloaded presets should retain their recipe payload");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  return EXIT_SUCCESS;
}
