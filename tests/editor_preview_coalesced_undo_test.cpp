#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QString>

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

  return EXIT_SUCCESS;
}
