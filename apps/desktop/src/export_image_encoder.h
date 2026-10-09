#pragma once

#include "export_image_settings.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QString>

namespace photo_editor::desktop {

// Qt maps 12 on its PNG compression scale to zlib level 1. Pixels and
// metadata stay lossless; avoid expensive default compression during export.
inline constexpr int kPngExportCompression = 12;

struct StandardExportEncodingResult final {
  bool valid = false;
  QByteArray bytes;
  QString error;
};

[[nodiscard]] inline QImage
prepare_image_for_export_encoding(
    const QImage& image,
    const ExportImageSettings& options) {
  if (image.isNull()) {
    return {};
  }
  const auto preserve_metadata = [&image](QImage& output) {
    output.setDotsPerMeterX(image.dotsPerMeterX());
    output.setDotsPerMeterY(image.dotsPerMeterY());
    output.setOffset(image.offset());
    output.setDevicePixelRatio(image.devicePixelRatio());
    for (const auto& key : image.textKeys()) {
      output.setText(key, image.text(key));
    }
  };
  if (options.hdr_delivery) {
    return image.convertToFormat(
        QImage::Format_RGBA32FPx4);
  }
  if (options.scene_linear) {
    const auto float_image =
        image.convertToFormat(
            QImage::Format_RGBA32FPx4);
    if (!options.flatten_alpha) {
      return float_image;
    }
    QImage flattened(
        image.size(),
        QImage::Format_RGBA32FPx4);
    flattened.setColorSpace(image.colorSpace());
    flattened.fill(Qt::white);
    QPainter painter(&flattened);
    painter.setCompositionMode(
        QPainter::CompositionMode_SourceOver);
    painter.drawImage(flattened.rect(), float_image, float_image.rect());
    painter.end();
    preserve_metadata(flattened);
    return flattened;
  }
  if (!options.flatten_alpha) {
    return image.convertToFormat(
        options.image_format);
  }

  QImage flattened(
      image.size(), QImage::Format_RGB888);
  flattened.setColorSpace(image.colorSpace());
  flattened.fill(Qt::white);
  QPainter painter(&flattened);
  painter.setCompositionMode(
      QPainter::CompositionMode_SourceOver);
  painter.drawImage(flattened.rect(), image, image.rect());
  painter.end();
  preserve_metadata(flattened);
  return flattened;
}

[[nodiscard]] inline StandardExportEncodingResult
encode_prepared_standard_export_image(
    const QImage& prepared_image,
    const ExportImageSettings& options,
    const QString& description) {
  StandardExportEncodingResult result;
  if (prepared_image.isNull()) {
    result.error =
        QStringLiteral("prepared export image is required");
    return result;
  }
  if (options.writer_format != "png" &&
      options.writer_format != "jpg" &&
      options.writer_format != "tif") {
    result.error = QStringLiteral(
                       "standard export encoder does not support %1")
                       .arg(QString::fromLatin1(
                           options.writer_format));
    return result;
  }

  QBuffer buffer(&result.bytes);
  if (!buffer.open(QIODevice::WriteOnly)) {
    result.error =
        QStringLiteral("standard export buffer could not be opened");
    return result;
  }
  QImageWriter writer(
      &buffer, options.writer_format);
  if (options.writer_format == "jpg") {
    writer.setQuality(options.jpeg_quality);
  } else if (options.writer_format == "png") {
    writer.setCompression(kPngExportCompression);
  }
  writer.setText(
      QStringLiteral("Description"),
      description);
  if (!writer.write(prepared_image) ||
      result.bytes.isEmpty()) {
    result.error = writer.errorString().trimmed();
    if (result.error.isEmpty()) {
      result.error = QStringLiteral(
          "standard export image encoding failed");
    }
    result.bytes.clear();
    return result;
  }
  result.valid = true;
  return result;
}

}  // namespace photo_editor::desktop
