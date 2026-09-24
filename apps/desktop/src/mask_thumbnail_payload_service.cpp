#include "mask_thumbnail_payload_service.h"

#include "canonical_composite_mask_alpha.h"
#include "thumbnail_refresh_queue_service.h"

#include <QColor>
#include <QRgb>

namespace photo_editor::desktop {
namespace {

QColor mask_thumbnail_solid_overlay_color() {
  return QColor(0, 230, 195);
}

QString mask_thumbnail_overlay_contract() {
  return QStringLiteral(
      "mask-thumbnail-overlay:v1|source:final-alpha|color:solid|debug-rgb:false");
}

QImage solid_color_mask_alpha_image(
    const MaskAlphaEvaluationResult& result,
    const QColor& color) {
  if (!result.valid || result.size.isEmpty() ||
      result.alpha_mask.size() != result.size.width() * result.size.height()) {
    return {};
  }

  QImage image(result.size, QImage::Format_ARGB32);
  for (int y = 0; y < result.size.height(); ++y) {
    auto* target_scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    const auto row_offset = y * result.size.width();
    for (int x = 0; x < result.size.width(); ++x) {
      const auto alpha =
          static_cast<unsigned char>(result.alpha_mask.at(row_offset + x));
      target_scanline[x] =
          qRgba(color.red(), color.green(), color.blue(), alpha);
    }
  }
  return image;
}

void insert_mask_thumbnail_overlay_payload_fields(
    QVariantMap* payload,
    const QString& source) {
  if (payload == nullptr || source.trimmed().isEmpty()) {
    return;
  }
  const auto color = mask_thumbnail_solid_overlay_color();
  payload->insert(QStringLiteral("thumbnailOverlaySource"), source);
  payload->insert(
      QStringLiteral("thumbnailOverlayVisualMode"),
      QStringLiteral("solid-color-alpha"));
  payload->insert(
      QStringLiteral("thumbnailOverlayRgbPolicy"),
      QStringLiteral("solid-color-preserves-alpha"));
  payload->insert(QStringLiteral("thumbnailOverlayDebugColorFree"), true);
  payload->insert(
      QStringLiteral("thumbnailOverlayContract"),
      mask_thumbnail_overlay_contract());
  payload->insert(
      QStringLiteral("thumbnailOverlayColor"),
      color.name(QColor::HexRgb));
}

}  // namespace

QVariantMap canonical_mask_thumbnail_payload(
    const QImage& thumbnail_image,
    const QVariantList& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance) {
  if (masks.isEmpty() || thumbnail_image.isNull()) {
    return {};
  }
  const auto alpha_result = evaluate_canonical_composite_mask_alpha(
      thumbnail_image,
      masks,
      exposure_gain,
      white_balance);
  return mask_thumbnail_payload_from_alpha(alpha_result);
}

QVariantMap mask_thumbnail_payload_from_alpha(
    const MaskAlphaEvaluationResult& alpha_result) {
  if (!alpha_result.valid || alpha_result.size.isEmpty() ||
      static_cast<qint64>(alpha_result.size.width()) * alpha_result.size.height() !=
          alpha_result.alpha_mask.size()) {
    return {};
  }

  QVariantMap payload;
  payload.insert(
      QStringLiteral("thumbnailSource"),
      encoded_layer_thumbnail_source(grayscale_mask_alpha_image(alpha_result)));
  insert_mask_thumbnail_overlay_payload_fields(
      &payload,
      encoded_layer_thumbnail_source(solid_color_mask_alpha_image(
          alpha_result,
          mask_thumbnail_solid_overlay_color())));
  payload.insert(
      QStringLiteral("thumbnailVisualMode"),
      QStringLiteral("grayscale-alpha"));
  payload.insert(
      QStringLiteral("thumbnailRgbPolicy"),
      QStringLiteral("rgb-equals-alpha"));
  payload.insert(QStringLiteral("thumbnailDebugColorFree"), true);
  insert_thumbnail_pixel_metrics(
      &payload,
      alpha_result.size,
      QSize(kMaskSetThumbnailDisplayEdge, kMaskSetThumbnailDisplayEdge));
  const auto alpha_metadata = mask_alpha_thumbnail_metadata(alpha_result);
  for (auto iterator = alpha_metadata.constBegin();
       iterator != alpha_metadata.constEnd();
       ++iterator) {
    payload.insert(iterator.key(), iterator.value());
  }
  return payload;
}

}  // namespace photo_editor::desktop
