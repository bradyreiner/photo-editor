#include "export_job_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QUrl>

#include <algorithm>
#include <cmath>

#include "edit_recipe_state.h"
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

int preset_max_edge(const QString& preset) {
  const auto normalized = preset.trimmed();
  if (normalized.compare(QStringLiteral("Quick Web"), Qt::CaseInsensitive) == 0) {
    return 1440;
  }
  if (normalized.compare(QStringLiteral("Client Preview"), Qt::CaseInsensitive) == 0) {
    return 2048;
  }
  if (normalized.compare(QStringLiteral("Web Proof"), Qt::CaseInsensitive) == 0) {
    return 2560;
  }
  return 0;
}

QString preset_format(const QString& preset) {
  const auto normalized = preset.trimmed();
  if (normalized.compare(QStringLiteral("Archive TIFF"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("TIFF");
  }
  if (normalized.compare(QStringLiteral("Original Copy"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Source");
  }
  if (normalized.compare(QStringLiteral("Web Proof"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("Client Preview"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("Quick Web"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("JPEG");
  }
  return QStringLiteral("PNG");
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
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &ExportJobController::refreshJobs);
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

void ExportJobController::initializeFromStore() {
  loadPersistedJobs(true);
}

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
  job.metadata_json = export_crop_metadata(crop_preset, crop_x, crop_y, crop_width, crop_height);
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
    const auto effective_crop_preset = is_photo_media(media_type) ? crop_preset : QString();
    if (queueExportWithCropConstraint(
            asset_id,
            media_type,
            display_label,
            file_path,
            QString(),
            recipe_json,
            effective_preset,
            destination_directory,
            effective_crop_preset,
            crop_x,
            crop_y,
            crop_width,
            crop_height)) {
      ++queued;
    }
  }
  return queued;
}

QVariantMap ExportJobController::exportPreflight(
    const QString& media_type,
    const QString& source_path,
    const QString& edited_preview_source,
    const QString& recipe_json,
    const QString& preset,
    const QString& destination_directory,
    int histogram_sample_count,
    int shadow_clip_count,
    int highlight_clip_count,
    int raw_clip_sample_count,
    int raw_shadow_clip_count,
    int raw_highlight_clip_count) const {
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
  const auto normalized_destination = normalized_local_path(destination_directory);
  const auto render_photo = should_render_photo_job(normalized_media_type, normalized_preset);

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

  const auto validation = validate_edit_recipe_json(recipe_json);
  if (!recipe_json.trimmed().isEmpty() && !validation.valid) {
    add_issue(
        QStringLiteral("error"),
        QStringLiteral("recipe-invalid"),
        QStringLiteral("The edit recipe has invalid export settings."));
  }

  const auto recipe_state = parse_edit_recipe_json(recipe_json);
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
      recipe_state.output_color_space.trimmed().isEmpty()) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("profile-default"),
        QStringLiteral("No explicit output profile is set; the export will use the recipe default."));
  }

  const auto output_profile =
      recipe_state.output_color_space.trimmed().isEmpty() ? QStringLiteral("sRGB")
                                                          : recipe_state.output_color_space.trimmed();
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

  if (!recipe_state.raw_gamut_mapping &&
      output_profile.compare(QStringLiteral("sRGB"), Qt::CaseInsensitive) == 0) {
    add_issue(
        QStringLiteral("warning"),
        QStringLiteral("gamut-mapping-off"),
        QStringLiteral("RAW gamut mapping is off for an sRGB export."));
  }

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

  QVariantMap result;
  result.insert(QStringLiteral("ok"), error_count == 0);
  result.insert(QStringLiteral("issueCount"), issues.size());
  result.insert(QStringLiteral("errorCount"), error_count);
  result.insert(QStringLiteral("warningCount"), warning_count);
  result.insert(QStringLiteral("issues"), issues);
  result.insert(QStringLiteral("preset"), normalized_preset);
  result.insert(QStringLiteral("format"), preset_format(normalized_preset));
  result.insert(QStringLiteral("outputColorSpace"), output_profile);
  result.insert(QStringLiteral("outputHeadroom"), qBound(1.0, recipe_state.output_headroom, 16.0));
  result.insert(QStringLiteral("softProofEnabled"), recipe_state.soft_proof_enabled);
  result.insert(QStringLiteral("softProofProfile"), recipe_state.soft_proof_profile);
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
  return result;
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
  value.insert(QStringLiteral("metadata"), job.metadata_json.toVariantMap());
  value.insert(
      QStringLiteral("exportCrop"),
      job.metadata_json.value(QStringLiteral("exportCrop")).toObject().toVariantMap());
  value.insert(
      QStringLiteral("previewExportDiff"),
      job.metadata_json.value(QStringLiteral("previewExportDiff")).toObject().toVariantMap());
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
  record.metadata_json = job.metadata_json;
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
