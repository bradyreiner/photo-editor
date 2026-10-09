#pragma once

#include "edit_recipe_state.h"
#include "history_payload_store.h"

namespace photo_editor::desktop {

// An immutable history revision with large binary values held by payload handles.
// No implicit conversion exposes an incomplete recipe. Restore must succeed in
// full before a caller replaces the active edit or advances undo/redo stacks.
class HistoryStateRecord final {
 public:
  // Worker-only: may hash and spill. A spill failure keeps a usable record in
  // RAM and reports warning. A capture failure returns no record.
  [[nodiscard]] static std::optional<HistoryStateRecord> capture(
      const EditRecipeState& state, HistoryPayloadStore& store,
      QString* warning = nullptr, qsizetype minimum_payload_bytes = 64 * 1024);
  [[nodiscard]] std::optional<EditRecipeState> restore(QString* error = nullptr) const;
  [[nodiscard]] qsizetype payloadCount() const { return payloads_.size(); }

 private:
  struct PathStep {
    enum class Kind { List, Map, Hash };
    Kind kind;
    qsizetype index = 0;
    QString key;
  };
  struct Payload {
    qsizetype field;
    QVector<PathStep> path;
    HistoryPayloadHandle handle;
  };
  EditRecipeState metadata_;
  QVector<Payload> payloads_;
};

}  // namespace photo_editor::desktop
