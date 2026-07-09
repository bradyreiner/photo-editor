#include "job_runner.h"

#include "app_bootstrap.h"
#include "export_image_settings.h"
#include "observability.h"
#include "timeline_asset_store.h"
#include "timeline_document_controller.h"
#include "worker_queue_registry.h"

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
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "edit_recipe_state.h"
#include "job_store.h"
#include "photo_render_utils.h"
#include "working_image.h"

namespace photo_editor::desktop {

namespace {

struct PreviewExportDiff final {
  bool available = false;
  bool within_tolerance = false;
  int width = 0;
  int height = 0;
  int max_channel_delta = 0;
  double mean_channel_delta = 0.0;
  int tolerance = 1;
  QString status;
};

QString iso_now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

qint64 queue_wait_msecs_since(const QString& created_at, const QDateTime& reference_time) {
  const auto created = QDateTime::fromString(created_at.trimmed(), Qt::ISODate);
  if (!created.isValid() || !reference_time.isValid()) {
    return 0;
  }
  return qMax<qint64>(0, created.toUTC().msecsTo(reference_time.toUTC()));
}

qint64 queue_wait_msecs_for_job(const PersistentJobRecord& job) {
  return queue_wait_msecs_since(job.created_at, QDateTime::currentDateTimeUtc());
}

QString worker_pool_id_for_job_kind(const QString& job_kind) {
  if (job_kind == QStringLiteral("catalog_import")) {
    return QStringLiteral("catalog-import");
  }
  if (job_kind == QStringLiteral("timeline_render")) {
    return QStringLiteral("timeline-media");
  }
  if (job_kind == QStringLiteral("metadata_extraction") ||
      job_kind == QStringLiteral("metadata_extract")) {
    return QStringLiteral("metadata-extraction");
  }
  if (job_kind == QStringLiteral("carousel_render")) {
    return QStringLiteral("carousel-render");
  }
  return QStringLiteral("export");
}

QString worker_surface_for_job_kind(const QString& job_kind) {
  if (job_kind == QStringLiteral("catalog_import") ||
      job_kind == QStringLiteral("metadata_extraction") ||
      job_kind == QStringLiteral("metadata_extract")) {
    return QStringLiteral("library");
  }
  if (job_kind == QStringLiteral("timeline_render")) {
    return QStringLiteral("timeline");
  }
  if (job_kind == QStringLiteral("carousel_render")) {
    return QStringLiteral("carousel");
  }
  return QStringLiteral("export");
}

QString cancellation_reason_for_job(const PersistentJobRecord& job) {
  const auto explicit_reason =
      job.metadata_json.value(QStringLiteral("cancellationReason")).toString().trimmed();
  if (!explicit_reason.isEmpty()) {
    return explicit_reason;
  }
  const auto cancel_action = job.metadata_json.value(QStringLiteral("cancelAction")).toString().trimmed();
  if (!cancel_action.isEmpty()) {
    return cancel_action;
  }
  if (job.metadata_json.value(QStringLiteral("cancellationRequested")).toBool() ||
      job.status == QStringLiteral("Canceling")) {
    return QStringLiteral("cooperative_cancel_requested");
  }
  if (job.status == QStringLiteral("Canceled")) {
    return QStringLiteral("canceled");
  }
  return {};
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

void apply_export_crop_constraint(EditRecipeState* state, const QJsonObject& metadata) {
  if (state == nullptr) {
    return;
  }
  const auto crop = metadata.value(QStringLiteral("exportCrop")).toObject();
  if (crop.isEmpty() || !crop.value(QStringLiteral("enabled")).toBool(true)) {
    return;
  }

  const auto preset = crop.value(QStringLiteral("preset")).toString().trimmed();
  const auto has_rect = crop.value(QStringLiteral("width")).toDouble() > 0.0 &&
                        crop.value(QStringLiteral("height")).toDouble() > 0.0;
  if (!preset.isEmpty() && preset.compare(QStringLiteral("Current"), Qt::CaseInsensitive) != 0) {
    state->crop_rect_x = 0.0;
    state->crop_rect_y = 0.0;
    state->crop_rect_width = 0.0;
    state->crop_rect_height = 0.0;
    state->crop_zoom = 1.0;
    state->crop_preset = preset.compare(QStringLiteral("Original"), Qt::CaseInsensitive) == 0
                             ? QStringLiteral("Original")
                             : preset;
  }

  if (has_rect) {
    const auto width = qBound(0.01, crop.value(QStringLiteral("width")).toDouble(), 1.0);
    const auto height = qBound(0.01, crop.value(QStringLiteral("height")).toDouble(), 1.0);
    state->crop_rect_x = qBound(0.0, crop.value(QStringLiteral("x")).toDouble(), 1.0 - width);
    state->crop_rect_y = qBound(0.0, crop.value(QStringLiteral("y")).toDouble(), 1.0 - height);
    state->crop_rect_width = width;
    state->crop_rect_height = height;
    state->crop_preset = QStringLiteral("Original");
    state->crop_zoom = 1.0;
  }
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

QImage prepare_image_for_export_write(const QImage& image, const ExportImageSettings& options) {
  if (image.isNull()) {
    return {};
  }
  if (!options.flatten_alpha) {
    return image.convertToFormat(options.image_format);
  }

  QImage flattened(image.size(), QImage::Format_RGB888);
  flattened.setColorSpace(image.colorSpace());
  flattened.fill(Qt::white);
  QPainter painter(&flattened);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.drawImage(0, 0, image);
  painter.end();
  return flattened;
}

QColorSpace export_target_color_space(
    const RenderEditState& state,
    const ExportImageSettings& options) {
  if (options.scene_linear) {
    return linear_srgb_color_space();
  }
  return preview_color_space_for_name(normalized_output_color_space_name(state.output_color_space));
}

void append_pfm_float(QByteArray* data, float value) {
  if (!std::isfinite(value)) {
    value = 0.0F;
  }
  quint32 bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const auto little_endian_bits = qToLittleEndian(bits);
  data->append(reinterpret_cast<const char*>(&little_endian_bits), sizeof(little_endian_bits));
}

bool write_pfm_image(const QImage& image, const QString& output_path) {
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return false;
  }

  const auto float_image = image.format() == QImage::Format_RGBA32FPx4
                               ? image
                               : image.convertToFormat(QImage::Format_RGBA32FPx4);
  if (float_image.isNull()) {
    return false;
  }

  QByteArray data;
  data.reserve(64 + float_image.width() * float_image.height() * 3 * static_cast<int>(sizeof(float)));
  data.append("PF\n");
  data.append(QByteArray::number(float_image.width()));
  data.append(' ');
  data.append(QByteArray::number(float_image.height()));
  data.append("\n-1.0\n");
  for (int y = float_image.height() - 1; y >= 0; --y) {
    const auto* scanline = reinterpret_cast<const float*>(float_image.constScanLine(y));
    for (int x = 0; x < float_image.width(); ++x) {
      const auto* pixel = scanline + (x * 4);
      append_pfm_float(&data, pixel[0]);
      append_pfm_float(&data, pixel[1]);
      append_pfm_float(&data, pixel[2]);
    }
  }

  QFile file(output_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(data) == data.size();
}

int preview_export_diff_tolerance_for_preset(const QString& preset) {
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

QVariantMap preview_export_diff_to_map(
    const PreviewExportDiff& diff,
    const QJsonObject& export_preview_job = {}) {
  QVariantMap value;
  value.insert(QStringLiteral("available"), diff.available);
  value.insert(QStringLiteral("withinTolerance"), diff.within_tolerance);
  value.insert(QStringLiteral("width"), diff.width);
  value.insert(QStringLiteral("height"), diff.height);
  value.insert(QStringLiteral("maxChannelDelta"), diff.max_channel_delta);
  value.insert(QStringLiteral("meanChannelDelta"), diff.mean_channel_delta);
  value.insert(QStringLiteral("tolerance"), diff.tolerance);
  value.insert(QStringLiteral("status"), diff.status);
  if (!export_preview_job.isEmpty()) {
    value.insert(
        QStringLiteral("exportPreviewJobId"),
        export_preview_job.value(QStringLiteral("jobId")).toString());
    value.insert(
        QStringLiteral("exportPreviewJobKind"),
        export_preview_job.value(QStringLiteral("jobKind")).toString());
    value.insert(
        QStringLiteral("exportPreviewStage"),
        export_preview_job.value(QStringLiteral("stage")).toString());
    value.insert(
        QStringLiteral("exportPreviewStateSignature"),
        export_preview_job.value(QStringLiteral("stateSignature")).toString());
  }
  return value;
}

QString sha1_hex(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString json_sha1(const QJsonObject& payload) {
  return sha1_hex(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QVariantMap export_trace_mask_source_payload(QVariantMap mask) {
  for (const auto& key : {
           QStringLiteral("mask_source_id"),
           QStringLiteral("mask_set_id"),
           QStringLiteral("mask_set_name"),
           QStringLiteral("mask_source_kind"),
           QStringLiteral("mask_source_operation"),
           QStringLiteral("mask_source_schema_version"),
           QStringLiteral("mask_source_count"),
           QStringLiteral("mask_source_ids"),
           QStringLiteral("mask_source_hash"),
           QStringLiteral("mask_alpha_trace_hash"),
           QStringLiteral("mask_alpha_trace_signature"),
           QStringLiteral("mask_alpha_provider_contract"),
           QStringLiteral("mask_alpha_provider"),
           QStringLiteral("mask_alpha_full_resolution_available"),
           QStringLiteral("mask_alpha_deterministic"),
           QStringLiteral("mask_alpha_target_width"),
           QStringLiteral("mask_alpha_target_height"),
           QStringLiteral("mask_alpha_provider_signature"),
           QStringLiteral("mask_alpha_component_providers"),
           QStringLiteral("mask_source_revision_hash"),
           QStringLiteral("mask_alpha_raster_width"),
           QStringLiteral("mask_alpha_raster_height"),
           QStringLiteral("mask_source_point_count"),
           QStringLiteral("mask_source_stroke_count"),
           QStringLiteral("mask_work_state_revision"),
       }) {
    mask.remove(key);
  }

  for (const auto& key : {
           QStringLiteral("segmentationData"),
           QStringLiteral("depthData"),
           QStringLiteral("generatedMaskCleanupSourceData"),
       }) {
    const auto bytes = mask.value(key).toByteArray();
    if (!bytes.isEmpty()) {
      QVariantMap fingerprint;
      fingerprint.insert(QStringLiteral("bytes"), bytes.size());
      fingerprint.insert(QStringLiteral("sha1"), sha1_hex(bytes));
      mask.insert(key, fingerprint);
    }
  }
  return mask;
}

QString export_mask_alpha_provider_for_mask(const QVariantMap& mask) {
  const auto kind = mask.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind == QStringLiteral("brush") &&
      (!mask.value(QStringLiteral("brushPoints")).toList().isEmpty() ||
       !mask.value(QStringLiteral("brushStrokes")).toList().isEmpty())) {
    return QStringLiteral("brush-stroke-replay");
  }

  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto has_raster_payload =
      width > 0 && height > 0 && data.size() == static_cast<qsizetype>(width * height);
  if (has_raster_payload || kind == QStringLiteral("alpha") ||
      kind == QStringLiteral("subject") || kind == QStringLiteral("object") ||
      kind == QStringLiteral("sky") || kind == QStringLiteral("background")) {
    return has_raster_payload ? QStringLiteral("raster-payload")
                              : QStringLiteral("raster-missing");
  }

  if (kind == QStringLiteral("linear") || kind == QStringLiteral("radial") ||
      kind == QStringLiteral("ellipse") || kind == QStringLiteral("rectangle") ||
      kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso") ||
      kind == QStringLiteral("luminance_range") || kind == QStringLiteral("color_range") ||
      kind == QStringLiteral("depth_range")) {
    return QStringLiteral("procedural-geometry");
  }

  return QStringLiteral("unavailable");
}

QString export_source_revision_hash(const QString& source_path) {
  const QFileInfo source_info(source_path);
  if (!source_info.exists()) {
    return {};
  }

  QJsonObject payload;
  payload.insert(QStringLiteral("version"), QStringLiteral("export-source-revision-v1"));
  payload.insert(QStringLiteral("path"), source_info.absoluteFilePath());
  payload.insert(QStringLiteral("size"), QString::number(source_info.size()));
  payload.insert(
      QStringLiteral("lastModified"),
      source_info.lastModified().toUTC().toString(Qt::ISODateWithMs));
  return json_sha1(payload);
}

QJsonObject export_mask_alpha_trace_metadata(
    const QString& recipe_json,
    const QString& source_path) {
  const auto state = parse_edit_recipe_json(recipe_json);
  if (state.mask_stack.isEmpty()) {
    return {};
  }

  const auto source_revision_hash = export_source_revision_hash(source_path);
  QJsonArray component_ids;
  QJsonArray component_hashes;
  QJsonArray component_signatures;
  QJsonArray component_providers;
  auto ordinal = 1;
  auto max_width = 0;
  auto max_height = 0;
  for (const auto& value : state.mask_stack) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask.value(QStringLiteral("enabled"), true).toBool()) {
      continue;
    }

    const auto source_id = mask.value(QStringLiteral("id")).toString().simplified();
    if (source_id.isEmpty()) {
      continue;
    }

    const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (width > 0 && height > 0) {
      max_width = std::max(max_width, width);
      max_height = std::max(max_height, height);
    }
    const auto provider = export_mask_alpha_provider_for_mask(mask);
    const auto provider_available =
        provider != QStringLiteral("unavailable") && provider != QStringLiteral("raster-missing");

    auto source_payload = QJsonObject::fromVariantMap(export_trace_mask_source_payload(mask));
    source_payload.insert(QStringLiteral("ordinal"), ordinal - 1);
    const auto source_hash = json_sha1(source_payload);

    QJsonObject trace_payload;
    trace_payload.insert(QStringLiteral("maskSourceHash"), source_hash);
    trace_payload.insert(QStringLiteral("alphaProvider"), provider);
    trace_payload.insert(QStringLiteral("alphaFullResolutionAvailable"), provider_available);
    trace_payload.insert(QStringLiteral("alphaDeterministic"), provider_available);
    if (!source_revision_hash.isEmpty()) {
      trace_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
    }
    if (width > 0 && height > 0) {
      trace_payload.insert(QStringLiteral("rasterWidth"), width);
      trace_payload.insert(QStringLiteral("rasterHeight"), height);
    }
    const auto trace_hash = json_sha1(trace_payload);
    component_ids.append(source_id);
    component_hashes.append(QStringLiteral("sha1:%1").arg(trace_hash));
    component_providers.append(provider);
    component_signatures.append(
        QStringLiteral("mask:%1|source:sha1:%2|revision:%3|raster:%4x%5|provider:%6|full:%7")
            .arg(source_id)
            .arg(source_hash)
            .arg(source_revision_hash.isEmpty()
                     ? QStringLiteral("none")
                      : QStringLiteral("sha1:%1").arg(source_revision_hash))
            .arg(width)
            .arg(height)
            .arg(provider)
            .arg(provider_available ? QStringLiteral("true") : QStringLiteral("false")));
  }

  if (component_ids.isEmpty()) {
    return {};
  }

  QJsonObject source_payload;
  source_payload.insert(QStringLiteral("context"), QStringLiteral("export-render"));
  source_payload.insert(QStringLiteral("componentSignatures"), component_signatures);
  source_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
  const auto source_hash = json_sha1(source_payload);

  QJsonObject trace_payload;
  trace_payload.insert(QStringLiteral("maskSourceHash"), source_hash);
  trace_payload.insert(QStringLiteral("componentHashes"), component_hashes);
  trace_payload.insert(QStringLiteral("componentProviders"), component_providers);
  trace_payload.insert(QStringLiteral("alphaProvider"), QStringLiteral("export-render"));
  const auto export_available =
      component_providers.size() == component_ids.size() &&
      !component_providers.contains(QStringLiteral("unavailable")) &&
      !component_providers.contains(QStringLiteral("raster-missing"));
  trace_payload.insert(QStringLiteral("alphaFullResolutionAvailable"), export_available);
  trace_payload.insert(QStringLiteral("alphaDeterministic"), export_available);
  if (!source_revision_hash.isEmpty()) {
    trace_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
  }
  if (max_width > 0 && max_height > 0) {
    trace_payload.insert(QStringLiteral("rasterWidth"), max_width);
    trace_payload.insert(QStringLiteral("rasterHeight"), max_height);
  }
  const auto trace_hash = json_sha1(trace_payload);

  QJsonObject fields;
  fields.insert(QStringLiteral("trace_context"), QStringLiteral("export-render"));
  fields.insert(QStringLiteral("mask_source_count"), component_ids.size());
  fields.insert(QStringLiteral("mask_source_ids"), component_ids);
  fields.insert(QStringLiteral("mask_source_hash"), QStringLiteral("sha1:%1").arg(source_hash));
  fields.insert(QStringLiteral("mask_alpha_trace_hash"), QStringLiteral("sha1:%1").arg(trace_hash));
  fields.insert(
      QStringLiteral("mask_alpha_provider_contract"),
      QStringLiteral("shared-mask-alpha-provider-v1"));
  fields.insert(QStringLiteral("mask_alpha_provider"), QStringLiteral("export-render"));
  fields.insert(QStringLiteral("mask_alpha_full_resolution_available"), export_available);
  fields.insert(QStringLiteral("mask_alpha_deterministic"), export_available);
  fields.insert(QStringLiteral("mask_alpha_component_providers"), component_providers);
  fields.insert(
      QStringLiteral("mask_alpha_provider_signature"),
      QStringLiteral("contract:shared-mask-alpha-provider-v1|provider:export-render|full:%1|deterministic:%2|components:%3|target:%4x%5")
          .arg(export_available ? QStringLiteral("true") : QStringLiteral("false"),
               export_available ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(component_ids.size())
          .arg(max_width)
          .arg(max_height));
  fields.insert(
      QStringLiteral("mask_alpha_trace_signature"),
      QStringLiteral("export-render|sources:%1|source:sha1:%2|revision:%3|raster:%4x%5|provider:export-render|full:%6")
          .arg(component_ids.size())
          .arg(source_hash)
          .arg(source_revision_hash.isEmpty()
                   ? QStringLiteral("none")
                   : QStringLiteral("sha1:%1").arg(source_revision_hash))
          .arg(max_width)
          .arg(max_height)
          .arg(export_available ? QStringLiteral("true") : QStringLiteral("false")));
  if (!source_revision_hash.isEmpty()) {
    fields.insert(
        QStringLiteral("mask_source_revision_hash"),
        QStringLiteral("sha1:%1").arg(source_revision_hash));
  }
  if (max_width > 0 && max_height > 0) {
    fields.insert(QStringLiteral("mask_alpha_raster_width"), max_width);
    fields.insert(QStringLiteral("mask_alpha_raster_height"), max_height);
  }
  return fields;
}

QString export_render_state_signature(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& mask_alpha_trace,
    const QJsonObject& export_image_settings) {
  QJsonObject payload;
  payload.insert(QStringLiteral("jobId"), job.job_id);
  payload.insert(QStringLiteral("assetId"), job.asset_id);
  payload.insert(QStringLiteral("sourcePath"), source_path);
  payload.insert(QStringLiteral("outputPath"), output_path);
  payload.insert(QStringLiteral("recipeJson"), job.recipe_json);
  payload.insert(QStringLiteral("preset"), job.preset_name);
  payload.insert(QStringLiteral("metadata"), job.metadata_json);
  payload.insert(QStringLiteral("renderPhoto"), render_photo);
  payload.insert(QStringLiteral("maskAlphaTrace"), mask_alpha_trace);
  payload.insert(QStringLiteral("exportImageSettings"), export_image_settings);
  return QStringLiteral("sha1:%1").arg(QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(payload).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex()));
}

QJsonObject export_render_job_metadata(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo) {
  const auto mask_alpha_trace =
      render_photo ? export_mask_alpha_trace_metadata(job.recipe_json, source_path) : QJsonObject{};
  const auto export_image_settings =
      render_photo ? export_image_settings_metadata(export_image_settings_for_preset(job.preset_name))
                   : QJsonObject{};
  const auto signature = export_render_state_signature(
      job,
      source_path,
      output_path,
      render_photo,
      mask_alpha_trace,
      export_image_settings);
  const auto signature_token =
      signature.mid(signature.indexOf(QLatin1Char(':')) + 1, 12);
  QJsonObject metadata;
  metadata.insert(
      QStringLiteral("jobId"),
      QStringLiteral("export_render-%1-%2").arg(job.job_id, signature_token));
  metadata.insert(
      QStringLiteral("jobKind"),
      render_photo ? QStringLiteral("full_resolution_export") : QStringLiteral("file_export_copy"));
  metadata.insert(QStringLiteral("stage"), QStringLiteral("export_render"));
  metadata.insert(QStringLiteral("assetId"), job.asset_id);
  metadata.insert(QStringLiteral("sourcePath"), source_path);
  metadata.insert(QStringLiteral("outputPath"), output_path);
  metadata.insert(QStringLiteral("preset"), job.preset_name);
  metadata.insert(QStringLiteral("stateSignature"), signature);
  metadata.insert(QStringLiteral("queuedAt"), job.created_at);
  metadata.insert(QStringLiteral("renderPhoto"), render_photo);
  if (!export_image_settings.isEmpty()) {
    metadata.insert(QStringLiteral("exportImageSettings"), export_image_settings);
    metadata.insert(
        QStringLiteral("format"),
        export_image_settings.value(QStringLiteral("format")).toString());
    metadata.insert(
        QStringLiteral("bitDepth"),
        export_image_settings.value(QStringLiteral("bitDepth")).toInt());
    metadata.insert(
        QStringLiteral("flattenAlpha"),
        export_image_settings.value(QStringLiteral("flattenAlpha")).toBool());
    metadata.insert(
        QStringLiteral("embedProfile"),
        export_image_settings.value(QStringLiteral("embedProfile")).toBool());
    metadata.insert(
        QStringLiteral("sceneLinear"),
        export_image_settings.value(QStringLiteral("sceneLinear")).toBool());
    metadata.insert(
        QStringLiteral("preservesOverRange"),
        export_image_settings.value(QStringLiteral("preservesOverRange")).toBool());
    metadata.insert(
        QStringLiteral("outputEncoding"),
        export_image_settings.value(QStringLiteral("outputEncoding")).toString());
    metadata.insert(
        QStringLiteral("headroomPolicy"),
        export_image_settings.value(QStringLiteral("headroomPolicy")).toString());
  }
  if (!mask_alpha_trace.isEmpty()) {
    metadata.insert(QStringLiteral("maskAlphaTrace"), mask_alpha_trace);
  }
  return metadata;
}

QString export_preview_state_signature(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& export_render_job,
    int tolerance) {
  QJsonObject payload;
  payload.insert(QStringLiteral("jobId"), job.job_id);
  payload.insert(QStringLiteral("assetId"), job.asset_id);
  payload.insert(QStringLiteral("sourcePath"), source_path);
  payload.insert(QStringLiteral("outputPath"), output_path);
  payload.insert(QStringLiteral("recipeJson"), job.recipe_json);
  payload.insert(QStringLiteral("preset"), job.preset_name);
  payload.insert(QStringLiteral("renderPhoto"), render_photo);
  payload.insert(QStringLiteral("tolerance"), tolerance);
  payload.insert(QStringLiteral("exportRenderJob"), export_render_job);
  return QStringLiteral("sha1:%1").arg(QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(payload).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex()));
}

QJsonObject export_preview_job_metadata(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& export_render_job) {
  const auto tolerance = render_photo ? preview_export_diff_tolerance_for_preset(job.preset_name) : 0;
  const auto signature = export_preview_state_signature(
      job,
      source_path,
      output_path,
      render_photo,
      export_render_job,
      tolerance);
  const auto signature_token =
      signature.mid(signature.indexOf(QLatin1Char(':')) + 1, 12);
  QJsonObject metadata;
  metadata.insert(
      QStringLiteral("jobId"),
      QStringLiteral("export_preview-%1-%2").arg(job.job_id, signature_token));
  metadata.insert(
      QStringLiteral("jobKind"),
      render_photo ? QStringLiteral("full_resolution_export_preview")
                   : QStringLiteral("file_export_copy_preview"));
  metadata.insert(QStringLiteral("stage"), QStringLiteral("export_preview_diff"));
  metadata.insert(QStringLiteral("assetId"), job.asset_id);
  metadata.insert(QStringLiteral("sourcePath"), source_path);
  metadata.insert(QStringLiteral("outputPath"), output_path);
  metadata.insert(QStringLiteral("preset"), job.preset_name);
  metadata.insert(QStringLiteral("stateSignature"), signature);
  metadata.insert(QStringLiteral("queuedAt"), job.created_at);
  metadata.insert(QStringLiteral("renderPhoto"), render_photo);
  metadata.insert(QStringLiteral("tolerance"), tolerance);
  metadata.insert(
      QStringLiteral("exportRenderJobId"),
      export_render_job.value(QStringLiteral("jobId")).toString());
  return metadata;
}

PreviewExportDiff compare_preview_to_export(
    const QImage& preview_image,
    const QImage& exported_image,
    int tolerance) {
  PreviewExportDiff diff;
  diff.tolerance = tolerance;
  if (preview_image.isNull() || exported_image.isNull()) {
    diff.status = QStringLiteral("unavailable");
    return diff;
  }

  diff.available = true;
  diff.width = exported_image.width();
  diff.height = exported_image.height();
  if (preview_image.size() != exported_image.size()) {
    diff.max_channel_delta = 255;
    diff.status = QStringLiteral("size-mismatch");
    return diff;
  }

  const auto preview_argb = preview_image.convertToFormat(QImage::Format_ARGB32);
  const auto exported_argb = exported_image.convertToFormat(QImage::Format_ARGB32);
  qint64 total_delta = 0;
  qint64 channel_count = 0;
  for (int y = 0; y < preview_argb.height(); ++y) {
    for (int x = 0; x < preview_argb.width(); ++x) {
      const auto preview_pixel = preview_argb.pixelColor(x, y);
      const auto exported_pixel = exported_argb.pixelColor(x, y);
      const auto red_delta = qAbs(preview_pixel.red() - exported_pixel.red());
      const auto green_delta = qAbs(preview_pixel.green() - exported_pixel.green());
      const auto blue_delta = qAbs(preview_pixel.blue() - exported_pixel.blue());
      diff.max_channel_delta = std::max(diff.max_channel_delta, red_delta);
      diff.max_channel_delta = std::max(diff.max_channel_delta, green_delta);
      diff.max_channel_delta = std::max(diff.max_channel_delta, blue_delta);
      total_delta += red_delta + green_delta + blue_delta;
      channel_count += 3;
    }
  }

  diff.mean_channel_delta = channel_count > 0 ? static_cast<double>(total_delta) / static_cast<double>(channel_count) : 0.0;
  diff.within_tolerance = diff.max_channel_delta <= diff.tolerance;
  diff.status = diff.within_tolerance ? QStringLiteral("matched") : QStringLiteral("differs");
  return diff;
}

QString output_suffix_for_preset(const QString& preset, bool rendered_output) {
  if (!rendered_output) {
    return {};
  }
  const auto options = export_image_settings_for_preset(preset);
  if (options.writer_format == "jpg") {
    return QStringLiteral("jpg");
  }
  if (options.writer_format == "tif") {
    return QStringLiteral("tif");
  }
  if (options.writer_format == "pfm") {
    return QStringLiteral("pfm");
  }
  return QStringLiteral("png");
}

bool render_photo_export(
    const QString& source_path,
    const QString& recipe_json,
    const QString& preset,
    const QJsonObject& export_metadata,
    const QString& output_path,
    RenderBackendTelemetry* telemetry = nullptr,
    QImage* rendered_output = nullptr) {
  auto state = parse_edit_recipe_json(recipe_json);
  apply_export_crop_constraint(&state, export_metadata);
  QImage image;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(source_path, &image, 0, state, &metadata)) {
    return false;
  }

  image = apply_crop(image, state);
  const auto options = export_image_settings_for_preset(preset);
  image = render_output_image(
      image,
      state,
      export_target_color_space(state, options),
      options.image_format,
      metadata.valid ? &metadata : nullptr,
      telemetry,
      RenderMaskOptions{.rebuild_editable_masks_at_output_resolution = true});
  if (image.isNull()) {
    return false;
  }
  if (options.max_edge > 0 && (image.width() > options.max_edge || image.height() > options.max_edge)) {
    image = image.scaled(options.max_edge, options.max_edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  image = prepare_image_for_export_write(image, options);
  if (image.isNull()) {
    return false;
  }
  if (rendered_output != nullptr) {
    *rendered_output = image;
  }

  if (options.writer_format == "pfm") {
    return write_pfm_image(image, output_path);
  }

  QImageWriter writer(output_path, options.writer_format);
  if (options.writer_format == "jpg") {
    writer.setQuality(options.jpeg_quality);
  }
  return writer.write(image);
}

QString output_file_path_for_job(
    const PersistentJobRecord& job,
    const QString& source_path,
    bool rendered_output) {
  const QFileInfo source_info(source_path);
  const auto safe_label = sanitize_token(display_label_for_job(job), QStringLiteral("export"));
  const auto suffix = rendered_output ? output_suffix_for_preset(job.preset_name, true) : source_info.suffix().toLower();
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
    const QJsonObject& export_metadata,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& export_render_job,
    const QJsonObject& export_preview_job) {
  QElapsedTimer timer;
  timer.start();
  QVariantMap result;
  result.insert(QStringLiteral("success"), false);
  result.insert(QStringLiteral("outputPath"), output_path);
  result.insert(QStringLiteral("backend"), render_photo ? QStringLiteral("unknown") : QStringLiteral("copy"));
  result.insert(QStringLiteral("fallbackReason"), QString());
  result.insert(QStringLiteral("exportRenderJob"), export_render_job.toVariantMap());
  result.insert(QStringLiteral("exportPreviewJob"), export_preview_job.toVariantMap());

  QDir().mkpath(QFileInfo(output_path).absolutePath());
  QFile::remove(output_path);
  RenderBackendTelemetry telemetry;
  QImage rendered_output;
  const auto success = render_photo
                           ? render_photo_export(
                                 source_path,
                                 recipe_json,
                                 preset_name,
                                 export_metadata,
                                 output_path,
                                 &telemetry,
                                 &rendered_output)
                           : QFile::copy(source_path, output_path);
  if (render_photo) {
    result.insert(
        QStringLiteral("backend"),
        telemetry.backend.isEmpty() ? QStringLiteral("unknown") : telemetry.backend);
    result.insert(QStringLiteral("fallbackReason"), telemetry.fallback_reason);
  }
  result.insert(QStringLiteral("success"), success);
  if (success && render_photo) {
    const auto tolerance = export_preview_job.value(QStringLiteral("tolerance"))
                               .toInt(preview_export_diff_tolerance_for_preset(preset_name));
    const auto preview_export_diff = compare_preview_to_export(
        rendered_output,
        QImage(output_path),
        tolerance);
    result.insert(
        QStringLiteral("previewExportDiff"),
        preview_export_diff_to_map(preview_export_diff, export_preview_job));
  }
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

QString import_program() {
  return AppBootstrap::helperProgram(QStringLiteral("import_catalog"));
}

QStringList import_arguments(const QString& folder_path, const QString& import_mode) {
  const auto drive_id = derive_drive_id(folder_path);
  const auto drive_name = derive_drive_name(folder_path);

  return AppBootstrap::helperArguments(
      QStringLiteral("import_catalog"),
      {folder_path, drive_id, drive_name, import_mode});
}

QString selection_manifest_path(const QJsonObject& metadata_json) {
  const auto selected_paths = metadata_json.value(QStringLiteral("selectedPaths")).toArray();
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

QVariantMap run_import_work(const QString& source_path, const QJsonObject& metadata_json) {
  QElapsedTimer timer;
  timer.start();
  QVariantMap result;
  result.insert(QStringLiteral("success"), false);

  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    result.insert(QStringLiteral("errorMessage"), environment_error);
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }

  const auto import_mode = metadata_json.value(QStringLiteral("importMode")).toString().trimmed().isEmpty()
                               ? QStringLiteral("referenced")
                               : metadata_json.value(QStringLiteral("importMode")).toString().trimmed();
  auto arguments = import_arguments(source_path, import_mode);
  const auto manifest_path = selection_manifest_path(metadata_json);
  if (metadata_json.value(QStringLiteral("selectionCount")).toInt() > 0 && manifest_path.isEmpty()) {
    result.insert(QStringLiteral("errorMessage"), QStringLiteral("Unable to stage the selected photos for import."));
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
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
    result.insert(
        QStringLiteral("errorMessage"),
        output.isEmpty() ? QStringLiteral("Import failed.") : output);
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }

  result.insert(QStringLiteral("success"), true);
  if (!output.isEmpty()) {
    result.insert(QStringLiteral("resultMessage"), output);
  }
  result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  return result;
}

}  // namespace

JobRunner::JobRunner(QObject* parent) : QObject(parent) {
  connect(&async_job_watcher_, &QFutureWatcher<QVariantMap>::finished, this, &JobRunner::handleAsyncJobFinished);
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &JobRunner::wake);
}

void JobRunner::initializeFromStore() {
  (void)recoverInterruptedJobRecords(nullptr);
  wake();
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
  active_queue_wait_ms_ = queue_wait_msecs_for_job(job);
  active_job_timer_.restart();
  observability::logInfo(
      photoEditorJobsLog(),
      QStringLiteral("job.started"),
      {
          {QStringLiteral("job_id"), job.job_id},
          {QStringLiteral("job_kind"), job.job_kind},
          {QStringLiteral("asset_id"), job.asset_id},
          {QStringLiteral("source_path"), job.source_path},
          {QStringLiteral("queue_wait_ms"), active_queue_wait_ms_},
      });
  WorkerQueueRegistry::logSample(
      worker_pool_id_for_job_kind(job.job_kind),
      job.job_kind,
      worker_surface_for_job_kind(job.job_kind),
      1,
      static_cast<int>(qMax<qsizetype>(0, queued_jobs.size() - 1)),
      1,
      active_queue_wait_ms_);
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
    active_queue_wait_ms_ = 0;
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    active_job_id_.clear();
    active_job_kind_.clear();
    active_queue_wait_ms_ = 0;
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
    active_queue_wait_ms_ = 0;
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
    active_queue_wait_ms_ = 0;
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto export_render_job = export_render_job_metadata(job, source_path, output_path, render_photo);
  job.metadata_json.insert(QStringLiteral("exportRenderJob"), export_render_job);
  const auto export_preview_job =
      export_preview_job_metadata(job, source_path, output_path, render_photo, export_render_job);
  job.metadata_json.insert(QStringLiteral("exportPreviewJob"), export_preview_job);
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  async_job_watcher_.setFuture(QtConcurrent::run(
      run_export_work,
      source_path,
      job.recipe_json,
      job.preset_name,
      job.metadata_json,
      output_path,
      render_photo,
      export_render_job,
      export_preview_job));
}

void JobRunner::handleAsyncJobFinished() {
  const auto job_id = active_job_id_;
  const auto job_kind = active_job_kind_;
  const auto queue_wait_ms = active_queue_wait_ms_;
  const auto total_elapsed_ms = active_job_timer_.isValid() ? active_job_timer_.elapsed() : 0;
  active_job_id_.clear();
  active_job_kind_.clear();
  active_queue_wait_ms_ = 0;

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
  const auto backend = result.value(QStringLiteral("backend")).toString().trimmed();
  const auto fallback_reason = result.value(QStringLiteral("fallbackReason")).toString().trimmed();
  const auto export_render_job = result.value(QStringLiteral("exportRenderJob")).toMap();
  const auto export_render_job_id =
      export_render_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto persisted_export_render_job =
      job.metadata_json.value(QStringLiteral("exportRenderJob")).toObject().toVariantMap();
  const auto persisted_export_render_job_id =
      persisted_export_render_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto export_preview_job = result.value(QStringLiteral("exportPreviewJob")).toMap();
  const auto export_preview_job_id =
      export_preview_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto persisted_export_preview_job =
      job.metadata_json.value(QStringLiteral("exportPreviewJob")).toObject().toVariantMap();
  const auto persisted_export_preview_job_id =
      persisted_export_preview_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto render_photo_result =
      export_render_job.value(QStringLiteral("renderPhoto")).toBool();
  const auto export_render_mask_trace =
      export_render_job.value(QStringLiteral("maskAlphaTrace")).toMap();
  const auto stale_export_render_result =
      job_kind == QStringLiteral("asset_export") &&
      (export_render_job_id.isEmpty() ||
       persisted_export_render_job_id.isEmpty() ||
       export_render_job_id != persisted_export_render_job_id);
  const auto stale_export_preview_result =
      job_kind == QStringLiteral("asset_export") &&
      success &&
      render_photo_result &&
      (export_preview_job_id.isEmpty() ||
       persisted_export_preview_job_id.isEmpty() ||
       export_preview_job_id != persisted_export_preview_job_id);
  const auto cancellation_reason = cancellation_reason_for_job(job);
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
    job.metadata_json.insert(
        QStringLiteral("cancellationReason"),
        cancellation_reason.isEmpty() ? QStringLiteral("cooperative_cancel_requested") : cancellation_reason);
  } else if (stale_export_render_result || stale_export_preview_result) {
    const auto output_path = result.value(QStringLiteral("outputPath")).toString().trimmed();
    if (!output_path.isEmpty()) {
      QFile::remove(output_path);
    }
    job.status = QStringLiteral("Failed");
    job.error_message =
        stale_export_preview_result
            ? QStringLiteral("Export preview result was discarded because the job changed.")
            : QStringLiteral("Export render result was discarded because the job changed.");
    job.progress = 0;
    job.metadata_json.insert(QStringLiteral("staleExportRenderResult"), stale_export_render_result);
    job.metadata_json.insert(
        QStringLiteral("staleExportRenderJobId"),
        export_render_job_id);
    job.metadata_json.insert(
        QStringLiteral("staleExportPreviewResult"),
        stale_export_preview_result);
    job.metadata_json.insert(
        QStringLiteral("staleExportPreviewJobId"),
        export_preview_job_id);
  } else if (success) {
    job.output_path = result.value(QStringLiteral("outputPath")).toString();
    if (!export_render_job.isEmpty()) {
      job.metadata_json.insert(
          QStringLiteral("exportRenderJob"),
          QJsonObject::fromVariantMap(export_render_job));
    }
    if (!export_preview_job.isEmpty()) {
      job.metadata_json.insert(
          QStringLiteral("exportPreviewJob"),
          QJsonObject::fromVariantMap(export_preview_job));
    }
    const auto preview_export_diff = result.value(QStringLiteral("previewExportDiff")).toMap();
    if (!preview_export_diff.isEmpty()) {
      job.metadata_json.insert(
          QStringLiteral("previewExportDiff"),
          QJsonObject::fromVariantMap(preview_export_diff));
    }
    if (job.job_kind == QStringLiteral("catalog_import")) {
      const auto result_message = result.value(QStringLiteral("resultMessage")).toString().trimmed();
      if (!result_message.isEmpty()) {
        job.metadata_json.insert(QStringLiteral("resultMessage"), result_message);
      }
      job.status = QStringLiteral("Completed");
      job.error_message.clear();
      job.progress = 100;
    } else if (job.job_kind == QStringLiteral("timeline_render")) {
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
                              : (job.job_kind == QStringLiteral("catalog_import")
                                     ? QStringLiteral("Import failed.")
                                     : QStringLiteral("The export job did not complete."));
    }
    job.progress = 0;
  }

  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);
  WorkerQueueRegistry::logSummary(
      worker_pool_id_for_job_kind(job_kind),
      job_kind,
      worker_surface_for_job_kind(job_kind),
      0,
      0,
      1,
      queue_wait_ms,
      worker_elapsed_ms,
      job.status == QStringLiteral("Completed") ? 1 : 0,
      cancellation_requested ? 1 : 0);
  if (job.status == QStringLiteral("Completed")) {
    observability::logInfo(
        photoEditorJobsLog(),
        QStringLiteral("job.completed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), job_kind},
            {QStringLiteral("asset_id"), job.asset_id},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("backend"), backend},
            {QStringLiteral("fallback_reason"), fallback_reason},
            {QStringLiteral("export_render_job_id"), export_render_job_id},
            {QStringLiteral("export_render_job_kind"),
             export_render_job.value(QStringLiteral("jobKind")).toString()},
            {QStringLiteral("export_render_stage"),
             export_render_job.value(QStringLiteral("stage")).toString()},
            {QStringLiteral("export_render_state_signature"),
             export_render_job.value(QStringLiteral("stateSignature")).toString()},
            {QStringLiteral("mask_source_count"),
             export_render_mask_trace.value(QStringLiteral("mask_source_count")).toInt()},
            {QStringLiteral("mask_source_ids"),
             export_render_mask_trace.value(QStringLiteral("mask_source_ids")).toList()},
            {QStringLiteral("mask_source_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_source_hash")).toString()},
            {QStringLiteral("mask_alpha_trace_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString()},
            {QStringLiteral("mask_alpha_trace_signature"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString()},
            {QStringLiteral("mask_source_revision_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_source_revision_hash")).toString()},
            {QStringLiteral("mask_alpha_raster_width"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt()},
            {QStringLiteral("mask_alpha_raster_height"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt()},
            {QStringLiteral("render_stage_telemetry_contract"),
             QStringLiteral("layer-mask-render-stage-telemetry-v1")},
            {QStringLiteral("layer_recipe_stage"), QStringLiteral("layer_recipe_evaluation")},
            {QStringLiteral("mask_alpha_stage"), QStringLiteral("mask_alpha_retrieval")},
            {QStringLiteral("mask_alpha_retrieval_provider"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_provider")).toString()},
            {QStringLiteral("mask_alpha_retrieval_signature"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString()},
            {QStringLiteral("layer_composite_stage"), QStringLiteral("layer_composite")},
            {QStringLiteral("thumbnail_render_stage"), QStringLiteral("thumbnail_render")},
            {QStringLiteral("final_export_stage"), QStringLiteral("final_export")},
            {QStringLiteral("final_export_ms"), worker_elapsed_ms},
            {QStringLiteral("export_preview_job_id"), export_preview_job_id},
            {QStringLiteral("export_preview_job_kind"),
             export_preview_job.value(QStringLiteral("jobKind")).toString()},
            {QStringLiteral("export_preview_stage"),
             export_preview_job.value(QStringLiteral("stage")).toString()},
            {QStringLiteral("export_preview_state_signature"),
             export_preview_job.value(QStringLiteral("stateSignature")).toString()},
            {QStringLiteral("queue_wait_ms"), queue_wait_ms},
            {QStringLiteral("cancellation_requested"), cancellation_requested},
            {QStringLiteral("cancellation_reason"), cancellation_reason},
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
            {QStringLiteral("backend"), backend},
            {QStringLiteral("fallback_reason"), fallback_reason},
            {QStringLiteral("export_render_job_id"), export_render_job_id},
            {QStringLiteral("export_render_job_kind"),
             export_render_job.value(QStringLiteral("jobKind")).toString()},
            {QStringLiteral("export_render_stage"),
             export_render_job.value(QStringLiteral("stage")).toString()},
            {QStringLiteral("export_render_state_signature"),
             export_render_job.value(QStringLiteral("stateSignature")).toString()},
            {QStringLiteral("mask_source_count"),
             export_render_mask_trace.value(QStringLiteral("mask_source_count")).toInt()},
            {QStringLiteral("mask_source_ids"),
             export_render_mask_trace.value(QStringLiteral("mask_source_ids")).toList()},
            {QStringLiteral("mask_source_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_source_hash")).toString()},
            {QStringLiteral("mask_alpha_trace_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString()},
            {QStringLiteral("mask_alpha_trace_signature"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString()},
            {QStringLiteral("mask_source_revision_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_source_revision_hash")).toString()},
            {QStringLiteral("mask_alpha_raster_width"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt()},
            {QStringLiteral("mask_alpha_raster_height"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt()},
            {QStringLiteral("render_stage_telemetry_contract"),
             QStringLiteral("layer-mask-render-stage-telemetry-v1")},
            {QStringLiteral("layer_recipe_stage"), QStringLiteral("layer_recipe_evaluation")},
            {QStringLiteral("mask_alpha_stage"), QStringLiteral("mask_alpha_retrieval")},
            {QStringLiteral("mask_alpha_retrieval_provider"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_provider")).toString()},
            {QStringLiteral("mask_alpha_retrieval_signature"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString()},
            {QStringLiteral("layer_composite_stage"), QStringLiteral("layer_composite")},
            {QStringLiteral("thumbnail_render_stage"), QStringLiteral("thumbnail_render")},
            {QStringLiteral("final_export_stage"), QStringLiteral("final_export")},
            {QStringLiteral("final_export_ms"), worker_elapsed_ms},
            {QStringLiteral("stale_export_render_result"), stale_export_render_result},
            {QStringLiteral("export_preview_job_id"), export_preview_job_id},
            {QStringLiteral("export_preview_job_kind"),
             export_preview_job.value(QStringLiteral("jobKind")).toString()},
            {QStringLiteral("export_preview_stage"),
             export_preview_job.value(QStringLiteral("stage")).toString()},
            {QStringLiteral("export_preview_state_signature"),
             export_preview_job.value(QStringLiteral("stateSignature")).toString()},
            {QStringLiteral("stale_export_preview_result"), stale_export_preview_result},
            {QStringLiteral("queue_wait_ms"), queue_wait_ms},
            {QStringLiteral("cancellation_requested"), cancellation_requested},
            {QStringLiteral("cancellation_reason"), cancellation_reason},
            {QStringLiteral("worker_elapsed_ms"), worker_elapsed_ms},
            {QStringLiteral("elapsed_ms"), total_elapsed_ms},
            {QStringLiteral("output_path"), job.output_path},
            {QStringLiteral("error"), job.error_message},
        });
  }
  QTimer::singleShot(0, this, &JobRunner::processNextJob);
}

void JobRunner::runImportJob(const QString& job_id) {
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    active_job_id_.clear();
    active_job_kind_.clear();
    active_queue_wait_ms_ = 0;
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    active_job_id_.clear();
    active_job_kind_.clear();
    active_queue_wait_ms_ = 0;
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  const auto source_path = normalized_local_path(job.source_path);
  if (source_path.isEmpty() || !QFileInfo::exists(source_path)) {
    const auto queue_wait_ms = active_queue_wait_ms_;
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The selected import source is unavailable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    active_job_id_.clear();
    active_job_kind_.clear();
    active_queue_wait_ms_ = 0;
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), QStringLiteral("catalog_import")},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("queue_wait_ms"), queue_wait_ms},
            {QStringLiteral("cancellation_requested"), false},
            {QStringLiteral("cancellation_reason"), QString()},
            {QStringLiteral("elapsed_ms"), active_job_timer_.isValid() ? active_job_timer_.elapsed() : 0},
            {QStringLiteral("error"), job.error_message},
        });
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  job.status = QStringLiteral("Running");
  job.progress = 15;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  async_job_watcher_.setFuture(QtConcurrent::run(
      run_import_work,
      source_path,
      job.metadata_json));
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
    active_queue_wait_ms_ = 0;
    QTimer::singleShot(0, this, &JobRunner::processNextJob);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    active_job_id_.clear();
    active_job_kind_.clear();
    active_queue_wait_ms_ = 0;
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
    active_queue_wait_ms_ = 0;
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
    active_queue_wait_ms_ = 0;
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
