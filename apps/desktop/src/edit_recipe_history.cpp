#include "edit_recipe_state.h"

#include "edit_adjustment_patch.h"

namespace photo_editor::desktop {

QString normalized_history_branch_id(const QString& branch_id) {
  const auto normalized = branch_id.trimmed();
  return normalized.isEmpty() ? QStringLiteral("main") : normalized;
}

EditRecipeState append_named_recipe_snapshot(
    const EditRecipeState& state,
    const QString& name,
    const QString& branch_id,
    const QString& parent_snapshot_id) {
  EditRecipeState next = state;
  const auto normalized_branch =
      normalized_history_branch_id(branch_id);
  next.active_history_branch = normalized_branch;

  EditRecipeState snapshot_state = state;
  snapshot_state.active_history_branch = normalized_branch;
  snapshot_state.recipe_snapshots.clear();

  const auto snapshot_number = next.recipe_snapshots.size() + 1;
  const auto snapshot_id =
      QStringLiteral("%1-snapshot-%2")
          .arg(normalized_branch)
          .arg(snapshot_number);
  QJsonObject snapshot;
  snapshot.insert(QStringLiteral("id"), snapshot_id);
  snapshot.insert(
      QStringLiteral("name"),
      name.trimmed().isEmpty()
          ? QStringLiteral("Snapshot %1").arg(snapshot_number)
          : name.trimmed());
  snapshot.insert(QStringLiteral("branchId"), normalized_branch);
  snapshot.insert(
      QStringLiteral("parentSnapshotId"),
      parent_snapshot_id.trimmed());
  snapshot.insert(
      QStringLiteral("adjustmentPatch"),
      serialize_adjustment_patch(
          snapshot_state,
          all_adjustment_groups(),
          AdjustmentPatchScope::AssetBound));
  next.recipe_snapshots.append(snapshot.toVariantMap());
  return next;
}

}  // namespace photo_editor::desktop
