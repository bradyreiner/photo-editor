#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QStringList>

#include "editor_preview_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

QStringList layer_order_ids(const photo_editor::desktop::EditorPreviewController& controller) {
  QStringList ids;
  const auto layers = controller.photoLayers();
  for (const auto& layer_value : layers) {
    ids.append(layer_value.toMap().value(QStringLiteral("id")).toString());
  }
  return ids;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  photo_editor::desktop::EditorPreviewController controller;
  controller.loadAsset(
      QStringLiteral("layer-mask-history-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  controller.addMask(QStringLiteral("linear"));
  if (controller.maskCount() != 1 || controller.photoLayerCount() != 1 || !controller.canUndo()) {
    return fail("mask creation should create a linked layer and enter undo history");
  }
  controller.undo();
  if (controller.maskCount() != 0 || controller.photoLayerCount() != 0 || !controller.canRedo()) {
    return fail("undo should remove a newly created mask and linked adjustment layer");
  }
  controller.redo();
  if (controller.maskCount() != 1 || controller.photoLayerCount() != 1) {
    return fail("redo should restore a newly created mask and linked adjustment layer");
  }

  controller.setActiveMaskExposure(0.46);
  if (!almost_equal(controller.activeMaskExposure(), 0.46)) {
    return fail("mask edit history proof should edit the active mask");
  }
  controller.undo();
  if (!almost_equal(controller.activeMaskExposure(), 0.0) || !controller.canRedo()) {
    return fail("undo should restore the previous active mask edit");
  }
  controller.redo();
  if (!almost_equal(controller.activeMaskExposure(), 0.46)) {
    return fail("redo should restore the active mask edit");
  }

  if (!controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("vibrance"), 37.0)) {
    return fail("layer-local color history proof should edit the linked adjustment layer");
  }
  controller.undo();
  auto history_layer = controller.photoLayers().at(0).toMap();
  if (!almost_equal(history_layer.value(QStringLiteral("vibrance")).toDouble(), 0.0) ||
      !controller.canRedo()) {
    return fail("undo should restore the previous layer-local color edit");
  }
  controller.redo();
  history_layer = controller.photoLayers().at(0).toMap();
  if (!almost_equal(history_layer.value(QStringLiteral("vibrance")).toDouble(), 37.0)) {
    return fail("redo should restore the layer-local color edit");
  }

  if (!controller.addFillLayer(QStringLiteral("#303840"), QStringLiteral("History Fill")) ||
      controller.photoLayerCount() != 2) {
    return fail("layer reorder/delete history proof should prepare a second layer");
  }
  const auto order_before_move = layer_order_ids(controller);
  if (!controller.movePhotoLayer(0, 1)) {
    return fail("layer reorder history proof should move a layer");
  }
  const auto order_after_move = layer_order_ids(controller);
  if (order_after_move == order_before_move) {
    return fail("layer reorder should change layer order before undo");
  }
  controller.undo();
  if (layer_order_ids(controller) != order_before_move || !controller.canRedo()) {
    return fail("undo should restore layer order before a reorder");
  }
  controller.redo();
  if (layer_order_ids(controller) != order_after_move) {
    return fail("redo should restore layer order after a reorder");
  }

  const auto count_before_delete = controller.photoLayerCount();
  const auto order_before_delete = layer_order_ids(controller);
  if (!controller.deletePhotoLayer(1) || controller.photoLayerCount() != count_before_delete - 1) {
    return fail("layer deletion history proof should delete one layer");
  }
  controller.undo();
  if (controller.photoLayerCount() != count_before_delete ||
      layer_order_ids(controller) != order_before_delete ||
      !controller.canRedo()) {
    return fail("undo should restore a deleted layer in its previous order");
  }
  controller.redo();
  if (controller.photoLayerCount() != count_before_delete - 1) {
    return fail("redo should delete the same layer again");
  }

  return EXIT_SUCCESS;
}
