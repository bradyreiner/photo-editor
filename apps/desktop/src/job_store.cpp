#include "job_store.h"

#include "app_bootstrap.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>

namespace photo_editor::desktop {

namespace {

JobStoreNotifier* notifier_instance() {
  static auto* notifier = new JobStoreNotifier();
  return notifier;
}

QString iso_now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString jobs_root_path() {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("jobs"));
}

QString jobs_index_path() {
  return QDir(jobs_root_path()).filePath(QStringLiteral("index.json"));
}

QString built_binary_path(const QString& binary_name) {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

bool use_built_helper_binary() {
  const QFileInfo binary(built_binary_path(QStringLiteral("catalog_export_jobs")));
  return binary.exists() && binary.isExecutable();
}

QString helper_program() {
  if (use_built_helper_binary()) {
    return built_binary_path(QStringLiteral("catalog_export_jobs"));
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
      QStringLiteral("catalog_export_jobs"),
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
      *error_message = QStringLiteral("Unable to start job store helper.");
    }
    return false;
  }

  if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_message != nullptr) {
      const auto helper_output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
      *error_message = helper_output.isEmpty() ? QStringLiteral("Job store helper failed.") : helper_output;
    }
    return false;
  }

  if (output != nullptr) {
    *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  }
  return true;
}

QJsonObject object_from_record(const PersistentJobRecord& record) {
  QJsonObject object;
  object.insert(QStringLiteral("job_id"), record.job_id);
  object.insert(QStringLiteral("job_kind"), record.job_kind);
  object.insert(QStringLiteral("asset_id"), record.asset_id);
  object.insert(QStringLiteral("media_type"), record.media_type);
  object.insert(QStringLiteral("display_label"), record.display_label);
  object.insert(QStringLiteral("source_path"), record.source_path);
  object.insert(QStringLiteral("edited_preview_source"), record.edited_preview_source);
  if (!record.recipe_json.trimmed().isEmpty()) {
    const auto document = QJsonDocument::fromJson(record.recipe_json.toUtf8());
    if (document.isObject()) {
      object.insert(QStringLiteral("recipe_json"), document.object());
    }
  }
  if (!record.metadata_json.isEmpty()) {
    object.insert(QStringLiteral("metadata_json"), record.metadata_json);
  }
  object.insert(QStringLiteral("preset_name"), record.preset_name);
  object.insert(QStringLiteral("destination_path"), record.destination_path);
  object.insert(QStringLiteral("output_path"), record.output_path);
  object.insert(QStringLiteral("status"), record.status);
  object.insert(QStringLiteral("error_message"), record.error_message);
  object.insert(QStringLiteral("progress"), record.progress);
  object.insert(QStringLiteral("created_at"), record.created_at);
  object.insert(QStringLiteral("updated_at"), record.updated_at);
  return object;
}

PersistentJobRecord record_from_object(const QJsonObject& object) {
  PersistentJobRecord record;
  record.job_id = object.value(QStringLiteral("job_id")).toString().trimmed();
  record.job_kind = object.value(QStringLiteral("job_kind")).toString().trimmed();
  if (record.job_kind.isEmpty()) {
    record.job_kind = QStringLiteral("asset_export");
  }
  record.asset_id = object.value(QStringLiteral("asset_id")).toString().trimmed();
  record.media_type = object.value(QStringLiteral("media_type")).toString().trimmed();
  record.display_label = object.value(QStringLiteral("display_label")).toString().trimmed();
  record.source_path = object.value(QStringLiteral("source_path")).toString().trimmed();
  record.edited_preview_source = object.value(QStringLiteral("edited_preview_source")).toString().trimmed();
  const auto recipe_value = object.value(QStringLiteral("recipe_json"));
  if (recipe_value.isObject()) {
    record.recipe_json = QString::fromUtf8(
        QJsonDocument(recipe_value.toObject()).toJson(QJsonDocument::Compact));
  } else if (recipe_value.isString()) {
    record.recipe_json = recipe_value.toString().trimmed();
  }
  if (object.value(QStringLiteral("metadata_json")).isObject()) {
    record.metadata_json = object.value(QStringLiteral("metadata_json")).toObject();
  }
  record.preset_name = object.value(QStringLiteral("preset_name")).toString().trimmed();
  record.destination_path = object.value(QStringLiteral("destination_path")).toString().trimmed();
  record.output_path = object.value(QStringLiteral("output_path")).toString().trimmed();
  record.status = object.value(QStringLiteral("status")).toString().trimmed();
  if (record.status.isEmpty()) {
    record.status = QStringLiteral("Queued");
  }
  record.error_message = object.value(QStringLiteral("error_message")).toString().trimmed();
  record.progress = object.value(QStringLiteral("progress")).toInt();
  record.created_at = object.value(QStringLiteral("created_at")).toString().trimmed();
  record.updated_at = object.value(QStringLiteral("updated_at")).toString().trimmed();
  return record;
}

QVector<PersistentJobRecord> load_legacy_records() {
  QVector<PersistentJobRecord> records;
  QFile file(jobs_index_path());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return records;
  }

  const auto parsed = QJsonDocument::fromJson(file.readAll());
  if (!parsed.isObject()) {
    return records;
  }

  const auto items = parsed.object().value(QStringLiteral("jobs")).toArray();
  records.reserve(items.size());
  for (const auto& value : items) {
    if (value.isObject()) {
      const auto record = record_from_object(value.toObject());
      if (!record.job_id.isEmpty()) {
        records.push_back(record);
      }
    }
  }
  return records;
}

bool save_legacy_records(
    const QVector<PersistentJobRecord>& records,
    QString* error_message = nullptr) {
  QDir().mkpath(jobs_root_path());
  QJsonArray items;
  for (const auto& record : records) {
    items.push_back(object_from_record(record));
  }

  QJsonObject root;
  root.insert(QStringLiteral("jobs"), items);
  QSaveFile file(jobs_index_path());
  if (!file.open(QIODevice::WriteOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to write job history.");
    }
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return file.commit();
}

}  // namespace

JobStoreNotifier::JobStoreNotifier(QObject* parent) : QObject(parent) {}

void JobStoreNotifier::notifyJobsChanged() {
  emit jobsChanged();
}

JobStoreNotifier* jobStoreNotifier() {
  return notifier_instance();
}

QVector<PersistentJobRecord> loadPersistedJobRecords() {
  QString output;
  if (run_helper_command({QStringLiteral("list")}, &output, nullptr)) {
    const auto parsed = QJsonDocument::fromJson(output.toUtf8());
    if (parsed.isArray()) {
      QVector<PersistentJobRecord> records;
      const auto items = parsed.array();
      records.reserve(items.size());
      for (const auto& value : items) {
        if (!value.isObject()) {
          continue;
        }
        const auto record = record_from_object(value.toObject());
        if (!record.job_id.isEmpty()) {
          records.push_back(record);
        }
      }
      return records;
    }
  }

  return load_legacy_records();
}

bool upsertPersistedJobRecord(
    const PersistentJobRecord& record,
    QString* error_message) {
  const auto payload = QString::fromUtf8(
      QJsonDocument(object_from_record(record)).toJson(QJsonDocument::Compact));
  if (run_helper_command({QStringLiteral("upsert"), payload}, nullptr, error_message)) {
    QMetaObject::invokeMethod(
        jobStoreNotifier(),
        [notifier = jobStoreNotifier()]() { notifier->notifyJobsChanged(); },
        Qt::QueuedConnection);
    return true;
  }

  auto records = load_legacy_records();
  bool updated_existing = false;
  for (auto& existing : records) {
    if (existing.job_id == record.job_id) {
      existing = record;
      updated_existing = true;
      break;
    }
  }
  if (!updated_existing) {
    records.push_back(record);
  }
  const auto saved = save_legacy_records(records, error_message);
  if (saved) {
    QMetaObject::invokeMethod(
        jobStoreNotifier(),
        [notifier = jobStoreNotifier()]() { notifier->notifyJobsChanged(); },
        Qt::QueuedConnection);
  }
  return saved;
}

bool removePersistedJobRecord(
    const QString& job_id,
    QString* error_message) {
  const auto trimmed_id = job_id.trimmed();
  if (trimmed_id.isEmpty()) {
    return true;
  }

  if (run_helper_command({QStringLiteral("delete"), trimmed_id}, nullptr, error_message)) {
    QMetaObject::invokeMethod(
        jobStoreNotifier(),
        [notifier = jobStoreNotifier()]() { notifier->notifyJobsChanged(); },
        Qt::QueuedConnection);
    return true;
  }

  auto records = load_legacy_records();
  QVector<PersistentJobRecord> next_records;
  next_records.reserve(records.size());
  for (const auto& record : records) {
    if (record.job_id != trimmed_id) {
      next_records.push_back(record);
    }
  }
  const auto saved = save_legacy_records(next_records, error_message);
  if (saved) {
    QMetaObject::invokeMethod(
        jobStoreNotifier(),
        [notifier = jobStoreNotifier()]() { notifier->notifyJobsChanged(); },
        Qt::QueuedConnection);
  }
  return saved;
}

int recoverInterruptedJobRecords(QString* error_message) {
  const auto records = loadPersistedJobRecords();
  auto recovered = 0;

  for (auto record : records) {
    if (record.status != QStringLiteral("Running")) {
      continue;
    }

    if (record.job_kind == QStringLiteral("asset_export") ||
        record.job_kind == QStringLiteral("timeline_render")) {
      record.status = QStringLiteral("Queued");
      record.error_message = QStringLiteral("Recovered after the previous app session ended.");
    } else {
      record.status = QStringLiteral("Failed");
      if (record.error_message.trimmed().isEmpty()) {
        record.error_message = QStringLiteral("Interrupted before completion.");
      }
    }

    record.progress = 0;
    record.updated_at = iso_now();
    record.metadata_json.insert(QStringLiteral("recoveredAfterRestart"), true);
    record.metadata_json.insert(
        QStringLiteral("recoveryAction"),
        record.status == QStringLiteral("Queued") ? QStringLiteral("requeued") : QStringLiteral("failed"));
    if (!upsertPersistedJobRecord(record, error_message)) {
      return recovered;
    }
    ++recovered;
  }

  return recovered;
}

bool retryPersistedJobRecord(
    const QString& job_id,
    QString* error_message) {
  const auto trimmed_id = job_id.trimmed();
  if (trimmed_id.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Choose a job to retry.");
    }
    return false;
  }

  const auto records = loadPersistedJobRecords();
  for (auto record : records) {
    if (record.job_id != trimmed_id) {
      continue;
    }

    if (record.job_kind != QStringLiteral("asset_export") &&
        record.job_kind != QStringLiteral("timeline_render")) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Only export and timeline render jobs can be retried right now.");
      }
      return false;
    }

    if (record.status != QStringLiteral("Failed")) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Only failed jobs can be retried.");
      }
      return false;
    }

    record.status = QStringLiteral("Queued");
    record.error_message.clear();
    record.progress = 0;
    record.updated_at = iso_now();
    const auto retry_count = record.metadata_json.value(QStringLiteral("retryCount")).toInt();
    record.metadata_json.insert(QStringLiteral("retryCount"), retry_count + 1);
    return upsertPersistedJobRecord(record, error_message);
  }

  if (error_message != nullptr) {
    *error_message = QStringLiteral("The selected job could not be found.");
  }
  return false;
}

bool cancelPersistedJobRecord(
    const QString& job_id,
    QString* error_message) {
  const auto trimmed_id = job_id.trimmed();
  if (trimmed_id.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Choose a job to cancel.");
    }
    return false;
  }

  const auto records = loadPersistedJobRecords();
  for (auto record : records) {
    if (record.job_id != trimmed_id) {
      continue;
    }

    if (record.status == QStringLiteral("Completed") ||
        record.status == QStringLiteral("Failed") ||
        record.status == QStringLiteral("Canceled")) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Only queued or active jobs can be canceled.");
      }
      return false;
    }

    if (record.status == QStringLiteral("Queued")) {
      record.status = QStringLiteral("Canceled");
      record.error_message = QStringLiteral("Canceled before processing started.");
      record.progress = 0;
      record.updated_at = iso_now();
      record.metadata_json.insert(QStringLiteral("canceled"), true);
      record.metadata_json.insert(QStringLiteral("canceledAt"), record.updated_at);
      record.metadata_json.insert(QStringLiteral("cancelAction"), QStringLiteral("dropped_from_queue"));
      return upsertPersistedJobRecord(record, error_message);
    }

    if (record.status == QStringLiteral("Running")) {
      if (record.job_kind != QStringLiteral("asset_export") &&
          record.job_kind != QStringLiteral("timeline_render")) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Only queued imports can be canceled right now.");
        }
        return false;
      }

      record.status = QStringLiteral("Canceling");
      record.error_message = QStringLiteral("Cancel requested. Finishing the current render step.");
      record.updated_at = iso_now();
      record.metadata_json.insert(QStringLiteral("cancellationRequested"), true);
      record.metadata_json.insert(QStringLiteral("cancellationRequestedAt"), record.updated_at);
      return upsertPersistedJobRecord(record, error_message);
    }

    if (record.status == QStringLiteral("Canceling")) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Cancellation is already in progress.");
      }
      return false;
    }
  }

  if (error_message != nullptr) {
    *error_message = QStringLiteral("The selected job could not be found.");
  }
  return false;
}

}  // namespace photo_editor::desktop
