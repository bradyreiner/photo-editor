#include "timeline_asset_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace photo_editor::desktop {

namespace {

QString app_data_root() {
  const auto override_root =
      qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_ROOT").trimmed();
  if (!override_root.isEmpty()) {
    return QDir::cleanPath(override_root);
  }

  auto data_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (data_root.isEmpty()) {
    data_root = QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-data"));
  }
  return data_root;
}

TimelineAssetRecord record_from_object(const QJsonObject& object) {
  TimelineAssetRecord record;
  record.asset_id = object.value(QStringLiteral("assetId")).toString().trimmed();
  record.timeline_id = object.value(QStringLiteral("timelineId")).toString().trimmed();
  record.name = object.value(QStringLiteral("name")).toString().trimmed();
  record.document_path = object.value(QStringLiteral("documentPath")).toString().trimmed();
  record.render_path = object.value(QStringLiteral("renderPath")).toString().trimmed();
  record.poster_path = object.value(QStringLiteral("posterPath")).toString().trimmed();
  record.duration_seconds = object.value(QStringLiteral("durationSeconds")).toDouble();
  record.created_at = object.value(QStringLiteral("createdAt")).toString().trimmed();
  record.updated_at = object.value(QStringLiteral("updatedAt")).toString().trimmed();

  if (record.asset_id.isEmpty() && !record.timeline_id.isEmpty()) {
    record.asset_id = QStringLiteral("timeline:%1").arg(record.timeline_id);
  }

  return record;
}

QJsonObject object_from_record(const TimelineAssetRecord& record) {
  QJsonObject object;
  object.insert(QStringLiteral("assetId"), record.asset_id);
  object.insert(QStringLiteral("timelineId"), record.timeline_id);
  object.insert(QStringLiteral("name"), record.name);
  object.insert(QStringLiteral("documentPath"), record.document_path);
  object.insert(QStringLiteral("renderPath"), record.render_path);
  object.insert(QStringLiteral("posterPath"), record.poster_path);
  object.insert(QStringLiteral("durationSeconds"), record.duration_seconds);
  object.insert(QStringLiteral("createdAt"), record.created_at);
  object.insert(QStringLiteral("updatedAt"), record.updated_at);
  return object;
}

}  // namespace

QString timelineAssetsRootPath() {
  return QDir(app_data_root()).filePath(QStringLiteral("timelines"));
}

QString timelineDocumentsRootPath() {
  return QDir(timelineAssetsRootPath()).filePath(QStringLiteral("documents"));
}

QString timelineRendersRootPath() {
  return QDir(timelineAssetsRootPath()).filePath(QStringLiteral("renders"));
}

QString timelinePostersRootPath() {
  return QDir(timelineAssetsRootPath()).filePath(QStringLiteral("posters"));
}

QString timelineIndexPath() {
  return QDir(timelineAssetsRootPath()).filePath(QStringLiteral("index.json"));
}

QVector<TimelineAssetRecord> loadTimelineAssetRecords() {
  QVector<TimelineAssetRecord> records;

  QFile file(timelineIndexPath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return records;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return records;
  }

  const auto items = document.object().value(QStringLiteral("timelines")).toArray();
  records.reserve(items.size());
  for (const auto& value : items) {
    if (!value.isObject()) {
      continue;
    }

    const auto record = record_from_object(value.toObject());
    if (record.timeline_id.isEmpty()) {
      continue;
    }
    records.push_back(record);
  }

  return records;
}

bool saveTimelineAssetRecords(const QVector<TimelineAssetRecord>& records) {
  QDir().mkpath(timelineAssetsRootPath());

  QJsonArray items;
  for (const auto& record : records) {
    items.push_back(object_from_record(record));
  }

  QJsonObject root;
  root.insert(QStringLiteral("timelines"), items);

  QSaveFile file(timelineIndexPath());
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return file.commit();
}

}  // namespace photo_editor::desktop
