#include "export_job_controller.h"

#include "export_image_settings.h"
#include "color_profile_asset_bridge.h"

#include "photo_editor/io/openexr_io.h"
#include "photo_editor/io/hdr_avif_io.h"

#include <QDateTime>
#include <QCache>
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <atomic>
#include <memory>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QThreadPool>
#include <QUuid>
#include <QUrl>
#include <QtConcurrent>
#include <exception>

#include <algorithm>
#include <cmath>

#include "edit_recipe_state.h"
#include "edit_adjustment_patch.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/output_rendering.h"

namespace photo_editor::desktop {

struct ExportPreflightRequest final {
  QVariantMap input;
  quint64 generation = 0;
  std::shared_ptr<std::atomic_bool> cancellation = std::make_shared<std::atomic_bool>(false);
};

struct ExportPreflightCache final {
  struct Entry {
    QVariantMap source_report;
    QByteArray source_revision;
    QElapsedTimer age;
  };
  // Worker-owned report cache, capped by approximate serialized/recipe KiB.
  QCache<QByteArray, Entry> reports{32 * 1024};
  int source_build_count = 0;
};

namespace {

QThreadPool* export_admission_pool() {
  struct SerialPool : QThreadPool {
    SerialPool() { setMaxThreadCount(1); }
  };
  static SerialPool pool;
  return &pool;
}

ExportAdmissionResult persist_export_admission(PersistentJobRecord record) {
  QString error;
  const auto records = loadPersistedJobRecords(&error, false);
  if (!error.isEmpty()) return {record, error};
  qint64 maximum_order = 0;
  for (const auto& existing : records) {
    if (existing.job_id == record.job_id) {
      // A failed helper receipt can follow a committed insert. Retrying the
      // same request must discover it, never reset its running/completed state.
      if (existing.asset_id == record.asset_id && existing.source_path == record.source_path &&
          existing.recipe_json == record.recipe_json && existing.preset_name == record.preset_name &&
          existing.destination_path == record.destination_path && existing.job_kind == record.job_kind)
        return {existing, {}};
      return {record, QStringLiteral("This export ID belongs to different catalog work. Its history was preserved.")};
    }
    if (existing.job_kind == QStringLiteral("asset_export") || existing.job_kind == QStringLiteral("timeline_render"))
      maximum_order = qMax(maximum_order, existing.metadata_json.value(QStringLiteral("queueOrder")).toVariant().toLongLong());
  }
  record.status = QStringLiteral("Queued");
  record.error_message.clear();
  record.metadata_json.insert(QStringLiteral("queueOrder"), maximum_order + 1);
  if (!upsertPersistedJobRecord(record, &error, true))
    return {record, error.isEmpty() ? QStringLiteral("The catalog did not confirm the export job.") : error};
  return {record, {}};
}

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

bool export_generated_mask_kind(const QString& kind) {
  const auto normalized = kind.trimmed().toLower();
  return normalized == QStringLiteral("subject") || normalized == QStringLiteral("sky") ||
         normalized == QStringLiteral("background") || normalized == QStringLiteral("people") ||
         normalized == QStringLiteral("object");
}

QString mask_display_name(const QVariantMap& mask) {
  const auto name = mask.value(QStringLiteral("name")).toString().simplified();
  if (!name.isEmpty()) {
    return name;
  }
  const auto kind = mask.value(QStringLiteral("kind")).toString().simplified();
  return kind.isEmpty() ? QStringLiteral("Selection") : kind;
}

bool generated_mask_missing_export_provenance(const QVariantMap& mask) {
  const auto metadata = mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  const QStringList required_keys = {
      QStringLiteral("schemaVersion"),
      QStringLiteral("sourceSchemaVersion"),
      QStringLiteral("rasterSchemaVersion"),
      QStringLiteral("generatedOutputSchemaVersion"),
      QStringLiteral("kind"),
      QStringLiteral("promptType"),
      QStringLiteral("sourceWidth"),
      QStringLiteral("sourceHeight"),
      QStringLiteral("sourceResolution"),
      QStringLiteral("cropX"),
      QStringLiteral("cropY"),
      QStringLiteral("cropW"),
      QStringLiteral("cropH"),
      QStringLiteral("cropZoom"),
      QStringLiteral("cropPreset"),
      QStringLiteral("transformRotation"),
      QStringLiteral("runtime"),
      QStringLiteral("modelName"),
      QStringLiteral("modelVersion"),
      QStringLiteral("dependencies"),
  };
  for (const auto& key : required_keys) {
    if (!metadata.contains(key)) {
      return true;
    }
  }
  return false;
}

bool generated_mask_export_geometry_stale(
    const QVariantMap& mask,
    const EditRecipeState& state,
    const QSize& source_size) {
  const auto metadata = mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (source_size.isValid() &&
      (metadata.value(QStringLiteral("sourceWidth")).toInt() != source_size.width() ||
       metadata.value(QStringLiteral("sourceHeight")).toInt() != source_size.height())) {
    return true;
  }

  const auto crop_width = state.crop_rect_width > 0.0 ? state.crop_rect_width : 1.0;
  const auto crop_height = state.crop_rect_height > 0.0 ? state.crop_rect_height : 1.0;
  const auto stale_double = [&](const QString& key, double current) {
    return std::abs(metadata.value(key).toDouble() - current) > 1e-6;
  };
  return stale_double(QStringLiteral("cropX"), state.crop_rect_x) ||
         stale_double(QStringLiteral("cropY"), state.crop_rect_y) ||
         stale_double(QStringLiteral("cropW"), crop_width) ||
         stale_double(QStringLiteral("cropH"), crop_height) ||
         stale_double(QStringLiteral("cropZoom"), state.crop_zoom) ||
         stale_double(QStringLiteral("transformRotation"), state.transform_rotation);
}

QStringList stale_generated_mask_names_for_export(
    const EditRecipeState& state,
    const QSize& source_size) {
  QStringList names;
  for (const auto& value : state.mask_stack) {
    const auto mask = value.toMap();
    if (!export_generated_mask_kind(mask.value(QStringLiteral("kind")).toString())) {
      continue;
    }
    if (mask.value(QStringLiteral("generatedMaskStale"), false).toBool() ||
        generated_mask_missing_export_provenance(mask) ||
        generated_mask_export_geometry_stale(mask, state, source_size)) {
      names.push_back(mask_display_name(mask));
    }
  }
  names.removeDuplicates();
  return names;
}

QStringList final_inpaint_required_names_for_export(const EditRecipeState& state) {
  QStringList names;
  for (const auto& value : state.mask_stack) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("inpaintFinalRequired"), false).toBool()) {
      names.push_back(mask_display_name(mask));
    }
  }
  names.removeDuplicates();
  return names;
}

bool retouch_has_brush_points(const QVariantMap& mask) {
  return !mask.value(QStringLiteral("brushPoints")).toList().isEmpty();
}

bool retouch_has_clone_source(const QVariantMap& mask) {
  return mask.value(QStringLiteral("cloneSourceSet"), false).toBool() ||
         !mask.value(QStringLiteral("cloneSources")).toList().isEmpty();
}

bool retouch_has_patch_source(const QVariantMap& mask) {
  return mask.value(QStringLiteral("patchSourceSet"), false).toBool() &&
         mask.value(QStringLiteral("patchSourceWidth")).toDouble() > 0.0 &&
         mask.value(QStringLiteral("patchSourceHeight")).toDouble() > 0.0;
}

bool retouch_has_inpaint_artifact(const QVariantMap& mask) {
  return !mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() &&
         mask.value(QStringLiteral("inpaintPatchWidth")).toInt() > 0 &&
         mask.value(QStringLiteral("inpaintPatchHeight")).toInt() > 0 &&
         !mask.value(QStringLiteral("inpaintModelName")).toString().trimmed().isEmpty();
}

QStringList missing_retouch_provenance_names_for_export(const EditRecipeState& state) {
  QStringList names;
  for (const auto& value : state.mask_stack) {
    const auto mask = value.toMap();
    const auto mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
    if (mode.isEmpty()) {
      continue;
    }

    auto missing = false;
    if (mode == QStringLiteral("clone")) {
      missing = !retouch_has_brush_points(mask) || !retouch_has_clone_source(mask);
    } else if (mode == QStringLiteral("heal")) {
      missing = !retouch_has_brush_points(mask);
    } else if (mode == QStringLiteral("patch")) {
      missing = !retouch_has_brush_points(mask) || !retouch_has_patch_source(mask);
    } else if (mode == QStringLiteral("remove")) {
      missing = !retouch_has_inpaint_artifact(mask);
    }

    if (missing) {
      names.push_back(mask_display_name(mask));
    }
  }
  names.removeDuplicates();
  return names;
}

QStringList preview_export_parity_risk_groups(const EditRecipeState& state) {
  QStringList groups;
  if (state.crop_rect_width > 0.0 || state.crop_rect_height > 0.0 ||
      std::abs(state.perspective_vertical) > 1e-6 ||
      std::abs(state.perspective_horizontal) > 1e-6 ||
      std::abs(state.transform_rotation) > 1e-6 ||
      std::abs(state.lens_distortion) > 1e-6 ||
      std::abs(state.lens_vignette) > 1e-6) {
    groups.append(QStringLiteral("crop-transform-optics"));
  }
  if (state.linear_mask_enabled || state.radial_mask_enabled || !state.mask_stack.isEmpty()) {
    groups.append(QStringLiteral("masks-local-adjustments"));
  }
  for (const auto& value : state.mask_stack) {
    const auto mask = value.toMap();
    if (!mask.value(QStringLiteral("retouchMode")).toString().trimmed().isEmpty() ||
        mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")) {
      groups.append(QStringLiteral("retouch-inpaint"));
      break;
    }
  }
  if (!state.layers.isEmpty()) {
    groups.append(QStringLiteral("layers-luts"));
  }
  if (state.soft_proof_enabled ||
      state.output_color_space.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) != 0 ||
      std::abs(state.output_headroom - 1.0) > 1e-6) {
    groups.append(QStringLiteral("output-profile-proofing"));
  }
  groups.removeDuplicates();
  return groups;
}

int preview_export_parity_tolerance_for_preset(const QString& preset) {
  const auto normalized = preset.trimmed();
  if (normalized.compare(QStringLiteral("Quick Web"), Qt::CaseInsensitive) == 0) {
    return 18;
  }
  if (normalized.compare(QStringLiteral("Client Preview"), Qt::CaseInsensitive) == 0) {
    return 12;
  }
  if (normalized.compare(QStringLiteral("Web Proof"), Qt::CaseInsensitive) == 0) {
    return 10;
  }
  return 1;
}

QVariantMap preflight_issue(const QString& severity, const QString& code, const QString& message) {
  QVariantMap issue;
  issue.insert(QStringLiteral("severity"), severity);
  issue.insert(QStringLiteral("code"), code);
  issue.insert(QStringLiteral("message"), message);
  return issue;
}

bool preflight_issue_code_present(const QVariantList& issues, const QString& code) {
  return std::any_of(
      issues.cbegin(),
      issues.cend(),
      [&](const auto& value) {
        return value.toMap().value(QStringLiteral("code")).toString() == code;
      });
}

int preset_max_edge(const QString& preset) {
  return export_image_settings_for_preset(preset).max_edge;
}

QString preset_format(const QString& preset) {
  if (is_original_copy_preset(preset)) {
    return QStringLiteral("Source");
  }
  return export_image_settings_for_preset(preset).format_name;
}

QString safe_export_token(const QString& value, const QString& fallback) {
  auto cleaned = value.trimmed();
  cleaned.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
  cleaned.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));
  cleaned.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
  return cleaned.isEmpty() ? fallback : cleaned;
}

QString planned_output_suffix(const QString& preset, const QString& source_path, bool render_photo) {
  if (!render_photo) {
    return QFileInfo(source_path).suffix().toLower();
  }
  const auto writer_format = export_image_settings_for_preset(preset).writer_format;
  if (writer_format == "jpg") {
    return QStringLiteral("jpg");
  }
  if (writer_format == "tif") {
    return QStringLiteral("tif");
  }
  if (writer_format == "pfm") {
    return QStringLiteral("pfm");
  }
  if (writer_format == "exr") {
    return QStringLiteral("exr");
  }
  if (writer_format == "avif-hdr") {
    return QStringLiteral("avif");
  }
  return QStringLiteral("png");
}

QVariantMap plan_export_output(
    const QString& display_label,
    const QString& source_path,
    const QString& media_type,
    const QString& preset,
    const QString& destination_directory,
    QSet<QString>* reserved_paths = nullptr) {
  const auto render_photo = should_render_photo_job(media_type, preset);
  const auto source_info = QFileInfo(source_path);
  const auto fallback_label = source_info.completeBaseName().isEmpty()
                                  ? QStringLiteral("Export")
                                  : source_info.completeBaseName();
  auto stem = safe_export_token(
      display_label.trimmed().isEmpty() ? fallback_label : display_label,
      QStringLiteral("Export"));
  if (!is_original_copy_preset(preset)) {
    const auto display_preset = preset.section(QLatin1Char('|'), 0, 0).trimmed();
    stem += QStringLiteral("-%1").arg(
        safe_export_token(display_preset.toLower(), QStringLiteral("export")));
  }
  const auto suffix = planned_output_suffix(preset, source_path, render_photo);
  const auto extension = suffix.isEmpty() ? QString{} : QStringLiteral(".%1").arg(suffix);
  const QDir output_dir(destination_directory);
  const auto preferred_path = QDir::cleanPath(output_dir.filePath(stem + extension));
  auto output_path = preferred_path;
  auto collision_index = 1;
  const auto unavailable = [&](const QString& path) {
    return QFileInfo::exists(path) ||
           (reserved_paths != nullptr && reserved_paths->contains(QDir::cleanPath(path)));
  };
  while (unavailable(output_path) && collision_index < 1000) {
    ++collision_index;
    output_path = QDir::cleanPath(
        output_dir.filePath(QStringLiteral("%1-%2%3").arg(stem).arg(collision_index).arg(extension)));
  }
  if (reserved_paths != nullptr && !output_path.isEmpty()) {
    reserved_paths->insert(output_path);
  }

  QVariantMap plan;
  plan.insert(QStringLiteral("schema"), QStringLiteral("export-delivery-plan:v1"));
  plan.insert(QStringLiteral("preferredPath"), preferred_path);
  plan.insert(QStringLiteral("outputPath"), output_path);
  plan.insert(QStringLiteral("outputFileName"), QFileInfo(output_path).fileName());
  plan.insert(QStringLiteral("collision"), collision_index > 1);
  plan.insert(QStringLiteral("collisionIndex"), collision_index);
  plan.insert(
      QStringLiteral("collisionOutcome"),
      collision_index > 1
          ? QStringLiteral("A numbered filename will preserve the existing file.")
          : QStringLiteral("No existing output will be overwritten."));
  plan.insert(QStringLiteral("format"), render_photo ? export_image_settings_for_preset(preset).format_name
                                                       : QStringLiteral("Original format"));
  plan.insert(
      QStringLiteral("metadataPolicy"),
      is_original_copy_preset(preset)
          ? QStringLiteral("Original file metadata preserved by byte-for-byte copy.")
          : QStringLiteral("Color profile and delivery verification recorded; camera metadata is not copied."));
  const auto output_sharpening = export_output_sharpening_for_preset(preset);
  plan.insert(QStringLiteral("sharpeningPolicy"),
              output_sharpening == QStringLiteral("off")
                  ? QStringLiteral("No additional output sharpening.")
                  : QStringLiteral("%1 output sharpening after resizing.")
                        .arg(output_sharpening == QStringLiteral("screen") ? QStringLiteral("Screen") : QStringLiteral("Print")));
  return plan;
}

QSize estimated_export_size(const QSize& source_size, const EditRecipeState& state, const QString& preset) {
  if (!source_size.isValid()) {
    return {};
  }

  auto width = source_size.width();
  auto height = source_size.height();
  if (state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0) {
    width = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * state.crop_rect_width)));
    height = std::max(1, static_cast<int>(std::lround(static_cast<double>(height) * state.crop_rect_height)));
  }

  const auto max_edge = preset_max_edge(preset);
  if (max_edge > 0 && std::max(width, height) > max_edge) {
    const auto scale = static_cast<double>(max_edge) / static_cast<double>(std::max(width, height));
    width = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * scale)));
    height = std::max(1, static_cast<int>(std::lround(static_cast<double>(height) * scale)));
  }

  return QSize(width, height);
}

QJsonObject export_crop_metadata(
    const QString& crop_preset,
    double crop_x,
    double crop_y,
    double crop_width,
    double crop_height) {
  const auto normalized_preset = crop_preset.trimmed();
  const auto has_preset = !normalized_preset.isEmpty() &&
                          normalized_preset.compare(QStringLiteral("Current"), Qt::CaseInsensitive) != 0;
  const auto has_rect = crop_width > 0.0 && crop_height > 0.0;
  if (!has_preset && !has_rect) {
    return {};
  }

  QJsonObject crop;
  crop.insert(QStringLiteral("enabled"), true);
  if (has_preset) {
    crop.insert(QStringLiteral("preset"), normalized_preset);
  }
  if (has_rect) {
    const auto width = qBound(0.01, crop_width, 1.0);
    const auto height = qBound(0.01, crop_height, 1.0);
    crop.insert(QStringLiteral("x"), qBound(0.0, crop_x, 1.0 - width));
    crop.insert(QStringLiteral("y"), qBound(0.0, crop_y, 1.0 - height));
    crop.insert(QStringLiteral("width"), width);
    crop.insert(QStringLiteral("height"), height);
  }

  QJsonObject metadata;
  metadata.insert(QStringLiteral("exportCrop"), crop);
  return metadata;
}

}  // namespace

ExportJobController::ExportJobController(QObject* parent) : QObject(parent) {
  preflight_cache_ = std::make_shared<ExportPreflightCache>();
  preflight_timer_.setSingleShot(true);
  preflight_timer_.setInterval(150);
  connect(&preflight_timer_, &QTimer::timeout, this, &ExportJobController::startPreflight);
  connect(&preflight_watcher_, &QFutureWatcher<QVariantMap>::finished,
          this, &ExportJobController::finishPreflight);
  admission_writer_ = persist_export_admission;
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &ExportJobController::refreshJobs);
  history_reader_ = [] {
    ExportHistoryReadResult result;
    // Background refresh is read-only: startup recovery owns legacy migration.
    result.records = loadPersistedJobRecords(&result.error, false);
    return result;
  };
  connect(&history_watcher_, &QFutureWatcher<ExportHistoryReadResult>::finished, this, [this] {
    if (history_pending_) { startHistoryLoad(); return; }
    history_loading_ = false;
    applyHistory(history_watcher_.result());
    emit loadingChanged();
  });
}

void ExportJobController::finishPendingAdmissions() {
  // Preflight uses Qt image plugins too; finish worker reads while QApplication
  // is still alive. Normal window closing waits for batch submission first.
  preflightThreadPool().waitForDone();
  export_admission_pool()->waitForDone();
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

QString ExportJobController::status() const {
  if (!startup_recovery_error_.isEmpty()) return QStringLiteral("Background jobs paused: %1").arg(startup_recovery_error_);
  if (batch_submission_busy_) return QStringLiteral("Checking selected exports before saving jobs.");
  if (!submission_error_.isEmpty()) return submission_error_;
  if (pendingAdmissionCount() > 0) return QStringLiteral("Saving %1 export job(s) to the catalog…").arg(pendingAdmissionCount());
  const auto error = admissionError();
  return error.isEmpty() ? status_ : QStringLiteral("Export save unconfirmed: %1 Retry saving to check the same job IDs.").arg(error);
}

QString ExportJobController::admissionError() const {
  for (const auto& job : unconfirmed_jobs_) {
    if (!admitting_ids_.contains(job.job_id) && !job.error_message.isEmpty()) return job.error_message;
  }
  return {};
}

void ExportJobController::startAdmission(const ExportJob& job) {
  if (admitting_ids_.contains(job.job_id)) return;
  auto pending = job;
  pending.status = QStringLiteral("Saving");
  pending.error_message.clear();
  unconfirmed_jobs_.insert(job.job_id, pending);
  admitting_ids_.insert(job.job_id);
  const auto index = jobIndexById(job.job_id);
  if (index >= 0) jobs_[index] = pending;
  else jobs_.push_back(pending);
  updateBusyState();
  emit jobsChanged();
  emit admissionChanged();
  emit statusChanged();
  auto* watcher = new QFutureWatcher<ExportAdmissionResult>(this);
  connect(watcher, &QFutureWatcher<ExportAdmissionResult>::finished, this, [this, watcher, id = job.job_id] {
    const auto result = watcher->result();
    watcher->deleteLater();
    admitting_ids_.remove(id);
    const auto index = jobIndexById(id);
    if (result.error.isEmpty()) {
      unconfirmed_jobs_.remove(id);
      // A history observer may already have seen a more advanced catalog row.
      if (index < 0) jobs_.push_back(jobFromRecord(result.record));
      else if (jobs_[index].status == QStringLiteral("Saving") || jobs_[index].status == QStringLiteral("Save unconfirmed"))
        jobs_[index] = jobFromRecord(result.record);
    } else {
      auto failed = unconfirmed_jobs_.value(id);
      failed.status = QStringLiteral("Save unconfirmed");
      failed.error_message = result.error;
      unconfirmed_jobs_.insert(id, failed);
      if (index >= 0 && jobs_[index].status == QStringLiteral("Saving")) jobs_[index] = failed;
    }
    updateBusyState();
    emit jobsChanged();
    emit admissionChanged();
    emit statusChanged();
    emit admissionFinished(id, result.error.isEmpty(), result.error);
    // Coalescing rejects any pre-commit history snapshot, without a foreground
    // helper read. The durable row can now wake the ordinary runner.
    refreshJobs();
  });
  // Immutable payload and writer only: destruction cannot access the controller
  // from the worker, and already admitted tasks continue on the serial pool.
  watcher->setFuture(QtConcurrent::run(export_admission_pool(), [writer = admission_writer_, record = recordFromJob(pending)] {
    try { return writer(record); }
    catch (const std::exception& error) { return ExportAdmissionResult{record, QString::fromUtf8(error.what()).left(512)}; }
    catch (...) { return ExportAdmissionResult{record, QStringLiteral("Export admission failed unexpectedly.")}; }
  }));
}

void ExportJobController::retryUnconfirmedExports() {
  if (!historyMutationsEnabled()) return;
  const auto jobs = unconfirmed_jobs_;
  for (const auto& job : jobs) if (!admitting_ids_.contains(job.job_id)) startAdmission(job);
}

bool ExportJobController::rejectSubmission(const QString& error) {
  submission_error_ = error;
  emit admissionChanged();
  emit statusChanged();
  return false;
}

void ExportJobController::setStartupRecoveryError(const QString& error) {
  if (startup_recovery_error_ == error) return;
  startup_recovery_error_ = error;
  emit startupRecoveryErrorChanged();
  emit statusChanged();
}

void ExportJobController::initializeFromStore() {
  refreshJobs();
}

QString ExportJobController::defaultDestinationDirectory() const {
  auto root = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (root.isEmpty()) {
    root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  }
  if (root.isEmpty()) {
    root = QDir::homePath();
  }

  const auto destination = QDir(root).filePath(QStringLiteral("Looksy Exports"));
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
  return queueExportWithCropConstraint(
      asset_id,
      media_type,
      display_label,
      source_path,
      edited_preview_source,
      recipe_json,
      preset,
      destination_directory,
      QString(),
      0.0,
      0.0,
      0.0,
      0.0);
}

bool ExportJobController::queueExportWithCropConstraint(
    const QString& asset_id,
    const QString& media_type,
    const QString& display_label,
    const QString& source_path,
    const QString& edited_preview_source,
    const QString& recipe_json,
    const QString& preset,
    const QString& destination_directory,
    const QString& crop_preset,
    double crop_x,
    double crop_y,
    double crop_width,
    double crop_height) {
  const auto effective_preset = preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed();
  const auto normalized_source = normalized_local_path(source_path);
  const auto normalized_destination = normalized_local_path(destination_directory);
  QSet<QString> reserved_paths;
  for (const auto& queued_job : jobs_) {
    const auto output_path = normalized_local_path(queued_job.output_path);
    if (!output_path.isEmpty()) reserved_paths.insert(output_path);
  }
  for (auto it = unconfirmed_jobs_.cbegin(); it != unconfirmed_jobs_.cend(); ++it) {
    const auto output_path = normalized_local_path(it->output_path);
    if (!output_path.isEmpty()) reserved_paths.insert(output_path);
  }
  const auto delivery_plan = plan_export_output(
      display_label_for_job(display_label, normalized_source),
      normalized_source,
      media_type.trimmed().toLower(),
      effective_preset,
      normalized_destination,
      &reserved_paths);
  return queuePlannedExportWithCropConstraint(
      asset_id,
      media_type,
      display_label,
      source_path,
      edited_preview_source,
      recipe_json,
      effective_preset,
      destination_directory,
      crop_preset,
      crop_x,
      crop_y,
      crop_width,
      crop_height,
      delivery_plan.value(QStringLiteral("outputPath")).toString(),
      QJsonObject::fromVariantMap(delivery_plan));
}

bool ExportJobController::queuePlannedExportWithCropConstraint(
    const QString& asset_id,
    const QString& media_type,
    const QString& display_label,
    const QString& source_path,
    const QString& edited_preview_source,
    const QString& recipe_json,
    const QString& preset,
    const QString& destination_directory,
    const QString& crop_preset,
    double crop_x,
    double crop_y,
    double crop_width,
    double crop_height,
    const QString& planned_output_path,
    const QJsonObject& delivery_plan) {
  submission_error_.clear();
  emit admissionChanged();
  emit statusChanged();
  const auto normalized_destination = normalized_local_path(destination_directory);
  if (normalized_destination.isEmpty()) {
    return rejectSubmission(QStringLiteral("Choose an export destination first."));
  }

  if (!QDir(normalized_destination).exists() && !QDir().mkpath(normalized_destination)) {
    return rejectSubmission(QStringLiteral("Unable to create the export destination."));
  }

  ExportJob job;
  job.job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  job.asset_id = asset_id.trimmed();
  job.media_type = media_type.trimmed().toLower();
  job.source_path = normalized_local_path(source_path);
  job.edited_preview_source = edited_preview_source.trimmed();
  job.recipe_json =
      canonical_asset_bound_adjustment_patch_json(
          recipe_json);
  if (!recipe_json.trimmed().isEmpty() &&
      job.recipe_json.isEmpty()) {
    return rejectSubmission(QStringLiteral("The export adjustment payload is invalid."));
  }
  job.metadata_json = export_crop_metadata(crop_preset, crop_x, crop_y, crop_width, crop_height);
  job.metadata_json.insert(QStringLiteral("assetId"), job.asset_id);
  job.metadata_json.insert(
      QStringLiteral("resourcePolicy"), QStringLiteral("playback-first"));
  job.metadata_json.insert(QStringLiteral("resourcePolicyAffectsQuality"), false);
  if (!delivery_plan.isEmpty()) {
    auto enriched_delivery_plan = delivery_plan;
    const auto decoded_recipe =
        parse_adjustment_payload_or_legacy_recipe(
            job.recipe_json);
    const auto recipe_state =
        decoded_recipe.state.value_or(
            EditRecipeState{});
    enriched_delivery_plan.insert(
        QStringLiteral("outputProfile"),
        recipe_state.output_color_space.trimmed().isEmpty()
            ? QStringLiteral("sRGB")
            : recipe_state.output_color_space.trimmed());
    enriched_delivery_plan.insert(
        QStringLiteral("metadataPolicy"),
        is_original_copy_preset(job.preset)
            ? QStringLiteral("Original file metadata preserved by byte-for-byte copy.")
            : QStringLiteral("Color profile and delivery verification recorded; camera metadata is not copied."));
    enriched_delivery_plan.insert(
        QStringLiteral("sharpeningPolicy"),
        delivery_plan.value(QStringLiteral("sharpeningPolicy"))
            .toString(QStringLiteral("No additional output sharpening.")));
    job.metadata_json.insert(QStringLiteral("deliveryPlan"), enriched_delivery_plan);
  }
  job.preset = preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed();
  if (should_render_photo_job(job.media_type, job.preset)) {
    job.metadata_json.insert(
        QStringLiteral("exportImageSettings"),
        export_image_settings_metadata(export_image_settings_for_preset(job.preset)));
  }
  job.destination_directory = normalized_destination;
  job.output_path = normalized_local_path(planned_output_path);
  job.display_label = display_label_for_job(display_label, job.source_path);
  job.created_at = iso_now();
  job.updated_at = job.created_at;

  if (should_render_photo_job(job.media_type, job.preset)) {
    if (job.source_path.isEmpty() || !QFileInfo::exists(job.source_path) || !preview_supported(job.source_path)) {
      return rejectSubmission(QStringLiteral("The selected photo source is unavailable for rendered export."));
    }
  } else {
    bool uses_preview = false;
    if (resolvedSourcePath(job, &uses_preview).isEmpty()) {
      return rejectSubmission(QStringLiteral("No export source is available for the current selection."));
    }
  }

  qint64 pending_bytes = job.recipe_json.size() * qint64{2};
  for (const auto& pending : unconfirmed_jobs_) pending_bytes += pending.recipe_json.size() * qint64{2};
  if (unconfirmed_jobs_.size() >= 64 || pending_bytes > 128 * 1024 * 1024) {
    return rejectSubmission(QStringLiteral("The export save queue is full. Wait for catalog confirmation before adding more exports."));
  }
  startAdmission(job);

  return true;
}

int ExportJobController::queueBatchExport(
    const QVariantList& assets,
    const QString& preset,
    const QString& destination_directory) {
  return queueBatchExportWithCropConstraint(
      assets, preset, destination_directory, QString(), 0.0, 0.0, 0.0, 0.0);
}

int ExportJobController::queueBatchExportWithCropConstraint(
    const QVariantList& assets,
    const QString& preset,
    const QString& destination_directory,
    const QString& crop_preset,
    double crop_x,
    double crop_y,
    double crop_width,
    double crop_height) {
  return queueBatchExportWithPlan(assets, preset, destination_directory, crop_preset,
      crop_x, crop_y, crop_width, crop_height, batchExportPreflight(assets, preset, destination_directory));
}

int ExportJobController::queueBatchExportWithPlan(
    const QVariantList& assets, const QString& preset, const QString& destination_directory,
    const QString& crop_preset, double crop_x, double crop_y, double crop_width, double crop_height,
    const QVariantMap& batch_preflight) {
  QSet<QString> reserved_paths;
  for (const auto& job : jobs_) {
    if (!job.output_path.isEmpty()) reserved_paths.insert(QDir::cleanPath(job.output_path));
  }
  const auto planned_items = batch_preflight.value(QStringLiteral("items")).toList();
  int queued = 0;
  for (qsizetype index = 0; index < assets.size(); ++index) {
    const auto& value = assets.at(index);
    const auto asset = value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    const auto media_type = asset.value(QStringLiteral("mediaType")).toString().trimmed().toLower();
    const auto file_path = asset.value(QStringLiteral("filePath")).toString();
    const auto adjustment_payload =
        asset.value(QStringLiteral("adjustmentPatchJson")).toString();
    const auto display_label = asset.value(QStringLiteral("displayLabel")).toString();
    const auto effective_preset =
        is_photo_media(media_type) ? preset : QStringLiteral("Original Copy");
    const auto effective_crop_preset = is_photo_media(media_type) ? crop_preset : QString();
    auto planned_item = index < planned_items.size() ? planned_items.at(index).toMap() : QVariantMap{};
    if (planned_item.isEmpty() || planned_item.value(QStringLiteral("errorCount")).toInt() > 0) {
      continue;
    }
    // Files and queued reservations may have changed since review. Re-plan the
    // filename at admission; the cached report is never authority to overwrite.
    const auto fresh_plan = plan_export_output(display_label_for_job(display_label, file_path),
        normalized_local_path(file_path), media_type, effective_preset,
        normalized_local_path(destination_directory), &reserved_paths);
    for (auto item = fresh_plan.cbegin(); item != fresh_plan.cend(); ++item)
      planned_item.insert(item.key(), item.value());
    if (queuePlannedExportWithCropConstraint(
            asset_id,
            media_type,
            display_label,
            file_path,
            asset.value(QStringLiteral("editedPreviewSource")).toString(),
            adjustment_payload,
            effective_preset,
            destination_directory,
            effective_crop_preset,
            crop_x,
            crop_y,
            crop_width,
            crop_height,
            planned_item.value(QStringLiteral("outputPath")).toString(),
            QJsonObject::fromVariantMap(planned_item))) {
      ++queued;
    }
  }
  return queued;
}

namespace {

QVariantMap build_export_source_report(
    const QString& media_type, const QString& source_path,
    const QString& edited_preview_source, const QString& recipe_json,
    const QString& preset, bool* cacheable = nullptr) {
  QVariantList issues;
  auto error_count = 0;
  auto warning_count = 0;
  auto add_issue = [&](const QString& severity, const QString& code, const QString& message) {
    issues.push_back(preflight_issue(severity, code, message));
    if (severity == QStringLiteral("error")) {
      ++error_count;
    } else if (severity == QStringLiteral("warning")) {
      ++warning_count;
    }
  };

  const auto normalized_media_type = media_type.trimmed().toLower();
  const auto normalized_preset = preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed();
  const auto normalized_source = normalized_local_path(source_path);
  const auto normalized_preview = normalized_local_path(edited_preview_source);
  const auto render_photo = should_render_photo_job(normalized_media_type, normalized_preset);
  const auto export_settings = export_image_settings_for_preset(normalized_preset);
  const auto scene_linear_export = render_photo && export_settings.scene_linear;
  const auto explicit_scene_linear_interchange =
      scene_linear_export && !export_settings.scene_linear_color_space.isEmpty();
  const auto hdr_delivery_export = render_photo && export_settings.hdr_delivery;
  if (render_photo && export_settings.writer_format == "exr" &&
      !photo_editor::io::openexr_backend_available()) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("openexr-backend-unavailable"),
        QStringLiteral(
            "OpenEXR export was requested, but this build does not contain the OpenEXR backend."));
  }
  if (hdr_delivery_export &&
      !photo_editor::io::hdr_avif_backend_available()) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("hdr-avif-backend-unavailable"),
        QStringLiteral(
            "HDR AVIF export was requested, but this build does not contain a working 10-bit AV1 encoder and decoder."));
  }

  const auto delivery_issue_offset = issues.size();

  QSize source_size;
  if (render_photo) {
    QImageReader reader(normalized_source);
    source_size = reader.size();
    if (normalized_source.isEmpty() || !QFileInfo::exists(normalized_source) ||
        !preview_supported(normalized_source)) {
      add_issue(
          QStringLiteral("error"),
          QStringLiteral("source-missing"),
          QStringLiteral("The rendered export source is missing or unsupported."));
    } else if (!source_size.isValid()) {
      add_issue(
          QStringLiteral("warning"),
          QStringLiteral("source-size-unknown"),
          QStringLiteral("The source dimensions could not be read before export."));
    }
  } else if (normalized_source.isEmpty() && normalized_preview.isEmpty()) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("source-missing"),
        QStringLiteral("No source or rendered preview is available to export."));
  } else if (!normalized_source.isEmpty() && !QFileInfo::exists(normalized_source) &&
             (normalized_preview.isEmpty() || !QFileInfo::exists(normalized_preview))) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("source-missing"),
        QStringLiteral("The original source and preview fallback are both unavailable."));
  }

  const auto decoded_recipe =
      parse_adjustment_payload_or_legacy_recipe(
          recipe_json);
  if (!recipe_json.trimmed().isEmpty() &&
      !decoded_recipe.ok()) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("recipe-invalid"),
        QStringLiteral("The edit recipe has invalid export settings."));
  }

  const auto recipe_state =
      decoded_recipe.state.value_or(EditRecipeState{});
  const auto stale_generated_masks =
      stale_generated_mask_names_for_export(recipe_state, source_size);
  if (!stale_generated_masks.isEmpty()) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("stale-generated-mask"),
        QStringLiteral("%1 generated selection%2 need update before export: %3.")
            .arg(stale_generated_masks.size())
            .arg(stale_generated_masks.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(stale_generated_masks.join(QStringLiteral(", "))));
  }
  const auto final_inpaint_required =
      final_inpaint_required_names_for_export(recipe_state);
  if (!final_inpaint_required.isEmpty()) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("final-inpaint-required"),
        QStringLiteral("%1 retouch patch%2 still need final-resolution inpaint before export: %3.")
            .arg(final_inpaint_required.size())
            .arg(final_inpaint_required.size() == 1 ? QString() : QStringLiteral("es"))
            .arg(final_inpaint_required.join(QStringLiteral(", "))));
  }
  const auto missing_retouch_provenance =
      missing_retouch_provenance_names_for_export(recipe_state);
  if (!missing_retouch_provenance.isEmpty()) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("retouch-provenance-missing"),
        QStringLiteral("%1 retouch edit%2 lack source or inpaint provenance before export: %3.")
            .arg(missing_retouch_provenance.size())
            .arg(missing_retouch_provenance.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(missing_retouch_provenance.join(QStringLiteral(", "))));
  }
  const auto parity_risk_groups =
      render_photo ? preview_export_parity_risk_groups(recipe_state) : QStringList{};
  if (!parity_risk_groups.isEmpty()) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("preview-export-parity-required"),
        QStringLiteral("Preview/export parity proof is required for high-risk edit groups before final delivery: %1.")
            .arg(parity_risk_groups.join(QStringLiteral(", "))));
  }
  const auto estimated_size = estimated_export_size(source_size, recipe_state, normalized_preset);
  if (render_photo && estimated_size.isValid() &&
      (estimated_size.width() < 512 || estimated_size.height() < 512)) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("export-size-small"),
        QStringLiteral("The estimated export size is below 512 px on one edge."));
  }

  if (render_photo &&
      normalized_preset.compare(QStringLiteral("Original Copy"), Qt::CaseInsensitive) != 0 &&
      recipe_state.output_color_space.trimmed().isEmpty() &&
      !hdr_delivery_export) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("profile-default"),
        QStringLiteral("No explicit output profile is set; the export will use the recipe default."));
  }

  const auto output_profile =
      recipe_state.output_color_space.trimmed().isEmpty() ? QStringLiteral("sRGB")
                                                          : recipe_state.output_color_space.trimmed();
  const auto working_profile = photo_editor::pipeline::working_color_profile_from_name(
      recipe_state.working_color_profile.toStdString());
  const auto export_profile = photo_editor::pipeline::output_color_profile_from_name(
      output_profile.toStdString());
  const auto output_transform = photo_editor::pipeline::compile_builtin_color_transform(
      working_profile.color_space,
      hdr_delivery_export
          ? photo_editor::pipeline::make_linear_rec2020_color_space()
          : export_profile.color_space);
  const auto icc_capability = photo_editor::pipeline::icc_backend_capability();
  const auto custom_export_profile =
      resolve_icc_profile_asset(recipe_state.export_icc_profile_asset);
  const auto custom_input_profile =
      resolve_icc_profile_asset(recipe_state.input_icc_profile_asset);
  const auto custom_proof_profile =
      resolve_icc_profile_asset(recipe_state.proof_icc_profile_asset);
  const auto custom_dcp_profile =
      resolve_dcp_profile_asset(recipe_state.camera_dcp_profile_asset);
  if (recipe_state.soft_proof_enabled &&
      custom_proof_profile.status !=
          photo_editor::pipeline::IccProfileAssetStatus::Unconfigured &&
      !custom_proof_profile.ready()) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("icc-proof-profile-asset-unavailable"),
        QStringLiteral(
            "The active preview proof profile is unavailable: %1. Export remains independent, but the on-screen proof cannot be reproduced until the profile is relinked.")
            .arg(QString::fromStdString(custom_proof_profile.failure_reason)));
  }
  photo_editor::pipeline::DecodedImageMetadata decoded_metadata;
  const auto decoded_boundary_available =
      read_raw_decoded_metadata(normalized_source, &decoded_metadata);
  const auto raw_source = raw_image_supported(normalized_source);
  const auto requested_input_profile =
      photo_editor::pipeline::input_color_profile_from_name(
          recipe_state.input_color_profile.toStdString());
  const auto resolved_camera_profile = photo_editor::pipeline::resolve_camera_profile(
      decoded_metadata,
      requested_input_profile,
      custom_dcp_profile.ready() ? custom_dcp_profile.profile.get() : nullptr,
      custom_input_profile.ready() ? &custom_input_profile : nullptr);
  if (requested_input_profile.kind ==
      photo_editor::pipeline::InputTransformKind::CameraDcpProfile) {
    const auto engine_owned_boundary =
        decoded_boundary_available &&
        photo_editor::pipeline::decoded_image_requires_engine_input_transform(
            decoded_metadata);
    if (!raw_source || !decoded_boundary_available || !engine_owned_boundary ||
        !custom_dcp_profile.ready() || !resolved_camera_profile.valid) {
      add_issue(
          QStringLiteral("error"),
          !raw_source || !decoded_boundary_available
              ? QStringLiteral("dcp-raw-color-boundary-unavailable")
              : (!engine_owned_boundary
                     ? QStringLiteral("dcp-profile-requires-camera-linear-raw")
                     : (!custom_dcp_profile.ready()
                            ? QStringLiteral("dcp-profile-asset-unavailable")
                            : QStringLiteral("dcp-profile-resolution-failed"))),
          !raw_source || !decoded_boundary_available
              ? QStringLiteral(
                    "The requested DCP cannot execute because the source does not expose a verified camera-linear RAW boundary.")
              : (!engine_owned_boundary
                     ? QStringLiteral(
                           "The RAW decoder has already applied camera characterization. This DCP requires a camera-linear RAW backend and cannot be silently ignored.")
                     : (!custom_dcp_profile.ready()
                            ? QStringLiteral("The requested immutable DCP asset is unavailable: %1.")
                                  .arg(QString::fromStdString(
                                      custom_dcp_profile.failure_reason.empty()
                                          ? "dcp_profile_asset_required"
                                          : custom_dcp_profile.failure_reason))
                            : QStringLiteral("The DCP cannot characterize this camera: %1.")
                                  .arg(QString::fromStdString(
                                      resolved_camera_profile.failure_reason)))));
    }
  }
  if (requested_input_profile.kind ==
      photo_editor::pipeline::InputTransformKind::CameraIccProfile) {
    const auto engine_owned_boundary =
        decoded_boundary_available &&
        photo_editor::pipeline::decoded_image_requires_engine_input_transform(
            decoded_metadata);
    if (!raw_source || !decoded_boundary_available || !engine_owned_boundary ||
        !custom_input_profile.ready() || !resolved_camera_profile.valid) {
      add_issue(
          QStringLiteral("error"),
          !raw_source || !decoded_boundary_available
              ? QStringLiteral("camera-icc-raw-color-boundary-unavailable")
              : (!engine_owned_boundary
                     ? QStringLiteral("camera-icc-profile-requires-camera-linear-raw")
                     : (!custom_input_profile.ready()
                            ? QStringLiteral("camera-icc-profile-asset-unavailable")
                            : QStringLiteral("camera-icc-profile-resolution-failed"))),
          !raw_source || !decoded_boundary_available
              ? QStringLiteral(
                    "The requested camera ICC cannot execute because the source does not expose a verified camera-linear RAW boundary.")
              : (!engine_owned_boundary
                     ? QStringLiteral(
                           "The RAW decoder has already applied camera characterization. This camera ICC cannot be applied a second time.")
                     : (!custom_input_profile.ready()
                            ? QStringLiteral("The requested immutable camera ICC asset is unavailable: %1.")
                                  .arg(QString::fromStdString(
                                      custom_input_profile.failure_reason.empty()
                                          ? "camera_icc_profile_asset_required"
                                          : custom_input_profile.failure_reason))
                            : QStringLiteral("The camera ICC could not be resolved: %1.")
                                  .arg(QString::fromStdString(
                                      resolved_camera_profile.failure_reason)))));
    }
  }
  if (recipe_state.input_icc_profile_asset.configured() && raw_source &&
      (!decoded_boundary_available ||
       !photo_editor::pipeline::decoded_image_accepts_custom_input_profile(
           decoded_metadata))) {
    add_issue(
        QStringLiteral("error"),
        decoded_boundary_available
            ? QStringLiteral("icc-input-profile-requires-camera-linear-raw")
            : QStringLiteral("raw-decode-color-boundary-unavailable"),
        decoded_boundary_available
            ? QStringLiteral(
                  "The RAW decoder has already applied camera characterization. This custom input ICC requires a camera-linear RAW backend to avoid a double transform.")
            : QStringLiteral(
                  "The RAW decoder did not declare a valid color boundary. Custom input profiling is blocked to prevent an unverified or double transform."));
  }
  const auto export_profile_asset_requested =
      custom_export_profile.status !=
      photo_editor::pipeline::IccProfileAssetStatus::Unconfigured;
  if (scene_linear_export && export_profile_asset_requested) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("output-profile-bypassed-scene-linear"),
        explicit_scene_linear_interchange
            ? QStringLiteral(
                  "Scene-linear interchange bypasses the display/output ICC profile and records explicit %1 chromaticities instead.")
                  .arg(export_settings.scene_linear_color_space)
            : QStringLiteral(
                  "Scene-linear export bypasses the display/output ICC profile and records the working-space chromaticities instead."));
  }
  if (hdr_delivery_export && export_profile_asset_requested) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("output-profile-bypassed-hdr-nclx"),
        QStringLiteral(
            "HDR AVIF uses explicit Rec.2020 nclx colorimetry; the document/export ICC profile is bypassed."));
  }
  const auto custom_export_profile_configured =
      !scene_linear_export && !hdr_delivery_export && export_profile_asset_requested;
  const auto photographic_rendering_model =
      hdr_delivery_export
          ? photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1
          : photo_editor::pipeline::photographic_rendering_model_from_name(
                recipe_state.photographic_rendering_model.toStdString());
  const auto aces2_renderer_selected =
      photo_editor::pipeline::photographic_rendering_model_uses_aces2(
          photographic_rendering_model);
  const auto aces2_renderer_active =
      !scene_linear_export &&
      aces2_renderer_selected &&
      (hdr_delivery_export ||
       recipe_state.render_intent.compare(
           QStringLiteral("Relative Colorimetric"),
           Qt::CaseInsensitive) != 0);
  auto aces2_output_target =
      photo_editor::pipeline::make_sdr_display_target(export_profile);
  if (hdr_delivery_export) {
    aces2_output_target = photo_editor::pipeline::DisplayTarget{
        .target_name = QStringLiteral("Rec.2100 %1 %2 nits")
                           .arg(export_settings.hdr_transfer)
                           .arg(export_settings.peak_luminance_nits, 0, 'f', 0)
                           .toStdString(),
        .color_space = photo_editor::pipeline::make_linear_rec2020_color_space(),
        .eotf = export_settings.hdr_transfer.compare(
                    QStringLiteral("HLG"),
                    Qt::CaseInsensitive) == 0
                    ? photo_editor::pipeline::DisplayEotf::Hlg
                    : photo_editor::pipeline::DisplayEotf::Pq,
        .peak_luminance_nits = export_settings.peak_luminance_nits,
        .black_luminance_nits = export_settings.black_luminance_nits,
        .reference_white_nits = export_settings.reference_white_nits,
        .surround_luminance_nits = 5.0,
        .surround = photo_editor::pipeline::DisplaySurround::Dim,
        .mastering_mode =
            photo_editor::pipeline::DisplayMasteringMode::HdrDisplayReferred,
    };
  }
  if (aces2_renderer_active && custom_export_profile_configured) {
    aces2_output_target.target_name =
        "ACES 2 linear Rec.2020 ICC interchange";
    aces2_output_target.color_space =
        photo_editor::pipeline::make_linear_rec2020_color_space();
  }
  auto custom_export_transform = photo_editor::pipeline::CompiledIccTransform{};
  auto custom_input_transform = photo_editor::pipeline::CompiledIccTransform{};
  if (custom_input_profile.status != photo_editor::pipeline::IccProfileAssetStatus::Unconfigured &&
      !custom_input_profile.ready()) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("icc-input-profile-asset-unavailable"),
        QStringLiteral("The requested input ICC profile '%1' is unavailable: %2.")
            .arg(
                recipe_state.input_icc_profile_asset.display_name.trimmed().isEmpty()
                    ? recipe_state.input_icc_profile_asset.asset_id
                    : recipe_state.input_icc_profile_asset.display_name,
                QString::fromStdString(custom_input_profile.failure_reason)));
  } else if (custom_input_profile.ready()) {
    custom_input_transform = photo_editor::pipeline::compile_icc_to_builtin_transform(
        custom_input_profile.payload_bytes(),
        working_profile.color_space,
        photo_editor::pipeline::IccTransformOptions{
            .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
            .black_point_compensation = true,
            .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
            .target = "input-to-working",
        });
    if (!custom_input_transform.ready()) {
      add_issue(
          QStringLiteral("error"),
          QStringLiteral("icc-input-profile-transform-unsupported"),
          QStringLiteral("The requested input ICC profile cannot be compiled: %1.")
              .arg(QString::fromStdString(
                  custom_input_transform.description().failure_reason)));
    }
  }
  if (custom_export_profile_configured && !custom_export_profile.ready()) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("icc-profile-asset-unavailable"),
        QStringLiteral("The requested export ICC profile '%1' is unavailable: %2.")
            .arg(
                recipe_state.export_icc_profile_asset.display_name.trimmed().isEmpty()
                    ? recipe_state.export_icc_profile_asset.asset_id
                    : recipe_state.export_icc_profile_asset.display_name,
                QString::fromStdString(custom_export_profile.failure_reason)));
  } else if (custom_export_profile.ready()) {
    custom_export_transform = photo_editor::pipeline::compile_builtin_to_icc_transform(
        aces2_renderer_active
            ? photo_editor::pipeline::make_linear_rec2020_color_space()
            : working_profile.color_space,
        custom_export_profile.payload_bytes(),
        photo_editor::pipeline::IccTransformOptions{
            .rendering_intent =
                recipe_state.render_intent.compare(
                    QStringLiteral("Relative Colorimetric"),
                    Qt::CaseInsensitive) == 0
                    ? photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric
                    : photo_editor::pipeline::IccRenderingIntent::Perceptual,
            .black_point_compensation =
                recipe_state.black_point_compensation_enabled,
            .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
            .target = aces2_renderer_active
                          ? "export-output:aces2-rec2020-interchange"
                          : "export-output",
        });
    if (!custom_export_transform.ready()) {
      add_issue(
          QStringLiteral("error"),
          QStringLiteral("icc-profile-transform-unsupported"),
          QStringLiteral("The requested export ICC profile cannot be compiled: %1.")
              .arg(QString::fromStdString(
                  custom_export_transform.description().failure_reason)));
    }
  }
  std::shared_ptr<const photo_editor::pipeline::Aces2OutputProcessor>
      aces2_output_processor;
  if (aces2_renderer_active) {
    aces2_output_processor =
        photo_editor::pipeline::compile_aces2_output_processor(
            working_profile.color_space,
            aces2_output_target,
            "Bradford",
            photo_editor::pipeline::
                    photographic_rendering_model_uses_official_aces2(
                        photographic_rendering_model)
                ? photo_editor::pipeline::Aces2OutputImplementation::
                      OfficialWhenAvailable
                : photo_editor::pipeline::Aces2OutputImplementation::
                      NativeFallback);
  }
  if ((normalized_preset.compare(QStringLiteral("Web Proof"), Qt::CaseInsensitive) == 0 ||
       normalized_preset.compare(QStringLiteral("Client Preview"), Qt::CaseInsensitive) == 0 ||
       normalized_preset.compare(QStringLiteral("Quick Web"), Qt::CaseInsensitive) == 0) &&
      output_profile.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) != 0) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("profile-web-wide-gamut"),
        QStringLiteral("This web preset is using a non-sRGB output profile."));
  }

  if (recipe_state.soft_proof_enabled &&
      recipe_state.soft_proof_profile.compare(output_profile, Qt::CaseInsensitive) != 0) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("soft-proof-mismatch"),
        QStringLiteral("The active soft proof profile differs from the export output profile."));
  }

  if (render_photo && export_settings.flatten_alpha) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("alpha-flattened"),
        QStringLiteral("This export format does not preserve alpha; transparent pixels will be flattened."));
  }

  const auto output_headroom =
      hdr_delivery_export
          ? export_settings.peak_luminance_nits /
                export_settings.reference_white_nits
          : qBound(1.0, recipe_state.output_headroom, 16.0);
  if (render_photo && output_headroom > 1.0 + 1.0e-6 &&
      !export_settings.preserves_over_range && !hdr_delivery_export) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("output-headroom-sdr-export"),
        QStringLiteral("This integer export will tone-map extended output headroom into SDR pixel values."));
  }

  if (!hdr_delivery_export && !recipe_state.raw_gamut_mapping &&
      output_profile.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) == 0) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("gamut-mapping-off"),
        QStringLiteral("RAW gamut mapping is off for an sRGB export."));
  }

  QVariantMap result;
  result.insert(QStringLiteral("ok"), error_count == 0);
  result.insert(QStringLiteral("issueCount"), issues.size());
  result.insert(QStringLiteral("errorCount"), error_count);
  result.insert(QStringLiteral("warningCount"), warning_count);
  result.insert(QStringLiteral("issues"), issues);
  result.insert(QStringLiteral("preset"), normalized_preset);
  result.insert(QStringLiteral("format"), render_photo ? export_settings.format_name : preset_format(normalized_preset));
  result.insert(QStringLiteral("bitDepth"), render_photo ? export_settings.bit_depth : 0);
  result.insert(QStringLiteral("flattenAlpha"), render_photo && export_settings.flatten_alpha);
  result.insert(QStringLiteral("embedProfile"), render_photo && export_settings.embed_profile);
  result.insert(QStringLiteral("sceneLinear"), render_photo && export_settings.scene_linear);
  result.insert(QStringLiteral("preservesOverRange"), render_photo && export_settings.preserves_over_range);
  result.insert(
      QStringLiteral("carriesChromaticities"),
      render_photo && export_settings.carries_chromaticities);
  result.insert(
      QStringLiteral("displayTransformApplied"),
      render_photo && !scene_linear_export);
  result.insert(QStringLiteral("hdrDelivery"), hdr_delivery_export);
  result.insert(QStringLiteral("hdrTransfer"), export_settings.hdr_transfer);
  result.insert(
      QStringLiteral("hdrAvifBackendAvailable"),
      photo_editor::io::hdr_avif_backend_available());
  result.insert(
      QStringLiteral("hdrAvifBackendVersion"),
      QString::fromStdString(
          std::string(photo_editor::io::hdr_avif_backend_version())));
  result.insert(
      QStringLiteral("openExrBackendAvailable"),
      photo_editor::io::openexr_backend_available());
  result.insert(
      QStringLiteral("openExrBackendVersion"),
      QString::fromStdString(
          std::string(photo_editor::io::openexr_backend_version())));
  result.insert(
      QStringLiteral("outputEncoding"),
      render_photo
          ? (export_settings.scene_linear
                 ? QStringLiteral("scene-linear-float")
                 : (hdr_delivery_export
                        ? QStringLiteral("rec2100-%1")
                              .arg(export_settings.hdr_transfer.toLower())
                        : QStringLiteral("integer-sdr")))
          : QString());
  result.insert(
      QStringLiteral("headroomPolicy"),
      render_photo
          ? (export_settings.preserves_over_range
                 ? QStringLiteral("preserve-over-range")
                 : (hdr_delivery_export
                        ? QStringLiteral("tone-map-to-hdr-target")
                        : QStringLiteral("tone-map-to-output-headroom")))
          : QString());
  result.insert(
      QStringLiteral("exportImageSettings"),
      export_image_settings_metadata(export_settings).toVariantMap());
  result.insert(
      QStringLiteral("outputColorSpace"),
      scene_linear_export
          ? (explicit_scene_linear_interchange
                 ? export_settings.scene_linear_color_space
                 : QString::fromStdString(working_profile.profile_name))
          : (hdr_delivery_export ? QStringLiteral("Rec.2020") : output_profile));
  result.insert(QStringLiteral("outputHeadroom"), output_headroom);
  const auto aces2_backend = QString::fromStdString(std::string(
      aces2_output_processor != nullptr
          ? photo_editor::pipeline::aces2_output_processor_backend_name(
                *aces2_output_processor)
          : photo_editor::pipeline::aces2_output_renderer_backend_name()));
  const auto aces2_version = QString::fromStdString(std::string(
      aces2_output_processor != nullptr
          ? photo_editor::pipeline::aces2_output_processor_version(
                *aces2_output_processor)
          : photo_editor::pipeline::aces2_output_renderer_version()));
  auto color_transform_provenance = QVariantMap{
          {QStringLiteral("schema"), QStringLiteral("color-transform-provenance:v10")},
          {QStringLiteral("executionBackend"),
           scene_linear_export
               ? (explicit_scene_linear_interchange
                      ? QStringLiteral("builtin-matrix")
                      : QStringLiteral("scene-linear-working"))
               : hdr_delivery_export
               ? QStringLiteral("%1 + libheif")
                     .arg(aces2_backend)
               : custom_export_profile_configured
               ? (aces2_renderer_active
                      ? QStringLiteral("%1 + Little CMS").arg(aces2_backend)
                      : QStringLiteral("Little CMS"))
               : (aces2_renderer_active ? aces2_backend
                                          : QStringLiteral("builtin-matrix"))},
          {QStringLiteral("processorVersion"),
           scene_linear_export
               ? (explicit_scene_linear_interchange
                      ? QStringLiteral("builtin-matrix:v1")
                      : QStringLiteral("scene-linear-working:v1"))
               : hdr_delivery_export
               ? QStringLiteral("%1 + libheif %2")
                     .arg(aces2_version)
                     .arg(QString::fromStdString(std::string(
                         photo_editor::io::hdr_avif_backend_version())))
               : custom_export_profile_configured
               ? (aces2_renderer_active
                      ? QStringLiteral("%1 + %2")
                            .arg(aces2_version)
                            .arg(QString::fromStdString(
                                custom_export_transform.description().backend_version))
                      : QString::fromStdString(
                            custom_export_transform.description().backend_version))
               : (aces2_renderer_active
                      ? aces2_version
                      : QString::fromStdString(output_transform.provenance))},
          {QStringLiteral("photographicRenderingModel"),
           QString::fromLatin1(
               photo_editor::pipeline::photographic_rendering_model_name(
                   photographic_rendering_model))},
          {QStringLiteral("photographicRenderingStatus"),
           scene_linear_export
               ? QStringLiteral("bypassed-scene-linear-export")
               : aces2_renderer_selected && !aces2_renderer_active
               ? QStringLiteral("bypassed-relative-colorimetric")
               : QStringLiteral("active")},
          {QStringLiteral("sourceProfile"),
           QString::fromStdString(output_transform.source.name)},
          {QStringLiteral("destinationProfile"),
           scene_linear_export
               ? (explicit_scene_linear_interchange
                      ? export_settings.scene_linear_color_space
                      : QString::fromStdString(working_profile.profile_name))
               : hdr_delivery_export
               ? QStringLiteral("Rec.2100 %1").arg(export_settings.hdr_transfer)
               : custom_export_profile_configured
               ? recipe_state.export_icc_profile_asset.display_name
               : QString::fromStdString(output_transform.destination.name)},
          {QStringLiteral("profileAssetStatus"),
           QString::fromLatin1(photo_editor::pipeline::icc_profile_asset_status_name(
               custom_export_profile.status))},
          {QStringLiteral("profileAssetId"),
           recipe_state.export_icc_profile_asset.asset_id},
          {QStringLiteral("profilePayloadSha256"),
           recipe_state.export_icc_profile_asset.payload_sha256},
          {QStringLiteral("profilePayloadBytes"),
           recipe_state.export_icc_profile_asset.byte_size},
          {QStringLiteral("transformCacheKey"),
           aces2_output_processor != nullptr
               ? QString::fromStdString(
                     aces2_output_processor->cache_key +
                     (custom_export_profile_configured
                          ? "->" +
                                custom_export_transform.description().cache_key
                          : std::string{}))
               : QString::fromStdString(
                     custom_export_transform.description().cache_key)},
          {QStringLiteral("transformFailureReason"),
           custom_export_profile.ready()
               ? QString::fromStdString(custom_export_transform.description().failure_reason)
               : QString::fromStdString(custom_export_profile.failure_reason)},
          {QStringLiteral("inputProfileAssetStatus"),
           QString::fromLatin1(photo_editor::pipeline::icc_profile_asset_status_name(
               custom_input_profile.status))},
          {QStringLiteral("inputProfileAssetId"),
           recipe_state.input_icc_profile_asset.asset_id},
          {QStringLiteral("inputProfilePayloadSha256"),
           recipe_state.input_icc_profile_asset.payload_sha256},
          {QStringLiteral("inputTransformCacheKey"),
           QString::fromStdString(custom_input_transform.description().cache_key)},
          {QStringLiteral("inputTransformFailureReason"),
           custom_input_profile.ready()
               ? QString::fromStdString(custom_input_transform.description().failure_reason)
               : QString::fromStdString(custom_input_profile.failure_reason)},
          {QStringLiteral("cameraProfileRenderingModel"),
           QString::fromLatin1(
               photo_editor::pipeline::camera_profile_rendering_model_name(
                   photo_editor::pipeline::camera_profile_rendering_model_from_name(
                       recipe_state.camera_profile_rendering_model.toStdString())))},
          {QStringLiteral("highlightReconstructionModel"),
           normalized_highlight_reconstruction_model_name(
               recipe_state.highlight_reconstruction_model)},
          {QStringLiteral("cameraProfileAssetId"),
           recipe_state.camera_dcp_profile_asset.asset_id},
          {QStringLiteral("cameraProfileAssetPayloadSha256"),
           recipe_state.camera_dcp_profile_asset.payload_sha256},
          {QStringLiteral("cameraTransformOwner"),
           !raw_source
               ? QStringLiteral("not-raw")
               : (!decoded_boundary_available ||
                          !photo_editor::pipeline::decoded_image_color_boundary_valid(
                          decoded_metadata)
                      ? QStringLiteral("invalid")
                      : (photo_editor::pipeline::decoded_image_requires_engine_input_transform(
                                 decoded_metadata)
                             ? QStringLiteral("engine")
                             : QStringLiteral("decoder")))},
          {QStringLiteral("chromaticAdaptation"),
           QString::fromStdString(output_transform.chromatic_adaptation_method)},
          {QStringLiteral("renderIntent"), recipe_state.render_intent},
          {QStringLiteral("blackPointCompensation"),
           recipe_state.black_point_compensation_enabled},
          {QStringLiteral("iccBackendAvailable"), icc_capability.available},
          {QStringLiteral("iccBackend"), QString::fromStdString(icc_capability.backend)},
          {QStringLiteral("iccCompiledVersion"),
           QString::fromStdString(icc_capability.compiled_version)},
          {QStringLiteral("iccRuntimeVersion"),
           QString::fromStdString(icc_capability.runtime_version)},
          {QStringLiteral("iccValidatedVersion"),
           QString::fromStdString(icc_capability.validated_version)},
          {QStringLiteral("iccStrictVersion"), icc_capability.strict_version},
          {QStringLiteral("iccFailureReason"),
           QString::fromStdString(icc_capability.failure_reason)},
      };
  insert_decoded_camera_profile_provenance(
      &color_transform_provenance,
      decoded_boundary_available,
      decoded_boundary_available &&
          photo_editor::pipeline::decoded_image_color_boundary_valid(decoded_metadata),
      decoded_metadata,
      requested_input_profile,
      resolved_camera_profile);
  insert_camera_profile_provenance(
      &color_transform_provenance, resolved_camera_profile, custom_dcp_profile);
  result.insert(QStringLiteral("colorTransformProvenance"),
                color_transform_provenance);
  result.insert(QStringLiteral("softProofEnabled"), recipe_state.soft_proof_enabled);
  result.insert(QStringLiteral("softProofProfile"), recipe_state.soft_proof_profile);
  result.insert(
      QStringLiteral("proofProfileAssetStatus"),
      QString::fromLatin1(photo_editor::pipeline::icc_profile_asset_status_name(
          custom_proof_profile.status)));
  result.insert(
      QStringLiteral("proofProfileAssetId"),
      recipe_state.proof_icc_profile_asset.asset_id);
  result.insert(
      QStringLiteral("proofProfilePayloadSha256"),
      recipe_state.proof_icc_profile_asset.payload_sha256);
  result.insert(QStringLiteral("estimatedPixelWidth"), estimated_size.isValid() ? estimated_size.width() : 0);
  result.insert(QStringLiteral("estimatedPixelHeight"), estimated_size.isValid() ? estimated_size.height() : 0);
  result.insert(
      QStringLiteral("previewExportParity"),
      QVariantMap{
          {QStringLiteral("required"), !parity_risk_groups.isEmpty()},
          {QStringLiteral("status"),
           parity_risk_groups.isEmpty()
               ? QStringLiteral("not-required")
               : QStringLiteral("required-at-export")},
          {QStringLiteral("highRiskGroups"), parity_risk_groups},
          {QStringLiteral("proofStage"), QStringLiteral("export_preview_diff")},
          {QStringLiteral("expectedTolerance"),
           preview_export_parity_tolerance_for_preset(normalized_preset)},
      });
  result.insert(QStringLiteral("_deliveryIssueOffset"), delivery_issue_offset);
  if (cacheable != nullptr) {
    // Registry mappings can change without a source-file revision. Re-evaluate
    // custom ICC/DCP requests until those registries expose revision tokens.
    *cacheable = !recipe_state.input_icc_profile_asset.configured() &&
        !recipe_state.export_icc_profile_asset.configured() &&
        !recipe_state.proof_icc_profile_asset.configured() &&
        !recipe_state.camera_dcp_profile_asset.configured();
  }
  return result;
}

QVariantMap finish_export_preflight(
    QVariantMap result, const QString& media_type, const QString& source_path,
    const QString& preset, const QString& destination_directory,
    int histogram_sample_count, int shadow_clip_count, int highlight_clip_count,
    int raw_clip_sample_count, int raw_shadow_clip_count, int raw_highlight_clip_count) {
  const auto normalized_source = normalized_local_path(source_path);
  const auto normalized_preset = preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed();
  const auto normalized_destination = normalized_local_path(destination_directory);
  const auto delivery_plan = plan_export_output(
      QFileInfo(normalized_source).completeBaseName(), normalized_source,
      media_type.trimmed().toLower(), normalized_preset, normalized_destination);
  auto issues = result.value(QStringLiteral("issues")).toList();
  const auto offset = result.take(QStringLiteral("_deliveryIssueOffset")).toInt();
  auto issue_position = offset;
  auto error_count = result.value(QStringLiteral("errorCount")).toInt();
  auto warning_count = result.value(QStringLiteral("warningCount")).toInt();
  const auto add_issue = [&](const QString& severity, const QString& code, const QString& message) {
    issues.insert(issue_position++, preflight_issue(severity, code, message));
    if (severity == QStringLiteral("error")) ++error_count;
    else if (severity == QStringLiteral("warning")) ++warning_count;
  };
  if (normalized_destination.isEmpty()) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("destination-missing"),
        QStringLiteral("Choose an export destination."));
  } else {
    const QFileInfo destination_info(normalized_destination);
    if (destination_info.exists() && !destination_info.isDir()) {
      add_issue(
          QStringLiteral("error"),
          QStringLiteral("destination-not-folder"),
          QStringLiteral("The export destination is not a folder."));
    } else if (!destination_info.exists()) {
      add_issue(
          QStringLiteral("warning"),
          QStringLiteral("destination-create"),
          QStringLiteral("The destination folder will be created before export."));
    } else if (!QFileInfo(normalized_destination).isWritable()) {
      add_issue(
          QStringLiteral("error"),
          QStringLiteral("destination-unwritable"),
          QStringLiteral("The export destination is not writable."));
    }
  }
  if (!normalized_destination.isEmpty() &&
      delivery_plan.value(QStringLiteral("collision")).toBool()) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("output-collision-renamed"),
        QStringLiteral("%1 already exists. The export will use %2 so the existing file is preserved.")
            .arg(
                QFileInfo(delivery_plan.value(QStringLiteral("preferredPath")).toString()).fileName(),
                delivery_plan.value(QStringLiteral("outputFileName")).toString()));
  }

  issue_position = static_cast<int>(issues.size());
  if (histogram_sample_count > 0 && shadow_clip_count + highlight_clip_count > 0) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("preview-clipping"),
        QStringLiteral("The current preview has shadow or highlight clipping."));
  }

  if (raw_clip_sample_count > 0 && raw_shadow_clip_count + raw_highlight_clip_count > 0) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("raw-clipping"),
        QStringLiteral("The source has RAW clipping before edits."));
  }

  result.insert(QStringLiteral("ok"), error_count == 0);
  result.insert(QStringLiteral("issueCount"), issues.size());
  result.insert(QStringLiteral("errorCount"), error_count);
  result.insert(QStringLiteral("warningCount"), warning_count);
  result.insert(QStringLiteral("issues"), issues);
  result.insert(QStringLiteral("outputPath"), delivery_plan.value(QStringLiteral("outputPath")));
  result.insert(QStringLiteral("outputFileName"), delivery_plan.value(QStringLiteral("outputFileName")));
  result.insert(QStringLiteral("collision"), delivery_plan.value(QStringLiteral("collision")));
  result.insert(QStringLiteral("collisionOutcome"), delivery_plan.value(QStringLiteral("collisionOutcome")));
  result.insert(QStringLiteral("metadataPolicy"), delivery_plan.value(QStringLiteral("metadataPolicy")));
  result.insert(QStringLiteral("sharpeningPolicy"), delivery_plan.value(QStringLiteral("sharpeningPolicy")));
  return result;
}

}  // namespace

QVariantMap ExportJobController::exportPreflight(
    const QString& media_type, const QString& source_path,
    const QString& edited_preview_source, const QString& recipe_json,
    const QString& preset, const QString& destination_directory,
    int histogram_sample_count, int shadow_clip_count, int highlight_clip_count,
    int raw_clip_sample_count, int raw_shadow_clip_count, int raw_highlight_clip_count) const {
  return finish_export_preflight(
      build_export_source_report(media_type, source_path, edited_preview_source, recipe_json, preset),
      media_type, source_path, preset, destination_directory,
      histogram_sample_count, shadow_clip_count, highlight_clip_count,
      raw_clip_sample_count, raw_shadow_clip_count, raw_highlight_clip_count);
}

namespace {

QVariantMap build_batch_export_preflight(
    const QVariantList& assets,
    const QString& preset,
    const QString& destination_directory,
    const std::function<QVariantMap(const QVariantMap&, const QString&, const QString&)>& single_report,
    const std::shared_ptr<std::atomic_bool>& cancellation = {}) {
  QVariantList items;
  QSet<QString> reserved_paths;
  auto ready_count = 0;
  auto blocked_count = 0;
  auto collision_count = 0;
  auto missing_count = 0;
  auto unsupported_count = 0;
  auto unavailable_destination_count = 0;
  const auto normalized_destination = normalized_local_path(destination_directory);

  for (qsizetype index = 0; index < assets.size(); ++index) {
    if (cancellation && cancellation->load(std::memory_order_relaxed)) return {};
    const auto asset = assets.at(index).toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    const auto media_type = asset.value(QStringLiteral("mediaType"), QStringLiteral("photo")).toString().trimmed().toLower();
    const auto source_path = asset.value(QStringLiteral("filePath")).toString();
    const auto display_label = display_label_for_job(
        asset.value(QStringLiteral("displayLabel")).toString(), source_path);
    const auto effective_preset = is_photo_media(media_type) ? preset : QStringLiteral("Original Copy");
    const auto single_preflight = single_report(asset, effective_preset, normalized_destination);
    auto plan = plan_export_output(
        display_label,
        normalized_local_path(source_path),
        media_type,
        effective_preset,
        normalized_destination,
        &reserved_paths);
    plan.insert(QStringLiteral("index"), static_cast<int>(index));
    plan.insert(QStringLiteral("assetId"), asset_id);
    plan.insert(QStringLiteral("displayLabel"), display_label);
    plan.insert(QStringLiteral("mediaType"), media_type);
    plan.insert(QStringLiteral("sourcePath"), normalized_local_path(source_path));
    plan.insert(QStringLiteral("preset"), effective_preset);
    plan.insert(QStringLiteral("errorCount"), single_preflight.value(QStringLiteral("errorCount")));
    plan.insert(QStringLiteral("warningCount"), single_preflight.value(QStringLiteral("warningCount")));
    plan.insert(QStringLiteral("issues"), single_preflight.value(QStringLiteral("issues")));
    plan.insert(QStringLiteral("estimatedPixelWidth"), single_preflight.value(QStringLiteral("estimatedPixelWidth")));
    plan.insert(QStringLiteral("estimatedPixelHeight"), single_preflight.value(QStringLiteral("estimatedPixelHeight")));
    plan.insert(QStringLiteral("outputColorSpace"), single_preflight.value(QStringLiteral("outputColorSpace")));
    plan.insert(QStringLiteral("bitDepth"), single_preflight.value(QStringLiteral("bitDepth")));

    const auto issues = single_preflight.value(QStringLiteral("issues")).toList();
    if (preflight_issue_code_present(issues, QStringLiteral("source-missing"))) {
      ++missing_count;
    }
    if (preflight_issue_code_present(issues, QStringLiteral("recipe-invalid")) ||
        preflight_issue_code_present(issues, QStringLiteral("openexr-backend-unavailable")) ||
        preflight_issue_code_present(issues, QStringLiteral("hdr-avif-backend-unavailable"))) {
      ++unsupported_count;
    }
    if (preflight_issue_code_present(issues, QStringLiteral("destination-missing")) ||
        preflight_issue_code_present(issues, QStringLiteral("destination-not-folder")) ||
        preflight_issue_code_present(issues, QStringLiteral("destination-unwritable"))) {
      ++unavailable_destination_count;
    }
    if (plan.value(QStringLiteral("collision")).toBool()) {
      ++collision_count;
    }
    if (single_preflight.value(QStringLiteral("errorCount")).toInt() > 0) {
      ++blocked_count;
    } else {
      ++ready_count;
    }
    items.push_back(plan);
  }

  QVariantMap result;
  result.insert(QStringLiteral("schema"), QStringLiteral("export-batch-preflight:v1"));
  result.insert(QStringLiteral("ok"), blocked_count == 0 && !assets.isEmpty());
  result.insert(QStringLiteral("assetCount"), assets.size());
  result.insert(QStringLiteral("readyCount"), ready_count);
  result.insert(QStringLiteral("blockedCount"), blocked_count);
  result.insert(QStringLiteral("collisionCount"), collision_count);
  result.insert(QStringLiteral("missingOriginalCount"), missing_count);
  result.insert(QStringLiteral("unsupportedCount"), unsupported_count);
  result.insert(QStringLiteral("unavailableDestinationCount"), unavailable_destination_count);
  result.insert(QStringLiteral("destinationPath"), normalized_destination);
  result.insert(QStringLiteral("preset"), preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed());
  result.insert(QStringLiteral("items"), items);
  result.insert(QStringLiteral("metadataPolicy"), QStringLiteral("Color profile and delivery verification recorded; camera metadata is not copied."));
  const auto sharpening = export_output_sharpening_for_preset(preset);
  result.insert(QStringLiteral("sharpeningPolicy"),
                sharpening == QStringLiteral("off")
                    ? QStringLiteral("No additional output sharpening.")
                    : QStringLiteral("%1 output sharpening after resizing.")
                          .arg(sharpening == QStringLiteral("screen") ? QStringLiteral("Screen") : QStringLiteral("Print")));
  return result;
}

}  // namespace

QVariantMap ExportJobController::batchExportPreflight(
    const QVariantList& assets, const QString& preset, const QString& destination_directory) const {
  return build_batch_export_preflight(assets, preset, destination_directory,
      [this](const QVariantMap& asset, const QString& effective_preset, const QString& destination) {
        return exportPreflight(asset.value(QStringLiteral("mediaType"), QStringLiteral("photo")).toString(),
            asset.value(QStringLiteral("filePath")).toString(), {},
            asset.value(QStringLiteral("adjustmentPatchJson")).toString(), effective_preset, destination,
            0, 0, 0, 0, 0, 0);
      });
}

namespace {

QByteArray export_source_revision(const QString& source, const QString& preview) {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  for (const auto& path : {source, preview}) {
    const QFileInfo info(path);
    stream << path << info.exists() << info.isReadable() << info.canonicalFilePath()
           << info.size() << info.lastModified().toMSecsSinceEpoch()
           << info.metadataChangeTime().toMSecsSinceEpoch();
  }
  return bytes;
}

QVariantMap cached_export_preflight(
    ExportPreflightCache& cache, const QVariantMap& asset, const QString& preset,
    const QString& destination, const QVariantMap& scopes = {}) {
  const auto media = asset.value(QStringLiteral("mediaType"), QStringLiteral("photo")).toString();
  const auto source = normalized_local_path(asset.value(QStringLiteral("filePath")).toString());
  const auto preview = normalized_local_path(asset.value(QStringLiteral("editedPreviewSource")).toString());
  const auto recipe = asset.value(QStringLiteral("adjustmentPatchJson")).toString();
  QByteArray input;
  QDataStream stream(&input, QIODevice::WriteOnly);
  stream << media << source << preview << recipe << preset;
  const auto key = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
  const auto revision = export_source_revision(source, preview);
  auto* entry = cache.reports.object(key);
  QVariantMap source_report;
  if (entry && entry->source_revision == revision && entry->age.elapsed() < 30000) {
    source_report = entry->source_report;
  } else {
    bool cacheable = false;
    source_report = build_export_source_report(media, source, preview, recipe, preset, &cacheable);
    ++cache.source_build_count;
    if (cacheable && export_source_revision(source, preview) == revision) {
      auto next = std::make_unique<ExportPreflightCache::Entry>();
      next->source_report = source_report;
      next->source_revision = revision;
      next->age.start();
      const auto bytes = QJsonDocument::fromVariant(source_report).toJson(QJsonDocument::Compact).size()
          + recipe.size() * 2 + revision.size() + 512;
      const auto cost = static_cast<int>(std::min<qsizetype>(32 * 1024 + 1, (bytes + 1023) / 1024));
      cache.reports.insert(key, next.release(), cost);
    }
  }
  return finish_export_preflight(source_report, media, source, preset, destination,
      scopes.value(QStringLiteral("histogramSamples")).toInt(),
      scopes.value(QStringLiteral("shadowClip")).toInt(), scopes.value(QStringLiteral("highlightClip")).toInt(),
      scopes.value(QStringLiteral("rawSamples")).toInt(),
      scopes.value(QStringLiteral("rawShadowClip")).toInt(), scopes.value(QStringLiteral("rawHighlightClip")).toInt());
}

}  // namespace

bool ExportJobController::queueBatchExportAsync(
    const QVariantList& assets, const QString& preset, const QString& destination_directory,
    const QString& crop_preset, double crop_x, double crop_y, double crop_width, double crop_height) {
  if (batch_submission_busy_ || assets.isEmpty()) return false;
  cancelPreflight();
  batch_submission_busy_ = true;
  batch_submission_cancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto cancellation = batch_submission_cancellation_;
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
      [this, watcher, assets, preset, destination_directory, crop_preset,
       crop_x, crop_y, crop_width, crop_height] {
    int queued = 0;
    try {
      queued = queueBatchExportWithPlan(assets, preset, destination_directory, crop_preset,
          crop_x, crop_y, crop_width, crop_height, watcher->result());
    } catch (...) {
      rejectSubmission(QStringLiteral("Export checks failed. Retry the selected exports."));
    }
    watcher->deleteLater();
    batch_submission_busy_ = false;
    batch_submission_cancellation_.reset();
    emit batchSubmissionChanged();
    emit statusChanged();
    emit batchSubmissionFinished(queued, static_cast<int>(assets.size()));
  });
  watcher->setFuture(QtConcurrent::run(&preflightThreadPool(), [assets, preset, destination_directory, cancellation] {
    // Fresh source/profile validation for submission, independent of review cache.
    return build_batch_export_preflight(assets, preset, destination_directory,
        [](const QVariantMap& asset, const QString& effective_preset, const QString& destination) {
          const auto media = asset.value(QStringLiteral("mediaType"), QStringLiteral("photo")).toString();
          const auto path = asset.value(QStringLiteral("filePath")).toString();
          const auto preview = asset.value(QStringLiteral("editedPreviewSource")).toString();
          return finish_export_preflight(build_export_source_report(media, path, preview,
              asset.value(QStringLiteral("adjustmentPatchJson")).toString(), effective_preset),
              media, path, effective_preset, destination, 0, 0, 0, 0, 0, 0);
        }, cancellation);
  }));
  emit batchSubmissionChanged();
  emit statusChanged();
  return true;
}

ExportJobController::~ExportJobController() {
  if (batch_submission_cancellation_) batch_submission_cancellation_->store(true, std::memory_order_relaxed);
  if (active_preflight_) active_preflight_->cancellation->store(true, std::memory_order_relaxed);
}

QThreadPool& ExportJobController::preflightThreadPool() {
  static QThreadPool pool;
  static const bool configured = [] { pool.setMaxThreadCount(1); pool.setExpiryTimeout(30000); return true; }();
  Q_UNUSED(configured)
  return pool;
}

void ExportJobController::requestPreflight(const QVariantMap& request, bool force) {
  if (!force && request == preflight_input_ &&
      (preflight_busy_ || (preflight_age_.isValid() && preflight_age_.elapsed() < 2000))) return;
  if (active_preflight_) active_preflight_->cancellation->store(true, std::memory_order_relaxed);
  preflight_input_ = request;
  auto next = std::make_shared<ExportPreflightRequest>();
  next->input = request;
  next->generation = ++preflight_generation_;
  pending_preflight_ = std::move(next);
  single_preflight_.clear();
  batch_preflight_.clear();
  preflight_busy_ = true;
  preflight_timer_.start();
  emit preflightChanged();
}

void ExportJobController::cancelPreflight() {
  ++preflight_generation_;
  if (active_preflight_) active_preflight_->cancellation->store(true, std::memory_order_relaxed);
  pending_preflight_.reset();
  preflight_timer_.stop();
  preflight_age_.invalidate();
  preflight_input_.clear();
  single_preflight_.clear();
  batch_preflight_.clear();
  preflight_busy_ = false;
  emit preflightChanged();
}

void ExportJobController::startPreflight() {
  if (active_preflight_ || !pending_preflight_) return;
  active_preflight_ = std::move(pending_preflight_);
  const auto request = active_preflight_;
  const auto cache = preflight_cache_;
  preflight_watcher_.setFuture(QtConcurrent::run(&preflightThreadPool(), [request, cache] {
    const auto cancelled = [&] { return request->cancellation->load(std::memory_order_relaxed); };
    if (cancelled()) return QVariantMap{};
    const auto single = request->input.value(QStringLiteral("single")).toMap();
    const auto assets = request->input.value(QStringLiteral("assets")).toList();
    const auto preset = request->input.value(QStringLiteral("preset")).toString();
    const auto destination = request->input.value(QStringLiteral("destination")).toString();
    const auto scopes = request->input.value(QStringLiteral("scopes")).toMap();
    auto single_result = cached_export_preflight(*cache, single, preset, destination, scopes);
    if (cancelled()) return QVariantMap{};
    auto batch_result = build_batch_export_preflight(assets, preset, destination,
        [&](const QVariantMap& asset, const QString& effective_preset, const QString& output) {
          return cached_export_preflight(*cache, asset, effective_preset, output);
        }, request->cancellation);
    return QVariantMap{{QStringLiteral("single"), single_result}, {QStringLiteral("batch"), batch_result},
                      {QStringLiteral("sourceBuildCount"), cache->source_build_count}};
  }));
}

void ExportJobController::finishPreflight() {
  const auto request = std::move(active_preflight_);
  if (!request) return;
  QVariantMap result;
  try {
    result = preflight_watcher_.result();
  } catch (...) {
    const QVariantMap failure{{QStringLiteral("ok"), false}, {QStringLiteral("errorCount"), 1},
        {QStringLiteral("readyCount"), 0}, {QStringLiteral("blockedCount"), 1},
        {QStringLiteral("issues"), QVariantList{preflight_issue(QStringLiteral("error"),
            QStringLiteral("preflight-failed"), QStringLiteral("Export checks failed. Change the settings or retry."))}}};
    result.insert(QStringLiteral("single"), failure);
    result.insert(QStringLiteral("batch"), failure);
  }
  if (request->generation == preflight_generation_ &&
      !request->cancellation->load(std::memory_order_relaxed)) {
    single_preflight_ = result.value(QStringLiteral("single")).toMap();
    batch_preflight_ = result.value(QStringLiteral("batch")).toMap();
    preflight_source_build_count_ = result.value(QStringLiteral("sourceBuildCount")).toInt();
    preflight_busy_ = false;
    preflight_age_.start();
    emit preflightChanged();
  }
  if (!preflight_timer_.isActive()) startPreflight();
}

QString ExportJobController::revealTarget(const QString& job_id) const {
  const auto index = jobIndexById(job_id);
  if (index < 0) {
    return {};
  }
  const auto output_path = normalized_local_path(jobs_.at(index).output_path);
  if (output_path.isEmpty() || !QFileInfo::exists(output_path)) {
    return {};
  }
  return output_path;
}

bool ExportJobController::revealExport(const QString& job_id) const {
  const auto output_path = revealTarget(job_id);
  return !output_path.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(output_path));
}

void ExportJobController::retryJob(const QString& job_id) {
  if (!historyMutationsEnabled()) return;
  if (unconfirmed_jobs_.contains(job_id)) {
    if (!admitting_ids_.contains(job_id)) startAdmission(unconfirmed_jobs_.value(job_id));
    return;
  }
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
  if (!historyMutationsEnabled()) return;
  if (unconfirmed_jobs_.contains(job_id)) {
    setStatus(QStringLiteral("Confirm the export save before removing its history."));
    return;
  }
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
  if (!historyMutationsEnabled()) return;
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
  value.insert(QStringLiteral("metadata"), job.metadata_json.toVariantMap());
  value.insert(
      QStringLiteral("exportCrop"),
      job.metadata_json.value(QStringLiteral("exportCrop")).toObject().toVariantMap());
  value.insert(
      QStringLiteral("previewExportDiff"),
      job.metadata_json.value(QStringLiteral("previewExportDiff")).toObject().toVariantMap());
  value.insert(
      QStringLiteral("artifactVerification"),
      job.metadata_json.value(QStringLiteral("artifactVerification")).toObject().toVariantMap());
  value.insert(
      QStringLiteral("exportRenderJob"),
      job.metadata_json.value(QStringLiteral("exportRenderJob")).toObject().toVariantMap());
  value.insert(
      QStringLiteral("exportPreviewJob"),
      job.metadata_json.value(QStringLiteral("exportPreviewJob")).toObject().toVariantMap());
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
  job.metadata_json = record.metadata_json;
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
  // Mutations retain their immediate post-command history contract. Any older
  // background result is superseded; notification/initialization reads are async.
  if (history_loading_) history_pending_ = true;
  applyHistory(history_reader_(), emit_signal);
  emit loadingChanged();
}

void ExportJobController::applyHistory(const ExportHistoryReadResult& result, bool emit_signal) {
  history_error_ = result.error;
  if (!result.error.isEmpty() && !jobs_.isEmpty()) {
    setStatus(QStringLiteral("Export history unavailable: %1 Existing history is preserved.").arg(result.error));
    return;
  }
  const auto previous_jobs = jobs_;
  QVector<ExportJob> next_jobs;
  const auto& records = result.records;
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

  for (auto it = unconfirmed_jobs_.begin(); it != unconfirmed_jobs_.end();) {
    const bool in_catalog = std::any_of(next_jobs.cbegin(), next_jobs.cend(), [&](const auto& job) { return job.job_id == it.key(); });
    if (in_catalog && !admitting_ids_.contains(it.key())) it = unconfirmed_jobs_.erase(it);
    else {
      if (!in_catalog) next_jobs.push_back(it.value());
      ++it;
    }
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
          lhs.metadata_json != rhs.metadata_json || lhs.preset != rhs.preset ||
          lhs.destination_directory != rhs.destination_directory ||
          lhs.output_path != rhs.output_path || lhs.status != rhs.status ||
          lhs.error_message != rhs.error_message || lhs.created_at != rhs.created_at ||
          lhs.updated_at != rhs.updated_at || lhs.progress != rhs.progress) {
        return true;
      }
    }
    return false;
  }();

  jobs_ = next_jobs;
  emit admissionChanged();
  updateBusyState();
  if (!result.error.isEmpty()) setStatus(QStringLiteral("Export history unavailable: %1 Existing history is preserved.").arg(result.error));
  if (emit_signal && jobs_changed) {
    emit jobsChanged();
  }
}

PersistentJobRecord ExportJobController::recordFromJob(const ExportJob& job) {
  PersistentJobRecord record;
  record.job_id = job.job_id;
  record.job_kind = QStringLiteral("asset_export");
  record.asset_id = job.asset_id;
  record.media_type = job.media_type;
  record.display_label = job.display_label;
  record.source_path = job.source_path;
  record.edited_preview_source = job.edited_preview_source;
  record.recipe_json = job.recipe_json;
  record.metadata_json = job.metadata_json;
  record.preset_name = job.preset;
  record.destination_path = job.destination_directory;
  record.output_path = job.output_path;
  record.status = job.status;
  record.error_message = job.error_message;
  record.progress = job.progress;
  record.created_at = job.created_at;
  record.updated_at = job.updated_at;
  return record;
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
  history_pending_ = true;
  if (history_loading_) return;
  history_loading_ = true;
  history_error_.clear();
  emit loadingChanged();
  setStatus(QStringLiteral("Loading export history…"));
  startHistoryLoad();
}

void ExportJobController::startHistoryLoad() {
  history_pending_ = false;
  // No QObject capture: a closed controller does not wait for the helper and
  // cannot be accessed by the detached worker. Publication stays on the GUI thread.
  history_watcher_.setFuture(QtConcurrent::run([reader = history_reader_] {
    try { return reader(); }
    catch (const std::exception& error) { return ExportHistoryReadResult{{}, QString::fromUtf8(error.what()).left(512)}; }
    catch (...) { return ExportHistoryReadResult{{}, QStringLiteral("Export history read failed unexpectedly.")}; }
  }));
}

void ExportJobController::updateBusyState() {
  const auto has_active_jobs = std::any_of(
      jobs_.cbegin(),
      jobs_.cend(),
      [](const auto& job) {
        return job.status == QStringLiteral("Saving") ||
               job.status == QStringLiteral("Queued") ||
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
