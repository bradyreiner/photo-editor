#pragma once

#include <algorithm>

#include <QBuffer>
#include <QColorSpace>
#include <QImage>
#include <QImageWriter>
#include <QSize>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

inline void insert_thumbnail_pixel_metrics(
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

inline QString encoded_layer_thumbnail_source(const QImage& image) {
  constexpr qint64 kMaxLayerThumbnailSourceBytes = 512 * 1024;
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
  if (!writer.write(encoded_image) ||
      encoded.size() > kMaxLayerThumbnailSourceBytes) {
    return {};
  }

  return QStringLiteral("data:image/png;base64,%1")
      .arg(QString::fromLatin1(encoded.toBase64()));
}

}  // namespace photo_editor::desktop
