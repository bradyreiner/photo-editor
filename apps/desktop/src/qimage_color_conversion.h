#pragma once

#include <QColorSpace>
#include <QImage>
#include <QtGlobal>

namespace photo_editor::desktop {

enum class QImageColorConversionPurpose {
  DecodeNormalization,
  OutputPublication,
  MonitorPublication,
  ProofPublication,
  EvidenceMeasurement,
};

[[nodiscard]] inline QImage convert_tagged_qimage_color_space(
    const QImage& image,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    [[maybe_unused]] QImageColorConversionPurpose purpose,
    Qt::ImageConversionFlags flags = Qt::AutoColor) {
  if (image.isNull() || !target_color_space.isValid()) {
    return {};
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  return image.convertedToColorSpace(target_color_space, output_format, flags);
#else
  auto converted = image.convertedToColorSpace(target_color_space);
  if (converted.isNull()) {
    return {};
  }
  converted = converted.convertToFormat(output_format, flags);
  if (!converted.isNull()) {
    converted.setColorSpace(target_color_space);
  }
  return converted;
#endif
}

[[nodiscard]] inline QImage convert_tagged_qimage_color_space(
    const QImage& image,
    const QColorSpace& target_color_space,
    [[maybe_unused]] QImageColorConversionPurpose purpose) {
  if (image.isNull() || !target_color_space.isValid()) {
    return {};
  }
  return image.convertedToColorSpace(target_color_space);
}

}  // namespace photo_editor::desktop
