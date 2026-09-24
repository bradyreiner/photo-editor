#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

#include <QCoreApplication>
#include <QTemporaryDir>

#include "editor_preview_controller.h"

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static bool strandedFullRequestArmsDispatch(
      EditorPreviewController* controller) {
    controller->pending_full_request_ = RenderRequest{};
    controller->full_request_pending_ = true;
    controller->updateProcessingState();
    const auto dispatch_armed =
        controller->render_scheduler_.pendingDispatchActive();
    controller->full_request_pending_ = false;
    controller->render_scheduler_.stopPendingDispatch();
    controller->updateProcessingState();
    return dispatch_armed && !controller->processing();
  }
};

}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void load_history_asset(photo_editor::desktop::EditorPreviewController* controller) {
  controller->loadAsset(
      QStringLiteral("human-history-25-action-asset"),
      QString(),
      QString(),
      QStringLiteral("History Camera"),
      QString(),
      QString(),
      QString());
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir app_data;
  if (!app_data.isValid()) {
    return fail("history proof should create isolated app data");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data.path().toUtf8());

  photo_editor::desktop::EditorPreviewController controller;
  if (!photo_editor::desktop::EditorPreviewControllerTestAccess::
           strandedFullRequestArmsDispatch(&controller)) {
    return fail(
        "an idle pending full request should re-arm dispatch and clear cleanly");
  }
  load_history_asset(&controller);
  std::vector<std::function<void()>> actions{
      [&]() {
        controller.beginCoalescedEdit();
        controller.setExposure(0.10);
        controller.setExposure(0.22);
        controller.setExposure(0.35);
        controller.endCoalescedEdit();
      },
      [&]() { controller.setContrast(0.18); },
      [&]() { controller.setHighlights(-0.24); },
      [&]() { controller.setShadows(0.31); },
      [&]() { controller.setWhites(0.12); },
      [&]() { controller.setBlacks(-0.16); },
      [&]() { controller.setTemperature(22.0); },
      [&]() { controller.setTint(-9.0); },
      [&]() { controller.setVibrance(18.0); },
      [&]() { controller.setSaturation(7.0); },
      [&]() { controller.setTexture(0.14); },
      [&]() { controller.setClarity(0.19); },
      [&]() { controller.setDehaze(0.08); },
      [&]() { controller.setSharpening(0.42); },
      [&]() { controller.setLensDistortion(0.11); },
      [&]() { controller.setChromaticAberration(0.07); },
      [&]() { controller.setCropPreset(QStringLiteral("4x5")); },
      [&]() { controller.setCropZoom(1.14); },
      [&]() {
        static_cast<void>(controller.addFillLayer(
            QStringLiteral("#32485c"), QStringLiteral("Backdrop")));
      },
      [&]() {
        static_cast<void>(controller.renamePhotoLayer(
            0, QStringLiteral("Cool Backdrop")));
      },
      [&]() { static_cast<void>(controller.setPhotoLayerOpacity(0, 0.72)); },
      [&]() { controller.addMask(QStringLiteral("linear")); },
      [&]() { controller.setActiveMaskExposure(0.48); },
      [&]() { controller.setActiveMaskInverted(true); },
      [&]() { controller.startRemoveBrush(); },
  };
  if (actions.size() != 25U) {
    return fail("history proof should declare exactly 25 mixed actions");
  }

  std::vector<QString> recipes;
  recipes.reserve(actions.size() + 1U);
  recipes.push_back(controller.currentAdjustmentPatchJson());
  QString snapshot_id;
  QString snapshot_recipe;
  for (std::size_t action_index = 0; action_index < actions.size(); ++action_index) {
    actions.at(action_index)();
    recipes.push_back(controller.currentAdjustmentPatchJson());
    const auto expected_entry_count = static_cast<int>(action_index) + 2;
    if (controller.editHistoryEntries().size() != expected_entry_count) {
      return fail("each mixed action should add one chronological history entry");
    }
    if (action_index == 9U) {
      snapshot_recipe = controller.currentAdjustmentPatchJson();
      snapshot_id = controller.createEditHistorySnapshot(QStringLiteral("Color approved"));
      if (snapshot_id.isEmpty() || controller.editHistorySnapshots().size() != 1 ||
          controller.editHistoryEntries().size() != expected_entry_count) {
        return fail("a named snapshot should persist without adding a recipe step");
      }
    }
  }

  const auto entries = controller.editHistoryEntries();
  if (entries.size() != 26 || controller.editHistoryCurrentIndex() != 25) {
    return fail("25 actions should expose Original plus 25 entries at the current tip");
  }
  for (int entry_index = 1; entry_index < entries.size(); ++entry_index) {
    const auto entry = entries.at(entry_index).toMap();
    const auto label = entry.value(QStringLiteral("label")).toString();
    const auto detail = entry.value(QStringLiteral("detail")).toString();
    const auto scope = entry.value(QStringLiteral("scope")).toString();
    if (label.isEmpty() || label == QStringLiteral("Edit change") ||
        label.contains(QLatin1Char('_')) || detail.isEmpty() ||
        detail.contains(QStringLiteral("<missing>")) ||
        detail.contains(QStringLiteral("{object:")) ||
        detail.contains(QStringLiteral("[array:")) ||
        detail.contains(QStringLiteral("audit"), Qt::CaseInsensitive) ||
        detail.contains(QStringLiteral("changed adjustment"), Qt::CaseInsensitive) ||
        scope.isEmpty()) {
      return fail("history entries should expose user labels, values or summaries, and scope without implementation text");
    }
  }

  const auto count_before_preview_actions = controller.editHistoryEntries().size();
  controller.setCompareMode(true);
  controller.setCompareMode(false);
  controller.setMaskPreviewMode(QStringLiteral("outline"));
  controller.setMaskPreviewMode(QStringLiteral("overlay"));
  if (controller.editHistoryEntries().size() != count_before_preview_actions) {
    return fail("preview-only compare and mask-display actions should not mutate history");
  }

  for (int action_index = 24; action_index >= 0; --action_index) {
    controller.undo();
    if (controller.currentAdjustmentPatchJson() != recipes.at(static_cast<std::size_t>(action_index))) {
      return fail("undo should reconstruct every expected recipe exactly");
    }
  }
  for (std::size_t action_index = 1; action_index < recipes.size(); ++action_index) {
    controller.redo();
    if (controller.currentAdjustmentPatchJson() != recipes.at(action_index)) {
      return fail("redo should reconstruct every expected recipe exactly");
    }
  }
  if (!controller.restoreEditHistorySnapshot(snapshot_id) ||
      controller.currentAdjustmentPatchJson() != snapshot_recipe ||
      controller.undoLabel() != QStringLiteral("Restore snapshot “Color approved”")) {
    return fail("snapshot restore should recover its exact recipe as an undoable step");
  }

  controller.loadAsset(
      QStringLiteral("human-history-other-asset"),
      QString(), QString(), QString(), QString(), QString(), QString());
  if (controller.editHistoryEntries().size() != 1) {
    return fail("asset switches should isolate the visible session history");
  }
  load_history_asset(&controller);
  if (controller.editHistoryEntries().size() < 2 || controller.editHistorySnapshots().size() != 1) {
    return fail("switching back should restore the asset session history and snapshots");
  }

  photo_editor::desktop::EditorPreviewController reloaded;
  load_history_asset(&reloaded);
  const auto reloaded_snapshots = reloaded.editHistorySnapshots();
  if (reloaded_snapshots.size() != 1 ||
      reloaded_snapshots.constFirst().toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Color approved") ||
      !reloaded.restoreEditHistorySnapshot(snapshot_id) ||
      reloaded.currentAdjustmentPatchJson() != snapshot_recipe) {
    return fail("named snapshots should survive relaunch and restore the exact recipe");
  }

  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
