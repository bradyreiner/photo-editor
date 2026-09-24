#pragma once

#include "edit_recipe_state.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace photo_editor::desktop {

struct StoredEditHistorySnapshot final {
  QString id;
  QString name;
  QString created_at;
  EditRecipeState state;
};

using StoredEditHistorySnapshots =
    QHash<QString, QVector<StoredEditHistorySnapshot>>;

[[nodiscard]] StoredEditHistorySnapshots
load_edit_history_snapshots(const QString& file_path);

bool save_edit_history_snapshots(
    const QString& file_path,
    const StoredEditHistorySnapshots& snapshots);

}  // namespace photo_editor::desktop
