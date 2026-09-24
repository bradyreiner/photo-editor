#pragma once

#include "export_image_settings.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QString>

namespace photo_editor::desktop {

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
    painter.drawImage(0, 0, float_image);
    painter.end();
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
  painter.drawImage(0, 0, image);
  painter.end();
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
