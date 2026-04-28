#include "export_job_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QUrl>

#include <algorithm>

#include "photo_render_utils.h"

namespace photo_editor::desktop {

namespace {

QString iso_now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString normalized_local_path(const QString& path) {
  const auto trimmed = path.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  const QUrl url(trimmed);
  if (url.isValid() && url.isLocalFile()) {
    return QDir::cleanPath(url.toLocalFile());
  }

  return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

QString display_label_for_job(const QString& display_label, const QString& source_path) {
  const auto trimmed = display_label.trimmed();
  if (!trimmed.isEmpty()) {
    return trimmed;
  }

  const QFileInfo source_info(source_path);
  if (!source_info.completeBaseName().isEmpty()) {
    return source_info.completeBaseName();
  }

  if (!source_info.fileName().isEmpty()) {
    return source_info.fileName();
  }

  return QStringLiteral("Export");
}

bool is_photo_media(const QString& media_type) {
  return media_type.trimmed().compare(QStringLiteral("photo"), Qt::CaseInsensitive) == 0;
}

bool is_original_copy_preset(const QString& preset) {
  return preset.trimmed().compare(QStringLiteral("Original Copy"), Qt::CaseInsensitive) == 0;
}

bool should_render_photo_job(const QString& media_type, const QString& preset) {
  return is_photo_media(media_type) && !is_original_copy_preset(preset);
}

}  // namespace

ExportJobController::ExportJobController(QObject* parent) : QObject(parent) {
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &ExportJobController::refreshJobs);
  loadPersistedJobs(false);
}

QVariantList ExportJobController::jobs() const {
  QVariantList values;
  values.reserve(jobs_.size());
  for (const auto& job : jobs_) {
    values.push_back(jobMap(job));
  }
  return values;
}

int ExportJobController::jobCount() const { return static_cast<int>(jobs_.size()); }

bool ExportJobController::busy() const { return busy_; }

QString ExportJobController::status() const { return status_; }

QString ExportJobController::defaultDestinationDirectory() const {
  auto root = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (root.isEmpty()) {
    root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  }
  if (root.isEmpty()) {
    root = QDir::homePath();
  }

  const auto destination = QDir(root).filePath(QStringLiteral("PhotoEditor Exports"));
  QDir().mkpath(destination);
  return destination;
}

QString ExportJobController::pickDestinationFolder(const QString& title) const {
  return QFileDialog::getExistingDirectory(
      nullptr,
      title,
      defaultDestinationDirectory());
}

bool ExportJobController::queueExport(
    const QString& asset_id,
    const QString& media_type,
    const QString& display_label,
    const QString& source_path,
    const QString& edited_preview_source,
    const QString& recipe_json,
    const QString& preset,
    const QString& destination_directory) {
  const auto normalized_destination = normalized_local_path(destination_directory);
  if (normalized_destination.isEmpty()) {
    setStatus(QStringLiteral("Choose an export destination first."));
    return false;
  }

  if (!QDir(normalized_destination).exists() && !QDir().mkpath(normalized_destination)) {
    setStatus(QStringLiteral("Unable to create the export destination."));
    return false;
  }

  ExportJob job;
  job.job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  job.asset_id = asset_id.trimmed();
  job.media_type = media_type.trimmed().toLower();
  job.source_path = normalized_local_path(source_path);
  job.edited_preview_source = edited_preview_source.trimmed();
  job.recipe_json = recipe_json.trimmed();
  job.preset = preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed();
  job.destination_directory = normalized_destination;
  job.display_label = display_label_for_job(display_label, job.source_path);
  job.created_at = iso_now();
  job.updated_at = job.created_at;

  if (should_render_photo_job(job.media_type, job.preset)) {
    if (job.source_path.isEmpty() || !QFileInfo::exists(job.source_path) || !preview_supported(job.source_path)) {
      setStatus(QStringLiteral("The selected photo source is unavailable for rendered export."));
      return false;
    }
  } else {
    bool uses_preview = false;
    if (resolvedSourcePath(job, &uses_preview).isEmpty()) {
      setStatus(QStringLiteral("No export source is available for the current selection."));
      return false;
    }
  }

  jobs_.push_back(job);
  persistJob(job);
  loadPersistedJobs();
  setStatus(QStringLiteral("Queued '%1' for export.").arg(job.display_label));

  return true;
}

int ExportJobController::queueBatchExport(
    const QVariantList& assets,
    const QString& preset,
    const QString& destination_directory) {
  int queued = 0;
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    const auto media_type = asset.value(QStringLiteral("mediaType")).toString().trimmed().toLower();
    const auto file_path = asset.value(QStringLiteral("filePath")).toString();
    const auto recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
    const auto display_label = asset.value(QStringLiteral("displayLabel")).toString();
    const auto effective_preset =
        is_photo_media(media_type) ? preset : QStringLiteral("Original Copy");
    if (queueExport(asset_id, media_type, display_label, file_path,
                    QString(), recipe_json, effective_preset, destination_directory)) {
      ++queued;
    }
  }
  return queued;
}

void ExportJobController::retryJob(const QString& job_id) {
  const auto index = jobIndexById(job_id);
  if (index < 0) {
    return;
  }

  if (jobs_[index].status != QStringLiteral("Failed")) {
    return;
  }
  const auto label = jobs_[index].display_label;

  QString error_message;
  if (!retryPersistedJobRecord(job_id, &error_message)) {
    if (!error_message.isEmpty()) {
      setStatus(error_message);
    }
    return;
  }

  loadPersistedJobs();
  setStatus(QStringLiteral("Retrying '%1'.").arg(label));
}

void ExportJobController::removeJob(const QString& job_id) {
  const auto index = jobIndexById(job_id);
  if (index < 0) {
    return;
  }

  if (jobs_[index].status == QStringLiteral("Running") ||
      jobs_[index].status == QStringLiteral("Canceling")) {
    setStatus(QStringLiteral("Wait for the running export to finish before removing it."));
    return;
  }

  removePersistedJob(job_id);
  loadPersistedJobs();

  if (jobs_.isEmpty()) {
    setStatus(QStringLiteral("Export queue ready."));
  }
}

void ExportJobController::clearFinishedJobs() {
  for (qsizetype index = jobs_.size(); index > 0; --index) {
    const auto& job = jobs_.at(index - 1);
    if (job.status == QStringLiteral("Completed") ||
        job.status == QStringLiteral("Failed") ||
        job.status == QStringLiteral("Canceled")) {
      removePersistedJob(job.job_id);
    }
  }

  loadPersistedJobs();
  if (jobs_.isEmpty()) {
    setStatus(QStringLiteral("Export queue ready."));
  }
}

QVariantMap ExportJobController::jobMap(const ExportJob& job) const {
  QVariantMap value;
  value.insert(QStringLiteral("jobId"), job.job_id);
  value.insert(QStringLiteral("assetId"), job.asset_id);
  value.insert(QStringLiteral("mediaType"), job.media_type);
  value.insert(QStringLiteral("displayLabel"), job.display_label);
  value.insert(QStringLiteral("preset"), job.preset);
  value.insert(QStringLiteral("destinationDirectory"), job.destination_directory);
  value.insert(QStringLiteral("outputPath"), job.output_path);
  value.insert(QStringLiteral("status"), job.status);
  value.insert(QStringLiteral("errorMessage"), job.error_message);
  value.insert(QStringLiteral("createdAt"), job.created_at);
  value.insert(QStringLiteral("updatedAt"), job.updated_at);
  value.insert(QStringLiteral("progress"), job.progress);
  return value;
}

int ExportJobController::jobIndexById(const QString& job_id) const {
  for (int index = 0; index < jobs_.size(); ++index) {
    if (jobs_.at(index).job_id == job_id) {
      return index;
    }
  }

  return -1;
}

ExportJobController::ExportJob ExportJobController::jobFromRecord(const PersistentJobRecord& record) const {
  ExportJob job;
  job.job_id = record.job_id;
  job.asset_id = record.asset_id;
  job.media_type = record.media_type;
  job.display_label = record.display_label;
  job.source_path = record.source_path;
  job.edited_preview_source = record.edited_preview_source;
  job.recipe_json = record.recipe_json;
  job.preset = record.preset_name;
  job.destination_directory = record.destination_path;
  job.output_path = record.output_path;
  job.status = record.status;
  job.error_message = record.error_message;
  job.progress = record.progress;
  job.created_at = record.created_at;
  job.updated_at = record.updated_at;
  if (job.preset.isEmpty()) {
    job.preset = QStringLiteral("Full Resolution");
  }
  if (job.status.isEmpty()) {
    job.status = QStringLiteral("Queued");
  }
  return job;
}

void ExportJobController::loadPersistedJobs(bool emit_signal) {
  const auto previous_jobs = jobs_;
  QVector<ExportJob> next_jobs;
  const auto records = loadPersistedJobRecords();
  next_jobs.reserve(records.size());
  for (const auto& record : records) {
    if (record.job_kind != QStringLiteral("asset_export")) {
      continue;
    }
    auto job = jobFromRecord(record);
    if (job.job_id.isEmpty()) {
      continue;
    }
    next_jobs.push_back(job);
  }

  const auto jobs_changed = [&]() {
    if (previous_jobs.size() != next_jobs.size()) {
      return true;
    }
    for (int index = 0; index < previous_jobs.size(); ++index) {
      const auto& lhs = previous_jobs.at(index);
      const auto& rhs = next_jobs.at(index);
      if (lhs.job_id != rhs.job_id || lhs.asset_id != rhs.asset_id || lhs.media_type != rhs.media_type ||
          lhs.display_label != rhs.display_label || lhs.source_path != rhs.source_path ||
          lhs.edited_preview_source != rhs.edited_preview_source || lhs.recipe_json != rhs.recipe_json ||
          lhs.preset != rhs.preset || lhs.destination_directory != rhs.destination_directory ||
          lhs.output_path != rhs.output_path || lhs.status != rhs.status ||
          lhs.error_message != rhs.error_message || lhs.created_at != rhs.created_at ||
          lhs.updated_at != rhs.updated_at || lhs.progress != rhs.progress) {
        return true;
      }
    }
    return false;
  }();

  jobs_ = next_jobs;
  updateBusyState();
  if (emit_signal && jobs_changed) {
    emit jobsChanged();
  }
}

void ExportJobController::persistJob(const ExportJob& job) const {
  PersistentJobRecord record;
  record.job_id = job.job_id;
  record.job_kind = QStringLiteral("asset_export");
  record.asset_id = job.asset_id;
  record.media_type = job.media_type;
  record.display_label = job.display_label;
  record.source_path = job.source_path;
  record.edited_preview_source = job.edited_preview_source;
  record.recipe_json = job.recipe_json;
  record.preset_name = job.preset;
  record.destination_path = job.destination_directory;
  record.output_path = job.output_path;
  record.status = job.status;
  record.error_message = job.error_message;
  record.progress = job.progress;
  record.created_at = job.created_at;
  record.updated_at = job.updated_at;
  [[maybe_unused]] const auto persisted = upsertPersistedJobRecord(record, nullptr);
}

void ExportJobController::removePersistedJob(const QString& job_id) const {
  if (job_id.trimmed().isEmpty()) {
    return;
  }
  [[maybe_unused]] const auto removed = removePersistedJobRecord(job_id, nullptr);
}

QString ExportJobController::resolvedSourcePath(const ExportJob& job, bool* uses_preview) const {
  const auto preview_path = normalized_local_path(job.edited_preview_source);
  const auto source_path = normalized_local_path(job.source_path);
  const auto prefer_preview = is_photo_media(job.media_type) && !is_original_copy_preset(job.preset);

  if (uses_preview != nullptr) {
    *uses_preview = false;
  }

  if (prefer_preview && !preview_path.isEmpty() && QFileInfo::exists(preview_path)) {
    if (uses_preview != nullptr) {
      *uses_preview = true;
    }
    return preview_path;
  }

  if (!source_path.isEmpty() && QFileInfo::exists(source_path)) {
    return source_path;
  }

  if (!preview_path.isEmpty() && QFileInfo::exists(preview_path)) {
    if (uses_preview != nullptr) {
      *uses_preview = true;
    }
    return preview_path;
  }

  return {};
}

void ExportJobController::refreshJobs() {
  loadPersistedJobs();
}

void ExportJobController::updateBusyState() {
  const auto has_active_jobs = std::any_of(
      jobs_.cbegin(),
      jobs_.cend(),
      [](const auto& job) {
        return job.status == QStringLiteral("Queued") ||
               job.status == QStringLiteral("Running") ||
               job.status == QStringLiteral("Canceling");
      });
  setBusy(has_active_jobs);

  if (jobs_.isEmpty()) {
    setStatus(QStringLiteral("Export queue ready."));
    return;
  }

  if (has_active_jobs) {
    setStatus(QStringLiteral("Export queue active."));
    return;
  }

  const auto has_failures = std::any_of(
      jobs_.cbegin(),
      jobs_.cend(),
      [](const auto& job) { return job.status == QStringLiteral("Failed"); });
  setStatus(has_failures ? QStringLiteral("Export queue needs attention.") : QStringLiteral("Export queue idle."));
}

void ExportJobController::setBusy(bool busy) {
  if (busy_ == busy) {
    return;
  }

  busy_ = busy;
  emit busyChanged();
}

void ExportJobController::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }

  status_ = status;
  emit statusChanged();
}

}  // namespace photo_editor::desktop
