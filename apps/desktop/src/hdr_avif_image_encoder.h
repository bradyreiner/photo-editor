#pragma once

#include "export_image_settings.h"

#include <QImage>
#include <QString>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "photo_editor/io/hdr_avif_io.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

namespace photo_editor::desktop {

struct HdrAvifEncodingResult final {
  bool valid = false;
  QString error;
};

struct HdrAvifPixelVerification final {
  bool valid = false;
  double maximum_delta = 0.0;
  double p99_delta = 0.0;
  double mean_delta = 0.0;
};

[[nodiscard]] inline HdrAvifPixelVerification
verify_hdr_avif_encoded_pixels(
    const QImage& rendered_output,
    const ExportImageSettings& settings,
    const std::vector<float>& decoded_encoded_rgb) {
  HdrAvifPixelVerification result;
  const auto expected_float =
      rendered_output.format() == QImage::Format_RGBA32FPx4
          ? rendered_output
          : rendered_output.convertToFormat(QImage::Format_RGBA32FPx4);
  const auto expected_count =
      static_cast<std::size_t>(expected_float.width()) *
      static_cast<std::size_t>(expected_float.height()) * 3U;
  if (expected_float.isNull() ||
      decoded_encoded_rgb.size() != expected_count) {
    return result;
  }
  constexpr double kHlgA = 0.17883277;
  constexpr double kHlgB = 1.0 - 4.0 * kHlgA;
  const auto hlg_c = 0.5 - kHlgA * std::log(4.0 * kHlgA);
  const auto hlg = settings.hdr_transfer.compare(
                       QStringLiteral("HLG"),
                       Qt::CaseInsensitive) == 0;
  const auto encode_channel = [&](double value) {
    if (!hlg) {
      constexpr double m1 = 2610.0 / 16384.0;
      constexpr double m2 = 2523.0 / 32.0;
      constexpr double c1 = 3424.0 / 4096.0;
      constexpr double c2 = 2413.0 / 128.0;
      constexpr double c3 = 2392.0 / 128.0;
      const auto absolute = std::clamp(
          value * settings.reference_white_nits / 10000.0,
          0.0,
          1.0);
      const auto powered = std::pow(absolute, m1);
      return std::pow(
          (c1 + c2 * powered) / (1.0 + c3 * powered),
          m2);
    }
    const auto target_relative = std::clamp(
        value * settings.reference_white_nits /
            settings.peak_luminance_nits,
        0.0,
        1.0);
    return target_relative <= (1.0 / 12.0)
               ? std::sqrt(3.0 * target_relative)
               : kHlgA *
                         std::log(
                             12.0 * target_relative - kHlgB) +
                     hlg_c;
  };
  long double sum = 0.0;
  auto maximum = 0.0;
  std::vector<double> differences;
  differences.reserve(expected_count);
  for (auto y = 0; y < expected_float.height(); ++y) {
    const auto* line = reinterpret_cast<const float*>(
        expected_float.constScanLine(y));
    for (auto x = 0; x < expected_float.width(); ++x) {
      const auto destination =
          (static_cast<std::size_t>(y) *
               static_cast<std::size_t>(expected_float.width()) +
           static_cast<std::size_t>(x)) *
          3U;
      for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const auto expected = encode_channel(
            static_cast<double>(
                line[static_cast<std::size_t>(x) * 4U + channel]));
        const auto actual = static_cast<double>(
            decoded_encoded_rgb[destination + channel]);
        if (!std::isfinite(expected) || !std::isfinite(actual)) {
          return result;
        }
        const auto difference = std::abs(expected - actual);
        differences.push_back(difference);
        sum += difference;
        maximum = std::max(maximum, difference);
      }
    }
  }
  result.maximum_delta = maximum;
  std::sort(differences.begin(), differences.end());
  const auto p99_index = std::min(
      differences.size() - 1U,
      static_cast<std::size_t>(
          std::ceil(0.99 * static_cast<double>(differences.size())) -
          1.0));
  result.p99_delta = differences[p99_index];
  result.mean_delta = static_cast<double>(
      sum / static_cast<long double>(expected_count));
  result.valid =
      result.maximum_delta <= 0.25 && result.p99_delta <= 0.04 &&
      result.mean_delta <= 0.01;
  return result;
}

[[nodiscard]] inline photo_editor::pipeline::DisplayTarget
hdr_display_target_for_export_settings(
    const ExportImageSettings& options) {
  return photo_editor::pipeline::DisplayTarget{
      .target_name = QStringLiteral("Rec.2100 %1 %2 nits")
                         .arg(options.hdr_transfer)
                         .arg(options.peak_luminance_nits, 0, 'f', 0)
                         .toStdString(),
      .color_space = photo_editor::pipeline::make_linear_rec2020_color_space(),
      .eotf = options.hdr_transfer.compare(
                  QStringLiteral("HLG"),
                  Qt::CaseInsensitive) == 0
                  ? photo_editor::pipeline::DisplayEotf::Hlg
                  : photo_editor::pipeline::DisplayEotf::Pq,
      .peak_luminance_nits = options.peak_luminance_nits,
      .black_luminance_nits = options.black_luminance_nits,
      .reference_white_nits = options.reference_white_nits,
      .surround_luminance_nits = 5.0,
      .surround = photo_editor::pipeline::DisplaySurround::Dim,
      .mastering_mode =
          photo_editor::pipeline::DisplayMasteringMode::HdrDisplayReferred,
  };
}

[[nodiscard]] inline HdrAvifEncodingResult
write_prepared_hdr_avif_export_image(
    const QImage& image,
    const QString& output_path,
    const ExportImageSettings& options,
    photo_editor::pipeline::CancellationToken cancellation = {}) {
  HdrAvifEncodingResult result;
  if (cancellation.cancelled()) {
    result.error = QStringLiteral("hdr_avif_write_canceled");
    return result;
  }
  if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
      !options.hdr_delivery || options.writer_format != "avif-hdr") {
    result.error = QStringLiteral("invalid_hdr_avif_export_input");
    return result;
  }
  const auto float_image =
      image.format() == QImage::Format_RGBA32FPx4
          ? image
          : image.convertToFormat(QImage::Format_RGBA32FPx4);
  if (float_image.isNull() ||
      float_image.bytesPerLine() %
              static_cast<qsizetype>(sizeof(float)) !=
          0) {
    result.error = QStringLiteral("invalid_hdr_avif_float_surface");
    return result;
  }
  const auto component_count = static_cast<std::size_t>(
      float_image.bytesPerLine() /
      static_cast<qsizetype>(sizeof(float)) *
      float_image.height());
  const auto* components =
      reinterpret_cast<const float*>(float_image.constBits());
  std::string failure_reason;
  result.valid = photo_editor::io::write_hdr_avif_rec2020(
      output_path.toStdString(),
      std::span<const float>(components, component_count),
      photo_editor::io::HdrAvifWriteOptions{
          .width = float_image.width(),
          .height = float_image.height(),
          .row_stride_floats = static_cast<std::size_t>(
              float_image.bytesPerLine() /
              static_cast<qsizetype>(sizeof(float))),
          .bit_depth = options.bit_depth,
          .quality = options.jpeg_quality,
          .transfer = options.hdr_transfer.compare(
                          QStringLiteral("HLG"),
                          Qt::CaseInsensitive) == 0
                          ? photo_editor::io::HdrAvifTransfer::Hlg
                          : photo_editor::io::HdrAvifTransfer::Pq,
          .peak_luminance_nits = options.peak_luminance_nits,
          .black_luminance_nits = options.black_luminance_nits,
          .reference_white_nits = options.reference_white_nits,
          .max_content_light_level = options.max_content_light_level,
          .max_frame_average_light_level =
              options.max_frame_average_light_level,
          .cancellation_requested = [cancellation]() {
            return cancellation.cancelled();
          },
      },
      &failure_reason);
  if (!result.valid) {
    result.error = QString::fromStdString(failure_reason);
    if (result.error.isEmpty()) {
      result.error = QStringLiteral("hdr_avif_export_write_failed");
    }
  }
  return result;
}

}  // namespace photo_editor::desktop
