#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QColorSpace>
#include <QImage>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <vector>

namespace photo_editor::desktop {

struct WorkingImage final {
  int width = 0;
  int height = 0;
  std::vector<photo_editor::pipeline::ColorPixel> pixels;

  [[nodiscard]] bool isNull() const {
    return width <= 0 || height <= 0 ||
           pixels.size() != static_cast<std::size_t>(width * height);
  }
};

[[nodiscard]] inline QColorSpace preview_color_space() {
  return QColorSpace(QColorSpace::SRgb);
}

[[nodiscard]] inline QColorSpace linear_srgb_color_space() {
  return QColorSpace(QColorSpace::SRgbLinear);
}

[[nodiscard]] inline QColorSpace rec709_color_space() {
  return QColorSpace(
      QColorSpace::Primaries::SRgb,
      QColorSpace::TransferFunction::Gamma,
      2.4F);
}

[[nodiscard]] inline QColorSpace linear_display_p3_color_space() {
  return QColorSpace(QColorSpace::Primaries::DciP3D65, QColorSpace::TransferFunction::Linear);
}

[[nodiscard]] inline QColorSpace linear_prophoto_color_space() {
  return QColorSpace(QColorSpace::Primaries::ProPhotoRgb, QColorSpace::TransferFunction::Linear);
}

[[nodiscard]] inline QColorSpace linear_rec2020_color_space() {
  return QColorSpace(QColorSpace::Primaries::Bt2020, QColorSpace::TransferFunction::Linear);
}

[[nodiscard]] inline QColorSpace preview_color_space_for_name(const QString& profile_name) {
  const auto normalized = profile_name.trimmed();
  if (normalized.compare(QStringLiteral("Adobe RGB"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("adobe-rgb"), Qt::CaseInsensitive) == 0) {
    return QColorSpace(QColorSpace::AdobeRgb);
  }
  if (normalized.compare(QStringLiteral("Display P3"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("display-p3"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("p3"), Qt::CaseInsensitive) == 0) {
    return QColorSpace(QColorSpace::DisplayP3);
  }
  if (normalized.compare(QStringLiteral("ProPhoto RGB"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("prophoto-rgb"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("prophoto"), Qt::CaseInsensitive) == 0) {
    return QColorSpace(QColorSpace::ProPhotoRgb);
  }
  if (normalized.compare(QStringLiteral("Rec.709"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("rec709"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("rec-709"), Qt::CaseInsensitive) == 0) {
    return rec709_color_space();
  }
  if (normalized.compare(QStringLiteral("Rec.2020"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("rec2020"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("rec-2020"), Qt::CaseInsensitive) == 0) {
    return QColorSpace(QColorSpace::Bt2020);
  }
  return preview_color_space();
}

[[nodiscard]] inline QColorSpace working_color_space(
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB")) {
  const auto normalized = working_profile_name.trimmed();
  if (normalized.compare(QStringLiteral("Linear sRGB"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("linear-srgb"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("srgb-linear"), Qt::CaseInsensitive) == 0) {
    return linear_srgb_color_space();
  }
  if (normalized.compare(QStringLiteral("Linear Display P3"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("linear-display-p3"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("display-p3-linear"), Qt::CaseInsensitive) == 0) {
    return linear_display_p3_color_space();
  }
  if (normalized.compare(QStringLiteral("Linear Rec.2020"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("linear-rec.2020"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("linear-rec2020"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("rec2020-linear"), Qt::CaseInsensitive) == 0) {
    return linear_rec2020_color_space();
  }
  return linear_prophoto_color_space();
}

[[nodiscard]] inline QImage working_image_qimage(
    QImage image,
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB")) {
  if (image.isNull()) {
    return {};
  }

  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_color_space());
  }

  const auto converted =
      image.convertedToColorSpace(working_color_space(working_profile_name), QImage::Format_RGBA16FPx4);
  if (!converted.isNull()) {
    return converted;
  }

  auto fallback = image.convertToFormat(QImage::Format_RGBA16FPx4);
  fallback.setColorSpace(working_color_space(working_profile_name));
  return fallback;
}

[[nodiscard]] inline WorkingImage working_image_from_qimage(
    const QImage& image,
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB")) {
  const auto linear_image = working_image_qimage(image, working_profile_name);
  if (linear_image.isNull()) {
    return {};
  }

  WorkingImage working;
  working.width = linear_image.width();
  working.height = linear_image.height();
  working.pixels.resize(static_cast<std::size_t>(working.width * working.height));

  for (int y = 0; y < working.height; ++y) {
    const auto* scanline =
        reinterpret_cast<const QRgbaFloat16*>(linear_image.constScanLine(y));
    for (int x = 0; x < working.width; ++x) {
      const auto& pixel = scanline[x];
      working.pixels[static_cast<std::size_t>(y * working.width + x)] =
          photo_editor::pipeline::ColorPixel{
              .red = std::max(0.0F, static_cast<float>(pixel.r)),
              .green = std::max(0.0F, static_cast<float>(pixel.g)),
              .blue = std::max(0.0F, static_cast<float>(pixel.b)),
              .alpha = static_cast<float>(
                  photo_editor::pipeline::clamp_unit(static_cast<double>(pixel.a))),
          };
    }
  }

  return working;
}

[[nodiscard]] inline WorkingImage crop_working_image(const WorkingImage& image, const QRect& crop_rect) {
  if (image.isNull() || !crop_rect.isValid()) {
    return image;
  }

  const auto bounded = crop_rect.intersected(QRect(0, 0, image.width, image.height));
  if (!bounded.isValid()) {
    return {};
  }

  WorkingImage cropped;
  cropped.width = bounded.width();
  cropped.height = bounded.height();
  cropped.pixels.resize(static_cast<std::size_t>(cropped.width * cropped.height));

  for (int y = 0; y < cropped.height; ++y) {
    const auto source_y = bounded.y() + y;
    const auto source_offset = static_cast<std::size_t>(source_y * image.width + bounded.x());
    const auto target_offset = static_cast<std::size_t>(y * cropped.width);
    std::copy_n(
        image.pixels.begin() + static_cast<std::ptrdiff_t>(source_offset),
        cropped.width,
        cropped.pixels.begin() + static_cast<std::ptrdiff_t>(target_offset));
  }

  return cropped;
}

[[nodiscard]] inline QImage qimage_from_working_image(
    const WorkingImage& working,
    const QColorSpace& target_color_space = preview_color_space(),
    QImage::Format output_format = QImage::Format_ARGB32,
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB")) {
  if (working.isNull()) {
    return {};
  }

  QImage linear_image(working.width, working.height, QImage::Format_RGBA16FPx4);
  if (linear_image.isNull()) {
    return {};
  }
  linear_image.setColorSpace(working_color_space(working_profile_name));

  for (int y = 0; y < working.height; ++y) {
    auto* scanline = reinterpret_cast<QRgbaFloat16*>(linear_image.scanLine(y));
    for (int x = 0; x < working.width; ++x) {
      const auto& pixel = working.pixels[static_cast<std::size_t>(y * working.width + x)];
      scanline[x] = QRgbaFloat16{
          qfloat16(std::max(0.0F, pixel.red)),
          qfloat16(std::max(0.0F, pixel.green)),
          qfloat16(std::max(0.0F, pixel.blue)),
          qfloat16(photo_editor::pipeline::clamp_unit(pixel.alpha)),
      };
    }
  }

  if (!target_color_space.isValid()) {
    auto fallback = linear_image.convertToFormat(output_format);
    fallback.setColorSpace(preview_color_space());
    return fallback;
  }

  const auto converted = linear_image.convertedToColorSpace(target_color_space, output_format);
  if (!converted.isNull()) {
    return converted;
  }

  auto fallback = linear_image.convertToFormat(output_format);
  fallback.setColorSpace(target_color_space);
  return fallback;
}

[[nodiscard]] inline QImage finalize_output_image(
    QImage image,
    const QColorSpace& target_color_space,
    QImage::Format output_format = QImage::Format_ARGB32) {
  if (image.isNull()) {
    return {};
  }

  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_color_space());
  }

  if (!target_color_space.isValid()) {
    auto fallback = image.convertToFormat(output_format);
    fallback.setColorSpace(preview_color_space());
    return fallback;
  }

  const auto converted = image.convertedToColorSpace(target_color_space, output_format);
  if (!converted.isNull()) {
    return converted;
  }

  auto fallback = image.convertToFormat(output_format);
  fallback.setColorSpace(target_color_space);
  return fallback;
}

}  // namespace photo_editor::desktop
