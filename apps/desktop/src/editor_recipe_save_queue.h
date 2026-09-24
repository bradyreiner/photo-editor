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
};

struct EditorRecipeSaveCheckpoint final {
  QList<EditorRecipeSaveSnapshot> pending;
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
    active_.reset();
  }

  bool pending() const { return retry_migration_.has_value() || !order_.isEmpty(); }
  bool active() const { return active_.has_value(); }
  QString activeAssetId() const { return active_ ? active_->asset_id : QString{}; }

  EditorRecipeSaveCheckpoint checkpoint() const {
    EditorRecipeSaveCheckpoint result;
    for (const auto& id : order_) result.pending.append(pending_.value(id));
    result.active = active_;
    result.retry_migration = retry_migration_;
    return result;
  }

  void restore(const EditorRecipeSaveCheckpoint& checkpoint) {
    pending_.clear();
    order_.clear();
    for (const auto& snapshot : checkpoint.pending) enqueue(snapshot);
    active_ = checkpoint.active;
    retry_migration_ = checkpoint.retry_migration;
    // A crashed helper may have committed, but did not acknowledge locally.
    // Retry using the same ordering rules as a failed live helper.
    complete(false);
  }

 private:
  QHash<QString, EditorRecipeSaveSnapshot> pending_;
  QStringList order_;
  std::optional<EditorRecipeSaveSnapshot> active_;
  std::optional<EditorRecipeSaveSnapshot> retry_migration_;
};

}  // namespace photo_editor::desktop
