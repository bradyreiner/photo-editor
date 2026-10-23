#pragma once

#include "output_lut_publication_telemetry.h"
#include "qimage_color_conversion.h"
#include "preview_device_icc_options.h"

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/cpu_work_scheduler.h"
#include "photo_editor/pipeline/output_rendering.h"
#include "photo_editor/pipeline/render_executor.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#if defined(PHOTO_EDITOR_HAS_CUDA_BACKEND) && PHOTO_EDITOR_HAS_CUDA_BACKEND
#include "photo_editor/pipeline/cuda/cuda_pipeline_backend.h"
#endif

#include <QColorSpace>
#include <QElapsedTimer>
#include <QImage>
#include <QRect>
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include <QThreadPool>
#endif
#include <QtGlobal>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace photo_editor::desktop {

struct WorkingImage final {
  int width = 0;
  int height = 0;
  std::vector<photo_editor::pipeline::ColorPixel> pixels;

  struct RetainedSurfaceState final {
    std::mutex mutex;
    photo_editor::pipeline::RenderSurface surface;
    std::uint64_t materialized_gpu_to_cpu_bytes = 0;
  };

  std::shared_ptr<RetainedSurfaceState> retained_surface;

  [[nodiscard]] bool isNull() const {
    return width <= 0 || height <= 0 ||
           pixels.size() != static_cast<std::size_t>(width * height);
  }

  [[nodiscard]] bool hasRetainedSurface() const {
    if (retained_surface == nullptr) {
      return false;
    }
    const std::scoped_lock lock(retained_surface->mutex);
    return retained_surface->surface.valid() &&
           retained_surface->surface.descriptor.width ==
               static_cast<std::uint32_t>(std::max(width, 0)) &&
           retained_surface->surface.descriptor.height ==
               static_cast<std::uint32_t>(std::max(height, 0));
  }

  [[nodiscard]] std::shared_ptr<const photo_editor::pipeline::RenderSurfaceBacking>
  retainedBacking() const {
    if (retained_surface == nullptr) {
      return {};
    }
    const std::scoped_lock lock(retained_surface->mutex);
    return retained_surface->surface.valid()
               ? retained_surface->surface.backing
               : std::shared_ptr<const photo_editor::pipeline::RenderSurfaceBacking>{};
  }

  [[nodiscard]] bool ensureCpuPixels(
      photo_editor::pipeline::CancellationToken cancellation = {}) {
    if (!isNull()) {
      return true;
    }
    if (retained_surface == nullptr || cancellation.cancelled()) {
      return false;
    }
    const auto retained = retained_surface;
    const std::scoped_lock lock(retained->mutex);
    if (!isNull()) {
      return true;
    }
    if (!retained->surface.ensure_cpu_pixels(cancellation)) {
      return false;
    }
    const auto& descriptor = retained->surface.descriptor;
    if (descriptor.width != static_cast<std::uint32_t>(std::max(width, 0)) ||
        descriptor.height != static_cast<std::uint32_t>(std::max(height, 0))) {
      return false;
    }
    pixels = std::move(retained->surface.pixels);
    retained->materialized_gpu_to_cpu_bytes =
        static_cast<std::uint64_t>(pixels.size()) *
        sizeof(photo_editor::pipeline::ColorPixel);
    retained->surface = {};
    return !isNull();
  }

  [[nodiscard]] std::uint64_t takeMaterializedGpuToCpuBytes() {
    if (retained_surface == nullptr) {
      return 0;
    }
    const std::scoped_lock lock(retained_surface->mutex);
    return std::exchange(
        retained_surface->materialized_gpu_to_cpu_bytes,
        std::uint64_t{0});
  }

  [[nodiscard]] std::uint64_t byteSize() const {
    auto bytes = static_cast<std::uint64_t>(pixels.size()) *
                 sizeof(photo_editor::pipeline::ColorPixel);
    if (retained_surface != nullptr) {
      const std::scoped_lock lock(retained_surface->mutex);
      bytes += retained_surface->surface.byte_size();
    }
    return bytes;
  }

  void adoptRetainedSurface(photo_editor::pipeline::RenderSurface surface) {
    width = static_cast<int>(surface.descriptor.width);
    height = static_cast<int>(surface.descriptor.height);
    pixels.clear();
    retained_surface = std::make_shared<RetainedSurfaceState>();
    retained_surface->surface = std::move(surface);
  }
};

struct WorkingImageQImageTelemetry final {
  qint64 linear_pack_ms = 0;
  qint64 color_convert_ms = 0;
  qint64 total_ms = 0;
  QString color_processor;
  QString profile_asset_status;
  QString profile_asset_id;
  QString profile_payload_sha256;
  QString transform_cache_key;
  QString icc_lut_status;
  QString icc_lut_cache_key;
  qint64 icc_lut_pixels = 0;
  qint64 icc_exact_fallback_pixels = 0;
  OutputLutPublicationTelemetry output_lut;
  QString failure_reason;
  QImage gamut_warning_mask;
};

struct WorkingImageOutputTransform final {
  const photo_editor::pipeline::ColorPipelineConfig* color_pipeline = nullptr;
  const photo_editor::pipeline::ResolvedIccProfileAsset*
      encoded_device_profile = nullptr;
  photo_editor::pipeline::GamutWarningTarget target =
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay;
  bool generate_proof_gamut_warning_mask = false;
  photo_editor::pipeline::Aces2OutputLutQuality aces2_lut_quality =
      photo_editor::pipeline::Aces2OutputLutQuality::Final;
  bool allow_metal_output_lut = true;
  bool allow_cuda_output_lut = true;
};

[[nodiscard]] inline QColorSpace encoded_device_color_space(
    const WorkingImageOutputTransform& output_transform) {
  const auto* profile =
      output_transform.encoded_device_profile;
  if (profile == nullptr || !profile->ready()) {
    return {};
  }
  const auto bytes = profile->payload_bytes();
  return QColorSpace::fromIccProfile(QByteArray(
      reinterpret_cast<const char*>(bytes.data()),
      static_cast<qsizetype>(bytes.size())));
}

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

  constexpr auto kParallelPackPixelThreshold = qint64{200'000};
  const auto pixel_count =
      static_cast<qint64>(bounded.width()) * static_cast<qint64>(bounded.height());
  if (pixel_count < kParallelPackPixelThreshold || bounded.height() <= 1) {
    return pack_range(0, bounded.height());
  }

  // Share persistent workers with color/effect stages. Each row still checks
  // cancellation; the scheduler joins every callback before output can escape.
  photo_editor::pipeline::run_cpu_parallel_for(bounded.height(), [&](int y) {
    (void)pack_range(y, y + 1);
  });
  return !cancellation.cancelled();
}

[[nodiscard]] inline bool pack_working_rows_to_rgba32(
    const WorkingImage& working,
    const QRect& source_rect,
    uchar* output_bits,
    qsizetype output_bytes_per_line,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (working.isNull() || !source_rect.isValid() || output_bits == nullptr ||
      output_bytes_per_line <= 0) {
    return false;
  }
  const auto bounded =
      source_rect.intersected(QRect(0, 0, working.width, working.height));
  if (!bounded.isValid() || bounded.width() <= 0 || bounded.height() <= 0) {
    return false;
  }
  const auto pack_range =
      [&working, bounded, output_bits, output_bytes_per_line, cancellation](
          int begin_y, int end_y) {
        for (int y = begin_y; y < end_y; ++y) {
          if (cancellation.cancelled()) {
            return false;
          }
          const auto source_y = bounded.y() + y;
          const auto source_offset = static_cast<std::size_t>(
              source_y * working.width + bounded.x());
          auto* scanline = reinterpret_cast<float*>(
              output_bits + static_cast<qsizetype>(y) * output_bytes_per_line);
          for (int x = 0; x < bounded.width(); ++x) {
            const auto& pixel =
                working.pixels[source_offset + static_cast<std::size_t>(x)];
            const auto offset = static_cast<std::size_t>(x) * 4U;
            scanline[offset] = std::max(0.0F, pixel.red);
            scanline[offset + 1U] = std::max(0.0F, pixel.green);
            scanline[offset + 2U] = std::max(0.0F, pixel.blue);
            scanline[offset + 3U] =
                static_cast<float>(photo_editor::pipeline::clamp_unit(pixel.alpha));
          }
        }
        return true;
      };
  constexpr auto kParallelPackPixelThreshold = qint64{200'000};
  const auto pixel_count =
      static_cast<qint64>(bounded.width()) * static_cast<qint64>(bounded.height());
  if (pixel_count < kParallelPackPixelThreshold || bounded.height() <= 1) {
    return pack_range(0, bounded.height());
  }
  // Share persistent workers with color/effect stages. Each row still checks
  // cancellation; the scheduler joins every callback before output can escape.
  photo_editor::pipeline::run_cpu_parallel_for(bounded.height(), [&](int y) {
    (void)pack_range(y, y + 1);
  });
  return !cancellation.cancelled();
}

[[nodiscard]] inline bool high_precision_publication_format(
    QImage::Format format) {
  return format == QImage::Format_RGBA64 ||
         format == QImage::Format_RGBA64_Premultiplied ||
         format == QImage::Format_RGBA32FPx4 ||
         format == QImage::Format_RGBA32FPx4_Premultiplied;
}

[[nodiscard]] inline QImage::Format working_publication_format(
    QImage::Format output_format,
    int width,
    int height) {
  if (output_format == QImage::Format_ARGB32 && width == 1 && height == 1) {
    // Qt 6.7's one-pixel RGBA16F -> ARGB32 conversion can corrupt otherwise
    // valid colors. Keep this narrow publication in float32 until Qt performs
    // the final conversion; normal-sized output remains on the half-float path.
    return QImage::Format_RGBA32FPx4;
  }
  return high_precision_publication_format(output_format)
             ? QImage::Format_RGBA32FPx4
             : QImage::Format_RGBA16FPx4;
}

[[nodiscard]] inline bool pack_working_rows_for_publication(
    const WorkingImage& working,
    const QRect& source_rect,
    QImage* image,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (image == nullptr) {
    return false;
  }
  return image->format() == QImage::Format_RGBA32FPx4
             ? pack_working_rows_to_rgba32(
                   working, source_rect, image->bits(), image->bytesPerLine(),
                   cancellation)
             : pack_working_rows_to_rgba16(
                   working, source_rect, image->bits(), image->bytesPerLine(),
                   cancellation);
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
  return QColorSpace(
      QPointF(0.3127, 0.3290),
      QPointF(0.7080, 0.2920),
      QPointF(0.1700, 0.7970),
      QPointF(0.1310, 0.0460),
      QColorSpace::TransferFunction::Linear);
#endif
}

[[nodiscard]] inline QColorSpace linear_adobe_rgb_color_space() {
  return QColorSpace(
      QColorSpace::Primaries::AdobeRgb,
      QColorSpace::TransferFunction::Linear);
}

[[nodiscard]] inline QColorSpace linear_builtin_color_space(
    const photo_editor::pipeline::RgbColorSpace& color_space) {
  const auto profile = photo_editor::pipeline::builtin_rgb_icc_profile_payload(
      color_space);
  if (profile.empty()) {
    return {};
  }
  return QColorSpace::fromIccProfile(QByteArray(
      reinterpret_cast<const char*>(profile.data()),
      static_cast<qsizetype>(profile.size())));
}

[[nodiscard]] inline QColorSpace linear_acescg_color_space() {
  static const auto color_space = linear_builtin_color_space(
      photo_editor::pipeline::make_acescg_color_space());
  return color_space;
}

[[nodiscard]] inline QColorSpace linear_aces2065_1_color_space() {
  static const auto color_space = linear_builtin_color_space(
      photo_editor::pipeline::make_aces2065_1_color_space());
  return color_space;
}

[[nodiscard]] inline QImage converted_to_color_space(
    QImage image,
    const QColorSpace& target_color_space,
    QImage::Format output_format) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  return convert_tagged_qimage_color_space(
      image,
      target_color_space,
      output_format,
      QImageColorConversionPurpose::OutputPublication);
#else
  auto conversion_source = image;
  if (high_precision_publication_format(output_format) ||
      output_format == QImage::Format_RGBA16FPx4 ||
      output_format == QImage::Format_RGBA16FPx4_Premultiplied) {
    // Qt versions before 6.8 cannot request the converted image format in the
    // same operation. Promote first so an 8-bit source is not color-converted
    // into an 8-bit intermediate before the working buffer is widened.
    conversion_source = image.convertToFormat(QImage::Format_RGBA32FPx4);
    if (conversion_source.isNull()) {
      return {};
    }
    conversion_source.setColorSpace(image.colorSpace());
  }
  auto converted = convert_tagged_qimage_color_space(
      conversion_source,
      target_color_space,
      QImageColorConversionPurpose::OutputPublication);
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
  if (normalized.compare(
          QStringLiteral("sRGB"),
          Qt::CaseInsensitive) == 0 ||
      normalized.compare(
          QStringLiteral("Display P3"),
          Qt::CaseInsensitive) == 0 ||
      normalized.compare(
          QStringLiteral("ProPhoto RGB"),
          Qt::CaseInsensitive) == 0 ||
      normalized.compare(
          QStringLiteral("Adobe RGB"),
          Qt::CaseInsensitive) == 0 ||
      normalized.compare(
          QStringLiteral("Rec.709"),
          Qt::CaseInsensitive) == 0 ||
      normalized.compare(
          QStringLiteral("Rec.2020"),
          Qt::CaseInsensitive) == 0) {
    return preview_color_space_for_name(normalized);
  }
  const auto canonical_name = QString::fromStdString(
      photo_editor::pipeline::normalize_working_color_profile_name(
          normalized.toStdString()));
  if (canonical_name == QStringLiteral("Linear sRGB")) {
    return linear_srgb_color_space();
  }
  if (canonical_name == QStringLiteral("Linear Display P3")) {
    return linear_display_p3_color_space();
  }
  if (canonical_name == QStringLiteral("Linear Rec.2020")) {
    return linear_rec2020_color_space();
  }
  if (canonical_name == QStringLiteral("Linear Adobe RGB")) {
    return linear_adobe_rgb_color_space();
  }
  if (canonical_name == QStringLiteral("ACEScg")) {
    return linear_acescg_color_space();
  }
  if (canonical_name == QStringLiteral("ACES2065-1")) {
    return linear_aces2065_1_color_space();
  }
  return linear_prophoto_color_space();
}

[[nodiscard]] inline bool qt_supports_exact_target_linear_profile(
    photo_editor::pipeline::ColorPrimaries primaries) {
  using photo_editor::pipeline::ColorPrimaries;
  switch (primaries) {
    case ColorPrimaries::Srgb:
    case ColorPrimaries::Rec709:
    case ColorPrimaries::DisplayP3:
    case ColorPrimaries::Rec2020:
    case ColorPrimaries::ProPhotoRgb:
      return true;
    case ColorPrimaries::Unknown:
    case ColorPrimaries::AdobeRgb:
    case ColorPrimaries::SonySGamut3:
    case ColorPrimaries::SonySGamut3Cine:
    case ColorPrimaries::AcesAp0:
    case ColorPrimaries::AcesAp1:
      return false;
  }
  return false;
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

  return converted_to_color_space(
      image,
      working_color_space(working_profile_name),
      QImage::Format_RGBA16FPx4);
}

[[nodiscard]] inline WorkingImage working_image_from_qimage(
    const QImage& image,
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB"),
    const photo_editor::pipeline::ResolvedIccProfileAsset* input_profile_asset = nullptr,
    bool preserve_device_rgb_values = false) {
  if (preserve_device_rgb_values &&
      (input_profile_asset == nullptr ||
       input_profile_asset->status ==
           photo_editor::pipeline::IccProfileAssetStatus::Unconfigured)) {
    const auto device_image = image.convertToFormat(QImage::Format_RGBA32FPx4);
    if (device_image.isNull()) {
      return {};
    }
    WorkingImage working;
    working.width = device_image.width();
    working.height = device_image.height();
    working.pixels.resize(
        static_cast<std::size_t>(working.width) *
        static_cast<std::size_t>(working.height));
    for (int y = 0; y < working.height; ++y) {
      const auto* scanline =
          reinterpret_cast<const QRgbaFloat32*>(device_image.constScanLine(y));
      for (int x = 0; x < working.width; ++x) {
        const auto& pixel = scanline[x];
        working.pixels[static_cast<std::size_t>(y * working.width + x)] =
            photo_editor::pipeline::ColorPixel{
                .red = static_cast<float>(pixel.r),
                .green = static_cast<float>(pixel.g),
                .blue = static_cast<float>(pixel.b),
                .alpha = static_cast<float>(pixel.a),
            };
      }
    }
    return working;
  }
  if (input_profile_asset != nullptr &&
      input_profile_asset->status != photo_editor::pipeline::IccProfileAssetStatus::Unconfigured) {
    if (image.isNull() || !input_profile_asset->ready()) {
      return {};
    }
    const auto device_image = image.convertToFormat(QImage::Format_RGBA32FPx4);
    if (device_image.isNull()) {
      return {};
    }
    const auto destination_space = photo_editor::pipeline::working_color_profile_from_name(
        working_profile_name.toStdString());
    const auto transform = photo_editor::pipeline::compile_icc_to_builtin_transform(
        input_profile_asset->payload_bytes(),
        destination_space.color_space,
        photo_editor::pipeline::IccTransformOptions{
            .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
            .black_point_compensation = true,
            .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
            .target = "input-to-working",
        });
    if (!transform.ready()) {
      return {};
    }
    std::vector<photo_editor::pipeline::IccRgbaFloat> transformed(
        static_cast<std::size_t>(device_image.width()) *
        static_cast<std::size_t>(device_image.height()));
    for (int y = 0; y < device_image.height(); ++y) {
      const auto* scanline = reinterpret_cast<const float*>(device_image.constScanLine(y));
      const auto row_offset = static_cast<std::size_t>(y * device_image.width());
      for (int x = 0; x < device_image.width(); ++x) {
        const auto channel_offset = static_cast<std::size_t>(x) * 4U;
        transformed[row_offset + static_cast<std::size_t>(x)] =
            photo_editor::pipeline::IccRgbaFloat{
                .red = scanline[channel_offset],
                .green = scanline[channel_offset + 1U],
                .blue = scanline[channel_offset + 2U],
                .alpha = scanline[channel_offset + 3U],
            };
      }
    }
    if (!transform.apply(transformed)) {
      return {};
    }
    WorkingImage working;
    working.width = device_image.width();
    working.height = device_image.height();
    working.pixels.resize(transformed.size());
    for (std::size_t index = 0; index < transformed.size(); ++index) {
      working.pixels[index] = photo_editor::pipeline::ColorPixel{
          .red = transformed[index].red,
          .green = transformed[index].green,
          .blue = transformed[index].blue,
          .alpha = transformed[index].alpha,
      };
    }
    return working;
  }

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
              // Signed RGB carries colors outside the chosen working gamut.
              // Only the final display/export transform owns gamut mapping.
              .red = static_cast<float>(pixel.r),
              .green = static_cast<float>(pixel.g),
              .blue = static_cast<float>(pixel.b),
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

[[nodiscard]] inline const photo_editor::pipeline::ResolvedIccProfileAsset*
custom_output_profile_asset(const WorkingImageOutputTransform& output_transform) {
  if (output_transform.color_pipeline == nullptr) {
    return nullptr;
  }
  return output_transform.target == photo_editor::pipeline::GamutWarningTarget::PreviewDisplay
             ? &output_transform.color_pipeline->preview_icc_profile_asset
             : &output_transform.color_pipeline->export_icc_profile_asset;
}

[[nodiscard]] inline QColorSpace output_color_space_for_transform(
    const QColorSpace& fallback,
    const WorkingImageOutputTransform& output_transform) {
  const auto encoded =
      encoded_device_color_space(output_transform);
  if (encoded.isValid()) {
    return encoded;
  }
  const auto* custom_profile = custom_output_profile_asset(output_transform);
  if (custom_profile == nullptr || !custom_profile->ready()) {
    return fallback.isValid() ? fallback : preview_color_space();
  }
  const auto bytes = custom_profile->payload_bytes();
  const auto custom = QColorSpace::fromIccProfile(QByteArray(
      reinterpret_cast<const char*>(bytes.data()),
      static_cast<qsizetype>(bytes.size())));
  return custom.isValid() ? custom : fallback;
}

[[nodiscard]] inline photo_editor::pipeline::IccRenderingIntent icc_rendering_intent(
    photo_editor::pipeline::RenderIntent intent) {
  return intent == photo_editor::pipeline::RenderIntent::RelativeColorimetric
             ? photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric
             : photo_editor::pipeline::IccRenderingIntent::Perceptual;
}

[[nodiscard]] inline const photo_editor::pipeline::DisplayTarget&
display_target_for_output_transform(
    const WorkingImageOutputTransform& output_transform) {
  return output_transform.target ==
                 photo_editor::pipeline::GamutWarningTarget::PreviewDisplay
             ? output_transform.color_pipeline->preview_display_target
             : output_transform.color_pipeline->export_display_target;
}

[[nodiscard]] inline const photo_editor::pipeline::OutputColorProfile&
output_profile_for_output_transform(
    const WorkingImageOutputTransform& output_transform) {
  return output_transform.target ==
                 photo_editor::pipeline::GamutWarningTarget::PreviewDisplay
             ? output_transform.color_pipeline->preview_output_profile
             : output_transform.color_pipeline->export_output_profile;
}

[[nodiscard]] inline bool soft_proof_for_output_transform(
    const WorkingImageOutputTransform& output_transform) {
  return output_transform.color_pipeline != nullptr &&
         output_transform.target ==
             photo_editor::pipeline::GamutWarningTarget::PreviewDisplay &&
         output_transform.color_pipeline->soft_proof_enabled;
}

[[nodiscard]] inline bool use_aces2_icc_composition(
    const WorkingImageOutputTransform& output_transform) {
  return output_transform.color_pipeline != nullptr &&
         photo_editor::pipeline::photographic_rendering_model_uses_aces2(
             output_transform.color_pipeline->photographic_rendering_model) &&
         output_transform.color_pipeline->render_intent ==
             photo_editor::pipeline::RenderIntent::Perceptual;
}

[[nodiscard]] inline photo_editor::pipeline::DisplayTarget
aces2_icc_interchange_target(
    const WorkingImageOutputTransform& output_transform) {
  auto target = display_target_for_output_transform(output_transform);
  target.target_name = "ACES 2 linear Rec.2020 ICC interchange";
  target.color_space = photo_editor::pipeline::make_linear_rec2020_color_space();
  return target;
}

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
inline void schedule_icc_transform_lut_prewarm(
    photo_editor::pipeline::CompiledIccTransform transform,
    photo_editor::pipeline::IccTransformLutSettings settings) {
  struct PrewarmState final {
    std::mutex mutex;
    std::set<std::string> pending;
    std::set<std::string> rejected;
    std::deque<std::string> rejection_order;
  };
  static PrewarmState state;
  const auto key = transform.description().cache_key + "|" +
                   std::to_string(settings.cube_size) + "|" +
                   std::to_string(settings.input_shaper_exponent) + "|" +
                   std::to_string(settings.p99_delta_e_00_limit) + "|" +
                   std::to_string(settings.maximum_delta_e_00_limit) + "|" +
                   std::to_string(settings.maximum_unsafe_cell_fraction);
  {
    const std::scoped_lock lock(state.mutex);
    if (state.pending.contains(key) || state.rejected.contains(key)) {
      return;
    }
    state.pending.insert(key);
  }
  auto* const pool = QThreadPool::globalInstance();
  if (pool == nullptr) {
    const std::scoped_lock lock(state.mutex);
    state.pending.erase(key);
    return;
  }
  pool->start([transform = std::move(transform), settings, key]() {
    const auto compiled =
        photo_editor::pipeline::compile_icc_transform_lut(
            transform,
            settings);
    const std::scoped_lock lock(state.mutex);
    state.pending.erase(key);
    if (compiled == nullptr) {
      constexpr auto kMaximumRememberedRejections = std::size_t{64};
      if (state.rejected.insert(key).second) {
        state.rejection_order.push_back(key);
      }
      while (state.rejection_order.size() > kMaximumRememberedRejections) {
        state.rejected.erase(state.rejection_order.front());
        state.rejection_order.pop_front();
      }
    }
  });
}
#endif

[[nodiscard]] inline QImage qimage_from_working_image_icc_rect(
    const WorkingImage& working,
    const QRect& source_rect,
    const photo_editor::pipeline::ResolvedIccProfileAsset* destination_asset,
    QImage::Format output_format,
    const WorkingImageOutputTransform& output_transform,
    photo_editor::pipeline::CancellationToken cancellation,
    WorkingImageQImageTelemetry* telemetry) {
  QElapsedTimer total_timer;
  if (telemetry != nullptr) {
    *telemetry = {};
    total_timer.start();
    telemetry->color_processor = QStringLiteral("Little CMS");
    if (destination_asset != nullptr) {
      telemetry->profile_asset_status = QString::fromLatin1(
          photo_editor::pipeline::icc_profile_asset_status_name(destination_asset->status));
      telemetry->profile_asset_id =
          QString::fromStdString(destination_asset->reference.asset_id);
      telemetry->profile_payload_sha256 =
          QString::fromStdString(destination_asset->reference.payload_sha256);
      telemetry->failure_reason = QString::fromStdString(destination_asset->failure_reason);
    } else {
      telemetry->profile_asset_status = QStringLiteral("builtin");
    }
  }
  if (working.isNull() || output_transform.color_pipeline == nullptr ||
      (destination_asset != nullptr && !destination_asset->ready())) {
    if (telemetry != nullptr && telemetry->failure_reason.isEmpty()) {
      telemetry->failure_reason = QStringLiteral("icc_profile_asset_not_ready");
    }
    return {};
  }
  const auto destination_payload =
      destination_asset != nullptr
          ? std::vector<std::byte>(
                destination_asset->payload_bytes().begin(),
                destination_asset->payload_bytes().end())
          : photo_editor::pipeline::builtin_rgb_icc_profile_payload(
                output_profile_for_output_transform(output_transform).color_space);
  if (destination_payload.empty()) {
    if (telemetry != nullptr) {
      telemetry->failure_reason = QStringLiteral("icc_destination_profile_payload_missing");
    }
    return {};
  }
  const auto bounded = source_rect.intersected(QRect(0, 0, working.width, working.height));
  if (!bounded.isValid() || bounded.width() <= 0 || bounded.height() <= 0) {
    if (telemetry != nullptr) {
      telemetry->failure_reason = QStringLiteral("icc_output_source_rect_invalid");
    }
    return {};
  }

  QElapsedTimer convert_timer;
  if (telemetry != nullptr) {
    convert_timer.start();
  }
  const auto aces2_composition = use_aces2_icc_composition(output_transform);
  const auto interchange_target = aces2_composition
                                      ? aces2_icc_interchange_target(output_transform)
                                      : photo_editor::pipeline::DisplayTarget{};
  const auto aces2_processor =
      aces2_composition
          ? photo_editor::pipeline::compile_aces2_output_processor(
                output_transform.color_pipeline->working_profile.color_space,
                interchange_target,
                "Bradford",
                photo_editor::pipeline::
                        photographic_rendering_model_uses_official_aces2(
                            output_transform.color_pipeline->photographic_rendering_model)
                    ? photo_editor::pipeline::Aces2OutputImplementation::
                          OfficialWhenAvailable
                    : photo_editor::pipeline::Aces2OutputImplementation::
                          NativeFallback,
                output_transform.color_pipeline->renderer_transition_look)
          : std::shared_ptr<const photo_editor::pipeline::Aces2OutputProcessor>{};
  const auto icc_source_color_space =
      aces2_composition
          ? photo_editor::pipeline::make_linear_rec2020_color_space()
          : output_transform.color_pipeline->working_profile.color_space;
  const auto source_payload =
      photo_editor::pipeline::builtin_rgb_icc_profile_payload(icc_source_color_space);
  const auto soft_proof = soft_proof_for_output_transform(output_transform);
  std::vector<std::byte> proof_payload;
  if (soft_proof) {
    const auto& proof_asset = output_transform.color_pipeline->proof_icc_profile_asset;
    if (proof_asset.status !=
        photo_editor::pipeline::IccProfileAssetStatus::Unconfigured) {
      if (!proof_asset.ready()) {
        if (telemetry != nullptr) {
          telemetry->failure_reason = QString::fromStdString(
              proof_asset.failure_reason.empty()
                  ? "icc_proof_profile_asset_not_ready"
                  : proof_asset.failure_reason);
        }
        return {};
      }
      const auto bytes = proof_asset.payload_bytes();
      proof_payload.assign(bytes.begin(), bytes.end());
    } else {
      proof_payload = photo_editor::pipeline::builtin_rgb_icc_profile_payload(
          output_transform.color_pipeline->proof_output_profile.color_space);
    }
  }
  auto transform = photo_editor::pipeline::CompiledIccTransform{};
  if (soft_proof) {
    const auto proof_options = photo_editor::pipeline::IccProofTransformOptions{
        .rendering_intent = icc_rendering_intent(
            output_transform.color_pipeline->render_intent),
        .proofing_intent =
            output_transform.color_pipeline->proof_simulate_paper_white
                ? photo_editor::pipeline::IccRenderingIntent::AbsoluteColorimetric
                : icc_rendering_intent(
                      output_transform.color_pipeline->proof_render_intent),
        .black_point_compensation =
            output_transform.color_pipeline->black_point_compensation_enabled &&
            !output_transform.color_pipeline->proof_simulate_paper_white &&
            !output_transform.color_pipeline->proof_simulate_ink_black,
        .gamut_check = false,
        .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
        .target = aces2_composition
                      ? "soft-proof:aces2-rec2020-interchange"
                      : "soft-proof",
    };
    transform = photo_editor::pipeline::compile_icc_proofing_transform(
        source_payload,
        proof_payload,
        destination_payload,
        proof_options);
  } else {
    auto options = preview_device_icc_options(*output_transform.color_pipeline, aces2_composition);
    if (output_transform.target != photo_editor::pipeline::GamutWarningTarget::PreviewDisplay) {
      options.target = aces2_composition ? "export-output:aces2-rec2020-interchange" : "export-output";
    }
    transform = photo_editor::pipeline::compile_icc_transform(
        source_payload, destination_payload, options);
  }
  if (telemetry != nullptr) {
    if (aces2_processor != nullptr) {
      telemetry->color_processor = QStringLiteral("%1 + Little CMS")
                                       .arg(QString::fromStdString(std::string(
                                           photo_editor::pipeline::
                                               aces2_output_processor_backend_name(
                                                   *aces2_processor))));
    }
    if (soft_proof) {
      telemetry->color_processor += QStringLiteral(" Soft Proof");
    }
    telemetry->transform_cache_key =
        aces2_processor != nullptr
            ? QString::fromStdString(
                  aces2_processor->cache_key + "->" +
                  transform.description().cache_key)
            : QString::fromStdString(transform.description().cache_key);
  }
  if (!transform.ready()) {
    if (telemetry != nullptr) {
      telemetry->failure_reason = QString::fromStdString(transform.description().failure_reason);
    }
    return {};
  }

  std::vector<photo_editor::pipeline::IccRgbaFloat> transformed_pixels(
      static_cast<std::size_t>(bounded.width()) * static_cast<std::size_t>(bounded.height()));
  for (int y = 0; y < bounded.height(); ++y) {
    if (cancellation.cancelled()) {
      return {};
    }
    const auto source_offset = static_cast<std::size_t>(
        (bounded.y() + y) * working.width + bounded.x());
    const auto destination_offset = static_cast<std::size_t>(y * bounded.width());
    for (int x = 0; x < bounded.width(); ++x) {
      const auto& source = working.pixels[source_offset + static_cast<std::size_t>(x)];
      const auto rendered =
          aces2_processor != nullptr
              ? photo_editor::pipeline::aces2_render_to_linear_display_rgb(
                    {source.red, source.green, source.blue},
                    aces2_processor->parameters)
              : std::array<double, 3>{
                    static_cast<double>(source.red),
                    static_cast<double>(source.green),
                    static_cast<double>(source.blue),
                };
      transformed_pixels[destination_offset + static_cast<std::size_t>(x)] =
          photo_editor::pipeline::IccRgbaFloat{
              .red = static_cast<float>(rendered[0]),
              .green = static_cast<float>(rendered[1]),
              .blue = static_cast<float>(rendered[2]),
              .alpha = source.alpha,
          };
    }
  }
  if (soft_proof && output_transform.generate_proof_gamut_warning_mask &&
      telemetry != nullptr) {
    const auto gamut_transform =
        photo_editor::pipeline::compile_icc_proofing_transform(
            source_payload,
            proof_payload,
            destination_payload,
            photo_editor::pipeline::IccProofTransformOptions{
                .rendering_intent = icc_rendering_intent(
                    output_transform.color_pipeline->render_intent),
                .proofing_intent =
                    output_transform.color_pipeline->proof_simulate_paper_white
                        ? photo_editor::pipeline::IccRenderingIntent::AbsoluteColorimetric
                        : icc_rendering_intent(
                              output_transform.color_pipeline->proof_render_intent),
                .black_point_compensation =
                    output_transform.color_pipeline->black_point_compensation_enabled &&
                    !output_transform.color_pipeline->proof_simulate_paper_white &&
                    !output_transform.color_pipeline->proof_simulate_ink_black,
                .gamut_check = true,
                .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
                .target = aces2_composition
                              ? "soft-proof:aces2-rec2020-interchange:gamut-mask"
                              : "soft-proof:gamut-mask",
            });
    const auto mask = gamut_transform.gamut_mask(transformed_pixels);
    if (mask.size() == transformed_pixels.size()) {
      telemetry->gamut_warning_mask =
          QImage(bounded.width(), bounded.height(), QImage::Format_Grayscale8);
      if (!telemetry->gamut_warning_mask.isNull()) {
        for (int y = 0; y < bounded.height(); ++y) {
          auto* row = telemetry->gamut_warning_mask.scanLine(y);
          const auto offset = static_cast<std::size_t>(y * bounded.width());
          std::copy_n(mask.data() + offset, bounded.width(), row);
        }
      }
    }
  }
  auto transformed_on_metal = false;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = photo_editor::pipeline::metal::shared_metal_pipeline_backend();
  if (metal.available()) {
    const auto lut_settings =
        photo_editor::pipeline::icc_transform_lut_settings(
            photo_editor::pipeline::IccTransformLutQuality::Final);
    auto transform_lut =
        photo_editor::pipeline::find_cached_icc_transform_lut(
            transform,
            lut_settings);
    if (transform_lut == nullptr &&
        output_transform.target ==
            photo_editor::pipeline::GamutWarningTarget::PreviewDisplay) {
      schedule_icc_transform_lut_prewarm(transform, lut_settings);
      if (telemetry != nullptr) {
        telemetry->icc_lut_status = QStringLiteral("prewarming-exact-fallback");
      }
    } else if (transform_lut == nullptr) {
      transform_lut = photo_editor::pipeline::compile_icc_transform_lut(
          transform,
          lut_settings);
    }
    if (transform_lut != nullptr) {
      auto apply_stats = photo_editor::pipeline::IccTransformLutApplyStats{};
      transformed_on_metal = metal.apply_icc_transform_lut(
          transformed_pixels,
          bounded.width(),
          bounded.height(),
          *transform_lut,
          transform,
          &apply_stats);
      if (telemetry != nullptr) {
        telemetry->icc_lut_cache_key =
            QString::fromStdString(transform_lut->cache_key);
        telemetry->icc_lut_pixels =
            static_cast<qint64>(apply_stats.lut_pixels);
        telemetry->icc_exact_fallback_pixels =
            static_cast<qint64>(apply_stats.exact_fallback_pixels);
        telemetry->icc_lut_status =
            transformed_on_metal
                ? QStringLiteral("metal-hybrid")
                : QStringLiteral("metal-failed-exact-fallback");
        if (transformed_on_metal) {
          telemetry->color_processor += QStringLiteral(" + Measured Metal LUT");
        }
      }
    }
  } else if (telemetry != nullptr) {
    telemetry->icc_lut_status = QStringLiteral("metal-unavailable-exact");
  }
#endif
  if ((!transformed_on_metal && !transform.apply(transformed_pixels)) ||
      cancellation.cancelled()) {
    if (telemetry != nullptr) {
      telemetry->failure_reason = QStringLiteral("icc_output_transform_apply_failed");
    }
    return {};
  }
  if (telemetry != nullptr) {
    telemetry->color_convert_ms = convert_timer.elapsed();
  }

  QElapsedTimer pack_timer;
  if (telemetry != nullptr) {
    pack_timer.start();
  }
  const auto profile_format = working_publication_format(
      output_format, bounded.width(), bounded.height());
  QImage profile_image(bounded.width(), bounded.height(), profile_format);
  if (profile_image.isNull()) {
    if (telemetry != nullptr) {
      telemetry->failure_reason = QStringLiteral("icc_output_image_allocation_failed");
    }
    return {};
  }
  const auto destination_color_space = QColorSpace::fromIccProfile(QByteArray(
      reinterpret_cast<const char*>(destination_payload.data()),
      static_cast<qsizetype>(destination_payload.size())));
  if (!destination_color_space.isValid()) {
    if (telemetry != nullptr) {
      telemetry->failure_reason = QStringLiteral("icc_destination_profile_tag_invalid");
    }
    return {};
  }
  profile_image.setColorSpace(destination_color_space);
  for (int y = 0; y < bounded.height(); ++y) {
    if (cancellation.cancelled()) {
      return {};
    }
    const auto row_offset = static_cast<std::size_t>(y * bounded.width());
    if (profile_format == QImage::Format_RGBA32FPx4) {
      auto* scanline = reinterpret_cast<float*>(profile_image.scanLine(y));
      for (int x = 0; x < bounded.width(); ++x) {
        const auto& pixel =
            transformed_pixels[row_offset + static_cast<std::size_t>(x)];
        const auto offset = static_cast<std::size_t>(x) * 4U;
        scanline[offset] = std::clamp(pixel.red, 0.0F, 1.0F);
        scanline[offset + 1U] = std::clamp(pixel.green, 0.0F, 1.0F);
        scanline[offset + 2U] = std::clamp(pixel.blue, 0.0F, 1.0F);
        scanline[offset + 3U] = std::clamp(pixel.alpha, 0.0F, 1.0F);
      }
    } else {
      auto* scanline = reinterpret_cast<QRgbaFloat16*>(profile_image.scanLine(y));
      for (int x = 0; x < bounded.width(); ++x) {
        const auto& pixel =
            transformed_pixels[row_offset + static_cast<std::size_t>(x)];
        scanline[x] = QRgbaFloat16{
            qfloat16(std::clamp(pixel.red, 0.0F, 1.0F)),
            qfloat16(std::clamp(pixel.green, 0.0F, 1.0F)),
            qfloat16(std::clamp(pixel.blue, 0.0F, 1.0F)),
            qfloat16(std::clamp(pixel.alpha, 0.0F, 1.0F)),
        };
      }
    }
  }
  auto result = output_format == profile_format
                    ? profile_image
                    : profile_image.convertToFormat(output_format);
  if (!result.isNull()) {
    result.setColorSpace(destination_color_space);
  }
  if (telemetry != nullptr) {
    telemetry->linear_pack_ms = pack_timer.elapsed();
    telemetry->total_ms = total_timer.elapsed();
  }
  return result;
}

[[nodiscard]] inline QImage qimage_from_working_image_builtin_output_rect(
    const WorkingImage& working,
    const QRect& source_rect,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const WorkingImageOutputTransform& output_transform,
    photo_editor::pipeline::CancellationToken cancellation,
    WorkingImageQImageTelemetry* telemetry) {
  QElapsedTimer total_timer;
  if (telemetry != nullptr) {
    *telemetry = {};
    total_timer.start();
    telemetry->color_processor = QString::fromStdString(
        photo_editor::pipeline::photographic_rendering_model_name(
            output_transform.color_pipeline->photographic_rendering_model));
    telemetry->profile_asset_status = QStringLiteral("builtin");
  }
  const auto bounded =
      source_rect.intersected(QRect(0, 0, working.width, working.height));
  if (working.isNull() || output_transform.color_pipeline == nullptr ||
      !bounded.isValid() || bounded.width() <= 0 || bounded.height() <= 0) {
    if (telemetry != nullptr) {
      telemetry->failure_reason =
          QStringLiteral("builtin_output_transform_input_invalid");
    }
    return {};
  }

  auto output = crop_working_image(working, bounded);
  if (output.isNull() || cancellation.cancelled()) {
    return {};
  }
  QElapsedTimer convert_timer;
  if (telemetry != nullptr) {
    convert_timer.start();
  }
  const auto aces_lut =
      photo_editor::pipeline::compile_aces2_output_lut_application(
          *output_transform.color_pipeline,
          output_transform.target,
          output_transform.aces2_lut_quality);
  if (aces_lut.ready()) {
    auto output_lut_applied_on_metal = false;
    auto output_lut_applied_on_cuda = false;
#if defined(PHOTO_EDITOR_HAS_CUDA_BACKEND) && PHOTO_EDITOR_HAS_CUDA_BACKEND
    auto cuda_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
    auto& cuda =
        photo_editor::pipeline::cuda::shared_cuda_pipeline_backend();
    if (output_transform.allow_cuda_output_lut && cuda.available()) {
      output_lut_applied_on_cuda = cuda.apply_aces2_output_lut_target(
          &output.pixels,
          output.width,
          output.height,
          aces_lut,
          &cuda_telemetry,
          cancellation);
    }
    if (telemetry != nullptr) {
      telemetry->output_lut.cpu_to_gpu_bytes =
          cuda_telemetry.gpu_cpu_to_gpu_bytes;
      telemetry->output_lut.gpu_to_cpu_bytes =
          cuda_telemetry.gpu_gpu_to_cpu_bytes;
      telemetry->output_lut.command_buffer_count =
          cuda_telemetry.gpu_command_buffer_count;
      telemetry->output_lut.kernel_dispatch_count =
          cuda_telemetry.gpu_kernel_dispatch_count;
    }
#endif
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    auto metal_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
    auto& metal =
        photo_editor::pipeline::metal::shared_metal_pipeline_backend();
    if (!output_lut_applied_on_cuda && output_transform.allow_metal_output_lut &&
        metal.available()) {
      output_lut_applied_on_metal = metal.apply_aces2_output_lut_target(
          &output.pixels,
          output.width,
          output.height,
          aces_lut,
          &metal_telemetry);
    }
    if (telemetry != nullptr) {
      telemetry->output_lut.cpu_to_gpu_bytes =
          metal_telemetry.gpu_cpu_to_gpu_bytes;
      telemetry->output_lut.gpu_to_cpu_bytes =
          metal_telemetry.gpu_gpu_to_cpu_bytes;
      telemetry->output_lut.command_buffer_count =
          metal_telemetry.gpu_command_buffer_count;
      telemetry->output_lut.kernel_dispatch_count =
          metal_telemetry.gpu_kernel_dispatch_count;
    }
#endif
    if (!output_lut_applied_on_cuda && !output_lut_applied_on_metal) {
      photo_editor::pipeline::
          apply_aces2_output_lut_target_application(
              output.pixels,
              aces_lut);
    }
    if (telemetry != nullptr) {
      telemetry->color_processor += output_lut_applied_on_cuda
                                        ? QStringLiteral(" Final LUT CUDA")
                                    : output_lut_applied_on_metal
                                        ? QStringLiteral(" Final LUT Metal")
                                        : QStringLiteral(" Final LUT CPU");
      telemetry->output_lut.status = output_lut_applied_on_cuda
                                         ? QStringLiteral("cuda")
                                     : output_lut_applied_on_metal
                                         ? QStringLiteral("metal")
                                         : QStringLiteral("exact-cpu-fallback");
    }
  } else {
    photo_editor::pipeline::apply_output_transform(
        output.pixels,
        *output_transform.color_pipeline,
        output_transform.target);
    if (telemetry != nullptr) {
      telemetry->color_processor += QStringLiteral(" Final Direct CPU");
      telemetry->output_lut.status = QStringLiteral("exact-cpu-fallback");
    }
  }
  if (cancellation.cancelled()) {
    return {};
  }
  if (telemetry != nullptr) {
    telemetry->color_convert_ms = convert_timer.elapsed();
  }

  QElapsedTimer pack_timer;
  if (telemetry != nullptr) {
    pack_timer.start();
  }
  const auto publication_format = working_publication_format(
      output_format, output.width, output.height);
  QImage linear_image(output.width, output.height, publication_format);
  if (linear_image.isNull()) {
    if (telemetry != nullptr) {
      telemetry->failure_reason =
          QStringLiteral("builtin_output_image_allocation_failed");
    }
    return {};
  }
  auto linear_source =
      target_color_space.isValid()
          ? target_color_space
          : preview_color_space();
  linear_source.setTransferFunction(QColorSpace::TransferFunction::Linear);
  linear_image.setColorSpace(linear_source);
  if (!pack_working_rows_for_publication(
          output,
          QRect(0, 0, output.width, output.height),
          &linear_image,
          cancellation)) {
    return {};
  }
  auto result = converted_to_color_space(
      linear_image,
      target_color_space.isValid() ? target_color_space
                                   : preview_color_space(),
      output_format);
  if (result.isNull()) {
    if (telemetry != nullptr) {
      telemetry->failure_reason =
          QStringLiteral("builtin_output_encoding_failed");
    }
    return {};
  }
  if (telemetry != nullptr) {
    telemetry->linear_pack_ms = pack_timer.elapsed();
    telemetry->total_ms = total_timer.elapsed();
  }
  return result;
}

[[nodiscard]] inline QImage qimage_from_encoded_device_working_rect(
    const WorkingImage& working,
    const QRect& source_rect,
    QImage::Format output_format,
    const WorkingImageOutputTransform& output_transform,
    photo_editor::pipeline::CancellationToken cancellation,
    WorkingImageQImageTelemetry* telemetry) {
  QElapsedTimer total_timer;
  if (telemetry != nullptr) {
    *telemetry = {};
    total_timer.start();
    telemetry->color_processor =
        QStringLiteral("Executor DeviceTransform");
  }
  const auto profile =
      encoded_device_color_space(output_transform);
  const auto bounded =
      source_rect.intersected(
          QRect(0, 0, working.width, working.height));
  if (working.isNull() || !profile.isValid() ||
      !bounded.isValid() || bounded.width() <= 0 ||
      bounded.height() <= 0) {
    if (telemetry != nullptr) {
      telemetry->failure_reason =
          QStringLiteral(
              "encoded_device_publication_input_invalid");
    }
    return {};
  }
  QElapsedTimer pack_timer;
  if (telemetry != nullptr) {
    pack_timer.start();
  }
  const auto publication_format =
      working_publication_format(
          output_format, bounded.width(), bounded.height());
  QImage encoded_image(
      bounded.width(),
      bounded.height(),
      publication_format);
  if (encoded_image.isNull() ||
      !pack_working_rows_for_publication(
          working,
          bounded,
          &encoded_image,
          cancellation)) {
    return {};
  }
  auto result =
      output_format == publication_format
          ? encoded_image
          : encoded_image.convertToFormat(output_format);
  if (result.isNull() || cancellation.cancelled()) {
    return {};
  }
  result.setColorSpace(profile);
  if (telemetry != nullptr) {
    telemetry->linear_pack_ms = pack_timer.elapsed();
    telemetry->total_ms = total_timer.elapsed();
    const auto* asset =
        output_transform.encoded_device_profile;
    telemetry->profile_asset_status =
        QString::fromLatin1(
            photo_editor::pipeline::
                icc_profile_asset_status_name(
                    asset->status));
    telemetry->profile_asset_id =
        QString::fromStdString(
            asset->reference.asset_id);
    telemetry->profile_payload_sha256 =
        QString::fromStdString(
            asset->reference.payload_sha256);
  }
  return result;
}

[[nodiscard]] inline QImage qimage_from_working_image(
    const WorkingImage& working,
    const QColorSpace& target_color_space,
    QImage::Format output_format = QImage::Format_ARGB32,
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB"),
    photo_editor::pipeline::CancellationToken cancellation = {},
    WorkingImageQImageTelemetry* telemetry = nullptr,
    WorkingImageOutputTransform output_transform = {}) {
  QElapsedTimer total_timer;
  if (telemetry != nullptr) {
    *telemetry = {};
    total_timer.start();
  }
  if (working.isNull()) {
    return {};
  }
  if (output_transform.encoded_device_profile != nullptr) {
    return qimage_from_encoded_device_working_rect(
        working,
        QRect(0, 0, working.width, working.height),
        output_format,
        output_transform,
        cancellation,
        telemetry);
  }
  const auto* custom_profile = custom_output_profile_asset(output_transform);
  if (soft_proof_for_output_transform(output_transform) ||
      (custom_profile != nullptr &&
       custom_profile->status != photo_editor::pipeline::IccProfileAssetStatus::Unconfigured)) {
    return qimage_from_working_image_icc_rect(
        working,
        QRect(0, 0, working.width, working.height),
        custom_profile != nullptr && custom_profile->status !=
                                         photo_editor::pipeline::IccProfileAssetStatus::Unconfigured
            ? custom_profile
            : nullptr,
        output_format,
        output_transform,
        cancellation,
        telemetry);
  }
  if (output_transform.color_pipeline != nullptr) {
    return qimage_from_working_image_builtin_output_rect(
        working,
        QRect(0, 0, working.width, working.height),
        target_color_space,
        output_format,
        output_transform,
        cancellation,
        telemetry);
  }

  QElapsedTimer pack_timer;
  if (telemetry != nullptr) {
    pack_timer.start();
  }
  QImage linear_image(
      working.width,
      working.height,
      working_publication_format(
          output_format, working.width, working.height));
  if (linear_image.isNull()) {
    return {};
  }
  linear_image.setColorSpace(working_color_space(working_profile_name));

  if (!pack_working_rows_for_publication(
          working, QRect(0, 0, working.width, working.height), &linear_image,
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
    const QColorSpace& target_color_space,
    QImage::Format output_format = QImage::Format_ARGB32,
    const QString& working_profile_name = QStringLiteral("Linear ProPhoto RGB"),
    photo_editor::pipeline::CancellationToken cancellation = {},
    WorkingImageQImageTelemetry* telemetry = nullptr,
    WorkingImageOutputTransform output_transform = {}) {
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
  if (output_transform.encoded_device_profile != nullptr) {
    return qimage_from_encoded_device_working_rect(
        working,
        bounded,
        output_format,
        output_transform,
        cancellation,
        telemetry);
  }
  const auto* custom_profile = custom_output_profile_asset(output_transform);
  if (soft_proof_for_output_transform(output_transform) ||
      (custom_profile != nullptr &&
       custom_profile->status != photo_editor::pipeline::IccProfileAssetStatus::Unconfigured)) {
    return qimage_from_working_image_icc_rect(
        working,
        bounded,
        custom_profile != nullptr && custom_profile->status !=
                                         photo_editor::pipeline::IccProfileAssetStatus::Unconfigured
            ? custom_profile
            : nullptr,
        output_format,
        output_transform,
        cancellation,
        telemetry);
  }
  if (output_transform.color_pipeline != nullptr) {
    return qimage_from_working_image_builtin_output_rect(
        working,
        bounded,
        target_color_space,
        output_format,
        output_transform,
        cancellation,
        telemetry);
  }

  QElapsedTimer pack_timer;
  if (telemetry != nullptr) {
    pack_timer.start();
  }
  QImage linear_image(
      bounded.width(),
      bounded.height(),
      working_publication_format(
          output_format, bounded.width(), bounded.height()));
  if (linear_image.isNull()) {
    return {};
  }
  linear_image.setColorSpace(working_color_space(working_profile_name));

  if (!pack_working_rows_for_publication(
          working, bounded, &linear_image, cancellation)) {
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
