#include "editor_recipe_save_queue.h"
#include <QCoreApplication>
#include <iostream>
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (!recipe_save_targets_missing_catalog_asset(
          "insert or update violates foreign key constraint \"edit_recipes_asset_id_fkey\"") ||
      recipe_save_targets_missing_catalog_asset("connection refused") ||
      recipe_save_targets_missing_catalog_asset("violates foreign key constraint \"some_other_relation_fkey\"")) return 1;
  if (!recipe_save_targets_canonical_recipe_conflict(
          "Catalog helper failed: legacy save cannot overwrite an asset already migrated to canonical V13") ||
      recipe_save_targets_canonical_recipe_conflict("connection refused") ||
      recipe_save_targets_canonical_recipe_conflict("invalid canonical recipe")) return 1;
  EditorRecipeSaveQueue board_recovery;
  EditorRecipeSaveSnapshot board{.asset_id = "photo", .file_path = "/photo.png"};
  board.state.photographic_rendering_model = "legacy-oklch-v1";
  board.state.exposure = 2;
  board_recovery.enqueue(board);
  board_recovery.enqueue({.asset_id = "other", .file_path = "/other.png"});
  if (!board_recovery.hasMatchingLegacyRendererSnapshot("photo", board.state) ||
      !board_recovery.quarantinePendingLegacyRendererSnapshot("photo", board.state, "renderer_conflict") ||
      board_recovery.checkpoint().rejected.size() != 1 ||
      board_recovery.checkpoint().rejected.first().state != board.state ||
      !board_recovery.begin() || board_recovery.activeAssetId() != "other") return 1;
  board_recovery.complete(true);
  // The synthetic renderer_conflict reason above is deliberately unclassified.
  if (board_recovery.canArchiveRecovery("photo") || board_recovery.canArchiveRecovery("unknown")) return 1;
  EditorRecipeSaveQueue archive_guard;
  archive_guard.enqueue(board);
  if (!archive_guard.begin()) return 1;
  archive_guard.rejectActive("legacy_recipe_conflicts_with_canonical_catalog");
  if (!archive_guard.canArchiveRecovery("photo")) return 1;
  auto saved_photo = board;
  saved_photo.state.photographic_rendering_model = "aces2-ocio-v1";
  saved_photo.state.exposure = 0.45;
  archive_guard.enqueue(saved_photo);
  if (archive_guard.canArchiveRecovery("photo") || archive_guard.removeArchivedRecovery("photo") ||
      !archive_guard.begin() || archive_guard.canArchiveRecovery("photo")) return 1;
  archive_guard.complete(true);
  if (!archive_guard.hasRejected() || !archive_guard.canArchiveRecovery("photo") ||
      archive_guard.recoverySnapshot("photo")->state != board.state ||
      !archive_guard.removeArchivedRecovery("photo") || archive_guard.hasRejected()) return 1;
  EditorRecipeSaveCheckpoint interrupted;
  auto classified_board = board;
  classified_board.migration_evidence.insert("saveRecoveryReason", "legacy_recipe_conflicts_with_canonical_catalog");
  interrupted.rejected.append(classified_board);
  interrupted.active = board;
  EditorRecipeSaveQueue restarted_conflict;
  restarted_conflict.restore(interrupted);
  if (restarted_conflict.pending() || !restarted_conflict.canArchiveRecovery("photo")) return 1;
  // A different, newer canonical revision remains queued across the same crash.
  interrupted.pending.append(saved_photo);
  restarted_conflict.restore(interrupted);
  if (!restarted_conflict.pending() || restarted_conflict.canArchiveRecovery("photo") ||
      !restarted_conflict.begin() || restarted_conflict.checkpoint().active->state != saved_photo.state) return 1;
  EditorRecipeSaveQueue active_conflict;
  active_conflict.enqueue(board);
  if (!active_conflict.begin()) return 1;
  auto newer_board = board;
  newer_board.state.exposure = 3;
  active_conflict.enqueue(newer_board);
  if (active_conflict.quarantinePendingLegacyRendererSnapshot("photo", newer_board.state, "renderer_conflict")) return 1;
  active_conflict.rejectActive("renderer_conflict");
  if (!active_conflict.begin() || active_conflict.checkpoint().active->state.exposure != 3) return 1;
  active_conflict.rejectActive("renderer_conflict");
  if (active_conflict.checkpoint().rejected.first().state.exposure != 3) return 1;
  EditorRecipeSaveQueue migration;
  auto migrating = board;
  migrating.state.photographic_rendering_model = "aces2-ocio-v1";
  migrating.migration_evidence.insert("migrationPending", true);
  migration.enqueue(migrating);
  if (!migration.begin()) return 1;
  migration.enqueue(board);
  if (migration.hasMatchingLegacyRendererSnapshot("photo", board.state) ||
      migration.quarantinePendingLegacyRendererSnapshot("photo", board.state, "renderer_conflict")) return 1;
  EditorRecipeSaveQueue missing_source;
  EditorRecipeSaveSnapshot orphan{.asset_id = "missing", .file_path = "/Volumes/Offline/photo.CR2"};
  orphan.state.exposure = 1.25;
  missing_source.enqueue(orphan);
  missing_source.enqueue(EditorRecipeSaveSnapshot{.asset_id = "present", .file_path = "/present.png"});
  if (!missing_source.begin()) return 1;
  missing_source.rejectActive();
  EditorRecipeSaveQueue restored_source;
  restored_source.restore(missing_source.checkpoint());
  if (!restored_source.hasRejected() || restored_source.checkpoint().rejected.first().state.exposure != 1.25 ||
      restored_source.checkpoint().rejected.first().file_path != orphan.file_path ||
      !restored_source.begin() || restored_source.activeAssetId() != "present") return 1;
  restored_source.complete(true);
  if (restored_source.pending() || !restored_source.hasRejected()) return 1;
  if (!restored_source.retryRecovery("missing", "/Recovered/photo.CR2") ||
      restored_source.retryRecovery("missing", "/must-not-overwrite.CR2") ||
      !restored_source.hasRejected()) return 1;
  EditorRecipeSaveQueue recovered_again;
  recovered_again.restore(restored_source.checkpoint());
  const auto* recovered_edit = recovered_again.begin();
  if (!recovered_edit || recovered_edit->file_path != "/Recovered/photo.CR2" ||
      recovered_edit->state.exposure != 1.25 || !recovered_again.hasRejected()) return 1;
  recovered_again.complete(true);
  if (recovered_again.hasRejected() || recovered_again.pending()) return 1;
  // A new user revision takes precedence over an older recovery snapshot.
  restored_source.enqueue(EditorRecipeSaveSnapshot{.asset_id = "missing", .file_path = "/new-edit.CR2"});
  if (restored_source.retryRecovery("missing", "/old-edit.CR2")) return 1;
  EditorRecipeSaveQueue queue;
  EditorRecipeSaveSnapshot a{.asset_id = "a", .file_path = "/a.dng"};
  a.state.exposure = 1;
  queue.enqueue(a);
  a.state.exposure = 2;
  queue.enqueue(a);
  auto b = a; b.asset_id = "b"; b.file_path = "/b.jpg";
  queue.enqueue(b);
  const auto* active = queue.begin();
  if (!active || active->asset_id != "a" || active->file_path != "/a.dng" || active->state.exposure != 2 || queue.begin()) return 1;
  a.state.exposure = 3;
  queue.enqueue(a);
  if (active->state.exposure != 2) return 1;
  queue.complete(false);
  active = queue.begin();
  if (!active || active->asset_id != "b") return 1;
  queue.complete(true);
  active = queue.begin();
  if (!active || active->asset_id != "a" || active->state.exposure != 3) return 1;
  queue.complete(false);
  active = queue.begin();
  if (!active || active->state.exposure != 3) return 1;
  queue.complete(true);
  if (queue.pending() || queue.active() || queue.begin()) return 1;
  a.migration_evidence.insert("migrationPending", true);
  queue.enqueue(a);
  if (!queue.begin()) return 1;
  a.state.exposure = 4;
  a.migration_evidence.clear();
  queue.enqueue(a);
  queue.complete(false);
  active = queue.begin();
  if (!active || active->state.exposure != 3 || !active->migration_evidence.value("migrationPending").toBool()) return 1;
  queue.complete(true);
  active = queue.begin();
  if (!active || active->state.exposure != 4 || !active->migration_evidence.isEmpty()) return 1;
  queue.complete(true);
  // Recovery treats an unacknowledged ordinary save as a failure without
  // resurrecting it over the newer pending revision of the same asset.
  queue.enqueue(a);
  if (!queue.begin()) return 1;
  a.state.exposure = 5;
  queue.enqueue(a);
  EditorRecipeSaveQueue recovered;
  recovered.restore(queue.checkpoint());
  active = recovered.begin();
  if (!active || active->state.exposure != 5 || recovered.begin()) return 1;
  recovered.complete(true);
  if (recovered.pending() || recovered.active()) return 1;
  // A paste supersedes an older queued/in-flight save without mutating the
  // immutable active snapshot or re-enqueuing assets with no outstanding save.
  EditorRecipeSaveQueue pasted;
  if (pasted.supersede(a)) return 1;
  pasted.enqueue(a);
  const auto* old_revision = pasted.begin();
  auto newer = a;
  newer.state.exposure = 8;
  if (!pasted.supersede(newer) || old_revision->state.exposure != 5) return 1;
  newer.state.exposure = 9;
  if (!pasted.supersede(newer)) return 1;
  pasted.complete(false);
  const auto* pasted_revision = pasted.begin();
  if (!pasted_revision || pasted_revision->state.exposure != 9) return 1;
  pasted.complete(true);
  if (pasted.pending() || pasted.active()) return 1;
  // A newer manual revision must not inherit ownership from the active agent
  // save, including when an older helper fails or the outbox is reopened.
  EditorRecipeSaveQueue attributed;
  a.agent_task_id = QStringLiteral("task-a");
  attributed.enqueue(a);
  const auto* owned = attributed.begin();
  auto manual = a;
  manual.agent_task_id.clear();
  manual.state.exposure = 10;
  attributed.enqueue(manual);
  if (!owned || owned->agent_task_id != QStringLiteral("task-a")) return 1;
  attributed.complete(false);
  EditorRecipeSaveQueue attributed_recovered;
  attributed_recovered.restore(attributed.checkpoint());
  const auto* recovered_manual = attributed_recovered.begin();
  if (!recovered_manual || !recovered_manual->agent_task_id.isEmpty() ||
      recovered_manual->state.exposure != 10) return 1;
  std::cout << "Asset-bound coalescing, immutable active revision, failure ordering and retry passed.\n";
}
