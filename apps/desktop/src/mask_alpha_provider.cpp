#include "mask_alpha_provider.h"

#include "mask_presentation_schema.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRgb>

namespace photo_editor::desktop {

namespace {

QString variant_sha1(const QVariant& value) {
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex());
}

QString bool_string(bool value) {
  return value ? QStringLiteral("true") : QStringLiteral("false");
}

bool provider_components_available(int expected_count, const QVariantList& providers) {
  return expected_count > 0 &&
         providers.size() == expected_count &&
         !providers.contains(QStringLiteral("unavailable")) &&
         !providers.contains(QStringLiteral("raster-missing"));
}

double clamp_unit_local(double value) {
  if (!std::isfinite(value)) {
    return 0.0;
  }
  return std::clamp(value, 0.0, 1.0);
}

bool image_has_qrgb_scanlines(const QImage& image) {
  switch (image.format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
      return true;
    default:
      return false;
  }
}

QHash<QString, MaskAlphaEvaluationResult>& alpha_result_cache() {
  static QHash<QString, MaskAlphaEvaluationResult> cache;
  return cache;
}

QMutex& alpha_result_cache_mutex() {
  static QMutex mutex;
  return mutex;
}

qsizetype& alpha_result_cache_bytes() {
  static qsizetype bytes = 0;
  return bytes;
}

constexpr qsizetype kMaxAlphaResultCacheBytes = 32 * 1024 * 1024;

MaskAlphaEvaluationResult cached_alpha_result(const QString& cache_key) {
  if (cache_key.isEmpty()) {
    return {};
  }
  QMutexLocker locker(&alpha_result_cache_mutex());
  auto iterator = alpha_result_cache().constFind(cache_key);
  if (iterator == alpha_result_cache().constEnd()) {
    return {};
  }
  auto result = iterator.value();
  result.cache_hit = true;
  result.cache_key = cache_key;
  return result;
}

void store_alpha_result(const QString& cache_key, const MaskAlphaEvaluationResult& result) {
  if (cache_key.isEmpty() || !result.valid || result.alpha_mask.isEmpty()) {
    return;
  }

  QMutexLocker locker(&alpha_result_cache_mutex());
  auto& cache = alpha_result_cache();
  auto& cache_bytes = alpha_result_cache_bytes();
  const auto new_cost = result.alpha_mask.size();
  if (new_cost > kMaxAlphaResultCacheBytes) {
    return;
  }
  if (cache.contains(cache_key)) {
    cache_bytes -= cache.value(cache_key).alpha_mask.size();
  }
  while (cache_bytes + new_cost > kMaxAlphaResultCacheBytes && !cache.isEmpty()) {
    const auto first_key = cache.constBegin().key();
    cache_bytes -= cache.value(first_key).alpha_mask.size();
    cache.remove(first_key);
  }
  auto stored = result;
  stored.cache_key = cache_key;
  stored.cache_hit = false;
  cache.insert(cache_key, stored);
  cache_bytes += new_cost;
}

void finalize_alpha_result(MaskAlphaEvaluationResult* result) {
  if (result == nullptr || result->alpha_mask.isEmpty() || !result->size.isValid() ||
      result->source_count <= 0) {
    return;
  }
  result->alpha_hash = QString::fromLatin1(
      QCryptographicHash::hash(result->alpha_mask, QCryptographicHash::Sha256).toHex());
  result->valid = true;
}

}  // namespace

QString shared_mask_alpha_provider_contract() {
  return QString::fromLatin1(kSharedMaskAlphaProviderContract);
}

QString mask_alpha_source_revision_hash(const QString& source_revision) {
  if (source_revision.isEmpty()) {
    return {};
  }
  return QString::fromLatin1(
      QCryptographicHash::hash(source_revision.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QVariantMap byte_payload_fingerprint(
    const QVariantMap& source,
    const QString& data_key,
    const QString& width_key,
    const QString& height_key) {
  const auto data = source.value(data_key).toByteArray();
  if (data.isEmpty()) {
    return {};
  }
  QVariantMap payload;
  payload.insert(QStringLiteral("bytes"), data.size());
  payload.insert(width_key, source.value(width_key).toInt());
  payload.insert(height_key, source.value(height_key).toInt());
  payload.insert(
      QStringLiteral("sha1"),
      QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()));
  return payload;
}

qsizetype mask_alpha_cache_bytes() {
  QMutexLocker locker(&alpha_result_cache_mutex());
  return alpha_result_cache_bytes();
}

qsizetype mask_alpha_cache_ceiling_bytes() {
  return kMaxAlphaResultCacheBytes;
}

MaskAlphaEvaluationResult evaluate_mask_alpha(
    const QImage& source_image,
    int source_count,
    const MaskAlphaWeightEvaluator& evaluator) {
  MaskAlphaEvaluationResult result;
  if (source_image.isNull() || source_image.width() <= 0 || source_image.height() <= 0 ||
      source_count <= 0 || !evaluator) {
    return result;
  }

  const auto readable_image = image_has_qrgb_scanlines(source_image)
                                  ? source_image
                                  : source_image.convertToFormat(QImage::Format_ARGB32);
  const auto width = readable_image.width();
  const auto height = readable_image.height();
  result.alpha_mask = QByteArray(width * height, '\0');
  result.size = QSize(width, height);
  result.source_count = source_count;

  for (int y = 0; y < height; ++y) {
    const auto* source_scanline = reinterpret_cast<const QRgb*>(readable_image.constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto rgb = QColor::fromRgb(source_scanline[x]);
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      result.alpha_mask[(y * width) + x] = static_cast<char>(
          std::lround(clamp_unit_local(evaluator(normalized_x, normalized_y, rgb)) * 255.0));
    }
  }

  finalize_alpha_result(&result);
  return result;
}

MaskAlphaEvaluationResult evaluate_mask_alpha_cached(
    const QString& cache_key,
    const QImage& source_image,
    int source_count,
    const MaskAlphaWeightEvaluator& evaluator) {
  if (const auto cached = cached_alpha_result(cache_key); cached.valid) {
    return cached;
  }
  auto result = evaluate_mask_alpha(source_image, source_count, evaluator);
  result.cache_key = cache_key;
  store_alpha_result(cache_key, result);
  return result;
}

MaskAlphaEvaluationResult evaluate_mask_alpha(
    const QSize& target_size,
    int source_count,
    const MaskAlphaSpatialWeightEvaluator& evaluator) {
  MaskAlphaEvaluationResult result;
  if (!target_size.isValid() || target_size.width() <= 0 || target_size.height() <= 0 ||
      source_count <= 0 || !evaluator) {
    return result;
  }

  const auto width = target_size.width();
  const auto height = target_size.height();
  result.alpha_mask = QByteArray(width * height, '\0');
  result.size = target_size;
  result.source_count = source_count;

  for (int y = 0; y < height; ++y) {
    const auto normalized_y =
        height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
    for (int x = 0; x < width; ++x) {
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      result.alpha_mask[(y * width) + x] = static_cast<char>(
          std::lround(clamp_unit_local(evaluator(x, y, normalized_x, normalized_y)) * 255.0));
    }
  }

  finalize_alpha_result(&result);
  return result;
}

MaskAlphaEvaluationResult evaluate_mask_alpha_cached(
    const QString& cache_key,
    const QSize& target_size,
    int source_count,
    const MaskAlphaSpatialWeightEvaluator& evaluator) {
  if (const auto cached = cached_alpha_result(cache_key); cached.valid) {
    return cached;
  }
  auto result = evaluate_mask_alpha(target_size, source_count, evaluator);
  result.cache_key = cache_key;
  store_alpha_result(cache_key, result);
  return result;
}

QVariantMap mask_alpha_thumbnail_metadata(const MaskAlphaEvaluationResult& result) {
  QVariantMap metadata;
  if (!result.valid || result.alpha_mask.isEmpty() || result.alpha_hash.isEmpty() ||
      !result.size.isValid() || result.source_count <= 0) {
    return metadata;
  }

  metadata.insert(QStringLiteral("thumbnailAlphaSource"), QStringLiteral("evaluated-mask-alpha"));
  metadata.insert(QStringLiteral("thumbnailAlphaWidth"), result.size.width());
  metadata.insert(QStringLiteral("thumbnailAlphaHeight"), result.size.height());
  metadata.insert(QStringLiteral("thumbnailAlphaBytes"), result.alpha_mask.size());
  metadata.insert(
      QStringLiteral("thumbnailAlphaHash"),
      QStringLiteral("sha256:%1").arg(result.alpha_hash));
  metadata.insert(
      QStringLiteral("thumbnailFreshnessSignature"),
      QStringLiteral("alpha:sha256:%1|size:%2x%3|sources:%4")
          .arg(result.alpha_hash)
          .arg(result.size.width())
          .arg(result.size.height())
          .arg(result.source_count));
  if (!result.cache_key.isEmpty()) {
    metadata.insert(QStringLiteral("maskAlphaProviderCacheKey"), result.cache_key);
    metadata.insert(QStringLiteral("maskAlphaProviderCacheHit"), result.cache_hit);
  }
  return metadata;
}

QImage grayscale_mask_alpha_image(const MaskAlphaEvaluationResult& result) {
  if (!result.valid || result.alpha_mask.isEmpty() || !result.size.isValid()) {
    return {};
  }

  QImage image(result.size, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  for (int y = 0; y < result.size.height(); ++y) {
    auto* target_scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    const auto row_offset = y * result.size.width();
    for (int x = 0; x < result.size.width(); ++x) {
      const auto alpha = static_cast<unsigned char>(result.alpha_mask.at(row_offset + x));
      target_scanline[x] = qRgba(alpha, alpha, alpha, alpha);
    }
  }
  return image;
}

QVariantMap shared_mask_alpha_provider_fields(
    const QString& provider,
    bool available,
    const QSize& target_size,
    const QString& aggregate_label,
    int aggregate_count) {
  QVariantMap fields;
  fields.insert(
      QStringLiteral("mask_alpha_provider_contract"),
      shared_mask_alpha_provider_contract());
  fields.insert(QStringLiteral("mask_alpha_provider"), provider);
  fields.insert(QStringLiteral("mask_alpha_full_resolution_available"), available);
  fields.insert(QStringLiteral("mask_alpha_deterministic"), available);
  if (target_size.isValid()) {
    fields.insert(QStringLiteral("mask_alpha_target_width"), target_size.width());
    fields.insert(QStringLiteral("mask_alpha_target_height"), target_size.height());
  }

  const auto target_width = target_size.isValid() ? target_size.width() : 0;
  const auto target_height = target_size.isValid() ? target_size.height() : 0;
  const auto signature =
      aggregate_label.isEmpty()
          ? QStringLiteral("contract:%1|provider:%2|full:%3|deterministic:%4|target:%5x%6")
                .arg(shared_mask_alpha_provider_contract(),
                     provider,
                     bool_string(available),
                     bool_string(available))
                .arg(target_width)
                .arg(target_height)
          : QStringLiteral(
                "contract:%1|provider:%2|full:%3|deterministic:%4|%5:%6|target:%7x%8")
                .arg(shared_mask_alpha_provider_contract(),
                     provider,
                     bool_string(available),
                     bool_string(available),
                     aggregate_label)
                .arg(aggregate_count)
                .arg(target_width)
                .arg(target_height);
  fields.insert(QStringLiteral("mask_alpha_provider_signature"), signature);
  return fields;
}

QVariantMap compose_composite_mask_alpha_trace_fields(
    const QString& trace_context,
    const QString& mask_set_id,
    const QVariantList& component_signatures,
    const QVariantList& component_ids,
    const QVariantList& component_hashes,
    const QVariantList& component_providers,
    const QString& source_revision_hash,
    const QSize& raster_size,
    int mask_work_state_revision) {
  const auto component_count = static_cast<int>(component_signatures.size());
  const auto composite_available =
      provider_components_available(component_count, component_providers);

  QVariantMap source_payload;
  source_payload.insert(QStringLiteral("context"), trace_context);
  source_payload.insert(QStringLiteral("maskSetId"), mask_set_id);
  source_payload.insert(QStringLiteral("components"), component_signatures);
  const auto source_hash = variant_sha1(source_payload);

  QVariantMap trace_payload;
  trace_payload.insert(QStringLiteral("context"), trace_context);
  trace_payload.insert(QStringLiteral("sourceHash"), source_hash);
  trace_payload.insert(QStringLiteral("componentHashes"), component_hashes);
  trace_payload.insert(QStringLiteral("componentProviders"), component_providers);
  trace_payload.insert(QStringLiteral("alphaProvider"), QStringLiteral("composite"));
  trace_payload.insert(QStringLiteral("alphaFullResolutionAvailable"), composite_available);
  trace_payload.insert(QStringLiteral("alphaDeterministic"), composite_available);
  trace_payload.insert(QStringLiteral("maskWorkRevision"), mask_work_state_revision);
  if (!source_revision_hash.isEmpty()) {
    trace_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
  }
  if (raster_size.isValid()) {
    trace_payload.insert(QStringLiteral("rasterWidth"), raster_size.width());
    trace_payload.insert(QStringLiteral("rasterHeight"), raster_size.height());
  }
  const auto trace_hash = variant_sha1(trace_payload);

  QVariantMap fields;
  fields.insert(
      QStringLiteral("mask_source_id"),
      mask_set_id.isEmpty() ? QStringLiteral("composite") : mask_set_id);
  fields.insert(QStringLiteral("mask_set_id"), mask_set_id);
  fields.insert(QStringLiteral("mask_source_kind"), QStringLiteral("composite"));
  fields.insert(QStringLiteral("mask_source_count"), component_count);
  fields.insert(QStringLiteral("mask_source_ids"), component_ids);
  fields.insert(QStringLiteral("mask_source_hash"), QStringLiteral("sha1:%1").arg(source_hash));
  fields.insert(QStringLiteral("mask_alpha_trace_hash"), QStringLiteral("sha1:%1").arg(trace_hash));
  const auto provider_fields = shared_mask_alpha_provider_fields(
      QStringLiteral("composite"),
      composite_available,
      raster_size,
      QStringLiteral("components"),
      component_count);
  for (auto iterator = provider_fields.constBegin(); iterator != provider_fields.constEnd(); ++iterator) {
    fields.insert(iterator.key(), iterator.value());
  }
  fields.insert(QStringLiteral("mask_alpha_component_providers"), component_providers);
  fields.insert(
      QStringLiteral("mask_alpha_trace_signature"),
      QStringLiteral(
          "%1-composite:set:%2|components:%3|source:sha1:%4|trace:sha1:%5|raster:%6x%7|revision:%8|provider:composite|full:%9")
          .arg(trace_context)
          .arg(mask_set_id)
          .arg(component_count)
          .arg(source_hash)
          .arg(trace_hash)
          .arg(raster_size.isValid() ? raster_size.width() : 0)
          .arg(raster_size.isValid() ? raster_size.height() : 0)
          .arg(mask_work_state_revision)
          .arg(bool_string(composite_available)));
  if (!source_revision_hash.isEmpty()) {
    fields.insert(
        QStringLiteral("mask_source_revision_hash"),
        QStringLiteral("sha1:%1").arg(source_revision_hash));
  }
  if (raster_size.isValid()) {
    fields.insert(QStringLiteral("mask_alpha_raster_width"), raster_size.width());
    fields.insert(QStringLiteral("mask_alpha_raster_height"), raster_size.height());
  }
  fields.insert(QStringLiteral("mask_work_state_revision"), mask_work_state_revision);
  return fields;
}

QVariantMap compose_layer_composite_mask_alpha_trace_fields(
    const QVariantList& layer_signatures,
    const QVariantList& layer_ids,
    const QVariantList& layer_mask_set_ids,
    const QVariantList& layer_hashes,
    const QVariantList& layer_providers,
    const QSize& raster_size,
    int mask_work_state_revision) {
  const auto masked_layer_count = static_cast<int>(layer_signatures.size());
  if (masked_layer_count <= 0) {
    return {};
  }
  const auto layer_composite_available =
      provider_components_available(masked_layer_count, layer_providers);

  QVariantMap source_payload;
  source_payload.insert(QStringLiteral("context"), QStringLiteral("layer-composite"));
  source_payload.insert(QStringLiteral("layers"), layer_signatures);
  source_payload.insert(QStringLiteral("layerIds"), layer_ids);
  const auto source_hash = variant_sha1(source_payload);

  QVariantMap trace_payload;
  trace_payload.insert(QStringLiteral("sourceHash"), source_hash);
  trace_payload.insert(QStringLiteral("layerHashes"), layer_hashes);
  trace_payload.insert(QStringLiteral("layerProviders"), layer_providers);
  trace_payload.insert(QStringLiteral("alphaProvider"), QStringLiteral("layer-composite"));
  trace_payload.insert(
      QStringLiteral("alphaFullResolutionAvailable"),
      layer_composite_available);
  trace_payload.insert(QStringLiteral("alphaDeterministic"), layer_composite_available);
  trace_payload.insert(QStringLiteral("maskWorkRevision"), mask_work_state_revision);
  if (raster_size.isValid()) {
    trace_payload.insert(QStringLiteral("rasterWidth"), raster_size.width());
    trace_payload.insert(QStringLiteral("rasterHeight"), raster_size.height());
  }
  const auto trace_hash = variant_sha1(trace_payload);

  QVariantMap fields;
  fields.insert(QStringLiteral("mask_source_id"), QStringLiteral("layer-composite"));
  fields.insert(QStringLiteral("mask_set_id"), layer_mask_set_ids.isEmpty()
                                               ? QString()
                                               : layer_mask_set_ids.constFirst().toString());
  fields.insert(QStringLiteral("mask_source_kind"), QStringLiteral("layer-composite"));
  fields.insert(QStringLiteral("mask_source_count"), masked_layer_count);
  fields.insert(QStringLiteral("mask_source_ids"), layer_mask_set_ids);
  fields.insert(QStringLiteral("mask_source_hash"), QStringLiteral("sha1:%1").arg(source_hash));
  fields.insert(QStringLiteral("mask_alpha_trace_hash"), QStringLiteral("sha1:%1").arg(trace_hash));
  const auto provider_fields = shared_mask_alpha_provider_fields(
      QStringLiteral("layer-composite"),
      layer_composite_available,
      raster_size,
      QStringLiteral("layers"),
      masked_layer_count);
  for (auto iterator = provider_fields.constBegin(); iterator != provider_fields.constEnd(); ++iterator) {
    fields.insert(iterator.key(), iterator.value());
  }
  fields.insert(QStringLiteral("mask_alpha_component_providers"), layer_providers);
  fields.insert(
      QStringLiteral("mask_alpha_trace_signature"),
      QStringLiteral(
          "layer-composite:layers:%1|source:sha1:%2|trace:sha1:%3|raster:%4x%5|revision:%6|provider:layer-composite|full:%7")
          .arg(masked_layer_count)
          .arg(source_hash)
          .arg(trace_hash)
          .arg(raster_size.isValid() ? raster_size.width() : 0)
          .arg(raster_size.isValid() ? raster_size.height() : 0)
          .arg(mask_work_state_revision)
          .arg(bool_string(layer_composite_available)));
  if (raster_size.isValid()) {
    fields.insert(QStringLiteral("mask_alpha_raster_width"), raster_size.width());
    fields.insert(QStringLiteral("mask_alpha_raster_height"), raster_size.height());
  }
  fields.insert(QStringLiteral("mask_work_state_revision"), mask_work_state_revision);
  fields.insert(QStringLiteral("layer_composite_masked_layer_count"), masked_layer_count);
  fields.insert(QStringLiteral("layer_composite_layer_ids"), layer_ids);
  fields.insert(QStringLiteral("layer_composite_mask_set_ids"), layer_mask_set_ids);
  return fields;
}

QVariantMap compose_outline_composite_mask_alpha_trace_fields(
    const QString& active_source_id,
    const QString& active_set_id,
    const QVariantList& component_signatures,
    const QVariantList& component_ids,
    const QVariantList& component_hashes,
    const QVariantList& component_providers,
    const QSize& raster_size,
    int mask_work_state_revision) {
  const auto component_count = static_cast<int>(component_signatures.size());
  const auto composite_available =
      provider_components_available(component_count, component_providers);

  QVariantMap source_payload;
  source_payload.insert(QStringLiteral("activeSourceId"), active_source_id);
  source_payload.insert(QStringLiteral("maskSetId"), active_set_id);
  source_payload.insert(QStringLiteral("components"), component_signatures);
  const auto source_hash = variant_sha1(source_payload);

  QVariantMap trace_payload;
  trace_payload.insert(QStringLiteral("sourceHash"), source_hash);
  trace_payload.insert(QStringLiteral("componentHashes"), component_hashes);
  trace_payload.insert(QStringLiteral("componentProviders"), component_providers);
  trace_payload.insert(QStringLiteral("alphaProvider"), QStringLiteral("composite"));
  trace_payload.insert(QStringLiteral("alphaFullResolutionAvailable"), composite_available);
  trace_payload.insert(QStringLiteral("alphaDeterministic"), composite_available);
  trace_payload.insert(QStringLiteral("maskWorkRevision"), mask_work_state_revision);
  if (raster_size.isValid()) {
    trace_payload.insert(QStringLiteral("rasterWidth"), raster_size.width());
    trace_payload.insert(QStringLiteral("rasterHeight"), raster_size.height());
  }
  const auto trace_hash = variant_sha1(trace_payload);

  QVariantMap fields;
  fields.insert(QStringLiteral("mask_source_id"), active_source_id);
  fields.insert(QStringLiteral("mask_set_id"), active_set_id);
  fields.insert(QStringLiteral("mask_source_kind"), QStringLiteral("composite"));
  fields.insert(QStringLiteral("mask_source_count"), component_count);
  fields.insert(QStringLiteral("mask_source_ids"), component_ids);
  fields.insert(QStringLiteral("mask_source_hash"), QStringLiteral("sha1:%1").arg(source_hash));
  fields.insert(QStringLiteral("mask_alpha_trace_hash"), QStringLiteral("sha1:%1").arg(trace_hash));
  const auto provider_fields = shared_mask_alpha_provider_fields(
      QStringLiteral("composite"),
      composite_available,
      raster_size,
      QStringLiteral("components"),
      component_count);
  for (auto iterator = provider_fields.constBegin(); iterator != provider_fields.constEnd(); ++iterator) {
    fields.insert(iterator.key(), iterator.value());
  }
  fields.insert(QStringLiteral("mask_alpha_component_providers"), component_providers);
  fields.insert(
      QStringLiteral("mask_alpha_trace_signature"),
      QStringLiteral(
          "outline-composite:set:%1|active:%2|components:%3|source:sha1:%4|trace:sha1:%5|raster:%6x%7|revision:%8|provider:composite|full:%9")
          .arg(active_set_id)
          .arg(active_source_id)
          .arg(component_count)
          .arg(source_hash)
          .arg(trace_hash)
          .arg(raster_size.isValid() ? raster_size.width() : 0)
          .arg(raster_size.isValid() ? raster_size.height() : 0)
          .arg(mask_work_state_revision)
          .arg(bool_string(composite_available)));
  if (raster_size.isValid()) {
    fields.insert(QStringLiteral("mask_alpha_raster_width"), raster_size.width());
    fields.insert(QStringLiteral("mask_alpha_raster_height"), raster_size.height());
  }
  return fields;
}

namespace {

QVariantMap mask_source_payload_for_trace(QVariantMap mask) {
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
           QStringLiteral("brushAlphaSourceContract"),
           QStringLiteral("brushAlphaSourceOfTruth"),
           QStringLiteral("brushAlphaStrokeCount"),
           QStringLiteral("brushAlphaPointCount"),
           QStringLiteral("brushAlphaRasterWidth"),
           QStringLiteral("brushAlphaRasterHeight"),
           QStringLiteral("brushAlphaTileCacheKind"),
            QStringLiteral("brushAlphaTileCacheEnabled"),
            QStringLiteral("brushAlphaTileCacheTileSize"),
            QStringLiteral("brushAlphaTileCacheDirtyTileCount"),
            QStringLiteral("brushAlphaSourceSignature"),
            QStringLiteral("shapeAlphaSourceContract"),
            QStringLiteral("shapeAlphaSourceOfTruth"),
            QStringLiteral("shapeAlphaPointCount"),
            QStringLiteral("shapeAlphaRawPointCount"),
            QStringLiteral("shapeAlphaClosed"),
            QStringLiteral("shapeAlphaEditable"),
            QStringLiteral("shapeAlphaTileCacheKind"),
            QStringLiteral("shapeAlphaTileCacheEnabled"),
            QStringLiteral("shapeAlphaTileCacheTileSize"),
            QStringLiteral("shapeAlphaSourceSignature"),
            QStringLiteral("generatedAlphaSourceContract"),
            QStringLiteral("generatedAlphaModelName"),
            QStringLiteral("generatedAlphaModelVersion"),
            QStringLiteral("generatedAlphaTargetType"),
            QStringLiteral("generatedAlphaPromptType"),
            QStringLiteral("generatedAlphaSourceImageVersion"),
            QStringLiteral("generatedAlphaWidth"),
            QStringLiteral("generatedAlphaHeight"),
            QStringLiteral("generatedAlphaCoordinateSpaceContract"),
            QStringLiteral("generatedAlphaSourceCoordinateSpace"),
            QStringLiteral("generatedAlphaPromptCoordinateSpace"),
            QStringLiteral("generatedAlphaRasterCoordinateSpace"),
            QStringLiteral("generatedAlphaDownscaleFactor"),
            QStringLiteral("generatedAlphaConfidenceAvailable"),
            QStringLiteral("generatedAlphaConfidenceWidth"),
            QStringLiteral("generatedAlphaConfidenceHeight"),
            QStringLiteral("generatedAlphaRefinementStateSignature"),
            QStringLiteral("generatedAlphaSourceSignature"),
            QStringLiteral("mask_source_revision_hash"),
            QStringLiteral("mask_alpha_raster_width"),
            QStringLiteral("mask_alpha_raster_height"),
           QStringLiteral("mask_source_point_count"),
           QStringLiteral("mask_source_stroke_count"),
           QStringLiteral("mask_work_state_revision"),
       }) {
    mask.remove(key);
  }
  return mask;
}


}  // namespace

QVariantMap mask_alpha_trace_fields(
    const QVariantMap& mask,
    const QString& source_revision,
    const QSize& raster_size) {
  const auto normalized = normalized_mask_definition(mask_source_payload_for_trace(mask), 1);
  const auto source_id = normalized.value(QStringLiteral("id")).toString().simplified();
  if (source_id.isEmpty()) {
    return {};
  }

  auto source_payload = normalized;
  const auto segmentation_fingerprint = byte_payload_fingerprint(
      normalized,
      QStringLiteral("segmentationData"),
      QStringLiteral("segmentationWidth"),
      QStringLiteral("segmentationHeight"));
  if (!segmentation_fingerprint.isEmpty()) {
    source_payload.insert(QStringLiteral("segmentationData"), segmentation_fingerprint);
  }
  const auto depth_fingerprint = byte_payload_fingerprint(
      normalized,
      QStringLiteral("depthData"),
      QStringLiteral("depthWidth"),
      QStringLiteral("depthHeight"));
  if (!depth_fingerprint.isEmpty()) {
    source_payload.insert(QStringLiteral("depthData"), depth_fingerprint);
  }
  const auto cleanup_source_fingerprint = byte_payload_fingerprint(
      normalized,
      QStringLiteral("generatedMaskCleanupSourceData"),
      QStringLiteral("generatedMaskCleanupSourceWidth"),
      QStringLiteral("generatedMaskCleanupSourceHeight"));
  if (!cleanup_source_fingerprint.isEmpty()) {
    source_payload.insert(
        QStringLiteral("generatedMaskCleanupSourceData"),
        cleanup_source_fingerprint);
  }

  const auto source_hash = variant_sha1(source_payload);
  const auto source_revision_hash = mask_alpha_source_revision_hash(source_revision);

  QVariantMap trace_payload;
  trace_payload.insert(QStringLiteral("maskSourceHash"), source_hash);
  if (!source_revision_hash.isEmpty()) {
    trace_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
  }
  const auto trace_size = raster_size.isValid() ? raster_size : raster_size_for_mask_trace(normalized);
  if (trace_size.isValid()) {
    trace_payload.insert(QStringLiteral("rasterWidth"), trace_size.width());
    trace_payload.insert(QStringLiteral("rasterHeight"), trace_size.height());
  }
  const auto availability_fields = mask_alpha_availability_fields(normalized, trace_size);
  trace_payload.insert(
      QStringLiteral("alphaProvider"),
      availability_fields.value(QStringLiteral("mask_alpha_provider")).toString());
  trace_payload.insert(
      QStringLiteral("alphaFullResolutionAvailable"),
      availability_fields.value(QStringLiteral("mask_alpha_full_resolution_available")).toBool());
  trace_payload.insert(
      QStringLiteral("alphaDeterministic"),
      availability_fields.value(QStringLiteral("mask_alpha_deterministic")).toBool());

  const auto trace_hash = variant_sha1(trace_payload);
  QVariantMap fields;
  fields.insert(QStringLiteral("mask_source_id"), source_id);
  fields.insert(QStringLiteral("mask_set_id"), normalized.value(QStringLiteral("maskSetId")).toString());
  fields.insert(
      QStringLiteral("mask_set_name"),
      normalized.value(QStringLiteral("maskSetName")).toString());
  fields.insert(QStringLiteral("mask_source_kind"), normalized.value(QStringLiteral("kind")).toString());
  fields.insert(
      QStringLiteral("mask_source_operation"),
      normalized.value(QStringLiteral("operation")).toString());
  fields.insert(
      QStringLiteral("mask_source_schema_version"),
      normalized.value(QStringLiteral("sourceSchemaVersion")).toInt());
  fields.insert(QStringLiteral("mask_source_hash"), QStringLiteral("sha1:%1").arg(source_hash));
  fields.insert(QStringLiteral("mask_alpha_trace_hash"), QStringLiteral("sha1:%1").arg(trace_hash));
  for (auto it = availability_fields.cbegin(); it != availability_fields.cend(); ++it) {
    fields.insert(it.key(), it.value());
  }
  fields.insert(
      QStringLiteral("mask_alpha_trace_signature"),
      QStringLiteral("mask:%1|set:%2|source:sha1:%3|revision:%4|raster:%5x%6|provider:%7|full:%8")
          .arg(source_id)
          .arg(normalized.value(QStringLiteral("maskSetId")).toString())
          .arg(source_hash)
          .arg(source_revision_hash.isEmpty()
                   ? QStringLiteral("none")
                   : QStringLiteral("sha1:%1").arg(source_revision_hash))
          .arg(trace_size.isValid() ? trace_size.width() : 0)
          .arg(trace_size.isValid() ? trace_size.height() : 0)
          .arg(availability_fields.value(QStringLiteral("mask_alpha_provider")).toString(),
               availability_fields.value(QStringLiteral("mask_alpha_full_resolution_available"))
                       .toBool()
                   ? QStringLiteral("true")
                   : QStringLiteral("false")));
  if (!source_revision_hash.isEmpty()) {
    fields.insert(
        QStringLiteral("mask_source_revision_hash"),
        QStringLiteral("sha1:%1").arg(source_revision_hash));
  }
  if (trace_size.isValid()) {
    fields.insert(QStringLiteral("mask_alpha_raster_width"), trace_size.width());
    fields.insert(QStringLiteral("mask_alpha_raster_height"), trace_size.height());
  }
  fields.insert(
      QStringLiteral("mask_source_point_count"),
      normalized.value(QStringLiteral("brushPoints")).toList().size());
  fields.insert(
      QStringLiteral("mask_source_stroke_count"),
      normalized.value(QStringLiteral("brushStrokes")).toList().size());
  return fields;
}

void insert_mask_alpha_trace_fields(
    QVariantMap* target,
    const QVariantMap& mask,
    const QString& source_revision,
    const QSize& raster_size) {
  if (target == nullptr) {
    return;
  }
  const auto fields = mask_alpha_trace_fields(mask, source_revision, raster_size);
  for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
    target->insert(it.key(), it.value());
  }
}

QVariantMap composite_mask_alpha_trace_fields(
    const QString& trace_context,
    const QVariantList& traced_masks,
    const QString& mask_set_id,
    const QString& source_revision,
    const QSize& raster_size,
    int mask_work_state_revision) {
  if (traced_masks.isEmpty()) {
    return {};
  }

  const auto source_revision_hash = mask_alpha_source_revision_hash(source_revision);

  QVariantList component_signatures;
  QVariantList component_ids;
  QVariantList component_hashes;
  QVariantList component_providers;
  auto ordinal = 1;
  for (const auto& traced_value : traced_masks) {
    const auto traced_mask = normalized_mask_definition(traced_value.toMap(), ordinal);
    ++ordinal;
    const auto fields = mask_alpha_trace_fields(traced_mask, source_revision, raster_size);
    const auto component_id = fields.value(QStringLiteral("mask_source_id")).toString();
    if (!component_id.isEmpty()) {
      component_ids.append(component_id);
    }
    const auto component_signature =
        fields.value(QStringLiteral("mask_alpha_trace_signature")).toString();
    if (!component_signature.isEmpty()) {
      component_signatures.append(component_signature);
    }
    const auto component_hash = fields.value(QStringLiteral("mask_alpha_trace_hash")).toString();
    if (!component_hash.isEmpty()) {
      component_hashes.append(component_hash);
    }
    const auto component_provider = fields.value(QStringLiteral("mask_alpha_provider")).toString();
    if (!component_provider.isEmpty()) {
      component_providers.append(component_provider);
    }
  }
  return compose_composite_mask_alpha_trace_fields(
      trace_context,
      mask_set_id,
      component_signatures,
      component_ids,
      component_hashes,
      component_providers,
      source_revision_hash,
      raster_size,
      mask_work_state_revision);
}


}  // namespace photo_editor::desktop
