#include "timeline_asset_store.h"

#include "app_bootstrap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>

namespace photo_editor::desktop {

namespace {

QString app_data_root() {
  const auto legacy_override_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_ROOT").trimmed();
  if (!legacy_override_root.isEmpty()) {
    return QDir::cleanPath(legacy_override_root);
  }

  return AppBootstrap::appDataLocation();
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

QJsonObject helper_object_from_record(const TimelineAssetRecord& record) {
  QJsonObject object;
  object.insert(QStringLiteral("timeline_id"), record.timeline_id);
  object.insert(QStringLiteral("asset_id"), record.asset_id);
  object.insert(QStringLiteral("name"), record.name);
  object.insert(QStringLiteral("document_path"), record.document_path);
  object.insert(QStringLiteral("render_path"), record.render_path);
  object.insert(QStringLiteral("poster_path"), record.poster_path);
  object.insert(QStringLiteral("duration_seconds"), record.duration_seconds);
  object.insert(QStringLiteral("created_at"), record.created_at);
  object.insert(QStringLiteral("updated_at"), record.updated_at);
  return object;
}

TimelineAssetRecord record_from_helper_object(const QJsonObject& object) {
  TimelineAssetRecord record;
  record.timeline_id = object.value(QStringLiteral("timeline_id")).toString().trimmed();
  record.asset_id = object.value(QStringLiteral("asset_id")).toString().trimmed();
  record.name = object.value(QStringLiteral("name")).toString().trimmed();
  record.document_path = object.value(QStringLiteral("document_path")).toString().trimmed();
  record.render_path = object.value(QStringLiteral("render_path")).toString().trimmed();
  record.poster_path = object.value(QStringLiteral("poster_path")).toString().trimmed();
  record.duration_seconds = object.value(QStringLiteral("duration_seconds")).toDouble();
  record.created_at = object.value(QStringLiteral("created_at")).toString().trimmed();
  record.updated_at = object.value(QStringLiteral("updated_at")).toString().trimmed();

  if (record.asset_id.isEmpty() && !record.timeline_id.isEmpty()) {
    record.asset_id = QStringLiteral("timeline:%1").arg(record.timeline_id);
  }

  return record;
}

QString timeline_backup_root_path() {
  return QDir(timelineAssetsRootPath()).filePath(QStringLiteral("backups"));
}

QString timeline_backup_path(const QString& timeline_id) {
  return QDir(timeline_backup_root_path()).filePath(QStringLiteral("%1.json").arg(timeline_id));
}

QJsonObject helper_object_from_document_backup(
    const TimelineAssetRecord& record,
    const QJsonObject& document) {
  auto object = helper_object_from_record(record);
  object.insert(QStringLiteral("document_json"), document);
  return object;
}

QJsonObject legacy_document_backup_object(
    const TimelineAssetRecord& record,
    const QJsonObject& document) {
  QJsonObject object = object_from_record(record);
  object.insert(QStringLiteral("document"), document);
  return object;
}

QString built_binary_path(const QString& binary_name) {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

bool use_built_helper_binary() {
  const QFileInfo binary(built_binary_path(QStringLiteral("catalog_timeline_documents")));
  return binary.exists() && binary.isExecutable();
}

QString helper_program() {
  if (use_built_helper_binary()) {
    return built_binary_path(QStringLiteral("catalog_timeline_documents"));
  }

  return QStringLiteral("cargo");
}

QStringList helper_arguments(const QStringList& command_arguments) {
  if (use_built_helper_binary()) {
    return command_arguments;
  }

  QStringList arguments = {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("catalog_timeline_documents"),
      QStringLiteral("--"),
  };
  arguments.append(command_arguments);
  return arguments;
}

bool run_helper_command(
    const QStringList& command_arguments,
    QString* output = nullptr,
    QString* error_message = nullptr) {
  QProcess process;
  process.setWorkingDirectory(QDir::currentPath());
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    if (error_message != nullptr) {
      *error_message = environment_error;
    }
    return false;
  }

  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(helper_program(), helper_arguments(command_arguments));
  if (!process.waitForStarted(5000)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to start timeline catalog helper.");
    }
    return false;
  }

  if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_message != nullptr) {
      const auto helper_output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
      *error_message = helper_output.isEmpty() ? QStringLiteral("Timeline catalog helper failed.") : helper_output;
    }
    return false;
  }

  if (output != nullptr) {
    *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  }
  return true;
}

QVector<TimelineAssetRecord> load_legacy_timeline_asset_records() {
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

bool save_legacy_timeline_asset_records(const QVector<TimelineAssetRecord>& records) {
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
  QString output;
  if (run_helper_command({QStringLiteral("list")}, &output, nullptr)) {
    const auto document = QJsonDocument::fromJson(output.toUtf8());
    if (document.isArray()) {
      QVector<TimelineAssetRecord> records;
      const auto items = document.array();
      records.reserve(items.size());
      for (const auto& value : items) {
        if (!value.isObject()) {
          continue;
        }

        const auto record = record_from_helper_object(value.toObject());
        if (record.timeline_id.isEmpty()) {
          continue;
        }
        records.push_back(record);
      }
      return records;
    }
  }

  return load_legacy_timeline_asset_records();
}

bool saveTimelineAssetRecords(const QVector<TimelineAssetRecord>& records) {
  QJsonArray items;
  for (const auto& record : records) {
    items.push_back(helper_object_from_record(record));
  }

  const auto payload = QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));
  if (run_helper_command({QStringLiteral("replace"), payload}, nullptr, nullptr)) {
    return true;
  }

  return save_legacy_timeline_asset_records(records);
}

QJsonObject loadTimelineDocumentBackup(const QString& timeline_id) {
  const auto trimmed_id = timeline_id.trimmed();
  if (trimmed_id.isEmpty()) {
    return {};
  }

  QString output;
  if (run_helper_command({QStringLiteral("get"), trimmed_id}, &output, nullptr)) {
    const auto document = QJsonDocument::fromJson(output.toUtf8());
    if (document.isObject()) {
      const auto root = document.object();
      const auto document_value = root.value(QStringLiteral("document_json"));
      if (document_value.isObject()) {
        return document_value.toObject();
      }
    }
  }

  QFile file(timeline_backup_path(trimmed_id));
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return {};
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return {};
  }

  return document.object().value(QStringLiteral("document")).toObject();
}

bool saveTimelineDocumentBackup(
    const TimelineAssetRecord& record,
    const QJsonObject& document) {
  if (record.timeline_id.trimmed().isEmpty() || document.isEmpty()) {
    return false;
  }

  const auto payload = QString::fromUtf8(
      QJsonDocument(helper_object_from_document_backup(record, document)).toJson(QJsonDocument::Compact));
  if (run_helper_command({QStringLiteral("upsert"), payload}, nullptr, nullptr)) {
    return true;
  }

  QDir().mkpath(timeline_backup_root_path());
  QSaveFile file(timeline_backup_path(record.timeline_id));
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  file.write(QJsonDocument(legacy_document_backup_object(record, document)).toJson(QJsonDocument::Indented));
  return file.commit();
}

}  // namespace photo_editor::desktop
