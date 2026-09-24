#include "edit_history_snapshot_store.h"

#include "edit_adjustment_patch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace photo_editor::desktop {

StoredEditHistorySnapshots load_edit_history_snapshots(
    const QString& file_path) {
  StoredEditHistorySnapshots result;
  QFile file(file_path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return result;
  }
  const auto document =
      QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return result;
  }

  for (const auto& asset_value :
       document.object()
           .value(QStringLiteral("assets"))
           .toArray()) {
    const auto asset_object = asset_value.toObject();
    const auto asset_id =
        asset_object.value(QStringLiteral("assetId"))
            .toString()
            .simplified();
    if (asset_id.isEmpty()) {
      continue;
    }
    QVector<StoredEditHistorySnapshot> snapshots;
    const auto values =
        asset_object.value(QStringLiteral("snapshots"))
            .toArray();
    snapshots.reserve(
        std::min(values.size(), qsizetype{50}));
    for (const auto& value : values) {
      if (snapshots.size() >= 50) {
        break;
      }
      const auto object = value.toObject();
      const auto id =
          object.value(QStringLiteral("id"))
              .toString()
              .simplified();
      const auto name =
          object.value(QStringLiteral("name"))
              .toString()
              .simplified();
      const auto stored = parse_stored_adjustment_state(
          object, AdjustmentPatchScope::AssetBound);
      if (id.isEmpty() || name.isEmpty() ||
          !stored.state.has_value()) {
        continue;
      }
      snapshots.push_back(
          StoredEditHistorySnapshot{
              .id = id,
              .name = name,
              .created_at =
                  object.value(
                            QStringLiteral("createdAt"))
                      .toString(),
              .state = *stored.state,
          });
    }
    if (!snapshots.isEmpty()) {
      result.insert(asset_id, snapshots);
    }
  }
  return result;
}

bool save_edit_history_snapshots(
    const QString& file_path,
    const StoredEditHistorySnapshots& stored) {
  const QFileInfo destination_info(file_path);
  QDir().mkpath(destination_info.dir().absolutePath());
  QJsonArray assets;
  auto asset_ids = stored.keys();
  asset_ids.sort();
  for (const auto& asset_id : asset_ids) {
    QJsonArray snapshots;
    for (const auto& snapshot : stored.value(asset_id)) {
      snapshots.push_back(
          QJsonObject{
              {QStringLiteral("id"), snapshot.id},
              {QStringLiteral("name"), snapshot.name},
              {QStringLiteral("createdAt"),
               snapshot.created_at},
              {QStringLiteral("adjustmentPatch"),
               serialize_adjustment_patch(
                   snapshot.state,
                   all_adjustment_groups(),
                   AdjustmentPatchScope::AssetBound)},
          });
    }
    if (!snapshots.isEmpty()) {
      assets.push_back(
          QJsonObject{
              {QStringLiteral("assetId"), asset_id},
              {QStringLiteral("snapshots"), snapshots},
          });
    }
  }
  const QJsonObject root{
      {QStringLiteral("format"),
       QStringLiteral(
           "photo-editor-edit-history-snapshots")},
      {QStringLiteral("version"), 2},
      {QStringLiteral("assets"), assets},
  };
  QSaveFile file(file_path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(
      QJsonDocument(root).toJson(
          QJsonDocument::Indented));
  return file.commit();
}

}  // namespace photo_editor::desktop
