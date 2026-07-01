#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QImage>
#include <QRect>
#include <QtGlobal>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <future>
#include <thread>
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

struct WorkingImageQImageTelemetry final {
  qint64 linear_pack_ms = 0;
  qint64 color_convert_ms = 0;
  qint64 total_ms = 0;
};

[[nodiscard]] inline bool pack_working_rows_to_rgba16(
    const WorkingImage& working,
    const QRect& source_rect,
    uchar* output_bits,
    qsizetype output_bytes_per_line,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (working.isNull() || !source_rect.isValid() || output_bits == nullptr ||
      output_bytes_per_line <= 0) {
    return false;
  }

  const auto bounded = source_rect.intersected(QRect(0, 0, working.width, working.height));
  if (!bounded.isValid() || bounded.width() <= 0 || bounded.height() <= 0) {
    return false;
  }

  const auto pack_range = [&working, bounded, output_bits, output_bytes_per_line, cancellation](
                              int begin_y,
                              int end_y) {
    for (int y = begin_y; y < end_y; ++y) {
      if (cancellation.cancelled()) {
        return false;
      }
      const auto source_y = bounded.y() + y;
      const auto source_offset = static_cast<std::size_t>(
          source_y * working.width + bounded.x());
      auto* scanline = reinterpret_cast<QRgbaFloat16*>(
          output_bits + static_cast<qsizetype>(y) * output_bytes_per_line);
      for (int x = 0; x < bounded.width(); ++x) {
        const auto& pixel = working.pixels[source_offset + static_cast<std::size_t>(x)];
        scanline[x] = QRgbaFloat16{
            qfloat16(std::max(0.0F, pixel.red)),
            qfloat16(std::max(0.0F, pixel.green)),
            qfloat16(std::max(0.0F, pixel.blue)),
            qfloat16(photo_editor::pipeline::clamp_unit(pixel.alpha)),
        };
      }
    }
    return true;
  };

  constexpr auto kParallelPackPixelThreshold = qint64{2'000'000};
  const auto pixel_count =
      static_cast<qint64>(bounded.width()) * static_cast<qint64>(bounded.height());
  if (pixel_count < kParallelPackPixelThreshold || bounded.height() <= 1) {
    return pack_range(0, bounded.height());
  }

  const auto hardware_threads = std::thread::hardware_concurrency();
  const auto max_threads = hardware_threads > 0U ? static_cast<int>(hardware_threads) : 1;
  if (max_threads <= 1) {
    return pack_range(0, bounded.height());
  }

  const auto task_count = std::min(max_threads, bounded.height());
  const auto rows_per_task = (bounded.height() + task_count - 1) / task_count;
  std::vector<std::future<bool>> futures;
  futures.reserve(static_cast<std::size_t>(task_count));
  for (int task = 0; task < task_count; ++task) {
    const auto begin_y = task * rows_per_task;
    if (begin_y >= bounded.height()) {
      break;
    }
    const auto end_y = std::min(bounded.height(), begin_y + rows_per_task);
    futures.push_back(std::async(
        std::launch::async,
        [pack_range, begin_y, end_y]() {
          return pack_range(begin_y, end_y);
        }));
  }
  for (auto& future : futures) {
    if (!future.get()) {
      return false;
    }
  }
  return true;
}

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
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  return QColorSpace(QColorSpace::Primaries::Bt2020, QColorSpace::TransferFunction::Linear);
#else
  return linear_srgb_color_space();
#endif
}

[[nodiscard]] inline QImage converted_to_color_space(
    QImage image,
    const QColorSpace& target_color_space,
    QImage::Format output_format) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  return image.convertedToColorSpace(target_color_space, output_format);
#else
  auto converted = image.convertedToColorSpace(target_color_space);
  if (converted.isNull()) {
    return {};
  }

  converted = converted.convertToFormat(output_format);
  converted.setColorSpace(target_color_space);
  return converted;
#endif
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    return QColorSpace(QColorSpace::Bt2020);
#else
    return preview_color_space();
#endif
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

  const auto converted = converted_to_color_space(
      image,
      working_color_space(working_profile_name),
      QImage::Format_RGBA16FPx4);
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
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB"),
    photo_editor::pipeline::CancellationToken cancellation = {},
    WorkingImageQImageTelemetry* telemetry = nullptr) {
  QElapsedTimer total_timer;
  if (telemetry != nullptr) {
    *telemetry = {};
    total_timer.start();
  }
  if (working.isNull()) {
    return {};
  }

  QElapsedTimer pack_timer;
  if (telemetry != nullptr) {
    pack_timer.start();
  }
  QImage linear_image(working.width, working.height, QImage::Format_RGBA16FPx4);
  if (linear_image.isNull()) {
    return {};
  }
  linear_image.setColorSpace(working_color_space(working_profile_name));

  if (!pack_working_rows_to_rgba16(
          working,
          QRect(0, 0, working.width, working.height),
          linear_image.bits(),
          linear_image.bytesPerLine(),
          cancellation)) {
    return {};
  }
  if (telemetry != nullptr) {
    telemetry->linear_pack_ms = pack_timer.elapsed();
  }
  if (cancellation.cancelled()) {
    return {};
  }

  QElapsedTimer convert_timer;
  if (telemetry != nullptr) {
    convert_timer.start();
  }
  QImage result;
  if (!target_color_space.isValid()) {
    result = linear_image.convertToFormat(output_format);
    if (cancellation.cancelled()) {
      return {};
    }
    result.setColorSpace(preview_color_space());
  } else {
    result = converted_to_color_space(linear_image, target_color_space, output_format);
    if (cancellation.cancelled()) {
      return {};
    }
    if (result.isNull()) {
      result = linear_image.convertToFormat(output_format);
      if (cancellation.cancelled()) {
        return {};
      }
      result.setColorSpace(target_color_space);
    }
  }
  if (telemetry != nullptr) {
    telemetry->color_convert_ms = convert_timer.elapsed();
    telemetry->total_ms = total_timer.elapsed();
  }
  return result;
}

[[nodiscard]] inline QImage qimage_from_working_image_rect(
    const WorkingImage& working,
    const QRect& source_rect,
    const QColorSpace& target_color_space = preview_color_space(),
    QImage::Format output_format = QImage::Format_ARGB32,
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB"),
    photo_editor::pipeline::CancellationToken cancellation = {},
    WorkingImageQImageTelemetry* telemetry = nullptr) {
  QElapsedTimer total_timer;
  if (telemetry != nullptr) {
    *telemetry = {};
    total_timer.start();
  }
  if (working.isNull() || !source_rect.isValid()) {
    return {};
  }

  const auto bounded = source_rect.intersected(QRect(0, 0, working.width, working.height));
  if (!bounded.isValid() || bounded.width() <= 0 || bounded.height() <= 0) {
    return {};
  }

  QElapsedTimer pack_timer;
  if (telemetry != nullptr) {
    pack_timer.start();
  }
  QImage linear_image(bounded.width(), bounded.height(), QImage::Format_RGBA16FPx4);
  if (linear_image.isNull()) {
    return {};
  }
  linear_image.setColorSpace(working_color_space(working_profile_name));

  if (!pack_working_rows_to_rgba16(
          working,
          bounded,
          linear_image.bits(),
          linear_image.bytesPerLine(),
          cancellation)) {
    return {};
  }
  if (telemetry != nullptr) {
    telemetry->linear_pack_ms = pack_timer.elapsed();
  }
  if (cancellation.cancelled()) {
    return {};
  }

  QElapsedTimer convert_timer;
  if (telemetry != nullptr) {
    convert_timer.start();
  }
  QImage result;
  if (!target_color_space.isValid()) {
    result = linear_image.convertToFormat(output_format);
    if (cancellation.cancelled()) {
      return {};
    }
    result.setColorSpace(preview_color_space());
  } else {
    result = converted_to_color_space(linear_image, target_color_space, output_format);
    if (cancellation.cancelled()) {
      return {};
    }
    if (result.isNull()) {
      result = linear_image.convertToFormat(output_format);
      if (cancellation.cancelled()) {
        return {};
      }
      result.setColorSpace(target_color_space);
    }
  }
  if (telemetry != nullptr) {
    telemetry->color_convert_ms = convert_timer.elapsed();
    telemetry->total_ms = total_timer.elapsed();
  }
  return result;
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

  const auto converted = converted_to_color_space(image, target_color_space, output_format);
  if (!converted.isNull()) {
    return converted;
  }

  auto fallback = image.convertToFormat(output_format);
  fallback.setColorSpace(target_color_space);
  return fallback;
}

}  // namespace photo_editor::desktop
