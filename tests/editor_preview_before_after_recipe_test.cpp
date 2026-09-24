#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include "editor_preview_controller.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static RenderRequest current_render_request(
      const EditorPreviewController& controller,
      bool full_resolution = false) {
    return controller.currentRenderRequest(full_resolution);
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
  const auto global_recipe_before = global_compare_controller.currentAdjustmentPatchJson();
  global_compare_controller.setCompareMode(true);
  if (!global_compare_controller.compareMode() ||
      global_compare_controller.currentAdjustmentPatchJson() != global_recipe_before ||
      !almost_equal(global_compare_controller.exposure(), 0.42) ||
      !almost_equal(global_compare_controller.contrast(), 18.0)) {
    return fail("global before/after compare should not mutate the current recipe");
  }
  global_compare_controller.setCompareMode(false);
  if (global_compare_controller.compareMode() ||
      global_compare_controller.currentAdjustmentPatchJson() != global_recipe_before ||
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
  const auto local_recipe_before = local_compare_controller.currentAdjustmentPatchJson();
  const auto local_mask_before = local_compare_controller.maskStack().value(0).toMap();
  local_compare_controller.setCompareMode(true);
  const auto local_mask_during_compare = local_compare_controller.maskStack().value(0).toMap();
  if (!local_compare_controller.compareMode() ||
      local_compare_controller.currentAdjustmentPatchJson() != local_recipe_before ||
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
  const auto preset_recipe_before = preset_preview_controller.currentAdjustmentPatchJson();
  const auto preview_adjustment_patch_json =
      preset_preview_controller.presetPreviewAdjustmentPatchJson(
      QStringLiteral("Landscape Pop"),
      {QStringLiteral("detail")},
      0.5);
  if (preview_adjustment_patch_json.isEmpty() ||
      preset_preview_controller.currentAdjustmentPatchJson() != preset_recipe_before ||
      !almost_equal(preset_preview_controller.texture(), 6.0) ||
      !almost_equal(preset_preview_controller.clarity(), 7.0)) {
    return fail("preset preview recipe should be generated without mutating current recipe state");
  }

  photo_editor::desktop::EditorPreviewController snapshot_compare_controller;
  snapshot_compare_controller.loadAsset(
      QStringLiteral("before-after-named-snapshot"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());
  snapshot_compare_controller.setExposure(0.25);
  snapshot_compare_controller.setContrast(11.0);
  const auto snapshot_id =
      snapshot_compare_controller.createEditHistorySnapshot(QStringLiteral("Color approved"));
  if (snapshot_id.isEmpty()) {
    return fail("named comparison snapshot should be created");
  }
  snapshot_compare_controller.setExposure(0.70);
  snapshot_compare_controller.setContrast(24.0);
  const auto snapshot_recipe_before = snapshot_compare_controller.currentAdjustmentPatchJson();
  const auto snapshot_history_before = snapshot_compare_controller.editHistoryEntries();
  const auto snapshot_history_index_before = snapshot_compare_controller.editHistoryCurrentIndex();

  snapshot_compare_controller.setCompareSnapshotId(snapshot_id);
  snapshot_compare_controller.setCompareSourceMode(QStringLiteral("snapshot"));
  snapshot_compare_controller.setCompareMode(true);
  const auto snapshot_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::current_render_request(
          snapshot_compare_controller);
  if (!snapshot_request.compare_mode ||
      !snapshot_request.has_comparison_reference_state ||
      !almost_equal(snapshot_request.comparison_reference_state.exposure, 0.25) ||
      !almost_equal(snapshot_request.comparison_reference_state.contrast, 11.0) ||
      !almost_equal(snapshot_request.state.exposure, 0.70) ||
      snapshot_compare_controller.compareSnapshotName() != QStringLiteral("Color approved")) {
    return fail("named snapshot comparison should render the snapshot recipe against the current edit");
  }

  snapshot_compare_controller.setCompareSourceMode(QStringLiteral("original"));
  const auto original_request =
      photo_editor::desktop::EditorPreviewControllerTestAccess::current_render_request(
          snapshot_compare_controller);
  if (original_request.has_comparison_reference_state ||
      !original_request.reference_file_path.isEmpty() ||
      snapshot_compare_controller.compareSourceMode() != QStringLiteral("original")) {
    return fail("original comparison should bypass snapshot and external reference state");
  }

  snapshot_compare_controller.setCompareSourceMode(QStringLiteral("snapshot"));
  snapshot_compare_controller.setCompareMode(false);
  if (snapshot_compare_controller.currentAdjustmentPatchJson() != snapshot_recipe_before ||
      snapshot_compare_controller.editHistoryEntries() != snapshot_history_before ||
      snapshot_compare_controller.editHistoryCurrentIndex() != snapshot_history_index_before ||
      !almost_equal(snapshot_compare_controller.exposure(), 0.70) ||
      !almost_equal(snapshot_compare_controller.contrast(), 24.0)) {
    return fail("comparison source and mode changes should leave recipe and history hashes unchanged");
  }

  if (!snapshot_compare_controller.deleteEditHistorySnapshot(snapshot_id) ||
      !snapshot_compare_controller.compareSnapshotId().isEmpty() ||
      !snapshot_compare_controller.compareSnapshotName().isEmpty()) {
    return fail("deleting an active comparison snapshot should safely clear the reference");
  }

  return EXIT_SUCCESS;
}
