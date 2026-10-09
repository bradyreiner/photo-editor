#pragma once

#include "edit_recipe_state.h"
#include <QHash>
#include <QStringList>
#include <optional>

namespace photo_editor::desktop {

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
    if (success) removeRejected(active_->asset_id);
    active_.reset();
  }

  // Validation failures cannot succeed on retry. Keep their exact edit snapshot
  // in the durable recovery section and let unrelated assets continue saving.
  void rejectActive() {
    if (!active_) return;
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
