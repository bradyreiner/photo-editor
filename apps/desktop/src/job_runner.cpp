#include "job_runner.h"

#include "app_bootstrap.h"
#include "observability.h"
#include "timeline_asset_store.h"
#include "timeline_document_controller.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QUrl>

#include <algorithm>

#include "edit_recipe_state.h"
#include "job_store.h"
#include "photo_render_utils.h"

namespace photo_editor::desktop {

namespace {

struct ExportImageOptions final {
  int max_edge = 0;
  int jpeg_quality = 95;
};

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

QString display_label_for_job(const PersistentJobRecord& job) {
  const auto trimmed = job.display_label.trimmed();
  if (!trimmed.isEmpty()) {
    return trimmed;
  }

  const QFileInfo source_info(job.source_path);
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

bool should_render_photo_job(const PersistentJobRecord& job) {
  return is_photo_media(job.media_type) && !is_original_copy_preset(job.preset_name);
}

QString resolved_source_path(const PersistentJobRecord& job, bool* uses_preview) {
  const auto preview_path = normalized_local_path(job.edited_preview_source);
  const auto source_path = normalized_local_path(job.source_path);
  const auto prefer_preview = is_photo_media(job.media_type) && !is_original_copy_preset(job.preset_name);

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

ExportImageOptions export_image_options_for_preset(const QString& preset) {
  const auto normalized = preset.trimmed();
  if (normalized.compare(QStringLiteral("Quick Web"), Qt::CaseInsensitive) == 0) {
    return {.max_edge = 1440, .jpeg_quality = 82};
  }
  if (normalized.compare(QStringLiteral("Client Preview"), Qt::CaseInsensitive) == 0) {
    return {.max_edge = 2048, .jpeg_quality = 86};
  }
  if (normalized.compare(QStringLiteral("Web Proof"), Qt::CaseInsensitive) == 0) {
    return {.max_edge = 2560, .jpeg_quality = 88};
  }
  return {.max_edge = 0, .jpeg_quality = 95};
}

bool render_photo_export(
    const QString& source_path,
    const QString& recipe_json,
    const QString& preset,
    const QString& output_path) {
  const auto state = parse_edit_recipe_json(recipe_json);
  QImage image;
  if (!decode_source_image(source_path, &image, 0, state)) {
    return false;
  }

  image = apply_crop(image, state);
  apply_color_adjustments(&image, state);

  const auto options = export_image_options_for_preset(preset);
  if (options.max_edge > 0 && (image.width() > options.max_edge || image.height() > options.max_edge)) {
    image = image.scaled(options.max_edge, options.max_edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }

  QImageWriter writer(output_path, "jpg");
  writer.setQuality(options.jpeg_quality);
  return writer.write(image);
}

QString output_file_path_for_job(
    const PersistentJobRecord& job,
    const QString& source_path,
    bool rendered_output) {
  const QFileInfo source_info(source_path);
  const auto safe_label = sanitize_token(display_label_for_job(job), QStringLiteral("export"));
  const auto suffix = rendered_output ? QStringLiteral("jpg") : source_info.suffix().toLower();
  const auto extension = suffix.isEmpty() ? QString{} : QStringLiteral(".%1").arg(suffix);

  auto stem = safe_label;
  if (!is_original_copy_preset(job.preset_name)) {
    stem += QStringLiteral("-%1").arg(preset_token(job.preset_name));
  }

  const QDir output_dir(job.destination_path);
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

bool queued_job_less_than(const PersistentJobRecord& lhs, const PersistentJobRecord& rhs) {
  const auto lhs_created = lhs.created_at.trimmed();
  const auto rhs_created = rhs.created_at.trimmed();
  if (lhs_created != rhs_created) {
    return lhs_created < rhs_created;
  }
  return lhs.job_id < rhs.job_id;
}

QVariantMap run_export_work(
    const QString& source_path,
    const QString& recipe_json,
    const QString& preset_name,
    const QString& output_path,
    bool render_photo) {
  QElapsedTimer timer;
  timer.start();
  QVariantMap result;
  result.insert(QStringLiteral("success"), false);
  result.insert(QStringLiteral("outputPath"), output_path);

  QDir().mkpath(QFileInfo(output_path).absolutePath());
  QFile::remove(output_path);
  const auto success = render_photo
                           ? render_photo_export(source_path, recipe_json, preset_name, output_path)
                           : QFile::copy(source_path, output_path);
  result.insert(QStringLiteral("success"), success);
  if (!success) {
    result.insert(
        QStringLiteral("errorMessage"),
        render_photo ? QStringLiteral("Unable to render the exported photo.")
                     : QStringLiteral("Unable to write the exported file."));
  }
  result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  return result;
}

QVariantMap run_timeline_render_work(
    const QString& document_path,
    const QString& output_path,
    const QString& timeline_id) {
  QElapsedTimer timer;
  timer.start();
  QVariantMap result;
  result.insert(QStringLiteral("success"), false);
  result.insert(QStringLiteral("outputPath"), output_path);

  TimelineDocumentController controller;
  if (!controller.loadTimeline(document_path)) {
    result.insert(QStringLiteral("errorMessage"), controller.status());
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }

  if (!controller.renderCurrentTimeline(output_path)) {
    result.insert(QStringLiteral("errorMessage"), controller.status());
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }

  const auto poster_source = controller.generateVideoPoster(output_path, timeline_id);
  if (!poster_source.isEmpty()) {
    result.insert(QStringLiteral("posterPath"), QUrl(poster_source).toLocalFile());
  }
  result.insert(QStringLiteral("success"), true);
  result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  return result;
}

bool save_document_object(const QString& document_path, const QJsonObject& document, QString* error_message) {
  if (document_path.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline document path is missing.");
    }
    return false;
  }

  QDir().mkpath(QFileInfo(document_path).absolutePath());
  QSaveFile file(document_path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to write the saved timeline document.");
    }
    return false;
  }

  file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to commit the saved timeline document.");
    }
    return false;
  }

  return true;
}

bool update_timeline_render_artifacts(
    const PersistentJobRecord& job,
    const QString& poster_path,
    QString* error_message) {
  const auto timeline_id = job.metadata_json.value(QStringLiteral("timelineId")).toString().trimmed();
  if (timeline_id.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render job is missing its timeline id.");
    }
    return false;
  }

  auto records = loadTimelineAssetRecords();
  const auto updated_at = iso_now();
  TimelineAssetRecord persisted_record;
  auto updated_existing = false;
  for (auto& record : records) {
    if (record.timeline_id != timeline_id) {
      continue;
    }

    record.asset_id = job.asset_id.isEmpty() ? QStringLiteral("timeline:%1").arg(timeline_id) : job.asset_id;
    record.name = job.display_label;
    record.document_path = job.source_path;
    record.render_path = job.output_path;
    if (!poster_path.isEmpty()) {
      record.poster_path = poster_path;
    }
    if (record.created_at.isEmpty()) {
      record.created_at = job.created_at;
    }
    record.updated_at = updated_at;
    persisted_record = record;
    updated_existing = true;
    break;
  }

  if (!updated_existing) {
    persisted_record.asset_id = job.asset_id.isEmpty() ? QStringLiteral("timeline:%1").arg(timeline_id) : job.asset_id;
    persisted_record.timeline_id = timeline_id;
    persisted_record.name = job.display_label;
    persisted_record.document_path = job.source_path;
    persisted_record.render_path = job.output_path;
    persisted_record.poster_path = poster_path.isEmpty()
                                       ? job.metadata_json.value(QStringLiteral("posterPath")).toString().trimmed()
                                       : poster_path;
    persisted_record.duration_seconds = job.metadata_json.value(QStringLiteral("durationSeconds")).toDouble();
    persisted_record.created_at = job.created_at;
    persisted_record.updated_at = updated_at;
    records.push_back(persisted_record);
  }

  if (!saveTimelineAssetRecords(records)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render completed, but the gallery index could not be updated.");
    }
    return false;
  }

  QJsonObject document;
  QFile file(job.source_path);
  if (file.exists() && file.open(QIODevice::ReadOnly)) {
    const auto parsed = QJsonDocument::fromJson(file.readAll());
    if (parsed.isObject()) {
      document = parsed.object();
    }
  }
  if (document.isEmpty()) {
    document = loadTimelineDocumentBackup(timeline_id);
  }
  if (document.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render completed, but the saved document could not be refreshed.");
    }
    return false;
  }

  document.insert(QStringLiteral("renderPath"), job.output_path);
  if (!persisted_record.poster_path.isEmpty()) {
    document.insert(QStringLiteral("posterPath"), persisted_record.poster_path);
  }
  document.insert(QStringLiteral("updatedAt"), updated_at);

  QString document_error;
  if (!save_document_object(job.source_path, document, &document_error)) {
    if (error_message != nullptr) {
      *error_message = document_error;
    }
    return false;
  }

  if (!saveTimelineDocumentBackup(persisted_record, document)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render completed, but the catalog backup could not be refreshed.");
    }
    return false;
  }

  return true;
}

QString derive_drive_id(const QString& folder_path) {
  const auto digest = QCryptographicHash::hash(folder_path.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QStringLiteral("drive-%1").arg(QString::fromLatin1(digest.left(12)));
}

QString derive_drive_name(const QString& folder_path) {
  const QFileInfo info(folder_path);
  if (!info.fileName().isEmpty()) {
    return info.fileName();
  }
  return folder_path;
}

QString built_binary_path(const QString& binary_name) {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

bool use_built_import_binary() {
  const QFileInfo binary(built_binary_path(QStringLiteral("import_catalog")));
  return binary.exists() && binary.isExecutable();
}

QString import_program() {
  if (use_built_import_binary()) {
    return built_binary_path(QStringLiteral("import_catalog"));
  }
  return QStringLiteral("cargo");
}

QStringList import_arguments(const QString& folder_path, const QString& import_mode) {
  const auto drive_id = derive_drive_id(folder_path);
  const auto drive_name = derive_drive_name(folder_path);

  if (use_built_import_binary()) {
    return {folder_path, drive_id, drive_name, import_mode};
  }

  return {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("import_catalog"),
      QStringLiteral("--"),
      folder_path,
      drive_id,
      drive_name,
      import_mode,
  };
}

QString selection_manifest_path(const PersistentJobRecord& job) {
  const auto selected_paths = job.metadata_json.value(QStringLiteral("selectedPaths")).toArray();
  if (selected_paths.isEmpty()) {
    return {};
  }

  auto temp_root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (temp_root.isEmpty()) {
    temp_root = QDir::tempPath();
  }

  const auto manifest_path = QDir(temp_root).filePath(
      QStringLiteral("photo-editor-import-runner-%1.txt").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  QFile manifest_file(manifest_path);
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return {};
  }

  for (const auto& value : selected_paths) {
    const auto path = normalized_local_path(value.toString());
    if (path.isEmpty()) {
      continue;
    }
    manifest_file.write(path.toUtf8());
    manifest_file.write("\n");
  }
  manifest_file.close();
  return manifest_path;
}

}  // namespace

JobRunner::JobRunner(QObject* parent) : QObject(parent) {
  (void)recoverInterruptedJobRecords(nullptr);
  connect(&async_job_watcher_, &QFutureWatcher<QVariantMap>::finished, this, &JobRunner::handleAsyncJobFinished);
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &JobRunner::wake);
  QTimer::singleShot(0, this, &JobRunner::processNextJob);
}

void JobRunner::wake() {
  QTimer::singleShot(0, this, &JobRunner::processNextJob);
}

void JobRunner::processNextJob() {
  if (!active_job_id_.isEmpty()) {
    return;
  }

  QVector<PersistentJobRecord> queued_jobs;
  const auto records = loadPersistedJobRecords();
  for (const auto& record : records) {
    if ((record.job_kind == QStringLiteral("asset_export") ||
         record.job_kind == QStringLiteral("catalog_import") ||
         record.job_kind == QStringLiteral("timeline_render")) &&
        record.status == QStringLiteral("Queued")) {
      queued_jobs.push_back(record);
    }
  }

  if (queued_jobs.isEmpty()) {
    return;
  }

  std::sort(queued_jobs.begin(), queued_jobs.end(), queued_job_less_than);
  const auto job = queued_jobs.front();
  active_job_id_ = job.job_id;
  active_job_kind_ = job.job_kind;
  active_job_timer_.restart();
  observability::logInfo(
      photoEditorJobsLog(),
      QStringLiteral("job.started"),
      {
          {QStringLiteral("job_id"), job.job_id},
          {QStringLiteral("job_kind"), job.job_kind},
          {QStringLiteral("asset_id"), job.asset_id},
          {QStringLiteral("source_path"), job.source_path},
      });
  if (job.job_kind == QStringLiteral("catalog_import")) {
    QTimer::singleShot(0, this, [this, job_id = active_job_id_]() { runImportJob(job_id); });
    return;
  }
  if (job.job_kind == QStringLiteral("timeline_render")) {
    QTimer::singleShot(0, this, [this, job_id = active_job_id_]() { runTimelineRenderJob(job_id); });
    return;
  }
  QTimer::singleShot(0, this, [this, job_id = active_job_id_]() { runExportJob(job_id); });
}

void JobRunner::runExportJob(const QString& job_id) {
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  job.status = QStringLiteral("Running");
  job.progress = 20;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  const auto render_photo = should_render_photo_job(job);
  bool uses_preview = false;
  const auto source_path = render_photo ? normalized_local_path(job.source_path) : resolved_source_path(job, &uses_preview);
  if (source_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The export source is no longer available.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto output_path = output_file_path_for_job(job, source_path, render_photo || uses_preview);
  if (output_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("Unable to allocate an export file name.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  async_job_watcher_.setFuture(QtConcurrent::run(
      run_export_work,
      source_path,
      job.recipe_json,
      job.preset_name,
      output_path,
      render_photo));
}

void JobRunner::handleAsyncJobFinished() {
  const auto job_id = active_job_id_;
  const auto job_kind = active_job_kind_;
  const auto total_elapsed_ms = active_job_timer_.isValid() ? active_job_timer_.elapsed() : 0;
  active_job_id_.clear();
  active_job_kind_.clear();

  if (job_id.isEmpty()) {
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto result = async_job_watcher_.result();
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  auto job = *job_it;
  const auto cancellation_requested =
      job.status == QStringLiteral("Canceling") ||
      job.metadata_json.value(QStringLiteral("cancellationRequested")).toBool();
  const auto success = result.value(QStringLiteral("success")).toBool();
  const auto worker_elapsed_ms = result.value(QStringLiteral("elapsedMs")).toLongLong();
  if (cancellation_requested) {
    const auto output_path = result.value(QStringLiteral("outputPath")).toString().trimmed();
    if (!output_path.isEmpty()) {
      QFile::remove(output_path);
    }
    const auto poster_path = result.value(QStringLiteral("posterPath")).toString().trimmed();
    if (!poster_path.isEmpty()) {
      QFile::remove(poster_path);
    }
    job.status = QStringLiteral("Canceled");
    job.error_message = QStringLiteral("Canceled before completion.");
    job.progress = 0;
    job.metadata_json.insert(QStringLiteral("canceled"), true);
    job.metadata_json.insert(QStringLiteral("canceledAt"), iso_now());
  } else if (success) {
    job.output_path = result.value(QStringLiteral("outputPath")).toString();
    if (job.job_kind == QStringLiteral("timeline_render")) {
      const auto poster_path = result.value(QStringLiteral("posterPath")).toString().trimmed();
      QString finalize_error;
      if (!update_timeline_render_artifacts(job, poster_path, &finalize_error)) {
        job.status = QStringLiteral("Failed");
        job.error_message = finalize_error;
        job.progress = 0;
      } else {
        if (!poster_path.isEmpty()) {
          job.metadata_json.insert(QStringLiteral("posterPath"), poster_path);
        }
        job.status = QStringLiteral("Completed");
        job.error_message.clear();
        job.progress = 100;
      }
    } else {
      job.status = QStringLiteral("Completed");
      job.error_message.clear();
      job.progress = 100;
    }
  } else {
    job.status = QStringLiteral("Failed");
    job.error_message = result.value(QStringLiteral("errorMessage")).toString().trimmed();
    if (job.error_message.isEmpty()) {
      job.error_message = job.job_kind == QStringLiteral("timeline_render")
                              ? QStringLiteral("The timeline render job did not complete.")
                              : QStringLiteral("The export job did not complete.");
    }
    job.progress = 0;
  }

  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);
  if (success) {
    observability::logInfo(
        photoEditorJobsLog(),
        QStringLiteral("job.completed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), job_kind},
            {QStringLiteral("asset_id"), job.asset_id},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("worker_elapsed_ms"), worker_elapsed_ms},
            {QStringLiteral("elapsed_ms"), total_elapsed_ms},
            {QStringLiteral("output_path"), job.output_path},
            {QStringLiteral("error"), job.error_message},
        });
  } else {
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), job_kind},
            {QStringLiteral("asset_id"), job.asset_id},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("worker_elapsed_ms"), worker_elapsed_ms},
            {QStringLiteral("elapsed_ms"), total_elapsed_ms},
            {QStringLiteral("output_path"), job.output_path},
            {QStringLiteral("error"), job.error_message},
        });
  }
  QTimer::singleShot(0, this, &JobRunner::processNextJob);
}

void JobRunner::runImportJob(const QString& job_id) {
  QElapsedTimer timer;
  timer.start();
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto source_path = normalized_local_path(job.source_path);
  if (source_path.isEmpty() || !QFileInfo::exists(source_path)) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The selected import source is unavailable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), QStringLiteral("catalog_import")},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("error"), job.error_message},
        });
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  job.status = QStringLiteral("Running");
  job.progress = 15;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    job.status = QStringLiteral("Failed");
    job.error_message = environment_error;
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), QStringLiteral("catalog_import")},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("error"), job.error_message},
        });
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto import_mode = job.metadata_json.value(QStringLiteral("importMode")).toString().trimmed().isEmpty()
                               ? QStringLiteral("referenced")
                               : job.metadata_json.value(QStringLiteral("importMode")).toString().trimmed();
  auto arguments = import_arguments(source_path, import_mode);
  const auto manifest_path = selection_manifest_path(job);
  if (job.metadata_json.value(QStringLiteral("selectionCount")).toInt() > 0 && manifest_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("Unable to stage the selected photos for import.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), QStringLiteral("catalog_import")},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("error"), job.error_message},
        });
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }
  if (!manifest_path.isEmpty()) {
    arguments.push_back(manifest_path);
  }

  QProcess process;
  process.setWorkingDirectory(QDir::currentPath());
  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(import_program(), arguments);

  const auto started = process.waitForStarted(5000);
  const auto finished = started && process.waitForFinished(120000);
  const auto output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  if (!manifest_path.isEmpty()) {
    QFile::remove(manifest_path);
  }

  if (!started || !finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    job.status = QStringLiteral("Failed");
    job.error_message = output.isEmpty() ? QStringLiteral("Import failed.") : output;
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), QStringLiteral("catalog_import")},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("error"), job.error_message},
        });
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  if (!output.isEmpty()) {
    job.metadata_json.insert(QStringLiteral("resultMessage"), output);
  }
  job.status = QStringLiteral("Completed");
  job.error_message.clear();
  job.progress = 100;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);
  active_job_id_.clear();
  active_job_kind_.clear();
  observability::logInfo(
      photoEditorJobsLog(),
      QStringLiteral("job.completed"),
      {
          {QStringLiteral("job_id"), job.job_id},
          {QStringLiteral("job_kind"), QStringLiteral("catalog_import")},
          {QStringLiteral("status"), job.status},
          {QStringLiteral("elapsed_ms"), timer.elapsed()},
          {QStringLiteral("result"), output},
      });
  QTimer::singleShot(0, this, &JobRunner::processNextJob);
}

void JobRunner::runTimelineRenderJob(const QString& job_id) {
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto source_path = normalized_local_path(job.source_path);
  if (source_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The saved timeline document is unavailable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto output_path = normalized_local_path(job.output_path);
  if (output_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The timeline render output path is unavailable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  job.source_path = source_path;
  job.output_path = output_path;
  job.status = QStringLiteral("Running");
  job.progress = 20;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  async_job_watcher_.setFuture(QtConcurrent::run(
      run_timeline_render_work,
      source_path,
      output_path,
      job.metadata_json.value(QStringLiteral("timelineId")).toString().trimmed()));
}

}  // namespace photo_editor::desktop
