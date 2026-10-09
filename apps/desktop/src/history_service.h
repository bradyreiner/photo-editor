#pragma once

#include <utility>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "edit_history_snapshot_store.h"
#include "edit_recipe_state.h"
#include "history_revision.h"

namespace photo_editor::desktop {

class HistoryService final {
 public:
  using State = EditRecipeState;
  using Snapshot = StoredEditHistorySnapshot;
  using SnapshotMap = QHash<QString, QVector<Snapshot>>;

  void configureStorage(const QString& directory, qint64 budget, std::function<void(const QString&)> warning = {}) {
    storage_ = std::make_unique<HistoryStorage>(directory, budget, std::move(warning));
    pin_budget_ = std::max(qint64{0}, budget / 4);
  }
  [[nodiscard]] HistoryRevision retainState(const State& state) {
    auto revision = storage_ ? storage_->retain(state) : HistoryRevision(state);
    if (storage_) pending_pins_.append(revision);
    return revision;
  }
  void refreshHistoryPins(const QString& asset_id) {
    QVector<HistoryRevision> next;
    if (storage_) {
      qint64 bytes = 0;
      const auto undo = undo_states_.value(asset_id);
      const auto redo = redo_states_.value(asset_id);
      for (qsizetype offset = 1; offset <= 2; ++offset) {
        for (const auto& stack : {undo, redo}) {
          if (stack.size() < offset) continue;
          const auto& revision = stack.at(stack.size() - offset);
          if (revision.payloadBytes() > pin_budget_ - bytes) continue;
          next.append(revision);
          bytes += revision.payloadBytes();
        }
      }
    }
    const auto contains = [](const auto& revisions, quint64 id) {
      return std::any_of(revisions.cbegin(), revisions.cend(), [id](const auto& revision) { return revision.id() == id; });
    };
    for (const auto& revision : pending_pins_) if (!contains(next, revision.id())) revision.unpin();
    pending_pins_.clear();
    for (const auto& revision : pinned_) if (!contains(next, revision.id())) revision.unpin();
    if (storage_) for (const auto& revision : next) if (!contains(pinned_, revision.id())) storage_->pin(revision);
    pinned_ = std::move(next);
    if (storage_) storage_->collectUnused();
  }
  void waitForStorage() { if (storage_) storage_->waitForIdle(); }
  [[nodiscard]] QVariantMap storageStatistics() const {
    auto result = storage_ ? storage_->statistics() : QVariantMap{};
    qint64 pinned_bytes = 0;
    for (const auto& revision : pinned_) pinned_bytes += revision.payloadBytes();
    result.insert(QStringLiteral("pinned_payload_bytes"), pinned_bytes);
    result.insert(QStringLiteral("pin_budget_bytes"), pin_budget_);
    return result;
  }

  struct TransactionResult final {
    bool outermost = false;
    bool undo_state_valid = false;
    State undo_state;
    QString undo_label;
    QString asset_id;
    bool persist_requested = false;
  };

  [[nodiscard]] QVector<HistoryRevision>& undoStack(const QString& asset_id) {
    return undo_states_[asset_id];
  }
  [[nodiscard]] QVector<HistoryRevision>& redoStack(const QString& asset_id) {
    return redo_states_[asset_id];
  }
  [[nodiscard]] QVector<QString>& undoLabels(const QString& asset_id) {
    return undo_labels_[asset_id];
  }
  [[nodiscard]] QVector<QString>& redoLabels(const QString& asset_id) {
    return redo_labels_[asset_id];
  }
  [[nodiscard]] const QVector<HistoryRevision> undoStackValue(const QString& asset_id) const {
    return undo_states_.value(asset_id);
  }
  [[nodiscard]] const QVector<HistoryRevision> redoStackValue(const QString& asset_id) const {
    return redo_states_.value(asset_id);
  }
  [[nodiscard]] const QVector<QString> undoLabelValue(const QString& asset_id) const {
    return undo_labels_.value(asset_id);
  }
  [[nodiscard]] const QVector<QString> redoLabelValue(const QString& asset_id) const {
    return redo_labels_.value(asset_id);
  }

  void clearAssetHistory(const QString& asset_id) {
    undo_states_.remove(asset_id);
    redo_states_.remove(asset_id);
    undo_labels_.remove(asset_id);
    redo_labels_.remove(asset_id);
    refreshHistoryPins(access_order_.isEmpty() ? QString{} : access_order_.front());
  }

  [[nodiscard]] QVector<Snapshot>& snapshotStack(const QString& asset_id) {
    return snapshots_[asset_id];
  }
  [[nodiscard]] QVector<Snapshot> snapshotValue(const QString& asset_id) const {
    return snapshots_.value(asset_id);
  }
  void removeSnapshotAsset(const QString& asset_id) { snapshots_.remove(asset_id); }
  void replaceSnapshots(SnapshotMap snapshots) { snapshots_ = std::move(snapshots); }
  [[nodiscard]] const SnapshotMap& snapshots() const { return snapshots_; }

  void setCachedState(QString asset_id, State state) {
    cached_states_.insert(std::move(asset_id), std::move(state));
  }
  [[nodiscard]] bool hasCachedState(const QString& asset_id) const {
    return cached_states_.contains(asset_id);
  }
  [[nodiscard]] State cachedState(const QString& asset_id) const {
    return cached_states_.value(asset_id);
  }

  [[nodiscard]] QStringList touchAsset(const QString& asset_id, int capacity) {
    access_order_.removeOne(asset_id);
    access_order_.prepend(asset_id);
    QStringList evicted;
    while (access_order_.size() > capacity) {
      const auto evicted_id = access_order_.takeLast();
      cached_states_.remove(evicted_id);
      clearAssetHistory(evicted_id);
      evicted.push_back(evicted_id);
    }
    refreshHistoryPins(asset_id);
    return evicted;
  }

  [[nodiscard]] bool applyingState() const { return applying_state_; }
  void beginApplyingState() { applying_state_ = true; }
  void endApplyingState() { applying_state_ = false; }

  void beginTransaction(const QString& asset_id) {
    if (transaction_depth_ == 0) {
      transaction_asset_id_ = asset_id;
      transaction_undo_state_valid_ = false;
      transaction_undo_state_ = {};
      transaction_undo_label_.clear();
      transaction_persist_requested_ = false;
    }
    ++transaction_depth_;
  }

  [[nodiscard]] bool transactionActive() const { return transaction_depth_ > 0; }
  [[nodiscard]] int transactionDepth() const { return transaction_depth_; }

  void captureTransactionUndo(
      const QString& asset_id,
      const State& state,
      const QString& label) {
    if (!transactionActive()) {
      return;
    }
    if (!transaction_undo_state_valid_ &&
        (transaction_asset_id_.isEmpty() || transaction_asset_id_ == asset_id)) {
      transaction_asset_id_ = asset_id;
      transaction_undo_state_ = state;
      transaction_undo_label_ = label;
      transaction_undo_state_valid_ = true;
    } else if (transaction_undo_label_.isEmpty() && !label.isEmpty()) {
      transaction_undo_label_ = label;
    }
  }

  void requestTransactionPersist() {
    if (transactionActive()) {
      transaction_persist_requested_ = true;
    }
  }

  [[nodiscard]] TransactionResult endTransaction() {
    if (transaction_depth_ <= 0) {
      return {};
    }
    --transaction_depth_;
    if (transaction_depth_ > 0) {
      return {};
    }
    TransactionResult result{
        .outermost = true,
        .undo_state_valid = transaction_undo_state_valid_,
        .undo_state = transaction_undo_state_,
        .undo_label = transaction_undo_label_,
        .asset_id = transaction_asset_id_,
        .persist_requested = transaction_persist_requested_,
    };
    transaction_asset_id_.clear();
    transaction_undo_state_ = {};
    transaction_undo_label_.clear();
    transaction_undo_state_valid_ = false;
    transaction_persist_requested_ = false;
    return result;
  }

  // Cancel the whole gesture, including nested control transactions. The caller
  // restores undo_state without pushing it onto history or saving the draft.
  [[nodiscard]] TransactionResult cancelTransaction() {
    if (!transactionActive()) return {};
    transaction_depth_ = 1;
    auto result = endTransaction();
    result.persist_requested = false;
    return result;
  }

 private:
  std::unique_ptr<HistoryStorage> storage_;
  QVector<HistoryRevision> pinned_;
  QVector<HistoryRevision> pending_pins_;
  qint64 pin_budget_ = 0;
  QHash<QString, State> cached_states_;
  QHash<QString, QVector<HistoryRevision>> undo_states_;
  QHash<QString, QVector<HistoryRevision>> redo_states_;
  QHash<QString, QVector<QString>> undo_labels_;
  QHash<QString, QVector<QString>> redo_labels_;
  SnapshotMap snapshots_;
  QList<QString> access_order_;
  bool applying_state_ = false;
  int transaction_depth_ = 0;
  QString transaction_asset_id_;
  State transaction_undo_state_;
  QString transaction_undo_label_;
  bool transaction_undo_state_valid_ = false;
  bool transaction_persist_requested_ = false;
};

}  // namespace photo_editor::desktop
