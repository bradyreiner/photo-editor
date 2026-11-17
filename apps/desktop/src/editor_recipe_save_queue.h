#pragma once

#include "edit_recipe_state.h"
#include <QHash>
#include <QStringList>
#include <optional>

namespace photo_editor::desktop {

inline bool recipe_save_targets_missing_catalog_asset(const QString& diagnostic) {
  return diagnostic.contains(QStringLiteral("foreign key constraint")) &&
      (diagnostic.contains(QStringLiteral("edit_recipes_asset_id_fkey")) ||
       diagnostic.contains(QStringLiteral("canonical_edit_recipes_v13_asset_id_fkey")));
}

inline bool recipe_save_targets_canonical_recipe_conflict(const QString& diagnostic) {
  return diagnostic.contains(QStringLiteral(
      "legacy save cannot overwrite an asset already migrated to canonical V13"));
}

inline bool recipe_save_is_canonical_conflict(const QString& reason) {
  return reason == QStringLiteral("legacy_recipe_conflicts_with_canonical_catalog") ||
      reason == QStringLiteral("legacy_renderer_conflicts_with_canonical_catalog");
}

struct EditorRecipeSaveSnapshot final {
  QString asset_id;
  QString file_path;
  EditRecipeState state;
  QVariantMap migration_evidence;
  // Attribution travels with the immutable revision, including durable retries.
  // An ordinary/manual save never inherits the currently active agent task.
  QString agent_task_id;
};

struct EditorRecipeSaveCheckpoint final {
  QList<EditorRecipeSaveSnapshot> pending;
  QList<EditorRecipeSaveSnapshot> rejected;
  std::optional<EditorRecipeSaveSnapshot> active;
  std::optional<EditorRecipeSaveSnapshot> retry_migration;
};

// One immutable in-flight snapshot and the newest pending edit per asset.
// A delayed save must never consult the currently selected document.
class EditorRecipeSaveQueue final {
 public:
  void enqueue(EditorRecipeSaveSnapshot snapshot) {
    if (snapshot.asset_id.isEmpty()) return;
    if (!pending_.contains(snapshot.asset_id)) order_.append(snapshot.asset_id);
    pending_.insert(snapshot.asset_id, std::move(snapshot));
  }

  bool supersede(EditorRecipeSaveSnapshot snapshot) {
    if (!pending_.contains(snapshot.asset_id) && activeAssetId() != snapshot.asset_id &&
        (!retry_migration_ || retry_migration_->asset_id != snapshot.asset_id)) return false;
    enqueue(std::move(snapshot));
    return true;
  }

  const EditorRecipeSaveSnapshot* begin() {
    if (active_) return nullptr;
    if (retry_migration_) {
      active_ = std::move(retry_migration_);
      retry_migration_.reset();
    } else if (!order_.isEmpty()) {
      active_ = pending_.take(order_.takeFirst());
    } else {
      return nullptr;
    }
    return &*active_;
  }

  void complete(bool success) {
    if (!active_) return;
    // A failed old revision must not replace a newer edit queued during save.
    if (!success && active_->migration_evidence.value(QStringLiteral("migrationPending")).toBool()) {
      // Approval evidence is bound to an exact recipe. Save that transaction
      // before later edits; never attach its proof to a different recipe.
      retry_migration_ = *active_;
    } else if (!success && !pending_.contains(active_->asset_id)) {
      enqueue(*active_);
    }
    if (success) {
      const auto* recovery = recoverySnapshot(active_->asset_id);
      // Saving a newer photo revision does not acknowledge a different,
      // incompatible older copy. Keep it until explicitly archived.
      if (!recovery || !recipe_save_is_canonical_conflict(
              recovery->migration_evidence.value(QStringLiteral("saveRecoveryReason")).toString()))
        removeRejected(active_->asset_id);
    }
    active_.reset();
  }

  // Validation failures cannot succeed on retry. Keep their exact edit snapshot
  // in the durable recovery section and let unrelated assets continue saving.
  void rejectActive(const QString& recovery_reason = {}) {
    if (!active_) return;
    if (!recovery_reason.isEmpty())
      active_->migration_evidence.insert(QStringLiteral("saveRecoveryReason"), recovery_reason);
    const auto asset_id = active_->asset_id;
    if (!pending_.contains(asset_id)) {
      if (!rejected_.contains(asset_id)) rejected_order_.append(asset_id);
      rejected_.insert(asset_id, *active_);
    }
    active_.reset();
  }

  bool pending() const { return retry_migration_.has_value() || !order_.isEmpty(); }
  bool active() const { return active_.has_value(); }
  bool hasRejected() const { return !rejected_order_.isEmpty(); }
  QStringList recoveryAssetIds() const { return rejected_order_; }
  const EditorRecipeSaveSnapshot* recoverySnapshot(const QString& asset_id) const {
    const auto found = rejected_.constFind(asset_id);
    return found == rejected_.cend() ? nullptr : &found.value();
  }
  bool canArchiveRecovery(const QString& asset_id) const {
    const auto* snapshot = recoverySnapshot(asset_id);
    return snapshot && snapshot->agent_task_id.isEmpty() &&
        recipe_save_is_canonical_conflict(
            snapshot->migration_evidence.value(QStringLiteral("saveRecoveryReason")).toString()) &&
        !pending_.contains(asset_id) && activeAssetId() != asset_id &&
        (!retry_migration_ || retry_migration_->asset_id != asset_id);
  }
  // Caller must durably archive the exact snapshot and checkpoint its removal first.
  bool removeArchivedRecovery(const QString& asset_id) {
    if (!canArchiveRecovery(asset_id)) return false;
    removeRejected(asset_id);
    return true;
  }
  [[nodiscard]] bool hasMatchingLegacyRendererSnapshot(
      const QString& asset_id, const EditRecipeState& state) const {
    const auto migration_pending = [&asset_id](const EditorRecipeSaveSnapshot& snapshot) {
      return snapshot.asset_id == asset_id &&
          snapshot.migration_evidence.value(QStringLiteral("migrationPending")).toBool();
    };
    if ((active_ && migration_pending(*active_)) ||
        (retry_migration_ && migration_pending(*retry_migration_))) return false;
    const auto pending_for_asset = pending_.constFind(asset_id);
    if (pending_for_asset != pending_.cend() && migration_pending(pending_for_asset.value())) return false;
    const auto matches = [&](const EditorRecipeSaveSnapshot& snapshot) {
      return snapshot.asset_id == asset_id && snapshot.state == state &&
          snapshot.state.photographic_rendering_model ==
              QStringLiteral("legacy-oklch-v1") &&
          !snapshot.migration_evidence.value(QStringLiteral("migrationPending")).toBool();
    };
    const auto pending = pending_.constFind(asset_id);
    if (pending != pending_.cend() && matches(pending.value())) return true;
    const auto rejected = rejected_.constFind(asset_id);
    if (rejected != rejected_.cend() && matches(rejected.value())) return true;
    return (active_ && matches(*active_)) ||
        (retry_migration_ && matches(*retry_migration_));
  }
  bool quarantinePendingLegacyRendererSnapshot(
      const QString& asset_id, const EditRecipeState& state, const QString& reason) {
    // An active older revision may fail while a newer same-asset pending
    // revision is present. Leave that newer snapshot queued so rejectActive()
    // cannot replace it with the older recovery record.
    if (activeAssetId() == asset_id) return false;
    auto pending = pending_.find(asset_id);
    if (pending == pending_.end() || pending.value().state != state ||
        pending.value().state.photographic_rendering_model !=
            QStringLiteral("legacy-oklch-v1") ||
        pending.value().migration_evidence.value(QStringLiteral("migrationPending")).toBool()) {
      return false;
    }
    auto snapshot = pending.value();
    pending_.erase(pending);
    order_.removeAll(asset_id);
    snapshot.migration_evidence.insert(QStringLiteral("saveRecoveryReason"), reason);
    if (!rejected_.contains(asset_id)) rejected_order_.append(asset_id);
    rejected_.insert(asset_id, std::move(snapshot));
    return true;
  }
  bool retryRecovery(const QString& asset_id, const QString& source_path) {
    const auto* snapshot = recoverySnapshot(asset_id);
    if (!snapshot || pending_.contains(asset_id) || activeAssetId() == asset_id ||
        (retry_migration_ && retry_migration_->asset_id == asset_id)) return false;
    auto retry = *snapshot;
    retry.file_path = source_path;
    enqueue(std::move(retry));
    // Keep the original recovery snapshot until the catalog acknowledges it.
    return true;
  }
  QString activeAssetId() const { return active_ ? active_->asset_id : QString{}; }

  EditorRecipeSaveCheckpoint checkpoint() const {
    EditorRecipeSaveCheckpoint result;
    for (const auto& id : order_) result.pending.append(pending_.value(id));
    for (const auto& id : rejected_order_) result.rejected.append(rejected_.value(id));
    result.active = active_;
    result.retry_migration = retry_migration_;
    return result;
  }

  void restore(const EditorRecipeSaveCheckpoint& checkpoint) {
    pending_.clear();
    order_.clear();
    rejected_.clear();
    rejected_order_.clear();
    for (const auto& snapshot : checkpoint.pending) enqueue(snapshot);
    for (const auto& snapshot : checkpoint.rejected) {
      if (snapshot.asset_id.isEmpty()) continue;
      if (!rejected_.contains(snapshot.asset_id)) rejected_order_.append(snapshot.asset_id);
      rejected_.insert(snapshot.asset_id, snapshot);
    }
    active_ = checkpoint.active;
    retry_migration_ = checkpoint.retry_migration;
    // A crashed helper may have committed, but did not acknowledge locally.
    // Retry using the same ordering rules as a failed live helper.
    complete(false);
    // A crash can leave an unacknowledged duplicate of an already classified
    // incompatible revision. It remains a backup rather than a new save retry.
    for (const auto& id : rejected_order_) {
      const auto snapshot = rejected_.value(id);
      const auto reason = snapshot.migration_evidence.value(QStringLiteral("saveRecoveryReason")).toString();
      if (recipe_save_is_canonical_conflict(reason))
        quarantinePendingLegacyRendererSnapshot(id, snapshot.state, reason);
    }
  }

 private:
  void removeRejected(const QString& asset_id) {
    rejected_.remove(asset_id);
    rejected_order_.removeAll(asset_id);
  }

  QHash<QString, EditorRecipeSaveSnapshot> pending_;
  QStringList order_;
  QHash<QString, EditorRecipeSaveSnapshot> rejected_;
  QStringList rejected_order_;
  std::optional<EditorRecipeSaveSnapshot> active_;
  std::optional<EditorRecipeSaveSnapshot> retry_migration_;
};

}  // namespace photo_editor::desktop
