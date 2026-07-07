#include "thumbnail_refresh_queue_service.h"

#include <algorithm>
#include <cmath>

#include <QBuffer>
#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFont>
#include <QGuiApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QIODevice>
#include <QJsonDocument>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QThread>

namespace photo_editor::desktop {

namespace {

constexpr qint64 kMaxLayerThumbnailSourceBytes = 512 * 1024;

QString thumbnail_variant_sha1(const QVariant& value) {
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex());
}

QStringList normalized_string_list_value(
    const QVariantMap& map,
    const QString& list_key,
    const QString& single_key) {
  QStringList values;
  const auto append_value = [&](const QString& raw_value) {
    const auto value = raw_value.simplified();
    if (!value.isEmpty() && !values.contains(value)) {
      values.push_back(value);
    }
  };

  const auto list_value = map.value(list_key);
  if (list_value.canConvert<QStringList>()) {
    for (const auto& value : list_value.toStringList()) {
      append_value(value);
    }
  } else {
    for (const auto& value : list_value.toList()) {
      append_value(value.toString());
    }
  }
  append_value(map.value(single_key).toString());
  return values;
}

bool gui_application_available() {
  return qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr;
}

void paint_thumbnail_checker(QPainter* painter, const QRect& rect) {
  if (painter == nullptr) {
    return;
  }
  constexpr int tile = 8;
  for (int y = rect.top(); y <= rect.bottom(); y += tile) {
    for (int x = rect.left(); x <= rect.right(); x += tile) {
      const auto alternating = ((x / tile) + (y / tile)) % 2 == 0;
      painter->fillRect(
          QRect(x, y, tile, tile),
          alternating ? QColor(24, 30, 35) : QColor(42, 50, 58));
    }
  }
}

QImage decoded_layer_pixel_image(const QVariantMap& layer) {
  const auto pixel_data = layer.value(QStringLiteral("pixelData")).toByteArray();
  if (!pixel_data.isEmpty()) {
    return QImage::fromData(pixel_data, "PNG");
  }

  const auto source_path = layer.value(QStringLiteral("sourcePath")).toString();
  if (source_path.isEmpty()) {
    return {};
  }
  QImageReader reader(source_path);
  const auto size = reader.size();
  if (size.isValid()) {
    reader.setScaledSize(size.scaled(kLayerThumbnailEdge, kLayerThumbnailEdge, Qt::KeepAspectRatio));
  }
  return reader.read();
}

}  // namespace

void insert_thumbnail_pixel_metrics(
    QVariantMap* target,
    const QSize& backing_size,
    const QSize& display_size) {
  if (target == nullptr) {
    return;
  }

  const auto backing_width = std::max(0, backing_size.width());
  const auto backing_height = std::max(0, backing_size.height());
  const auto display_width = std::max(1, display_size.width());
  const auto display_height = std::max(1, display_size.height());
  const auto horizontal_ratio =
      backing_width > 0 ? static_cast<double>(backing_width) / display_width : 0.0;
  const auto vertical_ratio =
      backing_height > 0 ? static_cast<double>(backing_height) / display_height : 0.0;
  const auto device_pixel_ratio =
      horizontal_ratio > 0.0 && vertical_ratio > 0.0
          ? std::min(horizontal_ratio, vertical_ratio)
          : 0.0;
  target->insert(QStringLiteral("thumbnailBackingWidth"), backing_width);
  target->insert(QStringLiteral("thumbnailBackingHeight"), backing_height);
  target->insert(QStringLiteral("thumbnailDisplayWidth"), display_width);
  target->insert(QStringLiteral("thumbnailDisplayHeight"), display_height);
  target->insert(QStringLiteral("thumbnailDevicePixelRatio"), device_pixel_ratio);
  target->insert(
      QStringLiteral("thumbnailPixelRatioSignature"),
      QStringLiteral("backing:%1x%2|display:%3x%4|dpr:%5")
          .arg(backing_width)
          .arg(backing_height)
          .arg(display_width)
          .arg(display_height)
          .arg(device_pixel_ratio, 0, 'f', 2));
}

QString thumbnail_invalidation_key(
    const QString& asset_id,
    const QString& layer_id,
    const QString& mask_id,
    const QString& recipe_hash,
    const QString& mask_alpha_hash,
    const QString& blend_mode,
    double opacity,
    bool visible) {
  const auto normalized_opacity =
      QString::number(std::clamp(opacity, 0.0, 1.0), 'f', 3);
  const auto normalized_blend = normalized_mask_blend_mode(blend_mode);
  const auto normalized_recipe_hash =
      recipe_hash.trimmed().isEmpty() ? QStringLiteral("none") : recipe_hash.trimmed();
  const auto normalized_mask_alpha_hash =
      mask_alpha_hash.trimmed().isEmpty() ? QStringLiteral("none") : mask_alpha_hash.trimmed();

  QVariantMap payload;
  payload.insert(QStringLiteral("version"), QStringLiteral("thumbnail-invalidation-v1"));
  payload.insert(QStringLiteral("assetId"), asset_id);
  payload.insert(QStringLiteral("layerId"), layer_id);
  payload.insert(QStringLiteral("maskId"), mask_id);
  payload.insert(QStringLiteral("recipeHash"), normalized_recipe_hash);
  payload.insert(QStringLiteral("maskAlphaHash"), normalized_mask_alpha_hash);
  payload.insert(QStringLiteral("blendMode"), normalized_blend);
  payload.insert(QStringLiteral("opacity"), normalized_opacity);
  payload.insert(QStringLiteral("visible"), visible);
  payload.insert(
      QStringLiteral("colorPipelineVersion"),
      QString::fromLatin1(kThumbnailColorPipelineVersion));
  return QStringLiteral("sha1:%1").arg(thumbnail_variant_sha1(payload));
}

void insert_thumbnail_invalidation_fields(
    QVariantMap* target,
    const QString& asset_id,
    const QString& layer_id,
    const QString& mask_id,
    const QString& recipe_hash,
    const QString& mask_alpha_hash,
    const QString& blend_mode,
    double opacity,
    bool visible) {
  if (target == nullptr) {
    return;
  }

  const auto normalized_opacity =
      QString::number(std::clamp(opacity, 0.0, 1.0), 'f', 3);
  const auto normalized_blend = normalized_mask_blend_mode(blend_mode);
  const auto normalized_recipe_hash =
      recipe_hash.trimmed().isEmpty() ? QStringLiteral("none") : recipe_hash.trimmed();
  const auto normalized_mask_alpha_hash =
      mask_alpha_hash.trimmed().isEmpty() ? QStringLiteral("none") : mask_alpha_hash.trimmed();
  const auto color_pipeline_version = QString::fromLatin1(kThumbnailColorPipelineVersion);
  const auto invalidation_hash = thumbnail_invalidation_key(
      asset_id,
      layer_id,
      mask_id,
      normalized_recipe_hash,
      normalized_mask_alpha_hash,
      normalized_blend,
      opacity,
      visible);

  target->insert(QStringLiteral("thumbnailInvalidationAssetId"), asset_id);
  target->insert(QStringLiteral("thumbnailInvalidationLayerId"), layer_id);
  target->insert(QStringLiteral("thumbnailInvalidationMaskId"), mask_id);
  target->insert(QStringLiteral("thumbnailInvalidationRecipeHash"), normalized_recipe_hash);
  target->insert(QStringLiteral("thumbnailInvalidationMaskAlphaHash"), normalized_mask_alpha_hash);
  target->insert(QStringLiteral("thumbnailInvalidationBlendMode"), normalized_blend);
  target->insert(QStringLiteral("thumbnailInvalidationOpacity"), normalized_opacity);
  target->insert(QStringLiteral("thumbnailInvalidationVisible"), visible);
  target->insert(
      QStringLiteral("thumbnailInvalidationColorPipelineVersion"),
      color_pipeline_version);
  target->insert(QStringLiteral("thumbnailInvalidationKey"), invalidation_hash);
  target->insert(
      QStringLiteral("thumbnailInvalidationComparator"),
      QStringLiteral("thumbnailInvalidationKey"));
  target->insert(
      QStringLiteral("thumbnailInvalidationComparatorContract"),
      QStringLiteral("thumbnail-invalidation-key-comparator-v1"));
  target->insert(
      QStringLiteral("thumbnailInvalidationSignature"),
      QStringLiteral(
          "asset:%1|layer:%2|mask:%3|recipe:%4|mask-alpha:%5|blend:%6|opacity:%7|visible:%8|color:%9|key:%10")
          .arg(asset_id,
               layer_id,
               mask_id,
               normalized_recipe_hash,
               normalized_mask_alpha_hash,
               normalized_blend,
               normalized_opacity,
               visible ? QStringLiteral("true") : QStringLiteral("false"),
               color_pipeline_version,
               invalidation_hash));
}

QString thumbnail_entry_cache_key(
    const QString& asset_id,
    const QString& recipe_hash,
    const QString& layer_id,
    const QString& mask_id,
    int backing_width,
    int backing_height,
    double device_pixel_ratio,
    const QString& color_pipeline_version,
    int mask_revision) {
  QVariantMap payload;
  payload.insert(QStringLiteral("version"), QStringLiteral("thumbnail-entry-cache-v1"));
  payload.insert(QStringLiteral("assetId"), asset_id);
  payload.insert(QStringLiteral("recipeHash"), recipe_hash.trimmed().isEmpty()
                                            ? QStringLiteral("none")
                                            : recipe_hash.trimmed());
  payload.insert(QStringLiteral("layerId"), layer_id);
  payload.insert(QStringLiteral("maskId"), mask_id);
  payload.insert(QStringLiteral("backingWidth"), backing_width);
  payload.insert(QStringLiteral("backingHeight"), backing_height);
  payload.insert(
      QStringLiteral("devicePixelRatio"),
      QString::number(device_pixel_ratio, 'f', 2));
  payload.insert(QStringLiteral("colorPipelineVersion"), color_pipeline_version);
  payload.insert(QStringLiteral("maskRevision"), mask_revision);
  return QStringLiteral("sha1:%1").arg(thumbnail_variant_sha1(payload));
}

void insert_thumbnail_entry_cache_fields(
    QVariantMap* target,
    const QString& cache_key,
    const QString& asset_id,
    const QString& recipe_hash,
    const QString& layer_id,
    const QString& mask_id,
    int backing_width,
    int backing_height,
    double device_pixel_ratio,
    const QString& color_pipeline_version,
    int mask_revision,
    bool hit,
    const QString& source) {
  if (target == nullptr) {
    return;
  }
  const auto normalized_recipe =
      recipe_hash.trimmed().isEmpty() ? QStringLiteral("none") : recipe_hash.trimmed();
  const auto scale = QStringLiteral("%1x%2@%3")
                         .arg(backing_width)
                         .arg(backing_height)
                         .arg(device_pixel_ratio, 0, 'f', 2);
  target->insert(QStringLiteral("thumbnailCacheContract"), QStringLiteral("thumbnail-entry-cache-v1"));
  target->insert(QStringLiteral("thumbnailCacheKey"), cache_key);
  target->insert(QStringLiteral("thumbnailCacheAssetId"), asset_id);
  target->insert(QStringLiteral("thumbnailCacheRecipeHash"), normalized_recipe);
  target->insert(QStringLiteral("thumbnailCacheLayerId"), layer_id);
  target->insert(QStringLiteral("thumbnailCacheMaskId"), mask_id);
  target->insert(QStringLiteral("thumbnailCacheBackingWidth"), backing_width);
  target->insert(QStringLiteral("thumbnailCacheBackingHeight"), backing_height);
  target->insert(QStringLiteral("thumbnailCacheDevicePixelRatio"), device_pixel_ratio);
  target->insert(QStringLiteral("thumbnailCacheOutputScale"), scale);
  target->insert(QStringLiteral("thumbnailCacheColorPipelineVersion"), color_pipeline_version);
  target->insert(QStringLiteral("thumbnailCacheMaskRevision"), mask_revision);
  target->insert(QStringLiteral("thumbnailCacheHit"), hit);
  target->insert(QStringLiteral("thumbnailCacheSource"), source);
  target->insert(
      QStringLiteral("thumbnailCacheSignature"),
      QStringLiteral("asset:%1|recipe:%2|layer:%3|mask:%4|scale:%5|color:%6|revision:%7|hit:%8|source:%9|key:%10")
          .arg(asset_id,
               normalized_recipe,
               layer_id,
               mask_id,
               scale,
               color_pipeline_version,
               QString::number(mask_revision),
               hit ? QStringLiteral("true") : QStringLiteral("false"),
               source,
               cache_key));
}

void clear_thumbnail_transient_fields(QVariantMap* target) {
  if (target == nullptr) {
    return;
  }
  target->insert(QStringLiteral("thumbnailStale"), false);
  target->insert(QStringLiteral("thumbnailProgressive"), false);
  target->insert(QStringLiteral("thumbnailProgressiveReason"), QString());
  target->insert(QStringLiteral("thumbnailProgressivePreviousInvalidationKey"), QString());
  target->insert(QStringLiteral("thumbnailProgressiveSignature"), QStringLiteral("progressive:false"));
  target->insert(QStringLiteral("thumbnailStaleDetected"), false);
  target->insert(QStringLiteral("thumbnailStaleMismatch"), false);
  target->insert(QStringLiteral("thumbnailStaleDetectionReason"), QString());
  target->insert(QStringLiteral("thumbnailStalePreviousRecipeHash"), QString());
  target->insert(QStringLiteral("thumbnailStaleCurrentRecipeHash"), QString());
  target->insert(QStringLiteral("thumbnailStalePreviousInvalidationKey"), QString());
  target->insert(QStringLiteral("thumbnailStaleDetectionSignature"), QString());
  target->insert(QStringLiteral("thumbnailFailureContained"), false);
  target->insert(QStringLiteral("thumbnailFailureStage"), QString());
  target->insert(QStringLiteral("thumbnailFailureDetail"), QString());
  target->insert(QStringLiteral("thumbnailFailureSignature"), QString());
  target->insert(QStringLiteral("thumbnailQueuePending"), false);
  target->insert(QStringLiteral("thumbnailQueueJobId"), QString());
  target->insert(QStringLiteral("thumbnailQueueJobKind"), QString());
  target->insert(QStringLiteral("thumbnailQueuePriority"), QString());
  target->insert(QStringLiteral("thumbnailQueuePriorityRank"), 0);
  target->insert(QStringLiteral("thumbnailQueueReason"), QString());
  target->insert(QStringLiteral("thumbnailQueueGeneration"), 0);
  target->insert(QStringLiteral("thumbnailQueueStateSignature"), QString());
  target->insert(QStringLiteral("thumbnailQueueDeferBlockers"), QStringList{});
  target->insert(QStringLiteral("thumbnailQueueWorkerRunning"), false);
  target->insert(QStringLiteral("thumbnailQueueActiveJobId"), QString());
  target->insert(QStringLiteral("thumbnailQueueWorkerStartedCount"), 0);
  target->insert(QStringLiteral("thumbnailQueueWorkerCompletedCount"), 0);
  target->insert(QStringLiteral("thumbnailQueueStaleResultCount"), 0);
  target->insert(QStringLiteral("thumbnailQueueDeferBlockerSignature"), QString());
  target->insert(QStringLiteral("thumbnailQueueContract"), QString());
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerJobId"), QString());
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerStateSignature"), QString());
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerQueueSignature"), QString());
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerReason"), QString());
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerElapsedMs"), 0);
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerEntryCount"), 0);
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerMaskEntryCount"), 0);
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerMaskDirty"), false);
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerLayerDirty"), false);
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerContract"), QString());
  target->insert(QStringLiteral("thumbnailRenderQueuedWorkerServiceContract"), QString());
  target->insert(QStringLiteral("thumbnailQueueWorkerRendered"), false);
  target->insert(QStringLiteral("thumbnailQueueWorkerRenderJobId"), QString());
  target->insert(QStringLiteral("thumbnailQueueWorkerRenderSource"), QString());
  target->insert(QStringLiteral("thumbnailQueueWorkerRenderEntryCount"), 0);
  target->insert(QStringLiteral("thumbnailQueueWorkerRenderMaskEntryCount"), 0);
  target->insert(QStringLiteral("thumbnailQueueWorkerRenderElapsedMs"), 0);
  target->insert(QStringLiteral("thumbnailQueueWorkerRenderContract"), QString());
  target->insert(QStringLiteral("thumbnailQueueWorkerServiceContract"), QString());
}

QString encoded_layer_thumbnail_source(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  auto encoded_image = image;
  if (!encoded_image.colorSpace().isValid()) {
    encoded_image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  }

  QByteArray encoded;
  QBuffer buffer(&encoded);
  if (!buffer.open(QIODevice::WriteOnly)) {
    return {};
  }

  QImageWriter writer(&buffer, "PNG");
  writer.setCompression(1);
  if (!writer.write(encoded_image) || encoded.size() > kMaxLayerThumbnailSourceBytes) {
    return {};
  }

  return QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(encoded.toBase64()));
}

QImage scaled_square_thumbnail_image(QImage image, int edge) {
  if (image.isNull()) {
    return {};
  }
  if (image.format() != QImage::Format_ARGB32_Premultiplied) {
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }
  const auto scaled = image.scaled(
      edge,
      edge,
      Qt::KeepAspectRatioByExpanding,
      Qt::SmoothTransformation);
  return scaled.copy(
      (scaled.width() - edge) / 2,
      (scaled.height() - edge) / 2,
      edge,
      edge);
}

QImage scaled_layer_thumbnail_image(QImage image) {
  return scaled_square_thumbnail_image(image, kLayerThumbnailEdge);
}

QString rendered_photo_layer_thumbnail_source(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  QImage thumbnail(kLayerThumbnailEdge, kLayerThumbnailEdge, QImage::Format_ARGB32_Premultiplied);
  thumbnail.fill(Qt::transparent);
  QPainter painter(&thumbnail);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  if (kind == QStringLiteral("pixel") || kind == QStringLiteral("source")) {
    const auto source_thumbnail = scaled_layer_thumbnail_image(decoded_layer_pixel_image(layer));
    if (!source_thumbnail.isNull()) {
      return encoded_layer_thumbnail_source(source_thumbnail);
    }
  }

  if (kind == QStringLiteral("fill")) {
    auto fill = QColor(layer.value(QStringLiteral("fillColor"), QStringLiteral("#808080")).toString());
    if (!fill.isValid()) {
      fill = QColor(QStringLiteral("#808080"));
    }
    fill.setAlphaF(static_cast<float>(
        std::clamp(layer.value(QStringLiteral("fillAlpha"), 1.0).toDouble(), 0.0, 1.0)));
    paint_thumbnail_checker(&painter, thumbnail.rect());
    painter.fillRect(thumbnail.rect(), fill);
  } else if (kind == QStringLiteral("text")) {
    paint_thumbnail_checker(&painter, thumbnail.rect());
    painter.fillRect(thumbnail.rect(), QColor(12, 18, 22, 168));
    auto text_color = QColor(layer.value(QStringLiteral("textColor"), QStringLiteral("#ffffff")).toString());
    if (!text_color.isValid()) {
      text_color = QColor(QStringLiteral("#ffffff"));
    }
    painter.setPen(text_color);
    if (gui_application_available()) {
      QFont font(layer.value(QStringLiteral("fontFamily"), QStringLiteral("Helvetica")).toString());
      font.setPixelSize(20);
      font.setBold(layer.value(QStringLiteral("fontWeight")).toString().trimmed().toLower() ==
                   QStringLiteral("bold"));
      painter.setFont(font);
      painter.drawText(
          thumbnail.rect().adjusted(6, 4, -6, -4),
          Qt::AlignCenter | Qt::TextWordWrap,
          layer.value(QStringLiteral("text"), QStringLiteral("Text")).toString().left(12));
    } else {
      painter.drawRect(thumbnail.rect().adjusted(18, 18, -18, -18));
    }
  } else if (kind == QStringLiteral("adjustment")) {
    const auto exposure = std::clamp(layer.value(QStringLiteral("exposure")).toDouble(), -2.0, 2.0);
    const auto temperature =
        std::clamp(layer.value(QStringLiteral("temperature")).toDouble(), -100.0, 100.0);
    QLinearGradient gradient(0, 0, kLayerThumbnailEdge, kLayerThumbnailEdge);
    gradient.setColorAt(0.0, QColor(20, 31, 37));
    gradient.setColorAt(
        1.0,
        temperature >= 0.0 ? QColor(74, 58, 38) : QColor(30, 65, 80));
    painter.fillRect(thumbnail.rect(), gradient);
    painter.setPen(QPen(QColor(35, 230, 198), 2));
    const auto center_y =
        kLayerThumbnailEdge / 2 - static_cast<int>(std::lround(exposure * 8.0));
    painter.drawLine(8, center_y + 10, 24, center_y - 2);
    painter.drawLine(24, center_y - 2, 40, center_y + 4);
    painter.drawLine(40, center_y + 4, 56, center_y - 12);
  } else if (kind == QStringLiteral("group")) {
    painter.fillRect(thumbnail.rect(), QColor(16, 22, 27));
    painter.setPen(QPen(QColor(138, 151, 164), 1));
    painter.setBrush(QColor(44, 55, 64));
    painter.drawRoundedRect(QRect(10, 12, 18, 16), 2, 2);
    painter.drawRoundedRect(QRect(32, 12, 22, 16), 2, 2);
    painter.drawRoundedRect(QRect(10, 34, 44, 16), 2, 2);
  } else {
    painter.fillRect(thumbnail.rect(), QColor(18, 28, 34));
    painter.setPen(QPen(QColor(35, 230, 198), 2));
    painter.drawRoundedRect(thumbnail.rect().adjusted(14, 14, -14, -14), 4, 4);
  }

  painter.end();
  return encoded_layer_thumbnail_source(thumbnail);
}

QString photo_layer_thumbnail_kind(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind == QStringLiteral("adjustment")) {
    return QStringLiteral("masked-adjustment-layer");
  }
  if (kind == QStringLiteral("pixel") || kind == QStringLiteral("source")) {
    return QStringLiteral("raster-layer");
  }
  if (kind == QStringLiteral("fill")) {
    return QStringLiteral("fill-layer");
  }
  if (kind == QStringLiteral("text")) {
    return QStringLiteral("text-layer");
  }
  if (kind == QStringLiteral("group")) {
    return QStringLiteral("group-layer");
  }
  return QStringLiteral("photo-layer");
}

QString non_adjustment_photo_layer_thumbnail_recipe_hash(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind == QStringLiteral("adjustment")) {
    return {};
  }

  QVariantMap payload;
  payload.insert(QStringLiteral("kind"), kind);
  payload.insert(QStringLiteral("enabled"), layer.value(QStringLiteral("enabled"), true).toBool());
  payload.insert(
      QStringLiteral("opacity"),
      QString::number(
          std::clamp(layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 0.0, 1.0),
          'f',
          3));
  payload.insert(
      QStringLiteral("blendMode"),
      normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString()));
  if (kind == QStringLiteral("fill")) {
    payload.insert(QStringLiteral("fillColor"), layer.value(QStringLiteral("fillColor")).toString());
    payload.insert(QStringLiteral("fillAlpha"), layer.value(QStringLiteral("fillAlpha"), 1.0).toDouble());
  } else if (kind == QStringLiteral("text")) {
    for (const auto& key : {
             QStringLiteral("text"),
             QStringLiteral("textColor"),
             QStringLiteral("fontFamily"),
             QStringLiteral("fontWeight"),
             QStringLiteral("textAlignment"),
         }) {
      payload.insert(key, layer.value(key).toString());
    }
    payload.insert(QStringLiteral("fontSize"), layer.value(QStringLiteral("fontSize")).toInt());
    payload.insert(QStringLiteral("pixelWidth"), layer.value(QStringLiteral("pixelWidth")).toInt());
    payload.insert(QStringLiteral("pixelHeight"), layer.value(QStringLiteral("pixelHeight")).toInt());
  } else if (kind == QStringLiteral("pixel") || kind == QStringLiteral("source")) {
    payload.insert(QStringLiteral("sourcePath"), layer.value(QStringLiteral("sourcePath")).toString());
    payload.insert(
        QStringLiteral("pixelDataHash"),
        QStringLiteral("sha1:%1").arg(QString::fromLatin1(
            QCryptographicHash::hash(
                layer.value(QStringLiteral("pixelData")).toByteArray(),
                QCryptographicHash::Sha1)
                .toHex())));
  } else {
    payload.insert(QStringLiteral("name"), layer.value(QStringLiteral("name")).toString());
  }
  return QStringLiteral("sha1:%1").arg(thumbnail_variant_sha1(payload));
}

QString non_adjustment_photo_layer_thumbnail_entry_recipe_hash(const QVariantMap& layer) {
  QVariantMap payload;
  payload.insert(
      QStringLiteral("recipeHash"),
      non_adjustment_photo_layer_thumbnail_recipe_hash(layer));
  payload.insert(
      QStringLiteral("blendMode"),
      normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString()));
  payload.insert(
      QStringLiteral("opacity"),
      QString::number(
          std::clamp(layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 0.0, 1.0),
          'f',
          3));
  payload.insert(
      QStringLiteral("visible"),
      layer.value(QStringLiteral("enabled"), true).toBool());
  return QStringLiteral("sha1:%1").arg(thumbnail_variant_sha1(payload));
}

QStringList selection_mask_set_ids_for_layer(const QVariantMap& layer) {
  return normalized_string_list_value(
      layer,
      QStringLiteral("selectionMaskSetIds"),
      QStringLiteral("selectionMaskSetId"));
}

QStringList selection_mask_set_names_for_layer(const QVariantMap& layer) {
  return normalized_string_list_value(
      layer,
      QStringLiteral("selectionMaskSetNames"),
      QStringLiteral("selectionMaskSetName"));
}

QString selection_mask_set_identity_signature(const QVariantMap& layer) {
  const auto ids = selection_mask_set_ids_for_layer(layer);
  const auto names = selection_mask_set_names_for_layer(layer);
  if (ids.isEmpty() && names.isEmpty()) {
    return {};
  }
  return QStringLiteral("ids:%1|names:%2").arg(ids.join(QChar('|')), names.join(QChar('|')));
}

bool photo_layer_thumbnail_queue_worker_supported(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  return !kind.isEmpty() && kind != QStringLiteral("adjustment");
}

QVariantMap rendered_adjustment_photo_layer_thumbnail_entry(
    const QVariantMap& source_layer,
    const QImage& cropped_image,
    const ThumbnailRefreshQueueRequest& request,
    const ThumbnailRefreshQueueAdjustmentLayerDependencies& dependencies) {
  if (cropped_image.isNull() ||
      source_layer.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
          QStringLiteral("adjustment") ||
      !dependencies.recipe_hash ||
      !dependencies.entry_recipe_hash ||
      !dependencies.thumbnail_source) {
    return {};
  }

  QElapsedTimer layer_timer;
  layer_timer.start();
  auto layer = source_layer;
  const auto layer_recipe_hash = dependencies.recipe_hash(layer);
  const auto layer_cache_recipe_hash = dependencies.entry_recipe_hash(layer);
  const auto layer_cache_mask_id = selection_mask_set_identity_signature(layer);
  const auto layer_cache_key = thumbnail_entry_cache_key(
      request.job.asset_id,
      layer_cache_recipe_hash,
      layer.value(QStringLiteral("id")).toString(),
      layer_cache_mask_id,
      kLayerThumbnailEdge,
      kLayerThumbnailEdge,
      kLayerThumbnailDevicePixelRatio,
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision);

  QVariantMap thumbnail_metadata;
  const auto thumbnail_source = dependencies.thumbnail_source(
      layer,
      cropped_image,
      request,
      &thumbnail_metadata);
  if (thumbnail_source.trimmed().isEmpty() || thumbnail_refresh_cancelled(request)) {
    return {};
  }

  layer.insert(QStringLiteral("thumbnailSource"), thumbnail_source);
  layer.insert(QStringLiteral("thumbnailReady"), true);
  layer.insert(QStringLiteral("thumbnailKind"), photo_layer_thumbnail_kind(layer));
  for (auto iterator = thumbnail_metadata.constBegin();
       iterator != thumbnail_metadata.constEnd();
       ++iterator) {
    layer.insert(iterator.key(), iterator.value());
  }
  insert_thumbnail_invalidation_fields(
      &layer,
      request.job.asset_id,
      layer.value(QStringLiteral("id")).toString(),
      layer_cache_mask_id,
      layer_recipe_hash,
      layer.value(QStringLiteral("thumbnailAlphaHash"),
                  layer.value(QStringLiteral("mask_alpha_trace_hash")))
          .toString(),
      normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString()),
      std::clamp(layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 0.0, 1.0),
      layer.value(QStringLiteral("enabled"), true).toBool());
  clear_thumbnail_transient_fields(&layer);
  const auto layer_opacity = QString::number(
      std::clamp(layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 0.0, 1.0),
      'f',
      3);
  const auto layer_blend_mode =
      normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString());
  layer.insert(
      QStringLiteral("thumbnailSignature"),
      QStringLiteral("%1|masked-adjustment:%2|opacity:%3|blend:%4|alpha:%5|invalidation:%6")
          .arg(layer.value(QStringLiteral("thumbnailSignature")).toString(),
               layer_cache_mask_id,
               layer_opacity,
               layer_blend_mode,
               layer.value(QStringLiteral("thumbnailAlphaHash")).toString(),
               layer.value(QStringLiteral("thumbnailInvalidationKey")).toString()));
  insert_thumbnail_entry_cache_fields(
      &layer,
      layer_cache_key,
      request.job.asset_id,
      layer_cache_recipe_hash,
      layer.value(QStringLiteral("id")).toString(),
      layer_cache_mask_id,
      kLayerThumbnailEdge,
      kLayerThumbnailEdge,
      kLayerThumbnailDevicePixelRatio,
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision,
      false,
      QStringLiteral("queue-worker"));
  insert_thumbnail_queue_worker_entry_fields(
      &layer,
      request.job.job_id,
      QStringLiteral("masked-adjustment-photo-layer"),
      layer_timer.elapsed());
  return layer;
}

QVariantMap rendered_non_adjustment_photo_layer_thumbnail_entry(
    const QVariantMap& source_layer,
    const ThumbnailRefreshQueueRequest& request) {
  if (!photo_layer_thumbnail_queue_worker_supported(source_layer)) {
    return {};
  }

  QElapsedTimer layer_timer;
  layer_timer.start();
  auto layer = source_layer;
  const auto layer_recipe_hash = non_adjustment_photo_layer_thumbnail_recipe_hash(layer);
  const auto layer_cache_recipe_hash = non_adjustment_photo_layer_thumbnail_entry_recipe_hash(layer);
  const auto layer_cache_mask_id = selection_mask_set_identity_signature(layer);
  const auto layer_cache_key = thumbnail_entry_cache_key(
      request.job.asset_id,
      layer_cache_recipe_hash,
      layer.value(QStringLiteral("id")).toString(),
      layer_cache_mask_id,
      kLayerThumbnailEdge,
      kLayerThumbnailEdge,
      kLayerThumbnailDevicePixelRatio,
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision);

  QVariantMap thumbnail_metadata;
  const auto thumbnail_source = rendered_photo_layer_thumbnail_source(layer);
  if (thumbnail_source.trimmed().isEmpty() || thumbnail_refresh_cancelled(request)) {
    return {};
  }

  insert_thumbnail_pixel_metrics(
      &thumbnail_metadata,
      QSize(kLayerThumbnailEdge, kLayerThumbnailEdge),
      QSize(kLayerThumbnailDisplayEdge, kLayerThumbnailDisplayEdge));
  thumbnail_metadata.insert(
      QStringLiteral("thumbnail_render_stage_contract"),
      QStringLiteral("layer-mask-render-stage-telemetry-v1"));
  thumbnail_metadata.insert(
      QStringLiteral("thumbnail_layer_recipe_stage"),
      QStringLiteral("layer_raster_thumbnail_recipe"));
  thumbnail_metadata.insert(QStringLiteral("thumbnail_layer_recipe_evaluation_ms"), 0);
  thumbnail_metadata.insert(QStringLiteral("thumbnail_mask_alpha_stage"), QStringLiteral("not_applicable"));
  thumbnail_metadata.insert(QStringLiteral("thumbnail_mask_alpha_retrieval_ms"), 0);
  thumbnail_metadata.insert(
      QStringLiteral("thumbnail_layer_composite_stage"),
      QStringLiteral("non_adjustment_layer_thumbnail"));
  thumbnail_metadata.insert(QStringLiteral("thumbnail_layer_composite_ms"), 0);
  thumbnail_metadata.insert(QStringLiteral("thumbnail_render_stage"), QStringLiteral("thumbnail_render"));
  thumbnail_metadata.insert(QStringLiteral("thumbnail_render_elapsed_ms"), layer_timer.elapsed());
  thumbnail_metadata.insert(QStringLiteral("thumbnailRenderSource"), QStringLiteral("layer-raster-thumbnail"));
  thumbnail_metadata.insert(QStringLiteral("thumbnailSharedRenderPath"), false);
  thumbnail_metadata.insert(
      QStringLiteral("thumbnailSharedRenderPathContract"),
      QStringLiteral("layer-thumbnail-raster-fallback-v1"));
  thumbnail_metadata.insert(QStringLiteral("thumbnailColorManaged"), false);
  thumbnail_metadata.insert(
      QStringLiteral("thumbnailColorManagementContract"),
      QStringLiteral("layer-thumbnail-active-color-management:v1|adjustment:shared-preview-render|render-mode:layer_thumbnail|fallback:raster-explicit"));
  thumbnail_metadata.insert(
      QStringLiteral("thumbnailPreviewRenderMode"),
      QStringLiteral("layer_thumbnail_raster"));
  thumbnail_metadata.insert(QStringLiteral("thumbnailMasked"), false);
  thumbnail_metadata.insert(QStringLiteral("thumbnailMaskSetId"), QString());

  layer.insert(QStringLiteral("thumbnailSource"), thumbnail_source);
  layer.insert(QStringLiteral("thumbnailReady"), true);
  layer.insert(QStringLiteral("thumbnailKind"), photo_layer_thumbnail_kind(layer));
  for (auto iterator = thumbnail_metadata.constBegin();
       iterator != thumbnail_metadata.constEnd();
       ++iterator) {
    layer.insert(iterator.key(), iterator.value());
  }
  insert_thumbnail_invalidation_fields(
      &layer,
      request.job.asset_id,
      layer.value(QStringLiteral("id")).toString(),
      layer_cache_mask_id,
      layer_recipe_hash,
      QStringLiteral("sha1:not-applicable"),
      normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString()),
      std::clamp(layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 0.0, 1.0),
      layer.value(QStringLiteral("enabled"), true).toBool());
  clear_thumbnail_transient_fields(&layer);

  const auto layer_opacity = QString::number(
      std::clamp(layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 0.0, 1.0),
      'f',
      3);
  const auto layer_blend_mode =
      normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString());
  layer.insert(
      QStringLiteral("thumbnailSignature"),
      QStringLiteral("ready:true|%1:%2|opacity:%3|blend:%4|invalidation:%5")
          .arg(photo_layer_thumbnail_kind(layer),
               layer.value(QStringLiteral("id")).toString(),
               layer_opacity,
               layer_blend_mode,
               layer.value(QStringLiteral("thumbnailInvalidationKey")).toString()));
  insert_thumbnail_entry_cache_fields(
      &layer,
      layer_cache_key,
      request.job.asset_id,
      layer_cache_recipe_hash,
      layer.value(QStringLiteral("id")).toString(),
      layer_cache_mask_id,
      kLayerThumbnailEdge,
      kLayerThumbnailEdge,
      kLayerThumbnailDevicePixelRatio,
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision,
      false,
      QStringLiteral("queue-worker"));
  insert_thumbnail_queue_worker_entry_fields(
      &layer,
      request.job.job_id,
      QStringLiteral("non-adjustment-photo-layer"),
      layer_timer.elapsed());
  return layer;
}

QVariantMap rendered_mask_source_thumbnail_entry(
    const QVariantMap& source,
    const QImage& thumbnail_image,
    const QString& thumbnail_source_revision,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    const ThumbnailRefreshQueueRequest& request,
    const ThumbnailRefreshQueueMaskThumbnailDependencies& dependencies) {
  if (thumbnail_image.isNull() ||
      !dependencies.scaled_mask_source ||
      !dependencies.thumbnail_payload ||
      !dependencies.thumbnail_metadata ||
      !dependencies.insert_source_alpha_trace_fields) {
    return {};
  }

  QElapsedTimer timer;
  timer.start();
  auto entry = source;
  const auto scaled_source = dependencies.scaled_mask_source(
      entry,
      thumbnail_image.size(),
      QStringLiteral("thumbnail"));
  const auto source_mask_id =
      entry.value(QStringLiteral("mask_source_id"), entry.value(QStringLiteral("id"))).toString();
  const auto source_cache_key = thumbnail_entry_cache_key(
      request.job.asset_id,
      request.job.state_signature,
      QString(),
      source_mask_id,
      kMaskThumbnailBackingEdge,
      kMaskThumbnailBackingEdge,
      static_cast<double>(kMaskThumbnailBackingEdge) /
          static_cast<double>(kMaskSetThumbnailDisplayEdge),
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision);

  auto source_preview_mask = scaled_source;
  source_preview_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  QVariantList single_source;
  single_source.append(source_preview_mask);
  const auto payload = dependencies.thumbnail_payload(
      thumbnail_image,
      single_source,
      exposure_gain,
      white_balance);
  for (auto iterator = payload.constBegin(); iterator != payload.constEnd(); ++iterator) {
    entry.insert(iterator.key(), iterator.value());
  }
  if (entry.value(QStringLiteral("thumbnailSource")).toString().trimmed().isEmpty() ||
      thumbnail_refresh_cancelled(request)) {
    return {};
  }
  entry.insert(QStringLiteral("thumbnailReady"), true);
  const auto thumbnail_metadata = dependencies.thumbnail_metadata(scaled_source);
  for (auto iterator = thumbnail_metadata.constBegin();
       iterator != thumbnail_metadata.constEnd();
       ++iterator) {
    entry.insert(iterator.key(), iterator.value());
  }
  dependencies.insert_source_alpha_trace_fields(
      &entry,
      scaled_source,
      thumbnail_source_revision,
      thumbnail_image.size());
  insert_thumbnail_invalidation_fields(
      &entry,
      request.job.asset_id,
      QString(),
      source_mask_id,
      request.job.state_signature,
      entry.value(QStringLiteral("thumbnailAlphaHash"),
                  entry.value(QStringLiteral("mask_alpha_trace_hash")))
          .toString(),
      QStringLiteral("normal"),
      1.0,
      true);
  clear_thumbnail_transient_fields(&entry);
  entry.insert(
      QStringLiteral("thumbnailSignature"),
      QStringLiteral("%1|invalidation:%2")
          .arg(entry.value(QStringLiteral("thumbnailSignature")).toString(),
               entry.value(QStringLiteral("thumbnailInvalidationKey")).toString()));
  insert_thumbnail_entry_cache_fields(
      &entry,
      source_cache_key,
      request.job.asset_id,
      request.job.state_signature,
      QString(),
      source_mask_id,
      kMaskThumbnailBackingEdge,
      kMaskThumbnailBackingEdge,
      static_cast<double>(kMaskThumbnailBackingEdge) /
          static_cast<double>(kMaskSetThumbnailDisplayEdge),
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision,
      false,
      QStringLiteral("queue-worker"));
  insert_thumbnail_queue_worker_entry_fields(
      &entry,
      request.job.job_id,
      QStringLiteral("mask-source"),
      timer.elapsed());
  return entry;
}

QVariantMap rendered_mask_set_thumbnail_entry(
    const QVariantMap& set,
    const QVariantList& thumbnail_masks,
    const QImage& thumbnail_image,
    const QString& thumbnail_source_revision,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    const ThumbnailRefreshQueueRequest& request,
    const ThumbnailRefreshQueueMaskThumbnailDependencies& dependencies) {
  if (thumbnail_image.isNull() ||
      !dependencies.thumbnail_payload ||
      !dependencies.thumbnail_metadata ||
      !dependencies.composite_alpha_trace_fields) {
    return {};
  }

  QElapsedTimer timer;
  timer.start();
  auto entry = set;
  const auto set_mask_id =
      entry.value(QStringLiteral("mask_set_id"), entry.value(QStringLiteral("id"))).toString();
  const auto set_cache_key = thumbnail_entry_cache_key(
      request.job.asset_id,
      request.job.state_signature,
      QString(),
      set_mask_id,
      kMaskThumbnailBackingEdge,
      kMaskThumbnailBackingEdge,
      static_cast<double>(kMaskThumbnailBackingEdge) /
          static_cast<double>(kMaskSetThumbnailDisplayEdge),
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision);

  const auto payload = dependencies.thumbnail_payload(
      thumbnail_image,
      thumbnail_masks,
      exposure_gain,
      white_balance);
  for (auto iterator = payload.constBegin(); iterator != payload.constEnd(); ++iterator) {
    entry.insert(iterator.key(), iterator.value());
  }
  if (entry.value(QStringLiteral("thumbnailSource")).toString().trimmed().isEmpty() ||
      thumbnail_refresh_cancelled(request)) {
    return {};
  }
  entry.insert(QStringLiteral("thumbnailReady"), true);
  if (!thumbnail_masks.isEmpty()) {
    const auto thumbnail_metadata = dependencies.thumbnail_metadata(thumbnail_masks.constFirst().toMap());
    for (auto iterator = thumbnail_metadata.constBegin();
         iterator != thumbnail_metadata.constEnd();
         ++iterator) {
      entry.insert(iterator.key(), iterator.value());
    }
  }
  const auto trace_fields = dependencies.composite_alpha_trace_fields(
      QStringLiteral("mask-thumbnail"),
      thumbnail_masks,
      entry.value(QStringLiteral("id")).toString(),
      thumbnail_source_revision,
      thumbnail_image.size(),
      request.job.state_revision);
  for (auto iterator = trace_fields.constBegin(); iterator != trace_fields.constEnd(); ++iterator) {
    entry.insert(iterator.key(), iterator.value());
  }
  insert_thumbnail_invalidation_fields(
      &entry,
      request.job.asset_id,
      QString(),
      set_mask_id,
      request.job.state_signature,
      entry.value(QStringLiteral("thumbnailAlphaHash"),
                  entry.value(QStringLiteral("mask_alpha_trace_hash")))
          .toString(),
      QStringLiteral("normal"),
      1.0,
      true);
  clear_thumbnail_transient_fields(&entry);
  entry.insert(
      QStringLiteral("thumbnailSignature"),
      QStringLiteral("%1|invalidation:%2")
          .arg(entry.value(QStringLiteral("thumbnailSignature")).toString(),
               entry.value(QStringLiteral("thumbnailInvalidationKey")).toString()));
  insert_thumbnail_entry_cache_fields(
      &entry,
      set_cache_key,
      request.job.asset_id,
      request.job.state_signature,
      QString(),
      set_mask_id,
      kMaskThumbnailBackingEdge,
      kMaskThumbnailBackingEdge,
      static_cast<double>(kMaskThumbnailBackingEdge) /
          static_cast<double>(kMaskSetThumbnailDisplayEdge),
      QString::fromLatin1(kThumbnailColorPipelineVersion),
      request.job.state_revision,
      false,
      QStringLiteral("queue-worker"));
  insert_thumbnail_queue_worker_entry_fields(
      &entry,
      request.job.job_id,
      QStringLiteral("mask-set"),
      timer.elapsed());
  return entry;
}

ThumbnailRefreshQueueResult compute_thumbnail_refresh_queue_result(
    ThumbnailRefreshQueueRequest request,
    const ThumbnailRefreshQueueRenderers& renderers) {
  QElapsedTimer timer;
  timer.start();
  ThumbnailRefreshQueueResult result;
  result.request = request;
  result.worker_thread =
      QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
  result.service_contract = thumbnail_refresh_queue_worker_service_contract();

  if (!thumbnail_refresh_cancelled(request) && request.photo_layer_thumbnail_dirty) {
    QImage adjustment_thumbnail_source_image;
    auto adjustment_thumbnail_source_ready = false;
    for (const auto& value : request.photo_layers) {
      if (thumbnail_refresh_cancelled(request)) {
        break;
      }
      const auto layer = value.toMap();
      const auto layer_kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
      QVariantMap entry;
      if (layer_kind == QStringLiteral("adjustment")) {
        if (!adjustment_thumbnail_source_ready) {
          if (renderers.adjustment_thumbnail_source_image) {
            adjustment_thumbnail_source_image =
                renderers.adjustment_thumbnail_source_image(request);
          }
          adjustment_thumbnail_source_ready = true;
        }
        entry = rendered_adjustment_photo_layer_thumbnail_entry(
            layer,
            adjustment_thumbnail_source_image,
            request,
            renderers.adjustment_layer_dependencies);
      } else {
        entry = rendered_non_adjustment_photo_layer_thumbnail_entry(
            layer,
            request);
      }
      const auto cache_key = entry.value(QStringLiteral("thumbnailCacheKey")).toString();
      if (!cache_key.isEmpty()) {
        result.photo_layer_thumbnail_entries.insert(cache_key, entry);
      }
    }
  }
  if (!thumbnail_refresh_cancelled(request) && request.mask_thumbnail_dirty &&
      renderers.mask_thumbnail_entries) {
    result.mask_thumbnail_entries = renderers.mask_thumbnail_entries(request);
  }

  result.photo_layer_thumbnail_entry_count = static_cast<int>(
      result.photo_layer_thumbnail_entries.size());
  result.mask_thumbnail_entry_count = static_cast<int>(
      result.mask_thumbnail_entries.size());
  for (auto iterator = result.photo_layer_thumbnail_entries.begin();
       iterator != result.photo_layer_thumbnail_entries.end();
       ++iterator) {
    auto entry = iterator.value().toMap();
    entry.insert(
        QStringLiteral("thumbnailQueueWorkerRenderEntryCount"),
        result.photo_layer_thumbnail_entry_count);
    iterator.value() = entry;
  }
  for (auto iterator = result.mask_thumbnail_entries.begin();
       iterator != result.mask_thumbnail_entries.end();
       ++iterator) {
    auto entry = iterator.value().toMap();
    entry.insert(
        QStringLiteral("thumbnailQueueWorkerRenderMaskEntryCount"),
        result.mask_thumbnail_entry_count);
    iterator.value() = entry;
  }
  result.cancelled = thumbnail_refresh_cancelled(request);
  result.success = !result.cancelled;
  result.elapsed_ms = timer.elapsed();
  return result;
}

}  // namespace photo_editor::desktop
