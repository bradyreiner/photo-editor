#include "export_job_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QUrl>

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

QString sanitize_token(const QString& value, const QString& fallback) {
  auto cleaned = value.trimmed();
  cleaned.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
  cleaned.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));
  cleaned.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
  return cleaned.isEmpty() ? fallback : cleaned;
}

QString preset_token(const QString& preset) {
  return sanitize_token(preset.toLower(), QStringLiteral("export"));
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

}  // namespace

ExportJobController::ExportJobController(QObject* parent) : QObject(parent) {}

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
  job.preset = preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed();
  job.destination_directory = normalized_destination;
  job.display_label = display_label_for_job(display_label, job.source_path);
  job.created_at = iso_now();
  job.updated_at = job.created_at;

  bool uses_preview = false;
  if (resolvedSourcePath(job, &uses_preview).isEmpty()) {
    setStatus(QStringLiteral("No export source is available for the current selection."));
    return false;
  }

  jobs_.push_back(job);
  emit jobsChanged();
  setStatus(QStringLiteral("Queued '%1' for export.").arg(job.display_label));

  if (!busy_) {
    QTimer::singleShot(0, this, &ExportJobController::processNextQueuedJob);
  }

  return true;
}

void ExportJobController::retryJob(const QString& job_id) {
  const auto index = jobIndexById(job_id);
  if (index < 0) {
    return;
  }

  auto& job = jobs_[index];
  if (job.status != QStringLiteral("Failed")) {
    return;
  }

  job.status = QStringLiteral("Queued");
  job.error_message.clear();
  job.progress = 0;
  job.updated_at = iso_now();
  emit jobsChanged();
  setStatus(QStringLiteral("Retrying '%1'.").arg(job.display_label));

  if (!busy_) {
    QTimer::singleShot(0, this, &ExportJobController::processNextQueuedJob);
  }
}

void ExportJobController::removeJob(const QString& job_id) {
  const auto index = jobIndexById(job_id);
  if (index < 0) {
    return;
  }

  if (jobs_[index].status == QStringLiteral("Running")) {
    setStatus(QStringLiteral("Wait for the running export to finish before removing it."));
    return;
  }

  jobs_.removeAt(index);
  emit jobsChanged();

  if (jobs_.isEmpty()) {
    setStatus(QStringLiteral("Export queue ready."));
  }
}

void ExportJobController::clearFinishedJobs() {
  for (qsizetype index = jobs_.size(); index > 0; --index) {
    const auto& job = jobs_.at(index - 1);
    if (job.status == QStringLiteral("Completed") || job.status == QStringLiteral("Failed")) {
      jobs_.removeAt(index - 1);
    }
  }

  emit jobsChanged();
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

QString ExportJobController::outputFilePathForJob(
    const ExportJob& job,
    const QString& source_path,
    bool uses_preview) const {
  const QFileInfo source_info(source_path);
  const auto safe_label =
      sanitize_token(display_label_for_job(job.display_label, source_path), QStringLiteral("export"));
  const auto suffix =
      uses_preview ? QStringLiteral("jpg") : source_info.suffix().toLower();
  const auto extension = suffix.isEmpty() ? QString{} : QStringLiteral(".%1").arg(suffix);

  auto stem = safe_label;
  if (!is_original_copy_preset(job.preset)) {
    stem += QStringLiteral("-%1").arg(preset_token(job.preset));
  }

  const QDir output_dir(job.destination_directory);
  auto candidate = output_dir.filePath(stem + extension);
  if (!QFileInfo::exists(candidate)) {
    return candidate;
  }

  for (int attempt = 2; attempt < 1000; ++attempt) {
    const auto numbered = output_dir.filePath(QStringLiteral("%1-%2%3").arg(stem).arg(attempt).arg(extension));
    if (!QFileInfo::exists(numbered)) {
      return numbered;
    }
  }

  return {};
}

void ExportJobController::processNextQueuedJob() {
  auto next_index = -1;
  for (int index = 0; index < jobs_.size(); ++index) {
    if (jobs_.at(index).status == QStringLiteral("Queued")) {
      next_index = index;
      break;
    }
  }

  if (next_index < 0) {
    setBusy(false);
    if (!jobs_.isEmpty()) {
      setStatus(QStringLiteral("Export queue idle."));
    }
    return;
  }

  setBusy(true);
  auto& job = jobs_[next_index];
  job.status = QStringLiteral("Running");
  job.progress = 20;
  job.updated_at = iso_now();
  emit jobsChanged();

  bool uses_preview = false;
  const auto source_path = resolvedSourcePath(job, &uses_preview);
  if (source_path.isEmpty()) {
    failJob(next_index, QStringLiteral("The export source is no longer available."));
    return;
  }

  const auto output_path = outputFilePathForJob(job, source_path, uses_preview);
  if (output_path.isEmpty()) {
    failJob(next_index, QStringLiteral("Unable to allocate an export file name."));
    return;
  }

  QDir().mkpath(QFileInfo(output_path).absolutePath());
  QFile::remove(output_path);
  if (!QFile::copy(source_path, output_path)) {
    failJob(next_index, QStringLiteral("Unable to write the exported file."));
    return;
  }

  finishJob(next_index, output_path);
}

void ExportJobController::finishJob(int index, const QString& output_path) {
  if (index < 0 || index >= jobs_.size()) {
    return;
  }

  auto& job = jobs_[index];
  job.output_path = output_path;
  job.status = QStringLiteral("Completed");
  job.error_message.clear();
  job.progress = 100;
  job.updated_at = iso_now();
  emit jobsChanged();
  setStatus(QStringLiteral("Exported '%1'.").arg(job.display_label));

  QTimer::singleShot(0, this, &ExportJobController::processNextQueuedJob);
}

void ExportJobController::failJob(int index, const QString& error_message) {
  if (index < 0 || index >= jobs_.size()) {
    return;
  }

  auto& job = jobs_[index];
  job.status = QStringLiteral("Failed");
  job.error_message = error_message;
  job.progress = 0;
  job.updated_at = iso_now();
  emit jobsChanged();
  setStatus(error_message);

  QTimer::singleShot(0, this, &ExportJobController::processNextQueuedJob);
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
