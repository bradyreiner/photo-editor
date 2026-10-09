#pragma once

#include "history_state_record.h"
#include <QThreadPool>
#include <memory>
#include <functional>

namespace photo_editor::desktop {
struct HistoryRevisionData;
struct HistoryStorageData;

class HistoryRevision final {
 public:
  HistoryRevision();
  HistoryRevision(const EditRecipeState& state);
  [[nodiscard]] quint64 id() const;
  [[nodiscard]] bool hasLargePayload() const;
  [[nodiscard]] qint64 payloadBytes() const;
  // Never performs disk I/O or waits for a spill. Empty means use a worker.
  [[nodiscard]] std::optional<EditRecipeState> residentState() const;
  [[nodiscard]] std::optional<EditRecipeState> restore(QString* error = nullptr) const;
  void unpin() const;

 private:
  friend class HistoryStorage;
  std::shared_ptr<HistoryRevisionData> data_;
};

// One capture/prefetch worker per controller, with weak queued revision handles.
// Dropped undo branches do not keep queued payloads alive. Shutdown discards queued
// work and waits only for the active operation; saved edit recovery is separate.
class HistoryStorage final {
 public:
  HistoryStorage(QString directory, qint64 budget, std::function<void(const QString&)> warning = {});
  ~HistoryStorage();
  [[nodiscard]] HistoryRevision retain(const EditRecipeState& state);
  void pin(const HistoryRevision& revision);
  void waitForIdle();
  void collectUnused();
  [[nodiscard]] QVariantMap statistics() const;

 private:
  std::shared_ptr<HistoryStorageData> data_;
  QThreadPool pool_;
};
}  // namespace photo_editor::desktop
