#include "job_store.h"

#include "app_bootstrap.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMetaObject>
#include <QSaveFile>
#include <QSet>
#include <QThread>

#include <algorithm>

namespace photo_editor::desktop {

namespace {

constexpr int kJobStoreHelperFinishTimeoutMs = 5000;

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

bool run_helper_command(
    const QStringList& command_arguments,
    QString* output = nullptr,
    QString* error_message = nullptr) {
  return AppBootstrap::runCatalogHelper(
      QStringLiteral("catalog_export_jobs"),
      command_arguments,
      output,
      error_message,
      kJobStoreHelperFinishTimeoutMs);
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
      object.insert(
          QStringLiteral("adjustment_payload"),
          document.object());
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
  const auto recipe_value =
      object.contains(QStringLiteral("adjustment_payload"))
          ? object.value(
                QStringLiteral("adjustment_payload"))
          : object.value(QStringLiteral("recipe_json"));
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

QVector<PersistentJobRecord> load_legacy_records(QString* error_message = nullptr) {
  if (error_message != nullptr) error_message->clear();
  QVector<PersistentJobRecord> records;
  QFile file(jobs_index_path());
  if (!file.exists()) return records;
  if (!file.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) *error_message = QStringLiteral("Unable to read legacy job history; existing data was preserved.");
    return {};
  }

  const auto parsed = QJsonDocument::fromJson(file.readAll());
  if (file.error() != QFile::NoError || !parsed.isObject() ||
      !parsed.object().value(QStringLiteral("jobs")).isArray()) {
    if (error_message != nullptr) *error_message = QStringLiteral("Invalid legacy job history; existing data was preserved.");
    return {};
  }

  const auto items = parsed.object().value(QStringLiteral("jobs")).toArray();
  records.reserve(items.size());
  QSet<QString> ids;
  for (const auto& value : items) {
    const auto record = record_from_object(value.toObject());
    if (!value.isObject() || record.job_id.isEmpty() || ids.contains(record.job_id)) {
      if (error_message != nullptr) *error_message = QStringLiteral("Invalid or duplicate legacy job record; existing data was preserved.");
      return {};
    }
    ids.insert(record.job_id);
    records.push_back(record);
  }
  return records;
}

bool lock_legacy_file(QLockFile& lock, QString* error_message) {
  if (QDir().mkpath(jobs_root_path())) {
    // Keep the existing one-second bound, but retry short file transactions
    // frequently. A coarse blocking retry can miss most available lock windows
    // in a burst of independent desktop/helper processes.
    QElapsedTimer deadline;
    deadline.start();
    do {
      if (lock.tryLock(0)) return true;
      if (lock.error() != QLockFile::LockFailedError || deadline.elapsed() >= 1000) break;
      QThread::msleep(5);
    } while (deadline.elapsed() < 1000);
  }
  if (error_message != nullptr) *error_message = QStringLiteral("Legacy job history is busy or unavailable. Try again; existing data was preserved.");
  return false;
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
  const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    if (error_message != nullptr) *error_message = QStringLiteral("Unable to commit job history; existing data was preserved.");
    return false;
  }
  return true;
}

bool migrate_legacy_records_to_helper(
    QString* error_message = nullptr) {
  // Serialize upload passes across processes without holding the mutation
  // lock during helper I/O. A contending observer retries rather than uploading
  // an older snapshot after another pass has already published a newer one.
  QLockFile migration_lock(QDir(jobs_root_path()).filePath(QStringLiteral("migration.lock")));
  if (!QDir().mkpath(jobs_root_path()) || !migration_lock.tryLock(0)) {
    if (error_message != nullptr) *error_message = QStringLiteral("Job history migration is already running or unavailable. Retry recovery.");
    return false;
  }
  QString snapshot_error;
  const auto legacy_records = load_legacy_records(&snapshot_error);
  if (!snapshot_error.isEmpty()) {
    if (error_message != nullptr) *error_message = snapshot_error;
    return false;
  }
  if (legacy_records.isEmpty()) {
    return true;
  }

  for (const auto& record : legacy_records) {
    const auto payload = QString::fromUtf8(
        QJsonDocument(object_from_record(record)).toJson(QJsonDocument::Compact));
    QString output;
    if (!run_helper_command({QStringLiteral("import-legacy"), payload}, &output, error_message)) {
      return false;
    }
    const auto receipt = QJsonDocument::fromJson(output.toUtf8());
    const auto accepted = receipt.object().value(QStringLiteral("accepted"));
    if (!receipt.isObject() || !accepted.isBool() || !accepted.toBool()) {
      if (error_message != nullptr) {
        *error_message = accepted.isBool()
            ? QStringLiteral("Legacy job %1 conflicts with existing catalog history. Both versions were preserved; review the conflict before retrying recovery.").arg(record.job_id)
            : QStringLiteral("Catalog did not confirm legacy job migration. Local history was preserved.");
      }
      return false;
    }
  }

  // Helpers run without a local-file lock. At publication, remove only the
  // exact uploaded versions; concurrent appends and same-ID changes survive.
  // The short read/modify/write lock is shared with fallback writers/deleters
  // and works across desktop processes, not just threads in this instance.
  QHash<QString, QJsonObject> uploaded_versions;
  uploaded_versions.reserve(legacy_records.size());
  for (const auto& record : legacy_records)
    uploaded_versions.insert(record.job_id, object_from_record(record));
  QLockFile lock(QDir(jobs_root_path()).filePath(QStringLiteral("index.lock")));
  if (!lock_legacy_file(lock, error_message)) return false;
  QString read_error;
  auto remaining = load_legacy_records(&read_error);
  if (!read_error.isEmpty()) {
    if (error_message != nullptr) *error_message = read_error;
    return false;
  }
  const auto original_size = remaining.size();
  remaining.erase(std::remove_if(remaining.begin(), remaining.end(), [&](const auto& current) {
    const auto uploaded = uploaded_versions.constFind(current.job_id);
    return uploaded != uploaded_versions.cend() && object_from_record(current) == uploaded.value();
  }), remaining.end());
  return remaining.size() == original_size || save_legacy_records(remaining, error_message);
}

}  // namespace

JobStoreNotifier::JobStoreNotifier(QObject* parent) : QObject(parent) {}

void JobStoreNotifier::notifyJobsChanged() {
  emit jobsChanged();
}

JobStoreNotifier* jobStoreNotifier() {
  return notifier_instance();
}

QVector<PersistentJobRecord> loadPersistedJobRecords(QString* error_message, bool migrate_legacy) {
  if (error_message != nullptr) error_message->clear();
  QString legacy_error;
  auto legacy_records = load_legacy_records(&legacy_error);
  if (migrate_legacy && !legacy_error.isEmpty()) {
    if (error_message != nullptr) *error_message = legacy_error;
    return {};
  }
  QString migration_error;
  if (migrate_legacy && !legacy_records.isEmpty()) {
    (void)migrate_legacy_records_to_helper(&migration_error);
    legacy_records = load_legacy_records(&legacy_error);
    if (migration_error.isEmpty()) migration_error = legacy_error;
    if (migration_error.isEmpty() && !legacy_records.isEmpty())
      migration_error = QStringLiteral("Job history changed during migration. Retry recovery before starting queued work.");
  }

  QString output;
  if (run_helper_command({QStringLiteral("list")}, &output, error_message)) {
    const auto parsed = QJsonDocument::fromJson(output.toUtf8());
    if (parsed.isArray()) {
      QVector<PersistentJobRecord> records;
      const auto items = parsed.array();
      records.reserve(items.size());
      for (const auto& value : items) {
        if (!value.isObject()) {
          if (error_message != nullptr) {
            *error_message = QStringLiteral("Catalog returned invalid job history records.");
          }
          return legacy_records;
        }
        const auto record = record_from_object(value.toObject());
        if (record.job_id.isEmpty()) {
          if (error_message != nullptr) {
            *error_message = QStringLiteral("Catalog returned a job history record without an ID.");
          }
          return legacy_records;
        }
        records.push_back(record);
      }
      if (error_message != nullptr && !migration_error.isEmpty()) *error_message = migration_error;
      if (!records.isEmpty() || legacy_records.isEmpty()) {
        return records;
      }
    } else if (error_message != nullptr) {
      *error_message = QStringLiteral("Catalog returned invalid job history.");
    }
  }

  return legacy_records;
}

qint64 persistentRenderQueueOrderForNewJob(const QString& job_id) {
  const auto normalized_id = job_id.trimmed();
  auto maximum_order = 0LL;
  for (const auto& record : loadPersistedJobRecords()) {
    if (record.job_kind != QStringLiteral("asset_export") &&
        record.job_kind != QStringLiteral("timeline_render")) {
      continue;
    }
    const auto order = record.metadata_json.value(QStringLiteral("queueOrder"))
                           .toVariant().toLongLong();
    if (!normalized_id.isEmpty() && record.job_id == normalized_id && order > 0) {
      return order;
    }
    maximum_order = qMax(maximum_order, order);
  }
  return maximum_order + 1;
}

bool upsertPersistedJobRecord(
    const PersistentJobRecord& record,
    QString* error_message, bool require_catalog) {
  const auto payload = QString::fromUtf8(
      QJsonDocument(object_from_record(record)).toJson(QJsonDocument::Compact));
  if (run_helper_command({QStringLiteral("upsert"), payload}, nullptr, error_message)) {
    QMetaObject::invokeMethod(
        jobStoreNotifier(),
        [notifier = jobStoreNotifier()]() { notifier->notifyJobsChanged(); },
        Qt::QueuedConnection);
    return true;
  }

  // Startup recovery must be durable in the selected catalog before dispatch
  // is enabled. A legacy fallback write is not confirmation of that commit.
  if (require_catalog) return false;
  QLockFile lock(QDir(jobs_root_path()).filePath(QStringLiteral("index.lock")));
  if (!lock_legacy_file(lock, error_message)) return false;
  QString read_error;
  auto records = load_legacy_records(&read_error);
  if (!read_error.isEmpty()) {
    if (error_message != nullptr) *error_message = read_error;
    return false;
  }
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

  QLockFile lock(QDir(jobs_root_path()).filePath(QStringLiteral("index.lock")));
  if (!lock_legacy_file(lock, error_message)) return false;
  QString read_error;
  auto records = load_legacy_records(&read_error);
  if (!read_error.isEmpty()) {
    if (error_message != nullptr) *error_message = read_error;
    return false;
  }
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

int recoverInterruptedJobRecords(QString* error_message, bool require_catalog) {
  if (error_message != nullptr) error_message->clear();
  QString read_error;
  const auto records = loadPersistedJobRecords(&read_error);
  if (require_catalog && !read_error.isEmpty()) {
    if (error_message != nullptr) *error_message = read_error;
    return 0;
  }
  auto recovered = 0;

  for (auto record : records) {
    const auto interrupted_status = record.status;
    if (interrupted_status != QStringLiteral("Running") &&
        interrupted_status != QStringLiteral("Pausing") &&
        interrupted_status != QStringLiteral("Canceling")) {
      continue;
    }

    if (record.job_kind == QStringLiteral("catalog_import") &&
        interrupted_status == QStringLiteral("Pausing")) {
      record.status = QStringLiteral("Paused");
      record.error_message = QStringLiteral("Paused after the previous app session ended.");
    } else if (interrupted_status == QStringLiteral("Canceling")) {
      record.status = QStringLiteral("Canceled");
      record.error_message = QStringLiteral("Canceled when the previous app session ended.");
    } else if (record.job_kind == QStringLiteral("asset_export") ||
        record.job_kind == QStringLiteral("timeline_render")) {
      // Export and render jobs write files outside the catalog. Never replay one
      // merely because the previous process disappeared: the user must confirm
      // the persisted delivery plan before another externally visible write.
      record.status = QStringLiteral("Paused");
      record.error_message = QStringLiteral(
          "Interrupted before completion. Review the destination, then resume when ready.");
    } else {
      record.status = QStringLiteral("Failed");
      if (record.error_message.trimmed().isEmpty()) {
        record.error_message = QStringLiteral("Interrupted before completion.");
      }
    }

    record.progress = 0;
    record.updated_at = iso_now();
    record.metadata_json.insert(QStringLiteral("recoveredAfterRestart"), true);
    record.metadata_json.insert(QStringLiteral("recoveryAcknowledged"), false);
    record.metadata_json.insert(QStringLiteral("potentiallyIncomplete"), true);
    record.metadata_json.insert(
        QStringLiteral("requiresRecoveryConfirmation"),
        record.status == QStringLiteral("Paused"));
    record.metadata_json.insert(
        QStringLiteral("recoveryAction"),
        record.status == QStringLiteral("Paused")
            ? QStringLiteral("awaiting-confirmation")
            : record.status.toLower());
    if (!upsertPersistedJobRecord(record, error_message, require_catalog)) {
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
        record.job_kind != QStringLiteral("catalog_import") &&
        record.job_kind != QStringLiteral("timeline_render")) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("This job type cannot be retried.");
      }
      return false;
    }

    const auto canceled_retryable_job =
        record.status == QStringLiteral("Canceled") &&
        (record.job_kind == QStringLiteral("catalog_import") ||
         record.job_kind == QStringLiteral("asset_export") ||
         record.job_kind == QStringLiteral("timeline_render"));
    if (record.status != QStringLiteral("Failed") && !canceled_retryable_job) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Only failed or canceled imports and renders can be retried.");
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
      record.metadata_json.insert(QStringLiteral("cancellationReason"), QStringLiteral("dropped_from_queue"));
      return upsertPersistedJobRecord(record, error_message);
    }

    if (record.status == QStringLiteral("Running")) {
      if (record.job_kind != QStringLiteral("asset_export") &&
          record.job_kind != QStringLiteral("catalog_import") &&
          record.job_kind != QStringLiteral("timeline_render")) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("This running job cannot be canceled safely.");
        }
        return false;
      }

      record.status = QStringLiteral("Canceling");
      record.error_message = record.job_kind == QStringLiteral("catalog_import")
                                 ? QStringLiteral("Cancel requested. Stopping at a safe file boundary.")
                                 : QStringLiteral("Cancel requested. Finishing the current render step.");
      record.updated_at = iso_now();
      record.metadata_json.insert(QStringLiteral("cancellationRequested"), true);
      record.metadata_json.insert(QStringLiteral("cancellationRequestedAt"), record.updated_at);
      record.metadata_json.insert(QStringLiteral("cancellationReason"), QStringLiteral("cooperative_cancel_requested"));
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

bool pausePersistedJobRecord(const QString& job_id, QString* error_message) {
  const auto trimmed_id = job_id.trimmed();
  const auto records = loadPersistedJobRecords();
  for (auto record : records) {
    if (record.job_id != trimmed_id) {
      continue;
    }
    if ((record.job_kind != QStringLiteral("catalog_import") &&
         record.job_kind != QStringLiteral("asset_export") &&
         record.job_kind != QStringLiteral("timeline_render")) ||
        (record.status != QStringLiteral("Queued") && record.status != QStringLiteral("Running"))) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Only queued or running imports and renders can be paused.");
      }
      return false;
    }

    const auto was_running = record.status == QStringLiteral("Running");
    record.status = was_running ? QStringLiteral("Pausing") : QStringLiteral("Paused");
    record.error_message = was_running
                               ? (record.job_kind == QStringLiteral("catalog_import")
                                      ? QStringLiteral("Pause requested. Stopping at a safe file boundary.")
                                      : QStringLiteral("Pause requested. Finishing the current render safely."))
                               : QStringLiteral("Paused before processing started.");
    record.updated_at = iso_now();
    record.metadata_json.insert(QStringLiteral("pauseRequested"), was_running);
    record.metadata_json.insert(QStringLiteral("pauseRequestedAt"), record.updated_at);
    return upsertPersistedJobRecord(record, error_message);
  }
  if (error_message != nullptr) {
    *error_message = QStringLiteral("The selected job could not be found.");
  }
  return false;
}

bool resumePersistedJobRecord(const QString& job_id, QString* error_message) {
  const auto trimmed_id = job_id.trimmed();
  const auto records = loadPersistedJobRecords();
  for (auto record : records) {
    if (record.job_id != trimmed_id) {
      continue;
    }
    if ((record.job_kind != QStringLiteral("catalog_import") &&
         record.job_kind != QStringLiteral("asset_export") &&
         record.job_kind != QStringLiteral("timeline_render")) ||
        record.status != QStringLiteral("Paused")) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Only paused imports and renders can be resumed.");
      }
      return false;
    }

    record.status = QStringLiteral("Queued");
    record.error_message.clear();
    record.progress = 0;
    record.updated_at = iso_now();
    record.metadata_json.remove(QStringLiteral("pauseRequested"));
    if (record.metadata_json.value(QStringLiteral("requiresRecoveryConfirmation")).toBool()) {
      record.metadata_json.insert(QStringLiteral("requiresRecoveryConfirmation"), false);
      record.metadata_json.insert(
          QStringLiteral("recoveryAction"),
          QStringLiteral("resumed-after-confirmation"));
    }
    const auto resume_count = record.metadata_json.value(QStringLiteral("resumeCount")).toInt();
    record.metadata_json.insert(QStringLiteral("resumeCount"), resume_count + 1);
    return upsertPersistedJobRecord(record, error_message);
  }
  if (error_message != nullptr) {
    *error_message = QStringLiteral("The selected job could not be found.");
  }
  return false;
}

bool pausePersistedImportJobRecord(const QString& job_id, QString* error_message) {
  return pausePersistedJobRecord(job_id, error_message);
}

bool resumePersistedImportJobRecord(const QString& job_id, QString* error_message) {
  return resumePersistedJobRecord(job_id, error_message);
}

}  // namespace photo_editor::desktop
