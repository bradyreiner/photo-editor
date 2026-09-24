#pragma once

#include <QColorSpace>
#include <QImage>

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
    [[maybe_unused]] QImageColorConversionPurpose purpose) {
  if (image.isNull() || !target_color_space.isValid()) {
    return {};
  }
  return image.convertedToColorSpace(target_color_space, output_format);
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
