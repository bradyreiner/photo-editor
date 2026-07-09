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

bool is_neutral_adjustment_layer(const QVariantMap& layer) {
  return almost_equal(layer.value(QStringLiteral("exposure")).toDouble(), 0.0) &&
         almost_equal(layer.value(QStringLiteral("temperature")).toDouble(), 0.0) &&
         almost_equal(layer.value(QStringLiteral("tint")).toDouble(), 0.0);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  photo_editor::desktop::EditorPreviewController selection_controller;
  selection_controller.loadAsset(
      QStringLiteral("local-edit-target-selection"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  selection_controller.addMask(QStringLiteral("linear"));
  if (selection_controller.maskCount() != 1 ||
      selection_controller.photoLayerCount() != 1 ||
      !selection_controller.hasActiveMask()) {
    return fail("starting a selection source should create one active local edit target");
  }
  const auto selection_mask = selection_controller.maskStack().at(0).toMap();
  const auto selection_layer = selection_controller.photoLayers().at(0).toMap();
  if (selection_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !selection_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      selection_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          selection_mask.value(QStringLiteral("maskSetId")).toString() ||
      !is_neutral_adjustment_layer(selection_layer)) {
    return fail("selection sources should create a neutral adjustment layer linked to the active selection");
  }
  if (selection_controller.ensureActiveSelectionAdjustmentLayer() != 0 ||
      selection_controller.photoLayerCount() != 1) {
    return fail("active selection should resolve to its existing linked adjustment layer");
  }

  selection_controller.undo();
  if (selection_controller.maskCount() != 0 || selection_controller.photoLayerCount() != 0) {
    return fail("undo should remove a selection source and its auto-created adjustment layer together");
  }
  selection_controller.redo();
  if (selection_controller.maskCount() != 1 ||
      selection_controller.photoLayerCount() != 1 ||
      !selection_controller.hasActiveMask()) {
    return fail("redo should restore both the active selection and linked adjustment layer");
  }

  photo_editor::desktop::EditorPreviewController brush_controller;
  brush_controller.loadAsset(
      QStringLiteral("local-edit-target-brush"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  brush_controller.startSelectionBrush(false);
  if (brush_controller.maskCount() != 1 ||
      brush_controller.photoLayerCount() != 1 ||
      !brush_controller.hasActiveMask()) {
    return fail("starting a selection brush should create and select one linked adjustment layer");
  }
  const auto brush_mask = brush_controller.maskStack().at(0).toMap();
  const auto brush_layer = brush_controller.photoLayers().at(0).toMap();
  if (brush_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !brush_layer.value(QStringLiteral("selectionScoped")).toBool() ||
      brush_layer.value(QStringLiteral("selectionMaskSetId")).toString() !=
          brush_mask.value(QStringLiteral("maskSetId")).toString()) {
    return fail("selection brush target should be the adjustment layer linked to the active brush selection");
  }

  if (!brush_controller.deletePhotoLayer(0) ||
      brush_controller.photoLayerCount() != 0 ||
      brush_controller.ensureActiveSelectionAdjustmentLayer() != 0 ||
      brush_controller.photoLayerCount() != 1) {
    return fail("a missing adjustment layer for an active selection should be recreated on demand");
  }

  photo_editor::desktop::EditorPreviewController retouch_controller;
  retouch_controller.loadAsset(
      QStringLiteral("local-edit-target-retouch"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  retouch_controller.startRemoveBrush();
  if (retouch_controller.maskCount() != 1 ||
      retouch_controller.photoLayerCount() != 0 ||
      !retouch_controller.hasActiveMask()) {
    return fail("retouch local edits should select a retouch mask without creating an adjustment layer");
  }

  return EXIT_SUCCESS;
}
