#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>

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

  return EXIT_SUCCESS;
}
