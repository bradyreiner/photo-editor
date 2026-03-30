#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

#include "editor_preview_controller.h"

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
  controller.setActiveMaskPosition(0.61);
  controller.setActiveMaskRotation(14.0);
  if (!almost_equal(controller.linearMaskExposure(), 0.75) ||
      !almost_equal(controller.linearMaskPosition(), 0.61)) {
    return fail("active mask edits should synchronize legacy compatibility fields");
  }

  controller.addMask(QStringLiteral("radial"));
  controller.setActiveMaskOperation(QStringLiteral("subtract"));
  controller.setActiveMaskCenterX(0.43);
  controller.setActiveMaskCenterY(0.57);
  controller.setActiveMaskRadiusX(0.18);
  controller.setActiveMaskRadiusY(0.22);
  if (controller.maskCount() != 2 ||
      controller.activeMaskOperation() != QStringLiteral("subtract")) {
    return fail("stack masks should retain per-entry operations");
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
  if (controller.activeMaskBrushPointCount() != 2) {
    return fail("brush masks should track painted points");
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
  controller.setActiveMaskHueCenter(0.12);
  controller.setActiveMaskHueRange(0.18);
  controller.setActiveMaskSaturationMin(0.24);
  controller.setActiveMaskSaturationMax(0.77);
  controller.setActiveMaskRemoveStrength(63.0);
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

  controller.setTexture(18.0);
  controller.setDehaze(11.0);
  if (!controller.savePreset(QStringLiteral("Portrait Base"), true) ||
      !controller.presetNames().contains(QStringLiteral("Portrait Base"))) {
    return fail("saving a preset should persist it in the controller");
  }

  controller.setTexture(0.0);
  controller.setDehaze(0.0);
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

  const auto smart_select_path = runtime_root.filePath(QStringLiteral("smart-select-source.png"));
  QImage smart_select_source(16, 16, QImage::Format_ARGB32);
  smart_select_source.fill(Qt::white);
  if (!smart_select_source.save(smart_select_path)) {
    return fail("smart select source image should be writable");
  }

  controller.loadAsset(
      QStringLiteral("asset-smart"),
      smart_select_path,
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  controller.updateSmartSelectPreview(0.15, 0.5);
  if (!controller.smartSelectPreviewActive() ||
      controller.smartSelectPreviewOutlinePoints().size() < 4) {
    return fail("smart select should expose a realtime hover preview before committing the selection");
  }
  controller.smartSelectObjectAt(0.15, 0.5, true);
  if (controller.maskCount() != 1 ||
      controller.activeMaskKind() != QStringLiteral("object")) {
    return fail("smart select should create an object selection mask");
  }
  auto smart_mask = controller.maskStack().at(0).toMap();
  if (smart_mask.value(QStringLiteral("name")).toString() != QStringLiteral("Smart Select 1")) {
    return fail("smart select should label newly created smart selections");
  }

  controller.smartSelectObjectAt(0.5, 0.5, false);
  if (controller.maskCount() != 1) {
    return fail("smart select should expand the active object mask instead of adding a new one");
  }
  smart_mask = controller.maskStack().at(0).toMap();
  const auto merged = smart_mask.value(QStringLiteral("segmentationData")).toByteArray();
  if (merged.size() != 16 ||
      static_cast<unsigned char>(merged.at(0)) != 0xff ||
      static_cast<unsigned char>(merged.at(1)) != 0xff ||
      static_cast<unsigned char>(merged.at(2)) != 0xff ||
      static_cast<unsigned char>(merged.at(3)) != 0x00) {
    return fail("smart select should union repeated object picks into one selection");
  }
  if (controller.activeMaskOutlinePoints().size() < 4) {
    return fail("smart select should expose a traced outline for the active object selection");
  }

  controller.smartSelectObjectAt(0.85, 0.5, true);
  if (controller.maskCount() != 2 ||
      controller.activeMaskIndex() != 1) {
    return fail("starting a new smart selection should create a separate object mask");
  }
  controller.clearSmartSelectPreview();
  if (controller.smartSelectPreviewActive()) {
    return fail("smart select preview should clear when the hover state is reset");
  }

  controller.startSelectionBrush(false);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("add")) {
    return fail("refine add should create an additive brush mask");
  }

  controller.startSelectionBrush(true);
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskOperation() != QStringLiteral("subtract")) {
    return fail("refine subtract should create a subtractive brush mask");
  }

  controller.startRemoveBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      !almost_equal(controller.activeMaskRemoveStrength(), 100.0)) {
    return fail("remove brush should create a brush mask configured for object removal");
  }

  controller.startHealBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
      controller.activeMaskRetouchMode() != QStringLiteral("heal") ||
      controller.activeMaskRemoveStrength() <= 0.0) {
    return fail("heal brush should create a dedicated retouch brush");
  }

  controller.startCloneBrush();
  if (controller.activeMaskKind() != QStringLiteral("brush") ||
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
