#include "job_queue_model.h"
#include "worker_queue_registry.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QUuid>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace photo_editor::desktop {

namespace {

QString normalized_job_priority(const QJsonObject& metadata);

QString summary_status_for_jobs(const QVector<PersistentJobRecord>& jobs, const QString& idle_status = QStringLiteral("No background jobs.")) {
  const auto active_count = std::count_if(
      jobs.cbegin(),
      jobs.cend(),
      [](const auto& job) {
        return job.status == QStringLiteral("Queued") ||
               job.status == QStringLiteral("Running") ||
               job.status == QStringLiteral("Pausing") ||
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
  const auto is_render_job = [](const PersistentJobRecord& job) {
    return job.job_kind == QStringLiteral("asset_export") ||
           job.job_kind == QStringLiteral("timeline_render");
  };
  const auto is_queue_state = [](const PersistentJobRecord& job) {
    return job.status == QStringLiteral("Queued") ||
           job.status == QStringLiteral("Paused");
  };
  const auto priority_rank = [](const PersistentJobRecord& job) {
    const auto priority = normalized_job_priority(job.metadata_json);
    return priority == QStringLiteral("high") ? 2
           : priority == QStringLiteral("low") ? 0
                                                : 1;
  };
  const auto queue_order = [](const PersistentJobRecord& job) {
    return job.metadata_json.value(QStringLiteral("queueOrder"))
        .toVariant().toLongLong();
  };
  if (is_render_job(lhs) && is_render_job(rhs) &&
      is_queue_state(lhs) && is_queue_state(rhs)) {
    const auto lhs_rank = priority_rank(lhs);
    const auto rhs_rank = priority_rank(rhs);
    if (lhs_rank != rhs_rank) {
      return lhs_rank > rhs_rank;
    }
    const auto lhs_order = queue_order(lhs);
    const auto rhs_order = queue_order(rhs);
    if (lhs_order > 0 && rhs_order > 0 && lhs_order != rhs_order) {
      return lhs_order < rhs_order;
    }
  }
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

bool is_render_queue_job(const PersistentJobRecord& job) {
  return job.job_kind == QStringLiteral("asset_export") ||
         job.job_kind == QStringLiteral("timeline_render");
}

bool is_reorderable_render_job(const PersistentJobRecord& job) {
  return is_render_queue_job(job) &&
         (job.status == QStringLiteral("Queued") ||
          job.status == QStringLiteral("Paused"));
}

int render_job_priority_rank(const PersistentJobRecord& job) {
  const auto priority = normalized_job_priority(job.metadata_json);
  return priority == QStringLiteral("high") ? 2
         : priority == QStringLiteral("low") ? 0
                                              : 1;
}

qint64 render_job_queue_order(const PersistentJobRecord& job) {
  return job.metadata_json.value(QStringLiteral("queueOrder"))
      .toVariant().toLongLong();
}

bool render_queue_dispatch_less(
    const PersistentJobRecord& lhs,
    const PersistentJobRecord& rhs) {
  const auto lhs_rank = render_job_priority_rank(lhs);
  const auto rhs_rank = render_job_priority_rank(rhs);
  if (lhs_rank != rhs_rank) {
    return lhs_rank > rhs_rank;
  }
  const auto lhs_order = render_job_queue_order(lhs);
  const auto rhs_order = render_job_queue_order(rhs);
  if (lhs_order > 0 && rhs_order > 0 && lhs_order != rhs_order) {
    return lhs_order < rhs_order;
  }
  if (lhs.created_at != rhs.created_at) {
    return lhs.created_at < rhs.created_at;
  }
  return lhs.job_id < rhs.job_id;
}

QVector<PersistentJobRecord> reorderable_render_jobs(
    const QVector<PersistentJobRecord>& records) {
  QVector<PersistentJobRecord> values;
  for (const auto& record : records) {
    if (is_reorderable_render_job(record)) {
      values.push_back(record);
    }
  }
  std::sort(values.begin(), values.end(), render_queue_dispatch_less);
  return values;
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
       job_kind == QStringLiteral("catalog_import") ||
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

QString normalized_job_priority(const QJsonObject& metadata) {
  const auto priority = metadata.value(QStringLiteral("priority")).toString().trimmed().toLower();
  return priority == QStringLiteral("high") || priority == QStringLiteral("low")
             ? priority
             : QStringLiteral("normal");
}

QString job_stage(const PersistentJobRecord& job) {
  auto stage = job.metadata_json.value(QStringLiteral("stage")).toString().trimmed();
  if (stage.isEmpty() && job.metadata_json.value(QStringLiteral("exportRenderJob")).isObject()) {
    stage = job.metadata_json.value(QStringLiteral("exportRenderJob")).toObject()
                .value(QStringLiteral("stage")).toString().trimmed();
  }
  if (!stage.isEmpty()) {
    stage.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!stage.isEmpty()) {
      stage[0] = stage.at(0).toUpper();
    }
    return stage;
  }
  if (job.status == QStringLiteral("Queued")) {
    return QStringLiteral("Waiting in queue");
  }
  if (job.status == QStringLiteral("Pausing")) {
    return QStringLiteral("Pausing safely");
  }
  if (job.status == QStringLiteral("Paused")) {
    return QStringLiteral("Paused");
  }
  if (job.status == QStringLiteral("Canceling")) {
    return QStringLiteral("Canceling safely");
  }
  if (job.status == QStringLiteral("Completed")) {
    return QStringLiteral("Complete");
  }
  if (job.status == QStringLiteral("Failed")) {
    return QStringLiteral("Failed");
  }
  if (job.status == QStringLiteral("Canceled")) {
    return QStringLiteral("Canceled");
  }
  if (job.job_kind == QStringLiteral("catalog_import")) {
    return QStringLiteral("Importing media");
  }
  if (job.job_kind == QStringLiteral("timeline_render")) {
    return QStringLiteral("Rendering timeline");
  }
  return QStringLiteral("Exporting media");
}

QVariantMap affected_item_for_job(const PersistentJobRecord& job) {
  const auto timeline_id = job.metadata_json.value(QStringLiteral("timelineId")).toString().trimmed();
  if (job.job_kind == QStringLiteral("timeline_render") ||
      job.asset_id.startsWith(QStringLiteral("timeline:"))) {
    const auto affected_id = !timeline_id.isEmpty()
                                 ? timeline_id
                                 : job.asset_id.mid(QStringLiteral("timeline:").size());
    return {
        {QStringLiteral("type"), QStringLiteral("timeline")},
        {QStringLiteral("id"), affected_id},
        {QStringLiteral("path"), job.source_path},
        {QStringLiteral("label"), QStringLiteral("Open timeline")},
    };
  }
  if (job.job_kind == QStringLiteral("catalog_import")) {
    return {
        {QStringLiteral("type"), QStringLiteral("import-source")},
        {QStringLiteral("id"), job.source_path},
        {QStringLiteral("path"), job.source_path},
        {QStringLiteral("label"), QStringLiteral("Show source")},
    };
  }
  return {
      {QStringLiteral("type"), QStringLiteral("asset")},
      {QStringLiteral("id"), job.asset_id},
      {QStringLiteral("path"), job.source_path},
      {QStringLiteral("label"), QStringLiteral("Show asset")},
  };
}

bool job_can_retry(const PersistentJobRecord& job) {
  return job.status == QStringLiteral("Failed") ||
         ((job.job_kind == QStringLiteral("catalog_import") ||
           job.job_kind == QStringLiteral("asset_export") ||
           job.job_kind == QStringLiteral("timeline_render")) &&
          job.status == QStringLiteral("Canceled"));
}

QVariantMap center_map_from_job(const PersistentJobRecord& job) {
  auto map = map_from_job(job);
  const auto affected = affected_item_for_job(job);
  const auto purpose = job.metadata_json.value(QStringLiteral("purpose"))
                           .toString().trimmed().toLower();
  const auto stage_token = job.metadata_json.value(QStringLiteral("stage"))
                               .toString().trimmed().toLower();
  const auto scheduling_workflow =
      purpose.contains(QStringLiteral("preview")) ||
              stage_token.contains(QStringLiteral("export_preview"))
          ? QStringLiteral("export-preview")
          : job.job_kind;
  const auto surface = job.job_kind == QStringLiteral("timeline_render")
                           ? QStringLiteral("timeline")
                           : job.job_kind == QStringLiteral("catalog_import")
                                 ? QStringLiteral("library")
                                 : QStringLiteral("export");
  const auto priority_decision = WorkerQueueRegistry::priorityDecision(
      scheduling_workflow,
      surface,
      affected.value(QStringLiteral("id")).toString(),
      normalized_job_priority(job.metadata_json));
  map.insert(QStringLiteral("origin"), QStringLiteral("persistent"));
  map.insert(QStringLiteral("stage"), job_stage(job));
  map.insert(QStringLiteral("priority"), normalized_job_priority(job.metadata_json));
  map.insert(
      QStringLiteral("effectivePriority"),
      priority_decision.value(QStringLiteral("effectivePriority")));
  map.insert(
      QStringLiteral("priorityReason"),
      priority_decision.value(QStringLiteral("reason")));
  map.insert(
      QStringLiteral("deferredByUserIntent"),
      priority_decision.value(QStringLiteral("deferred")));
  map.insert(QStringLiteral("progressKnown"), true);
  map.insert(
      QStringLiteral("canPause"),
      (job.job_kind == QStringLiteral("catalog_import") ||
       job.job_kind == QStringLiteral("asset_export") ||
       job.job_kind == QStringLiteral("timeline_render")) &&
          (job.status == QStringLiteral("Queued") || job.status == QStringLiteral("Running")));
  map.insert(
      QStringLiteral("canResume"),
      (job.job_kind == QStringLiteral("catalog_import") ||
       job.job_kind == QStringLiteral("asset_export") ||
       job.job_kind == QStringLiteral("timeline_render")) &&
          job.status == QStringLiteral("Paused"));
  map.insert(QStringLiteral("canCancel"), cancellation_enabled_for_job(job.job_kind, job.status));
  map.insert(QStringLiteral("canRetry"), job_can_retry(job));
  map.insert(
      QStringLiteral("canChangePriority"),
      job.status == QStringLiteral("Queued") || job.status == QStringLiteral("Paused"));
  map.insert(QStringLiteral("affectedType"), affected.value(QStringLiteral("type")));
  map.insert(QStringLiteral("affectedId"), affected.value(QStringLiteral("id")));
  map.insert(QStringLiteral("affectedPath"), affected.value(QStringLiteral("path")));
  map.insert(QStringLiteral("jumpLabel"), affected.value(QStringLiteral("label")));
  map.insert(QStringLiteral("queueOrder"), render_job_queue_order(job));
  map.insert(QStringLiteral("canDuplicate"), is_render_queue_job(job) &&
                                                job.status != QStringLiteral("Running") &&
                                                job.status != QStringLiteral("Pausing") &&
                                                job.status != QStringLiteral("Canceling"));
  map.insert(QStringLiteral("canRevealOutput"),
             job.status == QStringLiteral("Completed") &&
                 !job.output_path.trimmed().isEmpty() &&
                 QFileInfo::exists(job.output_path));
  return map;
}

}  // namespace

JobQueueModel::JobQueueModel(QObject* parent) : QAbstractListModel(parent) {
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &JobQueueModel::refresh);
  history_reader_ = [](bool recover_interrupted) {
    QString recovery_error;
    if (recover_interrupted) (void)recoverInterruptedJobRecords(&recovery_error);
    JobHistoryReadResult result;
    // The runner owns startup migration. Ordinary observers must not rewrite
    // legacy history concurrently with that recovery pass.
    result.records = loadPersistedJobRecords(&result.error, false);
    if (result.error.isEmpty()) result.error = recovery_error;
    std::sort(result.records.begin(), result.records.end(), job_less_than);
    return result;
  };
  connect(&history_watcher_, &QFutureWatcher<JobHistoryReadResult>::finished, this, [this] {
    if (history_pending_) {
      // A newer change notification supersedes this snapshot. Read once more
      // after this worker finishes; never publish an obsolete intermediate list.
      startHistoryLoad();
      return;
    }
    const auto result = history_watcher_.result();
    history_loading_ = false;
    history_error_ = result.error;
    if (result.error.isEmpty() || (jobs_.isEmpty() && !result.records.isEmpty())) {
      publishJobs(result.records);
    }
    if (!history_loading_) {
      if (result.error.isEmpty()) updateSummaryStatus();
      else setStatus(QStringLiteral("Job history unavailable: %1 Existing history is preserved.").arg(result.error));
      emit loadingChanged();
    }
  });
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
    case StageRole:
      return job_stage(job);
    case PriorityRole:
      return normalized_job_priority(job.metadata_json);
    case CanPauseRole:
      if (!historyMutationsEnabled()) return false;
      return (job.job_kind == QStringLiteral("catalog_import") ||
              job.job_kind == QStringLiteral("asset_export") ||
              job.job_kind == QStringLiteral("timeline_render")) &&
             (job.status == QStringLiteral("Queued") ||
              job.status == QStringLiteral("Running"));
    case CanResumeRole:
      if (!historyMutationsEnabled()) return false;
      return (job.job_kind == QStringLiteral("catalog_import") ||
              job.job_kind == QStringLiteral("asset_export") ||
              job.job_kind == QStringLiteral("timeline_render")) &&
             job.status == QStringLiteral("Paused");
    case CanCancelRole:
      return historyMutationsEnabled() && cancellation_enabled_for_job(job.job_kind, job.status);
    case CanRetryRole:
      return historyMutationsEnabled() && job_can_retry(job);
    case AffectedTypeRole:
      return affected_item_for_job(job).value(QStringLiteral("type"));
    case AffectedIdRole:
      return affected_item_for_job(job).value(QStringLiteral("id"));
    case JumpLabelRole:
      return affected_item_for_job(job).value(QStringLiteral("label"));
    case QueueOrderRole:
      return render_job_queue_order(job);
    case CanMoveUpRole:
      return canMoveRenderJob(job.job_id, -1);
    case CanMoveDownRole:
      return canMoveRenderJob(job.job_id, 1);
    case CanDuplicateRole:
      return canDuplicateRenderJob(job.job_id);
    case CanRevealOutputRole:
      return job.status == QStringLiteral("Completed") &&
             !job.output_path.trimmed().isEmpty() &&
             QFileInfo::exists(job.output_path);
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
      {StageRole, "stage"},
      {PriorityRole, "priority"},
      {CanPauseRole, "canPause"},
      {CanResumeRole, "canResume"},
      {CanCancelRole, "canCancel"},
      {CanRetryRole, "canRetry"},
      {AffectedTypeRole, "affectedType"},
      {AffectedIdRole, "affectedId"},
      {JumpLabelRole, "jumpLabel"},
      {QueueOrderRole, "queueOrder"},
      {CanMoveUpRole, "canMoveUp"},
      {CanMoveDownRole, "canMoveDown"},
      {CanDuplicateRole, "canDuplicate"},
      {CanRevealOutputRole, "canRevealOutput"},
  };
}

int JobQueueModel::jobCount() const { return static_cast<int>(jobs_.size()); }
int JobQueueModel::activeJobCount() const { return activeJobCountValue(); }
int JobQueueModel::failedJobCount() const { return failedJobCountValue(); }
int JobQueueModel::completedJobCount() const { return completedJobCountValue(); }
bool JobQueueModel::busy() const { return activeJobCountValue() > 0; }
bool JobQueueModel::hasFailures() const { return failedJobCountValue() > 0; }
bool JobQueueModel::hasRecoveryReport() const { return recoveredJobCount() > 0; }
int JobQueueModel::recoveredJobCount() const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [](const auto& job) {
        return job.metadata_json.value(QStringLiteral("recoveredAfterRestart")).toBool() &&
               !job.metadata_json.value(QStringLiteral("recoveryAcknowledged")).toBool();
      }));
}

QString JobQueueModel::recoverySummary() const {
  auto awaiting_confirmation = 0;
  auto incomplete = 0;
  auto canceled = 0;
  for (const auto& job : jobs_) {
    if (!job.metadata_json.value(QStringLiteral("recoveredAfterRestart")).toBool() ||
        job.metadata_json.value(QStringLiteral("recoveryAcknowledged")).toBool()) {
      continue;
    }
    if (job.status == QStringLiteral("Paused")) {
      ++awaiting_confirmation;
    } else if (job.status == QStringLiteral("Canceled")) {
      ++canceled;
    } else if (job.status == QStringLiteral("Failed")) {
      ++incomplete;
    }
  }
  return QStringLiteral("Recovered %1 %2 · %3 waiting for review · %4 incomplete · %5 canceled")
      .arg(recoveredJobCount())
      .arg(recoveredJobCount() == 1 ? QStringLiteral("operation") : QStringLiteral("operations"))
      .arg(awaiting_confirmation)
      .arg(incomplete)
      .arg(canceled);
}

QStringList JobQueueModel::recoveryDetails() const {
  QStringList details;
  for (const auto& job : jobs_) {
    if (!job.metadata_json.value(QStringLiteral("recoveredAfterRestart")).toBool() ||
        job.metadata_json.value(QStringLiteral("recoveryAcknowledged")).toBool()) {
      continue;
    }
    const auto label = job.display_label.trimmed().isEmpty() ? job.job_kind : job.display_label;
    const auto action = job.metadata_json.value(QStringLiteral("recoveryAction")).toString();
    details.push_back(QStringLiteral("%1 — %2 (%3)").arg(label, job.status, action));
  }
  return details;
}
QString JobQueueModel::status() const {
  return startup_recovery_error_.isEmpty() ? status_
      : QStringLiteral("Background jobs paused: %1").arg(startup_recovery_error_);
}

void JobQueueModel::setStartupRecoveryError(const QString& error) {
  if (startup_recovery_error_ == error) return;
  startup_recovery_error_ = error;
  emit startupRecoveryErrorChanged();
  emit statusChanged();
  if (!jobs_.isEmpty()) emit dataChanged(index(0), index(rowCount() - 1),
      {CanPauseRole, CanResumeRole, CanCancelRole, CanRetryRole,
       CanMoveUpRole, CanMoveDownRole, CanDuplicateRole});
  emit jobsChanged();
}

void JobQueueModel::refresh() {
  requestHistoryLoad(false);
}

void JobQueueModel::initializeFromStore() {
  requestHistoryLoad(true);
}

void JobQueueModel::acknowledgeRecoveryReport() {
  if (!historyMutationsEnabled()) return;
  for (auto job : jobs_) {
    if (!job.metadata_json.value(QStringLiteral("recoveredAfterRestart")).toBool() ||
        job.metadata_json.value(QStringLiteral("recoveryAcknowledged")).toBool()) {
      continue;
    }
    job.metadata_json.insert(QStringLiteral("recoveryAcknowledged"), true);
    (void)upsertPersistedJobRecord(job, nullptr);
  }
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
                job.status == QStringLiteral("Pausing") ||
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

bool JobQueueModel::hasCurrentFailureForKind(const QString& job_kind) const {
  for (const auto& job : jobs_) {
    if (job.job_kind == job_kind) {
      return job.status == QStringLiteral("Failed");
    }
  }
  return false;
}

int JobQueueModel::totalJobCountForKind(const QString& job_kind) const {
  return static_cast<int>(std::count_if(
      jobs_.cbegin(),
      jobs_.cend(),
      [&](const auto& job) { return job.job_kind == job_kind; }));
}

QVariantMap JobQueueModel::progressSummaryForKind(const QString& job_kind) const {
  const auto normalized_kind = job_kind.trimmed();
  auto total = 0;
  auto completed = 0;
  auto failed = 0;
  auto canceled = 0;
  auto active = 0;
  auto progress_sum = 0;
  for (const auto& job : jobs_) {
    if (job.job_kind != normalized_kind) {
      continue;
    }
    ++total;
    if (job.status == QStringLiteral("Completed")) {
      ++completed;
      progress_sum += 100;
    } else if (job.status == QStringLiteral("Failed")) {
      ++failed;
      progress_sum += 100;
    } else if (job.status == QStringLiteral("Canceled")) {
      ++canceled;
      progress_sum += 100;
    } else {
      if (job.status == QStringLiteral("Queued") ||
          job.status == QStringLiteral("Running") ||
          job.status == QStringLiteral("Pausing") ||
          job.status == QStringLiteral("Canceling")) {
        ++active;
      }
      progress_sum += std::clamp(job.progress, 0, 100);
    }
  }
  return {
      {QStringLiteral("total"), total},
      {QStringLiteral("completed"), completed},
      {QStringLiteral("failed"), failed},
      {QStringLiteral("canceled"), canceled},
      {QStringLiteral("active"), active},
      {QStringLiteral("terminal"), completed + failed + canceled},
      {QStringLiteral("overallPercent"), total > 0 ? progress_sum / total : 0},
  };
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

QVariantList JobQueueModel::jobsForCenter(const QString& job_kind) const {
  const auto normalized_kind = job_kind.trimmed();
  QVariantList values;
  values.reserve(jobs_.size());
  for (const auto& job : jobs_) {
    if (!normalized_kind.isEmpty() && job.job_kind != normalized_kind) {
      continue;
    }
    auto value = center_map_from_job(job);
    if (!historyMutationsEnabled()) {
      for (const auto* key : {"canPause", "canResume", "canCancel", "canRetry",
                              "canChangePriority", "canDuplicate"})
        value.insert(QString::fromLatin1(key), false);
    }
    value.insert(QStringLiteral("canMoveUp"), canMoveRenderJob(job.job_id, -1));
    value.insert(QStringLiteral("canMoveDown"), canMoveRenderJob(job.job_id, 1));
    values.push_back(value);
  }
  return values;
}

QVariantMap JobQueueModel::affectedItemForJob(const QString& job_id) const {
  const auto normalized_id = job_id.trimmed();
  for (const auto& job : jobs_) {
    if (job.job_id == normalized_id) {
      return affected_item_for_job(job);
    }
  }
  return {};
}

bool JobQueueModel::setJobPriority(
    const QString& job_id,
    const QString& priority) {
  if (!historyMutationsEnabled()) return false;
  const auto normalized_id = job_id.trimmed();
  const auto normalized_priority = priority.trimmed().toLower();
  if (normalized_id.isEmpty() ||
      (normalized_priority != QStringLiteral("high") &&
       normalized_priority != QStringLiteral("normal") &&
       normalized_priority != QStringLiteral("low"))) {
    setStatus(QStringLiteral("Choose High, Normal, or Low priority."));
    return false;
  }
  const auto records = loadPersistedJobRecords();
  for (auto job : records) {
    if (job.job_id != normalized_id) {
      continue;
    }
    if (job.status != QStringLiteral("Queued") &&
        job.status != QStringLiteral("Paused")) {
      setStatus(QStringLiteral("Priority can change only while a job is queued or paused."));
      return false;
    }
    job.metadata_json.insert(QStringLiteral("priority"), normalized_priority);
    job.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QString error_message;
    if (!upsertPersistedJobRecord(job, &error_message)) {
      setStatus(error_message.isEmpty()
                    ? QStringLiteral("Unable to update job priority.")
                    : error_message);
      return false;
    }
    reloadJobs();
    setStatus(QStringLiteral("Set job priority to %1.")
                  .arg(normalized_priority));
    return true;
  }
  setStatus(QStringLiteral("The selected job could not be found."));
  return false;
}

bool JobQueueModel::setRenderResourcePolicy(
    const QString& job_id,
    const QString& policy) {
  if (!historyMutationsEnabled()) return false;
  const auto normalized_id = job_id.trimmed();
  const auto normalized_policy = policy.trimmed().toLower();
  if (normalized_id.isEmpty() ||
      (normalized_policy != QStringLiteral("playback-first") &&
       normalized_policy != QStringLiteral("balanced") &&
       normalized_policy != QStringLiteral("export-first"))) {
    setStatus(QStringLiteral(
        "Choose Playback First, Balanced, or Export First resource policy."));
    return false;
  }
  const auto records = loadPersistedJobRecords();
  for (auto job : records) {
    if (job.job_id != normalized_id) {
      continue;
    }
    if (!is_render_queue_job(job) ||
        (job.status != QStringLiteral("Queued") &&
         job.status != QStringLiteral("Paused"))) {
      setStatus(QStringLiteral(
          "Resource policy can change only while a render is queued or paused."));
      return false;
    }
    job.metadata_json.insert(QStringLiteral("resourcePolicy"), normalized_policy);
    job.metadata_json.insert(
        QStringLiteral("resourcePolicyAffectsQuality"), false);
    job.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QString error_message;
    if (!upsertPersistedJobRecord(job, &error_message)) {
      setStatus(error_message.isEmpty()
                    ? QStringLiteral("Unable to update render resource policy.")
                    : error_message);
      return false;
    }
    reloadJobs();
    setStatus(QStringLiteral("Render resource policy set to %1; delivery settings are unchanged.")
                  .arg(normalized_policy));
    return true;
  }
  setStatus(QStringLiteral("The selected render job could not be found."));
  return false;
}

bool JobQueueModel::canMoveRenderJob(
    const QString& job_id,
    int direction) const {
  if (!historyMutationsEnabled()) return false;
  if (direction != -1 && direction != 1) {
    return false;
  }
  const auto queue = reorderable_render_jobs(jobs_);
  for (int index = 0; index < queue.size(); ++index) {
    if (queue.at(index).job_id != job_id.trimmed()) {
      continue;
    }
    const auto target_index = index + direction;
    return target_index >= 0 && target_index < queue.size() &&
           normalized_job_priority(queue.at(index).metadata_json) ==
               normalized_job_priority(queue.at(target_index).metadata_json);
  }
  return false;
}

bool JobQueueModel::moveRenderJob(
    const QString& job_id,
    int direction) {
  if (!historyMutationsEnabled()) return false;
  const auto normalized_id = job_id.trimmed();
  if (normalized_id.isEmpty() || (direction != -1 && direction != 1)) {
    setStatus(QStringLiteral("Choose a queued render and a valid move direction."));
    return false;
  }
  auto records = loadPersistedJobRecords();
  auto queue = reorderable_render_jobs(records);
  auto source_index = -1;
  for (int index = 0; index < queue.size(); ++index) {
    if (queue.at(index).job_id == normalized_id) {
      source_index = index;
      break;
    }
  }
  const auto target_index = source_index + direction;
  if (source_index < 0 || target_index < 0 || target_index >= queue.size()) {
    setStatus(QStringLiteral("That render is already at the edge of its queue."));
    return false;
  }
  if (normalized_job_priority(queue.at(source_index).metadata_json) !=
      normalized_job_priority(queue.at(target_index).metadata_json)) {
    setStatus(QStringLiteral(
        "Priority groups dispatch separately. Change priority before moving across groups."));
    return false;
  }
  std::swap(queue[source_index], queue[target_index]);
  const auto reordered_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  for (int index = 0; index < queue.size(); ++index) {
    auto record = queue.at(index);
    record.metadata_json.insert(
        QStringLiteral("queueOrder"), static_cast<qint64>(index + 1));
    record.metadata_json.insert(QStringLiteral("queueReorderedAt"), reordered_at);
    record.updated_at = reordered_at;
    QString error_message;
    if (!upsertPersistedJobRecord(record, &error_message)) {
      setStatus(error_message.isEmpty()
                    ? QStringLiteral("Unable to persist the render queue order.")
                    : error_message);
      reloadJobs();
      return false;
    }
  }
  reloadJobs();
  setStatus(direction < 0 ? QStringLiteral("Moved render earlier in its priority group.")
                          : QStringLiteral("Moved render later in its priority group."));
  return true;
}

bool JobQueueModel::canDuplicateRenderJob(const QString& job_id) const {
  if (!historyMutationsEnabled()) return false;
  const auto normalized_id = job_id.trimmed();
  return std::any_of(
      jobs_.cbegin(), jobs_.cend(),
      [&normalized_id](const auto& job) {
        return job.job_id == normalized_id && is_render_queue_job(job) &&
               job.status != QStringLiteral("Running") &&
               job.status != QStringLiteral("Pausing") &&
               job.status != QStringLiteral("Canceling");
      });
}

QString JobQueueModel::duplicateRenderJob(const QString& job_id) {
  if (!historyMutationsEnabled()) return {};
  const auto normalized_id = job_id.trimmed();
  const auto records = loadPersistedJobRecords();
  auto source = std::find_if(
      records.cbegin(), records.cend(),
      [&normalized_id](const auto& record) {
        return record.job_id == normalized_id;
      });
  if (source == records.cend() || !is_render_queue_job(*source) ||
      source->status == QStringLiteral("Running") ||
      source->status == QStringLiteral("Pausing") ||
      source->status == QStringLiteral("Canceling")) {
    setStatus(QStringLiteral("Only inactive export and timeline-render jobs can be duplicated."));
    return {};
  }

  auto duplicate = *source;
  duplicate.job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  duplicate.display_label = source->display_label.trimmed().isEmpty()
                                ? QStringLiteral("Render copy")
                                : QStringLiteral("%1 copy").arg(source->display_label);
  duplicate.status = QStringLiteral("Queued");
  duplicate.error_message.clear();
  duplicate.progress = 0;
  duplicate.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  duplicate.updated_at = duplicate.created_at;
  duplicate.metadata_json.remove(QStringLiteral("artifactVerification"));
  duplicate.metadata_json.remove(QStringLiteral("deliverySummary"));
  duplicate.metadata_json.remove(QStringLiteral("exportPreviewJob"));
  duplicate.metadata_json.remove(QStringLiteral("exportRenderJob"));
  duplicate.metadata_json.remove(QStringLiteral("previewExportDiff"));
  duplicate.metadata_json.remove(QStringLiteral("cancellationRequested"));
  duplicate.metadata_json.remove(QStringLiteral("pauseRequested"));
  duplicate.metadata_json.remove(QStringLiteral("canceled"));
  duplicate.metadata_json.remove(QStringLiteral("recoveredAfterRestart"));
  duplicate.metadata_json.remove(QStringLiteral("requiresRecoveryConfirmation"));
  duplicate.metadata_json.insert(QStringLiteral("duplicatedFromJobId"), source->job_id);
  duplicate.metadata_json.insert(QStringLiteral("duplicatedAt"), duplicate.created_at);
  duplicate.metadata_json.insert(QStringLiteral("retryCount"), 0);
  auto maximum_order = 0LL;
  for (const auto& record : records) {
    maximum_order = qMax(maximum_order, render_job_queue_order(record));
  }
  duplicate.metadata_json.insert(QStringLiteral("queueOrder"), maximum_order + 1);

  if (duplicate.job_kind == QStringLiteral("asset_export")) {
    duplicate.output_path.clear();
  } else {
    const QFileInfo original_output(source->output_path);
    if (source->output_path.trimmed().isEmpty() || original_output.absolutePath().isEmpty()) {
      setStatus(QStringLiteral("The timeline render has no reusable output destination."));
      return {};
    }
    QSet<QString> reserved_paths;
    for (const auto& record : records) {
      reserved_paths.insert(QFileInfo(record.output_path).absoluteFilePath());
    }
    const auto suffix = original_output.completeSuffix();
    const auto stem = original_output.completeBaseName();
    for (int copy_number = 1; copy_number < 1000; ++copy_number) {
      const auto copy_token = copy_number == 1
                                  ? QStringLiteral("copy")
                                  : QStringLiteral("copy-%1").arg(copy_number);
      const auto file_name = suffix.isEmpty()
                                 ? QStringLiteral("%1-%2").arg(stem, copy_token)
                                 : QStringLiteral("%1-%2.%3").arg(stem, copy_token, suffix);
      const auto candidate = QDir(original_output.absolutePath()).filePath(file_name);
      if (!QFileInfo::exists(candidate) &&
          !reserved_paths.contains(QFileInfo(candidate).absoluteFilePath())) {
        duplicate.output_path = candidate;
        break;
      }
    }
    if (duplicate.output_path == source->output_path) {
      setStatus(QStringLiteral("Unable to allocate a unique timeline-render output name."));
      return {};
    }
  }

  QString error_message;
  if (!upsertPersistedJobRecord(duplicate, &error_message)) {
    setStatus(error_message.isEmpty()
                  ? QStringLiteral("Unable to duplicate the render job.")
                  : error_message);
    return {};
  }
  reloadJobs();
  setStatus(QStringLiteral("Duplicated '%1' at the end of the render queue.")
                .arg(source->display_label));
  return duplicate.job_id;
}

QString JobQueueModel::revealOutputPath(const QString& job_id) const {
  const auto normalized_id = job_id.trimmed();
  for (const auto& job : jobs_) {
    if (job.job_id == normalized_id && is_render_queue_job(job) &&
        job.status == QStringLiteral("Completed")) {
      const auto path = QFileInfo(job.output_path).absoluteFilePath();
      return QFileInfo(path).isReadable() ? path : QString{};
    }
  }
  return {};
}

bool JobQueueModel::revealJobOutput(const QString& job_id) const {
  const auto output_path = revealOutputPath(job_id);
  if (output_path.isEmpty()) {
    return false;
  }
#ifdef Q_OS_MACOS
  return QProcess::startDetached(
      QStringLiteral("/usr/bin/open"),
      {QStringLiteral("-R"), output_path});
#else
  return QDesktopServices::openUrl(
      QUrl::fromLocalFile(QFileInfo(output_path).absolutePath()));
#endif
}

QString JobQueueModel::statusForKind(const QString& job_kind, const QString& idle_status) const {
  if (!startup_recovery_error_.isEmpty()) return status();
  QVector<PersistentJobRecord> matching_jobs;
  for (const auto& job : jobs_) {
    if (job.job_kind == job_kind) {
      matching_jobs.push_back(job);
    }
  }
  return summary_status_for_jobs(matching_jobs, idle_status);
}

bool JobQueueModel::canCancelJob(const QString& job_id) const {
  if (!historyMutationsEnabled()) return false;
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
  if (!historyMutationsEnabled()) return false;
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
  if (!historyMutationsEnabled()) return false;
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

bool JobQueueModel::canPauseJob(const QString& job_id) const {
  if (!historyMutationsEnabled()) return false;
  const auto trimmed_id = job_id.trimmed();
  return std::any_of(
      jobs_.cbegin(),
      jobs_.cend(),
      [&](const auto& job) {
        return job.job_id == trimmed_id &&
               (job.job_kind == QStringLiteral("catalog_import") ||
                job.job_kind == QStringLiteral("asset_export") ||
                job.job_kind == QStringLiteral("timeline_render")) &&
               (job.status == QStringLiteral("Queued") || job.status == QStringLiteral("Running"));
      });
}

bool JobQueueModel::canResumeJob(const QString& job_id) const {
  if (!historyMutationsEnabled()) return false;
  const auto trimmed_id = job_id.trimmed();
  return std::any_of(
      jobs_.cbegin(),
      jobs_.cend(),
      [&](const auto& job) {
        return job.job_id == trimmed_id &&
               (job.job_kind == QStringLiteral("catalog_import") ||
                job.job_kind == QStringLiteral("asset_export") ||
                job.job_kind == QStringLiteral("timeline_render")) &&
               job.status == QStringLiteral("Paused");
      });
}

bool JobQueueModel::pauseJob(const QString& job_id) {
  if (!historyMutationsEnabled()) return false;
  QString error_message;
  if (!pausePersistedJobRecord(job_id, &error_message)) {
    if (!error_message.isEmpty()) {
      setStatus(error_message);
    }
    return false;
  }
  reloadJobs();
  setStatus(QStringLiteral("Pause requested for the selected job."));
  return true;
}

bool JobQueueModel::resumeJob(const QString& job_id) {
  if (!historyMutationsEnabled()) return false;
  QString error_message;
  if (!resumePersistedJobRecord(job_id, &error_message)) {
    if (!error_message.isEmpty()) {
      setStatus(error_message);
    }
    return false;
  }
  reloadJobs();
  setStatus(QStringLiteral("Resumed the selected job."));
  return true;
}

void JobQueueModel::retryFailedJobs() {
  if (!historyMutationsEnabled()) return;
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
  if (!historyMutationsEnabled()) return;
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
  if (!historyMutationsEnabled()) return;
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
  // Job-control methods still promise an immediately updated model after a
  // synchronous store mutation. Preserve that contract until those operations
  // have explicit asynchronous completion; ordinary/history refresh uses the
  // worker instead. Invalidate any older background snapshot before publishing.
  if (history_loading_) history_pending_ = true;
  QString error;
  auto records = loadPersistedJobRecords(&error);
  history_error_ = error;
  if (!error.isEmpty() && !jobs_.isEmpty()) {
    emit loadingChanged();
    setStatus(QStringLiteral("Job history unavailable: %1 Existing history is preserved.").arg(error));
    return;
  }
  std::sort(records.begin(), records.end(), job_less_than);
  publishJobs(records);
  emit loadingChanged();
  if (error.isEmpty()) updateSummaryStatus();
  else setStatus(QStringLiteral("Job history unavailable: %1 Existing history is preserved.").arg(error));
}

void JobQueueModel::publishJobs(const QVector<PersistentJobRecord>& records) {
  // Preserve delegate identity across progress, completion, and queue changes.
  // Resetting the model destroys focused controls and completion observers.
  QSet<QString> retained_ids;
  for (const auto& record : records) retained_ids.insert(record.job_id);
  for (int row = static_cast<int>(jobs_.size()) - 1; row >= 0; --row) {
    if (retained_ids.contains(jobs_[row].job_id)) continue;
    beginRemoveRows({}, row, row);
    jobs_.removeAt(row);
    endRemoveRows();
  }
  for (int row = 0; row < static_cast<int>(records.size()); ++row) {
    int existing = row;
    while (existing < jobs_.size() && jobs_[existing].job_id != records[row].job_id) ++existing;
    if (existing == jobs_.size()) {
      beginInsertRows({}, row, row);
      jobs_.insert(row, records[row]);
      endInsertRows();
    } else {
      if (existing != row) {
        beginMoveRows({}, existing, existing, {}, row);
        jobs_.move(existing, row);
        endMoveRows();
      }
      jobs_[row] = records[row];
    }
  }
  if (!jobs_.isEmpty()) emit dataChanged(index(0), index(rowCount() - 1));
  emit jobsChanged();
}

void JobQueueModel::requestHistoryLoad(bool recover_interrupted) {
  history_pending_ = true;
  recovery_pending_ = recovery_pending_ || recover_interrupted;
  if (!history_loading_) {
    history_loading_ = true;
    history_error_.clear();
    emit loadingChanged();
    setStatus(QStringLiteral("Loading job history…"));
    startHistoryLoad();
  }
}

void JobQueueModel::startHistoryLoad() {
  const auto recover_interrupted = recovery_pending_;
  recovery_pending_ = false;
  history_pending_ = false;
  // Copy the reader, not this/model state: destruction never leaves a worker
  // accessing a deleted QObject, and QAbstractListModel updates stay on the UI.
  history_watcher_.setFuture(QtConcurrent::run([reader = history_reader_, recover_interrupted] {
    return reader(recover_interrupted);
  }));
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
               job.status == QStringLiteral("Pausing") ||
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
