#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QString>
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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  photo_editor::desktop::EditorPreviewController global_compare_controller;
  global_compare_controller.loadAsset(
      QStringLiteral("before-after-global"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  global_compare_controller.setExposure(0.42);
  global_compare_controller.setContrast(18.0);
  const auto global_recipe_before = global_compare_controller.currentRecipeJson();
  global_compare_controller.setCompareMode(true);
  if (!global_compare_controller.compareMode() ||
      global_compare_controller.currentRecipeJson() != global_recipe_before ||
      !almost_equal(global_compare_controller.exposure(), 0.42) ||
      !almost_equal(global_compare_controller.contrast(), 18.0)) {
    return fail("global before/after compare should not mutate the current recipe");
  }
  global_compare_controller.setCompareMode(false);
  if (global_compare_controller.compareMode() ||
      global_compare_controller.currentRecipeJson() != global_recipe_before ||
      !almost_equal(global_compare_controller.exposure(), 0.42) ||
      !almost_equal(global_compare_controller.contrast(), 18.0)) {
    return fail("leaving global before/after compare should restore preview mode without mutating recipe state");
  }

  photo_editor::desktop::EditorPreviewController local_compare_controller;
  local_compare_controller.loadAsset(
      QStringLiteral("before-after-local"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  local_compare_controller.addMask(QStringLiteral("linear"));
  local_compare_controller.setActiveMaskExposure(0.44);
  local_compare_controller.setActiveMaskTemperature(18.0);
  local_compare_controller.setActiveMaskVibrance(26.0);
  local_compare_controller.setMaskCompareAdjustmentGroups(
      {QStringLiteral("tone"), QStringLiteral("presence"), QStringLiteral("effects")});
  const auto local_recipe_before = local_compare_controller.currentRecipeJson();
  const auto local_mask_before = local_compare_controller.maskStack().value(0).toMap();
  local_compare_controller.setCompareMode(true);
  const auto local_mask_during_compare = local_compare_controller.maskStack().value(0).toMap();
  if (!local_compare_controller.compareMode() ||
      local_compare_controller.currentRecipeJson() != local_recipe_before ||
      local_mask_during_compare != local_mask_before ||
      !almost_equal(local_compare_controller.activeMaskExposure(), 0.44) ||
      !almost_equal(local_compare_controller.activeMaskTemperature(), 18.0) ||
      !almost_equal(local_compare_controller.activeMaskVibrance(), 26.0)) {
    return fail("local before/after compare should not mutate active mask recipe values");
  }

  photo_editor::desktop::EditorPreviewController preset_preview_controller;
  preset_preview_controller.loadAsset(
      QStringLiteral("before-after-preset-preview"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  preset_preview_controller.setTexture(6.0);
  preset_preview_controller.setClarity(7.0);
  const auto preset_recipe_before = preset_preview_controller.currentRecipeJson();
  const auto preview_recipe_json = preset_preview_controller.presetPreviewRecipeJson(
      QStringLiteral("Landscape Pop"),
      {QStringLiteral("detail")},
      0.5);
  if (preview_recipe_json.isEmpty() ||
      preset_preview_controller.currentRecipeJson() != preset_recipe_before ||
      !almost_equal(preset_preview_controller.texture(), 6.0) ||
      !almost_equal(preset_preview_controller.clarity(), 7.0)) {
    return fail("preset preview recipe should be generated without mutating current recipe state");
  }

  return EXIT_SUCCESS;
}
