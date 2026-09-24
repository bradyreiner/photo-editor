#include "history_service.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  photo_editor::desktop::HistoryService service;
  photo_editor::desktop::EditRecipeState first;
  first.exposure = 0.5;
  photo_editor::desktop::EditRecipeState second;
  second.exposure = 1.25;
  {
    photo_editor::desktop::HistoryService rollback;
    rollback.undoStack(QStringLiteral("asset-a")).push_back(second);
    rollback.redoStack(QStringLiteral("asset-a")).push_back(first);
    rollback.beginTransaction(QStringLiteral("asset-a"));
    rollback.captureTransactionUndo(QStringLiteral("asset-a"), first, QStringLiteral("Refine mask"));
    rollback.beginTransaction(QStringLiteral("asset-a"));
    rollback.captureTransactionUndo(QStringLiteral("asset-a"), second, QStringLiteral("Brush stroke"));
    rollback.requestTransactionPersist();
    const auto canceled = rollback.cancelTransaction();
    if (!canceled.outermost || !canceled.undo_state_valid || canceled.undo_state.exposure != first.exposure ||
        canceled.asset_id != QStringLiteral("asset-a") || canceled.persist_requested || rollback.transactionActive() ||
        rollback.undoStackValue(QStringLiteral("asset-a")).size() != 1 ||
        rollback.redoStackValue(QStringLiteral("asset-a")).size() != 1 || rollback.endTransaction().outermost)
      return fail("cancellation must return the initial state and discard nested draft intent without changing history");
    rollback.beginTransaction(QStringLiteral("asset-b"));
    const auto fresh = rollback.endTransaction();
    if (!fresh.outermost || fresh.undo_state_valid || fresh.persist_requested || fresh.asset_id != QStringLiteral("asset-b"))
      return fail("a transaction after cancellation must not inherit the canceled snapshot or save intent");
  }

  service.undoStack(QStringLiteral("asset-a")).push_back(first);
  service.undoLabels(QStringLiteral("asset-a")).push_back(QStringLiteral("Exposure"));
  service.redoStack(QStringLiteral("asset-a")).push_back(second);
  service.redoLabels(QStringLiteral("asset-a")).push_back(QStringLiteral("Contrast"));
  if (service.undoStackValue(QStringLiteral("asset-a")).size() != 1 ||
      service.undoLabelValue(QStringLiteral("asset-a")).front() !=
          QStringLiteral("Exposure") ||
      service.redoStackValue(QStringLiteral("asset-a")).front().exposure != 1.25) {
    return fail("undo and redo state-label pairs must remain asset-scoped");
  }
  service.clearAssetHistory(QStringLiteral("asset-a"));
  if (!service.undoStackValue(QStringLiteral("asset-a")).isEmpty() ||
      !service.redoLabelValue(QStringLiteral("asset-a")).isEmpty()) {
    return fail("clearing an asset must remove all four history collections");
  }

  service.snapshotStack(QStringLiteral("asset-a")).push_back(
      photo_editor::desktop::StoredEditHistorySnapshot{
          .id = QStringLiteral("snapshot-a"),
          .name = QStringLiteral("Look A"),
          .created_at = QStringLiteral("2026-08-04T00:00:00.000Z"),
          .state = first,
      });
  if (service.snapshotValue(QStringLiteral("asset-a")).front().state.exposure != 0.5) {
    return fail("named snapshot state must remain independent from undo history");
  }
  service.removeSnapshotAsset(QStringLiteral("asset-a"));
  if (!service.snapshotValue(QStringLiteral("asset-a")).isEmpty()) {
    return fail("snapshot deletion must remove empty asset ownership");
  }

  service.setCachedState(QStringLiteral("asset-a"), first);
  service.setCachedState(QStringLiteral("asset-b"), second);
  static_cast<void>(service.touchAsset(QStringLiteral("asset-a"), 2));
  static_cast<void>(service.touchAsset(QStringLiteral("asset-b"), 2));
  service.setCachedState(QStringLiteral("asset-c"), first);
  const auto evicted = service.touchAsset(QStringLiteral("asset-c"), 2);
  if (evicted != QStringList{QStringLiteral("asset-a")} ||
      service.hasCachedState(QStringLiteral("asset-a")) ||
      !service.hasCachedState(QStringLiteral("asset-b")) ||
      !service.hasCachedState(QStringLiteral("asset-c"))) {
    return fail("asset access order must evict cached state and history together");
  }

  if (service.applyingState()) {
    return fail("history application guard must start inactive");
  }
  service.beginApplyingState();
  if (!service.applyingState()) {
    return fail("history application guard must suppress recursive undo capture");
  }
  service.endApplyingState();

  service.beginTransaction(QStringLiteral("asset-b"));
  service.beginTransaction(QStringLiteral("asset-b"));
  service.captureTransactionUndo(
      QStringLiteral("asset-b"), first, QStringLiteral("Slider drag"));
  service.requestTransactionPersist();
  const auto nested = service.endTransaction();
  if (nested.outermost || !service.transactionActive()) {
    return fail("nested edit completion must not publish transaction state early");
  }
  const auto completed = service.endTransaction();
  if (!completed.outermost || !completed.undo_state_valid ||
      completed.asset_id != QStringLiteral("asset-b") ||
      completed.undo_label != QStringLiteral("Slider drag") ||
      completed.undo_state.exposure != 0.5 || !completed.persist_requested ||
      service.transactionActive()) {
    return fail("outer transaction completion must atomically return undo and persistence intent");
  }

  return EXIT_SUCCESS;
}
