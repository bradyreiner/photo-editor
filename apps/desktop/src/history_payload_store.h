#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <optional>

namespace photo_editor::desktop {

struct HistoryPayloadStoreState;
struct HistoryPayloadEntry;

// Immutable handle, independent of the store object's lifetime. A failed load
// returns an error; callers must retain their current edit/history position.
class HistoryPayloadHandle final {
 public:
  HistoryPayloadHandle() = default;
  [[nodiscard]] bool valid() const { return static_cast<bool>(entry_); }
  [[nodiscard]] QString contentId() const;
  [[nodiscard]] qint64 size() const;
  [[nodiscard]] std::optional<QByteArray> load(QString* error = nullptr) const;

 private:
  friend class HistoryPayloadStore;
  std::shared_ptr<HistoryPayloadStoreState> store_;
  std::shared_ptr<HistoryPayloadEntry> entry_;
};

// Synchronous, thread-safe storage primitive. Hashing, spill and restore perform
// I/O and MUST be called on a worker when used by the editor. No controller uses
// this primitive yet. The budget covers this store's resident ownership, not
// current edits, callers' returned buffers, OS file cache, or total process RSS.
class HistoryPayloadStore final {
 public:
  HistoryPayloadStore(const QString& parent_directory, qint64 resident_budget_bytes);
  [[nodiscard]] HistoryPayloadHandle retain(const QByteArray& bytes, QString* error = nullptr);
  [[nodiscard]] bool setResidentBudget(qint64 bytes, QString* error = nullptr);
  void collectUnused();
  [[nodiscard]] QVariantMap statistics() const;
  [[nodiscard]] QString directory() const;

 private:
  std::shared_ptr<HistoryPayloadStoreState> state_;
};

}  // namespace photo_editor::desktop
