#include "job_queue_model.h"

#include <QDateTime>

#include <algorithm>

namespace photo_editor::desktop {

namespace {

QString summary_status_for_jobs(const QVector<PersistentJobRecord>& jobs, const QString& idle_status = QStringLiteral("No background jobs.")) {
  const auto active_count = std::count_if(
      jobs.cbegin(),
      jobs.cend(),
      [](const auto& job) {
        return job.status == QStringLiteral("Queued") ||
               job.status == QStringLiteral("Running") ||
               job.status == QStringLiteral("Canceling");
      });
  const auto failed_count = std::count_if(
      jobs.cbegin(),
      jobs.cend(),
      [](const auto& job) { return job.status == QStringLiteral("Failed"); });

  if (jobs.isEmpty()) {
    return idle_status;
  }
  if (active_count > 0 && failed_count > 0) {
    return QStringLiteral("%1 active jobs, %2 failed.").arg(active_count).arg(failed_count);
  }
  if (active_count > 0) {
    return QStringLiteral("%1 active %2.")
        .arg(active_count)
        .arg(active_count == 1 ? QStringLiteral("job") : QStringLiteral("jobs"));
  }
  if (failed_count > 0) {
    return QStringLiteral("%1 failed %2.")
        .arg(failed_count)
        .arg(failed_count == 1 ? QStringLiteral("job") : QStringLiteral("jobs"));
  }
  return QStringLiteral("%1 completed %2.")
      .arg(jobs.size())
      .arg(jobs.size() == 1 ? QStringLiteral("job") : QStringLiteral("jobs"));
}

bool job_less_than(const PersistentJobRecord& lhs, const PersistentJobRecord& rhs) {
  const auto lhs_updated = lhs.updated_at.trimmed();
  const auto rhs_updated = rhs.updated_at.trimmed();
  if (lhs_updated != rhs_updated) {
    return lhs_updated > rhs_updated;
  }

  const auto lhs_created = lhs.created_at.trimmed();
  const auto rhs_created = rhs.created_at.trimmed();
  if (lhs_created != rhs_created) {
    return lhs_created > rhs_created;
  }

  return lhs.job_id < rhs.job_id;
}

QVariantMap map_from_job(const PersistentJobRecord& job) {
  QVariantMap map;
  map.insert(QStringLiteral("jobId"), job.job_id);
  map.insert(QStringLiteral("jobKind"), job.job_kind);
  map.insert(QStringLiteral("assetId"), job.asset_id);
  map.insert(QStringLiteral("mediaType"), job.media_type);
  map.insert(QStringLiteral("displayLabel"), job.display_label);
  map.insert(QStringLiteral("sourcePath"), job.source_path);
  map.insert(QStringLiteral("presetName"), job.preset_name);
  map.insert(QStringLiteral("destinationPath"), job.destination_path);
  map.insert(QStringLiteral("outputPath"), job.output_path);
  map.insert(QStringLiteral("status"), job.status);
  map.insert(QStringLiteral("errorMessage"), job.error_message);
  map.insert(QStringLiteral("progress"), job.progress);
  map.insert(QStringLiteral("createdAt"), job.created_at);
  map.insert(QStringLiteral("updatedAt"), job.updated_at);
  map.insert(QStringLiteral("metadata"), job.metadata_json.toVariantMap());
  return map;
}

QString cancellation_mode_for_job(const QString& job_kind, const QString& status) {
  if (status == QStringLiteral("Queued")) {
    return QStringLiteral("queued-drop");
  }
  if (status == QStringLiteral("Running") &&
      (job_kind == QStringLiteral("asset_export") ||
       job_kind == QStringLiteral("timeline_render"))) {
    return QStringLiteral("cooperative");
  }
  if (status == QStringLiteral("Canceling")) {
    return QStringLiteral("already-canceling");
  }
  return QStringLiteral("unsupported");
}

bool cancellation_enabled_for_job(const QString& job_kind, const QString& status) {
  const auto mode = cancellation_mode_for_job(job_kind, status);
  return mode == QStringLiteral("queued-drop") || mode == QStringLiteral("cooperative");
}

QString cancellation_policy_signature_for_job(const QString& job_kind, const QString& status) {
  const auto mode = cancellation_mode_for_job(job_kind, status);
  const auto enabled = mode == QStringLiteral("queued-drop") || mode == QStringLiteral("cooperative");
  return QStringLiteral("%1|%2|cancel:%3|mode:%4")
      .arg(job_kind, status, enabled ? QStringLiteral("true") : QStringLiteral("false"), mode);
}

}  // namespace

JobQueueModel::JobQueueModel(QObject* parent) : QAbstractListModel(parent) {
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &JobQueueModel::refresh);
  setStatus(QStringLiteral("Jobs load after the catalog is ready."));
}

int JobQueueModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(jobs_.size());
}

QVariant JobQueueModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= jobs_.size()) {
    return {};
  }

  const auto& job = jobs_.at(index.row());
  switch (role) {
    case JobIdRole:
      return job.job_id;
    case JobKindRole:
      return job.job_kind;
    case AssetIdRole:
      return job.asset_id;
    case MediaTypeRole:
      return job.media_type;
    case DisplayLabelRole:
      return job.display_label;
    case SourcePathRole:
      return job.source_path;
    case PresetNameRole:
      return job.preset_name;
    case DestinationPathRole:
      return job.destination_path;
    case OutputPathRole:
      return job.output_path;
    case StatusRole:
      return job.status;
    case ErrorMessageRole:
      return job.error_message;
    case ProgressRole:
      return job.progress;
    case CreatedAtRole:
      return job.created_at;
    case UpdatedAtRole:
      return job.updated_at;
    case MetadataRole:
      return job.metadata_json.toVariantMap();
    default:
      return {};
  }
}

QHash<int, QByteArray> JobQueueModel::roleNames() const {
  return {
      {JobIdRole, "jobId"},
      {JobKindRole, "jobKind"},
      {AssetIdRole, "assetId"},
      {MediaTypeRole, "mediaType"},
      {DisplayLabelRole, "displayLabel"},
      {SourcePathRole, "sourcePath"},
      {PresetNameRole, "presetName"},
      {DestinationPathRole, "destinationPath"},
      {OutputPathRole, "outputPath"},
      {StatusRole, "status"},
      {ErrorMessageRole, "errorMessage"},
      {ProgressRole, "progress"},
      {CreatedAtRole, "createdAt"},
      {UpdatedAtRole, "updatedAt"},
      {MetadataRole, "metadata"},
  };
}

int JobQueueModel::jobCount() const { return static_cast<int>(jobs_.size()); }
int JobQueueModel::activeJobCount() const { return activeJobCountValue(); }
int JobQueueModel::failedJobCount() const { return failedJobCountValue(); }
int JobQueueModel::completedJobCount() const { return completedJobCountValue(); }
bool JobQueueModel::busy() const { return activeJobCountValue() > 0; }
bool JobQueueModel::hasFailures() const { return failedJobCountValue() > 0; }
QString JobQueueModel::status() const { return status_; }

void JobQueueModel::refresh() {
  reloadJobs();
}

void JobQueueModel::initializeFromStore() {
  (void)recoverInterruptedJobRecords(nullptr);
  reloadJobs();
}

int JobQueueModel::activeJobCountForKind(const QString& job_kind) const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [&](const auto& job) {
        return job.job_kind == job_kind &&
               (job.status == QStringLiteral("Queued") ||
                job.status == QStringLiteral("Running") ||
                job.status == QStringLiteral("Canceling"));
      }));
}

int JobQueueModel::failedJobCountForKind(const QString& job_kind) const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [&](const auto& job) {
        return job.job_kind == job_kind && job.status == QStringLiteral("Failed");
      }));
}

int JobQueueModel::totalJobCountForKind(const QString& job_kind) const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [&](const auto& job) { return job.job_kind == job_kind; }));
}

QVariantMap JobQueueModel::latestJobForKind(const QString& job_kind) const {
  for (const auto& job : jobs_) {
    if (job.job_kind != job_kind) {
      continue;
    }

    return map_from_job(job);
  }

  return {};
}

QVariantMap JobQueueModel::latestFailedJobForKind(const QString& job_kind) const {
  for (const auto& job : jobs_) {
    if (job.job_kind == job_kind && job.status == QStringLiteral("Failed")) {
      return map_from_job(job);
    }
  }

  return {};
}

QString JobQueueModel::statusForKind(const QString& job_kind, const QString& idle_status) const {
  QVector<PersistentJobRecord> matching_jobs;
  for (const auto& job : jobs_) {
    if (job.job_kind == job_kind) {
      matching_jobs.push_back(job);
    }
  }
  return summary_status_for_jobs(matching_jobs, idle_status);
}

bool JobQueueModel::canCancelJob(const QString& job_id) const {
  const auto trimmed_id = job_id.trimmed();
  for (const auto& job : jobs_) {
    if (job.job_id == trimmed_id) {
      return cancellation_enabled_for_job(job.job_kind, job.status);
    }
  }
  return false;
}

QString JobQueueModel::cancellationPolicySignature(
    const QString& job_kind,
    const QString& status) const {
  return cancellation_policy_signature_for_job(job_kind.trimmed(), status.trimmed());
}

QString JobQueueModel::cancellationSignature(const QString& job_id) const {
  const auto trimmed_id = job_id.trimmed();
  for (const auto& job : jobs_) {
    if (job.job_id == trimmed_id) {
      return QStringLiteral("%1|%2")
          .arg(job.job_id, cancellation_policy_signature_for_job(job.job_kind, job.status));
    }
  }
  return QStringLiteral("%1|missing|cancel:false|mode:missing").arg(trimmed_id);
}

bool JobQueueModel::retryJob(const QString& job_id) {
  QString error_message;
  if (!retryPersistedJobRecord(job_id, &error_message)) {
    if (!error_message.isEmpty()) {
      setStatus(error_message);
    }
    return false;
  }

  reloadJobs();
  setStatus(QStringLiteral("Retried the selected job."));
  return true;
}

bool JobQueueModel::cancelJob(const QString& job_id) {
  QString error_message;
  if (!cancelPersistedJobRecord(job_id, &error_message)) {
    if (!error_message.isEmpty()) {
      setStatus(error_message);
    }
    return false;
  }

  reloadJobs();
  setStatus(QStringLiteral("Cancel requested for the selected job."));
  return true;
}

void JobQueueModel::retryFailedJobs() {
  const auto jobs = jobs_;
  auto retried = 0;
  for (const auto& job : jobs) {
    if (job.status != QStringLiteral("Failed")) {
      continue;
    }
    if (retryPersistedJobRecord(job.job_id, nullptr)) {
      ++retried;
    }
  }

  reloadJobs();
  if (retried > 0) {
    setStatus(QStringLiteral("Retried %1 failed %2.")
                  .arg(retried)
                  .arg(retried == 1 ? QStringLiteral("job") : QStringLiteral("jobs")));
  }
}

void JobQueueModel::removeJob(const QString& job_id) {
  if (!removePersistedJobRecord(job_id, nullptr)) {
    setStatus(QStringLiteral("Unable to remove the selected job."));
    return;
  }

  reloadJobs();
  if (jobs_.isEmpty()) {
    setStatus(QStringLiteral("No background jobs."));
  }
}

void JobQueueModel::clearFinishedJobs() {
  auto removed = 0;
  for (const auto& job : jobs_) {
    if (job.status != QStringLiteral("Completed") &&
        job.status != QStringLiteral("Failed") &&
        job.status != QStringLiteral("Canceled")) {
      continue;
    }
    if (removePersistedJobRecord(job.job_id, nullptr)) {
      ++removed;
    }
  }

  reloadJobs();
  if (removed > 0) {
    setStatus(QStringLiteral("Cleared %1 finished %2.")
                  .arg(removed)
                  .arg(removed == 1 ? QStringLiteral("job") : QStringLiteral("jobs")));
  }
}

void JobQueueModel::reloadJobs() {
  auto records = loadPersistedJobRecords();
  std::sort(records.begin(), records.end(), job_less_than);

  beginResetModel();
  jobs_ = records;
  endResetModel();
  emit jobsChanged();
  updateSummaryStatus();
}

void JobQueueModel::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  emit statusChanged();
}

QString JobQueueModel::summaryStatusForJobSubset(
    const QVector<PersistentJobRecord>& jobs,
    const QString& idle_status) {
  return summary_status_for_jobs(jobs, idle_status);
}

int JobQueueModel::activeJobCountValue() const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [](const auto& job) {
        return job.status == QStringLiteral("Queued") ||
               job.status == QStringLiteral("Running") ||
               job.status == QStringLiteral("Canceling");
      }));
}

int JobQueueModel::failedJobCountValue() const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [](const auto& job) { return job.status == QStringLiteral("Failed"); }));
}

int JobQueueModel::completedJobCountValue() const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [](const auto& job) {
        return job.status == QStringLiteral("Completed") || job.status == QStringLiteral("Canceled");
      }));
}

void JobQueueModel::updateSummaryStatus() {
  setStatus(summary_status_for_jobs(jobs_));
}

}  // namespace photo_editor::desktop
