#include "edit_recipe_state.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace photo_editor::desktop {

QString edit_recipe_sidecar_path_for_asset(const QString& asset_file_path) {
  const QFileInfo asset_info(asset_file_path);
  if (asset_info.filePath().trimmed().isEmpty()) {
    return {};
  }
  return asset_info.dir().filePath(
      QStringLiteral("%1.photo-editor-recipe.json").arg(asset_info.fileName()));
}

EditRecipeSidecarResult import_edit_recipe_sidecar(
    const QString& asset_file_path) {
  EditRecipeSidecarResult result;
  result.sidecar_path = edit_recipe_sidecar_path_for_asset(asset_file_path);
  if (result.sidecar_path.isEmpty()) {
    result.errors.append(QStringLiteral("asset file path is empty"));
    return result;
  }

  QFile file(result.sidecar_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    result.errors.append(
        QStringLiteral("could not open recipe sidecar for reading"));
    return result;
  }

  const auto migration =
      migrate_edit_recipe_json(QString::fromUtf8(file.readAll()));
  if (!migration.errors.isEmpty()) {
    result.errors = migration.errors;
    return result;
  }

  result.ok = true;
  result.recipe_json = migration.recipe_json;
  return result;
}

}  // namespace photo_editor::desktop
