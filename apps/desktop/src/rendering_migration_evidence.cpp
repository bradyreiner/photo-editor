#include "rendering_migration_evidence.h"

#include "export_image_encoder.h"
#include "hdr_avif_image_encoder.h"
#include "qimage_color_conversion.h"
#include "bounded_qimage_reader.h"
#include "working_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>

#include "photo_editor/pipeline/color_metrics.h"

namespace photo_editor::desktop {
namespace {

[[nodiscard]] double srgb_to_linear(double value) {
  const auto normalized = std::clamp(value, 0.0, 1.0);
  return normalized <= 0.04045
             ? normalized / 12.92
             : std::pow((normalized + 0.055) / 1.055, 2.4);
}

[[nodiscard]] photo_editor::pipeline::CieLab pixel_lab(QRgb pixel) {
  const auto red = srgb_to_linear(static_cast<double>(qRed(pixel)) / 255.0);
  const auto green = srgb_to_linear(static_cast<double>(qGreen(pixel)) / 255.0);
  const auto blue = srgb_to_linear(static_cast<double>(qBlue(pixel)) / 255.0);
  return photo_editor::pipeline::cie_xyz_to_lab(
      {
          .x = 0.4124564 * red + 0.3575761 * green + 0.1804375 * blue,
          .y = 0.2126729 * red + 0.7151522 * green + 0.0721750 * blue,
          .z = 0.0193339 * red + 0.1191920 * green + 0.9503041 * blue,
      },
      photo_editor::pipeline::cie_d65_white());
}

[[nodiscard]] QByteArray image_hash(const QImage& image) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(QByteArray::number(image.width()));
  hash.addData("x");
  hash.addData(QByteArray::number(image.height()));
  hash.addData(":");
  hash.addData(QByteArray::number(static_cast<int>(image.format())));
  for (auto y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)),
        image.bytesPerLine()));
  }
  return hash.result().toHex();
}

[[nodiscard]] double percentile(
    const std::vector<double>& sorted,
    double fraction) {
  if (sorted.empty()) {
    return 0.0;
  }
  const auto index = std::min(
      sorted.size() - 1,
      static_cast<std::size_t>(
          std::ceil(fraction * static_cast<double>(sorted.size())) - 1.0));
  return sorted[index];
}

[[nodiscard]] QRgb heatmap_pixel(double difference) {
  const auto value =
      std::clamp(difference / 10.0, 0.0, 1.0);
  const auto segment = value * 4.0;
  const auto channel = [](double input) {
    return static_cast<int>(std::lround(
        std::clamp(input, 0.0, 1.0) * 255.0));
  };
  if (segment <= 1.0) {
    return qRgb(0, 0, channel(segment));
  }
  if (segment <= 2.0) {
    return qRgb(0, channel(segment - 1.0), 255);
  }
  if (segment <= 3.0) {
    return qRgb(channel(segment - 2.0), 255,
                channel(3.0 - segment));
  }
  return qRgb(255, channel(4.0 - segment), 0);
}

[[nodiscard]] QByteArray encode_png(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) ||
      !image.save(&buffer, "PNG")) {
    return {};
  }
  return bytes;
}

[[nodiscard]] bool write_immutable_bytes(
    const QString& file_path,
    const QByteArray& bytes,
    const QString& expected_sha256,
    QString* error) {
  QFile existing(file_path);
  if (existing.exists()) {
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!existing.open(QIODevice::ReadOnly)) {
      if (error != nullptr) {
        *error = QStringLiteral(
                     "existing rendering migration artifact does not match: %1")
                     .arg(file_path);
      }
      return false;
    }
    while (!existing.atEnd()) {
      const auto chunk = existing.read(1024 * 1024);
      if (chunk.isEmpty() && existing.error() != QFile::NoError) {
        if (error != nullptr) {
          *error = QStringLiteral(
                       "existing rendering migration artifact could not be read: %1")
                       .arg(file_path);
        }
        return false;
      }
      digest.addData(chunk);
    }
    if (QString::fromLatin1(digest.result().toHex()) !=
        expected_sha256) {
      if (error != nullptr) {
        *error = QStringLiteral(
                     "existing rendering migration artifact does not match: %1")
                     .arg(file_path);
      }
      return false;
    }
    return true;
  }
  if (bytes.isEmpty() ||
      QString::fromLatin1(
          QCryptographicHash::hash(
              bytes, QCryptographicHash::Sha256)
              .toHex()) != expected_sha256) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "rendering migration artifact bytes do not match their identity");
    }
    return false;
  }
  QSaveFile output(file_path);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(bytes) != bytes.size() ||
      !output.commit()) {
    if (error != nullptr) {
      *error = QStringLiteral(
                   "rendering migration artifact write failed: %1")
                   .arg(file_path);
    }
    return false;
  }
  return true;
}

[[nodiscard]] QVariantMap image_difference_summary(
    const QImage& candidate,
    const QImage& reference) {
  if (candidate.isNull() || reference.isNull() ||
      candidate.size() != reference.size()) {
    return {};
  }
  constexpr auto kMaximumComparisonEdge = 512;
  const auto comparison_size =
      candidate.width() > kMaximumComparisonEdge ||
              candidate.height() > kMaximumComparisonEdge
          ? candidate.size().scaled(
                kMaximumComparisonEdge,
                kMaximumComparisonEdge,
                Qt::KeepAspectRatio)
          : candidate.size();
  const auto candidate_sample =
      candidate.convertToFormat(
                   QImage::Format_ARGB32)
          .scaled(
              comparison_size,
              Qt::IgnoreAspectRatio,
              Qt::SmoothTransformation);
  const auto reference_sample =
      reference.convertToFormat(
                   QImage::Format_ARGB32)
          .scaled(
              comparison_size,
              Qt::IgnoreAspectRatio,
              Qt::SmoothTransformation);
  std::vector<double> differences;
  differences.reserve(
      static_cast<std::size_t>(comparison_size.width()) *
      static_cast<std::size_t>(comparison_size.height()));
  long double sum = 0.0;
  auto maximum = 0.0;
  for (auto y = 0; y < comparison_size.height(); ++y) {
    const auto* candidate_row =
        reinterpret_cast<const QRgb*>(
            candidate_sample.constScanLine(y));
    const auto* reference_row =
        reinterpret_cast<const QRgb*>(
            reference_sample.constScanLine(y));
    for (auto x = 0; x < comparison_size.width(); ++x) {
      const auto difference =
          photo_editor::pipeline::delta_e_2000(
              pixel_lab(candidate_row[x]),
              pixel_lab(reference_row[x]));
      if (!std::isfinite(difference)) {
        return {};
      }
      differences.push_back(difference);
      sum += difference;
      maximum = std::max(maximum, difference);
    }
  }
  std::sort(differences.begin(), differences.end());
  return QVariantMap{
      {QStringLiteral("sampleCount"),
       static_cast<qulonglong>(differences.size())},
      {QStringLiteral("meanDeltaE00"),
       static_cast<double>(
           sum /
           static_cast<long double>(differences.size()))},
      {QStringLiteral("p95DeltaE00"),
       percentile(differences, 0.95)},
      {QStringLiteral("maximumDeltaE00"), maximum},
  };
}

[[nodiscard]] QVariantMap float_target_difference_summary(
    const QImage& candidate,
    const QImage& reference) {
  if (candidate.isNull() || reference.isNull() ||
      candidate.size() != reference.size()) {
    return {};
  }
  const auto candidate_float =
      candidate.convertToFormat(QImage::Format_RGBA32FPx4);
  const auto reference_float =
      reference.convertToFormat(QImage::Format_RGBA32FPx4);
  if (candidate_float.isNull() || reference_float.isNull()) {
    return {};
  }
  constexpr auto kMaximumComparisonEdge = 512;
  const auto x_step = std::max(
      1,
      static_cast<int>(std::ceil(
          static_cast<double>(candidate.width()) /
          static_cast<double>(kMaximumComparisonEdge))));
  const auto y_step = std::max(
      1,
      static_cast<int>(std::ceil(
          static_cast<double>(candidate.height()) /
          static_cast<double>(kMaximumComparisonEdge))));
  std::vector<double> channel_differences;
  long double channel_sum = 0.0;
  long double luminance_sum = 0.0;
  auto maximum_channel_difference = 0.0;
  auto maximum_luminance_difference = 0.0;
  qulonglong pixel_count = 0U;
  for (auto y = 0; y < candidate.height(); y += y_step) {
    const auto* candidate_row =
        reinterpret_cast<const float*>(
            candidate_float.constScanLine(y));
    const auto* reference_row =
        reinterpret_cast<const float*>(
            reference_float.constScanLine(y));
    for (auto x = 0; x < candidate.width(); x += x_step) {
      const auto offset = static_cast<std::size_t>(x) * 4U;
      std::array<double, 3U> differences{};
      for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const auto candidate_value =
            static_cast<double>(candidate_row[offset + channel]);
        const auto reference_value =
            static_cast<double>(reference_row[offset + channel]);
        if (!std::isfinite(candidate_value) ||
            !std::isfinite(reference_value)) {
          return {};
        }
        differences[channel] =
            std::abs(candidate_value - reference_value);
        channel_differences.push_back(differences[channel]);
        channel_sum += differences[channel];
        maximum_channel_difference = std::max(
            maximum_channel_difference, differences[channel]);
      }
      const auto luminance_difference = std::abs(
          0.2627 *
              (static_cast<double>(candidate_row[offset]) -
               static_cast<double>(reference_row[offset])) +
          0.6780 *
              (static_cast<double>(candidate_row[offset + 1U]) -
               static_cast<double>(reference_row[offset + 1U])) +
          0.0593 *
              (static_cast<double>(candidate_row[offset + 2U]) -
               static_cast<double>(reference_row[offset + 2U])));
      luminance_sum += luminance_difference;
      maximum_luminance_difference = std::max(
          maximum_luminance_difference, luminance_difference);
      ++pixel_count;
    }
  }
  if (channel_differences.empty() || pixel_count == 0U) {
    return {};
  }
  std::sort(
      channel_differences.begin(), channel_differences.end());
  return QVariantMap{
      {QStringLiteral("samplePixelCount"), pixel_count},
      {QStringLiteral("comparisonDomain"),
       QStringLiteral("linear-rec2020-reference-relative")},
      {QStringLiteral("meanAbsoluteChannelDifference"),
       static_cast<double>(
           channel_sum /
           static_cast<long double>(channel_differences.size()))},
      {QStringLiteral("p95AbsoluteChannelDifference"),
       percentile(channel_differences, 0.95)},
      {QStringLiteral("maximumAbsoluteChannelDifference"),
       maximum_channel_difference},
      {QStringLiteral("meanAbsoluteLuminanceDifference"),
       static_cast<double>(
           luminance_sum / static_cast<long double>(pixel_count))},
      {QStringLiteral("maximumAbsoluteLuminanceDifference"),
       maximum_luminance_difference},
  };
}

[[nodiscard]] QImage hdr_diagnostic_sdr_proxy(
    const QImage& image) {
  const auto source =
      image.convertToFormat(QImage::Format_RGBA32FPx4);
  if (source.isNull()) {
    return {};
  }
  QImage mapped(source.size(), QImage::Format_RGBA32FPx4);
  if (mapped.isNull()) {
    return {};
  }
  for (auto y = 0; y < source.height(); ++y) {
    const auto* source_row =
        reinterpret_cast<const float*>(source.constScanLine(y));
    auto* mapped_row =
        reinterpret_cast<float*>(mapped.scanLine(y));
    for (auto x = 0; x < source.width(); ++x) {
      const auto offset = static_cast<std::size_t>(x) * 4U;
      for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const auto value = std::max(
            0.0F, source_row[offset + channel]);
        mapped_row[offset + channel] = value / (1.0F + value);
      }
      mapped_row[offset + 3U] = 1.0F;
    }
  }
  mapped.setColorSpace(linear_rec2020_color_space());
  return convert_tagged_qimage_color_space(
      mapped,
      QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32,
      QImageColorConversionPurpose::EvidenceMeasurement);
}

[[nodiscard]] QVariantMap encoded_signal_difference_summary(
    const std::vector<float>& candidate,
    const std::vector<float>& reference) {
  if (candidate.empty() || candidate.size() != reference.size()) {
    return {};
  }
  std::vector<double> differences;
  differences.reserve(candidate.size());
  long double sum = 0.0;
  auto maximum = 0.0;
  for (std::size_t index = 0U; index < candidate.size(); ++index) {
    if (!std::isfinite(candidate[index]) ||
        !std::isfinite(reference[index])) {
      return {};
    }
    const auto difference = std::abs(
        static_cast<double>(candidate[index]) -
        static_cast<double>(reference[index]));
    differences.push_back(difference);
    sum += difference;
    maximum = std::max(maximum, difference);
  }
  std::sort(differences.begin(), differences.end());
  return QVariantMap{
      {QStringLiteral("sampleCount"),
       static_cast<qulonglong>(differences.size())},
      {QStringLiteral("comparisonDomain"),
       QStringLiteral("decoded-rec2100-encoded-rgb-signal")},
      {QStringLiteral("meanAbsoluteDifference"),
       static_cast<double>(
           sum / static_cast<long double>(differences.size()))},
      {QStringLiteral("p95AbsoluteDifference"),
       percentile(differences, 0.95)},
      {QStringLiteral("maximumAbsoluteDifference"), maximum},
  };
}

[[nodiscard]] QVariantMap hdr_avif_inspection_document(
    const photo_editor::io::HdrAvifInspection& inspection) {
  return QVariantMap{
      {QStringLiteral("valid"), inspection.valid},
      {QStringLiteral("width"), inspection.width},
      {QStringLiteral("height"), inspection.height},
      {QStringLiteral("bitDepth"), inspection.bit_depth},
      {QStringLiteral("colorPrimaries"), inspection.color_primaries},
      {QStringLiteral("transferCharacteristics"),
       inspection.transfer_characteristics},
      {QStringLiteral("matrixCoefficients"),
       inspection.matrix_coefficients},
      {QStringLiteral("fullRange"), inspection.full_range},
      {QStringLiteral("maxContentLightLevel"),
       inspection.max_content_light_level},
      {QStringLiteral("maxFrameAverageLightLevel"),
       inspection.max_frame_average_light_level},
      {QStringLiteral("hasMasteringDisplay"),
       inspection.has_mastering_display},
      {QStringLiteral("masteringPeakLuminanceNits"),
       inspection.mastering_peak_luminance_nits},
      {QStringLiteral("masteringBlackLuminanceNits"),
       inspection.mastering_black_luminance_nits},
      {QStringLiteral("failureReason"),
       QString::fromStdString(inspection.failure_reason)},
  };
}

[[nodiscard]] bool hdr_avif_inspection_matches(
    const photo_editor::io::HdrAvifInspection& inspection,
    const ExportImageSettings& settings,
    const QSize& expected_size) {
  const auto expected_transfer =
      settings.hdr_transfer.compare(
          QStringLiteral("HLG"), Qt::CaseInsensitive) == 0
          ? 18
          : 16;
  return inspection.valid &&
         inspection.width == expected_size.width() &&
         inspection.height == expected_size.height() &&
         inspection.bit_depth == settings.bit_depth &&
         inspection.color_primaries == 9 &&
         inspection.transfer_characteristics == expected_transfer &&
         inspection.matrix_coefficients == 9 && inspection.full_range &&
         inspection.has_mastering_display &&
         inspection.max_content_light_level ==
             settings.max_content_light_level &&
         inspection.max_frame_average_light_level ==
             settings.max_frame_average_light_level &&
         std::abs(
             inspection.mastering_peak_luminance_nits -
             settings.peak_luminance_nits) <= 0.1 &&
         std::abs(
             inspection.mastering_black_luminance_nits -
             settings.black_luminance_nits) <= 0.0001 &&
         !inspection.encoded_rgb.empty();
}

[[nodiscard]] bool refresh_evidence_identity(
    RenderingMigrationEvidence* evidence,
    QString* error) {
  evidence->metrics.remove(QStringLiteral("evidenceSha256"));
  evidence->evidence_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          canonical_rendering_migration_evidence_payload(
              evidence->metrics),
          QCryptographicHash::Sha256)
          .toHex());
  evidence->metrics.insert(
      QStringLiteral("evidenceSha256"),
      evidence->evidence_hash);
  evidence->valid = evidence->evidence_hash.size() == 64;
  if (!evidence->valid && error != nullptr) {
    *error = QStringLiteral(
        "migration evidence identity generation failed");
  }
  return evidence->valid;
}

}  // namespace

RenderingMigrationEvidence build_rendering_migration_evidence(
    const QImage& candidate,
    const QImage& reference,
    const QString& candidate_model,
    const QString& reference_model,
    const QString& plan_cache_key,
    const QString& source_adjustment_payload_json) {
  RenderingMigrationEvidence result;
  if (candidate.isNull() || reference.isNull()) {
    result.error = QStringLiteral("candidate and reference renders are required");
    return result;
  }
  if (candidate.size() != reference.size()) {
    result.error = QStringLiteral("candidate and reference render sizes differ");
    return result;
  }
  if (candidate_model.trimmed().isEmpty() ||
      reference_model.trimmed().isEmpty() ||
      plan_cache_key.trimmed().isEmpty() ||
      source_adjustment_payload_json.trimmed().isEmpty()) {
    result.error =
        QStringLiteral(
            "rendering and adjustment provenance are required");
    return result;
  }

  constexpr auto kMaximumEvidenceEdge = 512;
  const auto evidence_size =
      candidate.width() > kMaximumEvidenceEdge ||
              candidate.height() > kMaximumEvidenceEdge
          ? candidate.size().scaled(
                kMaximumEvidenceEdge,
                kMaximumEvidenceEdge,
                Qt::KeepAspectRatio)
          : candidate.size();
  const auto candidate_sample =
      candidate.convertToFormat(QImage::Format_ARGB32).scaled(
          evidence_size,
          Qt::IgnoreAspectRatio,
          Qt::SmoothTransformation);
  const auto reference_sample =
      reference.convertToFormat(QImage::Format_ARGB32).scaled(
          evidence_size,
          Qt::IgnoreAspectRatio,
          Qt::SmoothTransformation);

  std::vector<double> differences;
  differences.reserve(
      static_cast<std::size_t>(evidence_size.width()) *
      static_cast<std::size_t>(evidence_size.height()));
  long double sum = 0.0;
  auto maximum = 0.0;
  auto candidate_shadow_clips = 0;
  auto candidate_highlight_clips = 0;
  auto reference_shadow_clips = 0;
  auto reference_highlight_clips = 0;
  auto over_one_delta_e00 = 0;
  auto over_two_delta_e00 = 0;
  auto over_five_delta_e00 = 0;
  QImage heatmap(
      evidence_size, QImage::Format_ARGB32);
  for (auto y = 0; y < evidence_size.height(); ++y) {
    const auto* candidate_row =
        reinterpret_cast<const QRgb*>(candidate_sample.constScanLine(y));
    const auto* reference_row =
        reinterpret_cast<const QRgb*>(reference_sample.constScanLine(y));
    for (auto x = 0; x < evidence_size.width(); ++x) {
      const auto candidate_pixel = candidate_row[x];
      const auto reference_pixel = reference_row[x];
      const auto difference = photo_editor::pipeline::delta_e_2000(
          pixel_lab(candidate_pixel),
          pixel_lab(reference_pixel));
      if (!std::isfinite(difference)) {
        result.error = QStringLiteral("comparison produced a non-finite difference");
        return result;
      }
      differences.push_back(difference);
      sum += difference;
      maximum = std::max(maximum, difference);
      heatmap.setPixel(x, y, heatmap_pixel(difference));
      over_one_delta_e00 += difference > 1.0 ? 1 : 0;
      over_two_delta_e00 += difference > 2.0 ? 1 : 0;
      over_five_delta_e00 += difference > 5.0 ? 1 : 0;
      const auto candidate_luma =
          0.2126 * qRed(candidate_pixel) +
          0.7152 * qGreen(candidate_pixel) +
          0.0722 * qBlue(candidate_pixel);
      const auto reference_luma =
          0.2126 * qRed(reference_pixel) +
          0.7152 * qGreen(reference_pixel) +
          0.0722 * qBlue(reference_pixel);
      candidate_shadow_clips += candidate_luma <= 1.0 ? 1 : 0;
      candidate_highlight_clips += candidate_luma >= 254.0 ? 1 : 0;
      reference_shadow_clips += reference_luma <= 1.0 ? 1 : 0;
      reference_highlight_clips += reference_luma >= 254.0 ? 1 : 0;
    }
  }
  std::sort(differences.begin(), differences.end());
  const auto candidate_export =
      candidate.convertToFormat(QImage::Format_ARGB32);
  const auto reference_export =
      reference.convertToFormat(QImage::Format_ARGB32);
  result.candidate_export_png =
      encode_png(candidate_export);
  result.reference_export_png =
      encode_png(reference_export);
  result.delta_e00_heatmap_png = encode_png(heatmap);
  const auto candidate_round_trip =
      QImage::fromData(
          result.candidate_export_png, "PNG")
          .convertToFormat(QImage::Format_ARGB32);
  const auto reference_round_trip =
      QImage::fromData(
          result.reference_export_png, "PNG")
          .convertToFormat(QImage::Format_ARGB32);
  if (result.candidate_export_png.isEmpty() ||
      result.reference_export_png.isEmpty() ||
      result.delta_e00_heatmap_png.isEmpty() ||
      candidate_round_trip.isNull() ||
      reference_round_trip.isNull()) {
    result.error =
        QStringLiteral("migration evidence PNG encoding failed");
    return result;
  }
  const auto sample_count = static_cast<qulonglong>(differences.size());
  const auto candidate_export_hash =
      QCryptographicHash::hash(
          result.candidate_export_png,
          QCryptographicHash::Sha256)
          .toHex();
  const auto reference_export_hash =
      QCryptographicHash::hash(
          result.reference_export_png,
          QCryptographicHash::Sha256)
          .toHex();
  const auto heatmap_hash =
      QCryptographicHash::hash(
          result.delta_e00_heatmap_png,
          QCryptographicHash::Sha256)
          .toHex();
  const auto candidate_canonical_hash =
      image_hash(candidate_export);
  const auto reference_canonical_hash =
      image_hash(reference_export);
  QVariantMap shipping_format_evidence;
  const auto shipping_formats = std::array{
      std::pair{
          QStringLiteral("jpeg-8"),
          export_image_settings_for_preset(
              QStringLiteral("JPEG"))},
      std::pair{
          QStringLiteral("tiff-16"),
          export_image_settings_for_preset(
              QStringLiteral("TIFF 16-bit"))},
  };
  for (const auto& [format_identity, settings] :
       shipping_formats) {
    const auto prepared_candidate =
        prepare_image_for_export_encoding(
            candidate, settings);
    const auto prepared_reference =
        prepare_image_for_export_encoding(
            reference, settings);
    const auto description = QStringLiteral(
        "Software=Looksy;MigrationEvidence");
    const auto encoded_candidate =
        encode_prepared_standard_export_image(
            prepared_candidate,
            settings,
            description);
    const auto encoded_reference =
        encode_prepared_standard_export_image(
            prepared_reference,
            settings,
            description);
    QVariantMap format_document{
        {QStringLiteral("writerFormat"),
         QString::fromLatin1(settings.writer_format)},
        {QStringLiteral("bitDepth"),
         settings.bit_depth},
        {QStringLiteral("jpegQuality"),
         settings.jpeg_quality},
    };
    if (!encoded_candidate.valid ||
        !encoded_reference.valid) {
      format_document.insert(
          QStringLiteral("available"), false);
      format_document.insert(
          QStringLiteral("failureReason"),
          !encoded_candidate.valid
              ? encoded_candidate.error
              : encoded_reference.error);
      shipping_format_evidence.insert(
          format_identity, format_document);
      continue;
    }
    const ScopedQImageReaderAllocationLimit allocation_limit(
        prepared_candidate.size(),
        settings.bit_depth > 8 ? 8 : 4);
    if (!allocation_limit.valid()) {
      format_document.insert(QStringLiteral("available"), false);
      format_document.insert(
          QStringLiteral("failureReason"),
          QStringLiteral(
              "shipping format exceeds bounded decode memory"));
      shipping_format_evidence.insert(
          format_identity, format_document);
      continue;
    }
    const auto decoded_candidate =
        QImage::fromData(encoded_candidate.bytes);
    const auto decoded_reference =
        QImage::fromData(encoded_reference.bytes);
    const auto candidate_round_trip_difference =
        image_difference_summary(
            decoded_candidate,
            prepared_candidate);
    const auto reference_round_trip_difference =
        image_difference_summary(
            decoded_reference,
            prepared_reference);
    const auto encoded_candidate_reference_difference =
        image_difference_summary(
            decoded_candidate,
            decoded_reference);
    if (candidate_round_trip_difference.isEmpty() ||
        reference_round_trip_difference.isEmpty() ||
        encoded_candidate_reference_difference.isEmpty()) {
      format_document.insert(
          QStringLiteral("available"), false);
      format_document.insert(
          QStringLiteral("failureReason"),
          QStringLiteral(
              "shipping format could not be decoded for comparison"));
      shipping_format_evidence.insert(
          format_identity, format_document);
      continue;
    }
    result.candidate_shipping_format_bytes.insert(
        format_identity,
        encoded_candidate.bytes);
    result.reference_shipping_format_bytes.insert(
        format_identity,
        encoded_reference.bytes);
    format_document.insert(
        QStringLiteral("available"), true);
    format_document.insert(
        QStringLiteral("candidateSha256"),
        QString::fromLatin1(
            QCryptographicHash::hash(
                encoded_candidate.bytes,
                QCryptographicHash::Sha256)
                .toHex()));
    format_document.insert(
        QStringLiteral("referenceSha256"),
        QString::fromLatin1(
            QCryptographicHash::hash(
                encoded_reference.bytes,
                QCryptographicHash::Sha256)
                .toHex()));
    format_document.insert(
        QStringLiteral("candidateBytes"),
        encoded_candidate.bytes.size());
    format_document.insert(
        QStringLiteral("referenceBytes"),
        encoded_reference.bytes.size());
    format_document.insert(
        QStringLiteral("candidateRoundTrip"),
        candidate_round_trip_difference);
    format_document.insert(
        QStringLiteral("referenceRoundTrip"),
        reference_round_trip_difference);
    format_document.insert(
        QStringLiteral("candidateVsReference"),
        encoded_candidate_reference_difference);
    shipping_format_evidence.insert(
        format_identity, format_document);
  }
  result.metrics = {
      {QStringLiteral("schema"),
       QStringLiteral("photo-editor-rendering-migration-evidence:v2")},
      {QStringLiteral("candidateModel"), candidate_model},
      {QStringLiteral("referenceModel"), reference_model},
      {QStringLiteral("planCacheKey"), plan_cache_key},
      {QStringLiteral("sampleWidth"), evidence_size.width()},
      {QStringLiteral("sampleHeight"), evidence_size.height()},
      {QStringLiteral("sampleCount"), sample_count},
      {QStringLiteral("fullResolutionWidth"),
       candidate.width()},
      {QStringLiteral("fullResolutionHeight"),
       candidate.height()},
      {QStringLiteral("meanDeltaE00"),
       static_cast<double>(sum / static_cast<long double>(differences.size()))},
      {QStringLiteral("p95DeltaE00"), percentile(differences, 0.95)},
      {QStringLiteral("maximumDeltaE00"), maximum},
      {QStringLiteral("pixelsOverDeltaE00One"),
       over_one_delta_e00},
      {QStringLiteral("pixelsOverDeltaE00Two"),
       over_two_delta_e00},
      {QStringLiteral("pixelsOverDeltaE00Five"),
       over_five_delta_e00},
      {QStringLiteral("candidateShadowClipCount"), candidate_shadow_clips},
      {QStringLiteral("candidateHighlightClipCount"), candidate_highlight_clips},
      {QStringLiteral("referenceShadowClipCount"), reference_shadow_clips},
      {QStringLiteral("referenceHighlightClipCount"), reference_highlight_clips},
      {QStringLiteral("candidateImageSha256"),
       QString::fromLatin1(image_hash(candidate_sample))},
      {QStringLiteral("referenceImageSha256"),
       QString::fromLatin1(image_hash(reference_sample))},
      {QStringLiteral("candidateFullImageSha256"),
       QString::fromLatin1(image_hash(candidate))},
      {QStringLiteral("referenceFullImageSha256"),
       QString::fromLatin1(image_hash(reference))},
      {QStringLiteral("candidateEncodedPngSha256"),
       QString::fromLatin1(candidate_export_hash)},
      {QStringLiteral("referenceEncodedPngSha256"),
       QString::fromLatin1(reference_export_hash)},
      {QStringLiteral("deltaE00HeatmapPngSha256"),
       QString::fromLatin1(heatmap_hash)},
      {QStringLiteral("candidateEncodedPngBytes"),
       result.candidate_export_png.size()},
      {QStringLiteral("referenceEncodedPngBytes"),
       result.reference_export_png.size()},
      {QStringLiteral("deltaE00HeatmapPngBytes"),
       result.delta_e00_heatmap_png.size()},
      {QStringLiteral("candidateEncodedRoundTripImageSha256"),
       QString::fromLatin1(image_hash(candidate_round_trip))},
      {QStringLiteral("referenceEncodedRoundTripImageSha256"),
       QString::fromLatin1(image_hash(reference_round_trip))},
      {QStringLiteral("candidateEncodedRoundTripExact"),
       image_hash(candidate_round_trip) == candidate_canonical_hash},
      {QStringLiteral("referenceEncodedRoundTripExact"),
       image_hash(reference_round_trip) == reference_canonical_hash},
      {QStringLiteral("deltaE00HeatmapScaleMaximum"), 10.0},
      {QStringLiteral("shippingFormatEvidence"),
       shipping_format_evidence},
      {QStringLiteral("targetComparisonEvidence"),
       QVariantMap{
           {QStringLiteral("srgb-simulation"),
            QVariantMap{
                {QStringLiteral("outputColorSpace"),
                 QStringLiteral("sRGB")},
                {QStringLiteral("planCacheKey"),
                 plan_cache_key},
                {QStringLiteral("artifactKind"),
                 QStringLiteral("base-export")},
                {QStringLiteral("candidateSha256"),
                 QString::fromLatin1(candidate_export_hash)},
                {QStringLiteral("referenceSha256"),
                 QString::fromLatin1(reference_export_hash)},
                {QStringLiteral("candidateVsReference"),
                 QVariantMap{
                     {QStringLiteral("sampleCount"), sample_count},
                     {QStringLiteral("meanDeltaE00"),
                      static_cast<double>(
                          sum /
                          static_cast<long double>(differences.size()))},
                     {QStringLiteral("p95DeltaE00"),
                      percentile(differences, 0.95)},
                     {QStringLiteral("maximumDeltaE00"), maximum},
                 }},
            }},
       }},
      {QStringLiteral("sourceAdjustmentPayloadSha256"),
       QString::fromLatin1(
           QCryptographicHash::hash(
               source_adjustment_payload_json.toUtf8(),
               QCryptographicHash::Sha256)
               .toHex())},
  };
  const auto evidence_payload =
      QJsonDocument::fromVariant(result.metrics).toJson(QJsonDocument::Compact);
  result.evidence_hash = QString::fromLatin1(
      QCryptographicHash::hash(evidence_payload, QCryptographicHash::Sha256)
          .toHex());
  result.metrics.insert(QStringLiteral("evidenceSha256"), result.evidence_hash);
  result.valid = result.evidence_hash.size() == 64;
  if (!result.valid) {
    result.error = QStringLiteral("evidence hash generation failed");
  }
  return result;
}

bool write_immutable_rendering_migration_evidence_artifacts(
    const QString& directory_path,
    const RenderingMigrationEvidence& evidence,
    QStringList* artifact_file_names,
    QString* error) {
  if (!evidence.valid ||
      directory_path.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "valid migration evidence and artifact directory are required");
    }
    return false;
  }
  struct ArtifactSpec final {
    QString label;
    QString extension;
    QString hash;
    QByteArray bytes;
  };
  std::vector<ArtifactSpec> artifact_specs{
      ArtifactSpec{
          .label = QStringLiteral("candidate-export"),
          .extension = QStringLiteral("png"),
          .hash = evidence.metrics
                      .value(QStringLiteral(
                          "candidateEncodedPngSha256"))
                      .toString(),
          .bytes = evidence.candidate_export_png},
      ArtifactSpec{
          .label = QStringLiteral("reference-export"),
          .extension = QStringLiteral("png"),
          .hash = evidence.metrics
                      .value(QStringLiteral(
                          "referenceEncodedPngSha256"))
                      .toString(),
          .bytes = evidence.reference_export_png},
      ArtifactSpec{
          .label = QStringLiteral("delta-e00-heatmap"),
          .extension = QStringLiteral("png"),
          .hash = evidence.metrics
                      .value(QStringLiteral(
                          "deltaE00HeatmapPngSha256"))
                      .toString(),
          .bytes = evidence.delta_e00_heatmap_png},
  };
  const auto shipping_formats =
      evidence.metrics
          .value(QStringLiteral("shippingFormatEvidence"))
          .toMap();
  for (auto iterator = shipping_formats.cbegin();
       iterator != shipping_formats.cend();
       ++iterator) {
    const auto format = iterator.value().toMap();
    if (!format.value(QStringLiteral("available"))
             .toBool()) {
      continue;
    }
    const auto extension =
        format.value(QStringLiteral("writerFormat"))
            .toString();
    artifact_specs.push_back(
        ArtifactSpec{
            .label = QStringLiteral("candidate-%1")
                         .arg(iterator.key()),
            .extension = extension,
            .hash = format
                        .value(QStringLiteral("candidateSha256"))
                        .toString(),
            .bytes = evidence
                         .candidate_shipping_format_bytes
                         .value(iterator.key()),
        });
    artifact_specs.push_back(
        ArtifactSpec{
            .label = QStringLiteral("reference-%1")
                         .arg(iterator.key()),
            .extension = extension,
            .hash = format
                        .value(QStringLiteral("referenceSha256"))
                        .toString(),
            .bytes = evidence
                         .reference_shipping_format_bytes
                         .value(iterator.key()),
        });
  }
  const auto target_comparisons =
      evidence.metrics
          .value(QStringLiteral("targetComparisonEvidence"))
          .toMap();
  for (auto iterator = target_comparisons.cbegin();
       iterator != target_comparisons.cend();
       ++iterator) {
    const auto target = iterator.value().toMap();
    if (target.value(QStringLiteral("artifactKind"))
            .toString() != QStringLiteral("dedicated")) {
      continue;
    }
    artifact_specs.push_back(
        ArtifactSpec{
            .label = QStringLiteral("candidate-target-%1")
                         .arg(iterator.key()),
            .extension = QStringLiteral("png"),
            .hash = target
                        .value(QStringLiteral("candidateSha256"))
                        .toString(),
            .bytes = evidence.candidate_target_png_bytes
                         .value(iterator.key()),
        });
    artifact_specs.push_back(
        ArtifactSpec{
            .label = QStringLiteral("reference-target-%1")
                         .arg(iterator.key()),
            .extension = QStringLiteral("png"),
            .hash = target
                        .value(QStringLiteral("referenceSha256"))
                        .toString(),
            .bytes = evidence.reference_target_png_bytes
                         .value(iterator.key()),
        });
  }
  const auto structure_attributions =
      evidence.metrics
          .value(QStringLiteral("structureAttributionEvidence"))
          .toMap();
  for (auto iterator = structure_attributions.cbegin();
       iterator != structure_attributions.cend();
       ++iterator) {
    const auto attribution = iterator.value().toMap();
    if (!attribution.value(QStringLiteral("available"))
             .toBool()) {
      continue;
    }
    artifact_specs.push_back(
        ArtifactSpec{
            .label = QStringLiteral("candidate-without-%1")
                         .arg(iterator.key()),
            .extension = QStringLiteral("png"),
            .hash = attribution
                        .value(QStringLiteral("candidateSha256"))
                        .toString(),
            .bytes = evidence
                         .candidate_attribution_png_bytes
                         .value(iterator.key()),
        });
    artifact_specs.push_back(
        ArtifactSpec{
            .label = QStringLiteral("reference-without-%1")
                         .arg(iterator.key()),
            .extension = QStringLiteral("png"),
            .hash = attribution
                        .value(QStringLiteral("referenceSha256"))
                        .toString(),
            .bytes = evidence
                         .reference_attribution_png_bytes
                         .value(iterator.key()),
        });
  }
  QDir directory;
  if (!directory.mkpath(directory_path)) {
    if (error != nullptr) {
      *error = QStringLiteral(
                   "rendering migration artifact directory could not be created: %1")
                   .arg(directory_path);
    }
    return false;
  }
  QStringList file_names;
  for (const auto& artifact : artifact_specs) {
    if (artifact.hash.size() != 64 ||
        artifact.extension.trimmed().isEmpty()) {
      if (error != nullptr) {
        *error = QStringLiteral(
            "rendering migration evidence artifact is incomplete");
      }
      return false;
    }
    const auto file_name =
        QStringLiteral("%1-sha256-%2.%3")
            .arg(
                artifact.label,
                artifact.hash,
                artifact.extension);
    const auto file_path =
        QDir(directory_path).filePath(file_name);
    if (artifact.bytes.isEmpty() &&
        !QFileInfo::exists(file_path)) {
      if (error != nullptr) {
        *error = QStringLiteral(
            "rendering migration evidence artifact is incomplete");
      }
      return false;
    }
    if (!write_immutable_bytes(
            file_path,
            artifact.bytes,
            artifact.hash,
            error)) {
      return false;
    }
    file_names.append(file_name);
  }
  file_names.sort();
  if (artifact_file_names != nullptr) {
    *artifact_file_names = file_names;
  }
  return true;
}

bool bind_rendering_migration_evidence_context(
    RenderingMigrationEvidence* evidence,
    const QVariantMap& context,
    QString* error) {
  if (evidence == nullptr ||
      !evidence->valid ||
      context.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "valid migration evidence and context are required");
    }
    return false;
  }
  evidence->metrics.insert(
      QStringLiteral("migrationContext"),
      context);
  evidence->metrics.remove(
      QStringLiteral("evidenceSha256"));
  evidence->evidence_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          canonical_rendering_migration_evidence_payload(
              evidence->metrics),
          QCryptographicHash::Sha256)
          .toHex());
  evidence->metrics.insert(
      QStringLiteral("evidenceSha256"),
      evidence->evidence_hash);
  evidence->valid =
      evidence->evidence_hash.size() == 64;
  if (!evidence->valid && error != nullptr) {
    *error = QStringLiteral(
        "migration evidence context binding failed");
  }
  return evidence->valid;
}

bool add_rendering_migration_target_comparison(
    RenderingMigrationEvidence* evidence,
    const QString& target_identity,
    const QString& output_color_space,
    const QString& plan_cache_key,
    const QImage& candidate,
    const QImage& reference,
    QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (evidence == nullptr || !evidence->valid ||
      target_identity.trimmed().isEmpty() ||
      target_identity.contains(QLatin1Char('/')) ||
      output_color_space.trimmed().isEmpty() ||
      plan_cache_key.trimmed().isEmpty() ||
      candidate.isNull() || reference.isNull() ||
      candidate.size() != reference.size()) {
    return fail(QStringLiteral(
        "valid target comparison inputs are required"));
  }
  const auto candidate_png = encode_png(candidate);
  const auto reference_png = encode_png(reference);
  const auto candidate_round_trip =
      QImage::fromData(candidate_png, "PNG");
  const auto reference_round_trip =
      QImage::fromData(reference_png, "PNG");
  const auto candidate_srgb =
      candidate.colorSpace().isValid()
          ? convert_tagged_qimage_color_space(
                candidate,
                QColorSpace(QColorSpace::SRgb),
                QImageColorConversionPurpose::
                    EvidenceMeasurement)
          : candidate;
  const auto reference_srgb =
      reference.colorSpace().isValid()
          ? convert_tagged_qimage_color_space(
                reference,
                QColorSpace(QColorSpace::SRgb),
                QImageColorConversionPurpose::
                    EvidenceMeasurement)
          : reference;
  const auto difference = image_difference_summary(
      candidate_srgb, reference_srgb);
  if (candidate_png.isEmpty() || reference_png.isEmpty() ||
      candidate_round_trip.isNull() ||
      reference_round_trip.isNull() || difference.isEmpty()) {
    return fail(QStringLiteral(
        "target comparison encoding or measurement failed"));
  }
  const auto candidate_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          candidate_png, QCryptographicHash::Sha256)
          .toHex());
  const auto reference_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          reference_png, QCryptographicHash::Sha256)
          .toHex());
  auto comparisons = evidence->metrics
                         .value(QStringLiteral(
                             "targetComparisonEvidence"))
                         .toMap();
  comparisons.insert(
      target_identity,
      QVariantMap{
          {QStringLiteral("outputColorSpace"),
           output_color_space},
          {QStringLiteral("planCacheKey"), plan_cache_key},
          {QStringLiteral("artifactKind"),
           QStringLiteral("dedicated")},
          {QStringLiteral("candidateSha256"), candidate_hash},
          {QStringLiteral("referenceSha256"), reference_hash},
          {QStringLiteral("candidateBytes"), candidate_png.size()},
          {QStringLiteral("referenceBytes"), reference_png.size()},
          {QStringLiteral("candidateRoundTripExact"),
           image_hash(candidate_round_trip) == image_hash(candidate)},
          {QStringLiteral("referenceRoundTripExact"),
           image_hash(reference_round_trip) == image_hash(reference)},
          {QStringLiteral("candidateVsReference"), difference},
      });
  evidence->candidate_target_png_bytes.insert(
      target_identity, candidate_png);
  evidence->reference_target_png_bytes.insert(
      target_identity, reference_png);
  evidence->metrics.insert(
      QStringLiteral("targetComparisonEvidence"), comparisons);
  evidence->metrics.remove(QStringLiteral("evidenceSha256"));
  evidence->evidence_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          canonical_rendering_migration_evidence_payload(
              evidence->metrics),
          QCryptographicHash::Sha256)
          .toHex());
  evidence->metrics.insert(
      QStringLiteral("evidenceSha256"),
      evidence->evidence_hash);
  evidence->valid = evidence->evidence_hash.size() == 64;
  return evidence->valid ||
         fail(QStringLiteral(
             "target comparison identity generation failed"));
}

bool add_rendering_migration_hdr_avif_comparison(
    RenderingMigrationEvidence* evidence,
    const QString& target_identity,
    const QString& plan_cache_key,
    const QString& hdr_export_preset,
    const QImage& candidate,
    const QImage& reference,
    QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  const auto settings =
      export_image_settings_for_preset(hdr_export_preset);
  if (evidence == nullptr || !evidence->valid ||
      target_identity.trimmed().isEmpty() ||
      target_identity.contains(QLatin1Char('/')) ||
      plan_cache_key.trimmed().isEmpty() ||
      !settings.hdr_delivery || settings.writer_format != "avif-hdr" ||
      candidate.isNull() || reference.isNull() ||
      candidate.size() != reference.size()) {
    return fail(QStringLiteral(
        "valid HDR AVIF comparison inputs are required"));
  }
  const auto candidate_proxy = hdr_diagnostic_sdr_proxy(candidate);
  const auto reference_proxy = hdr_diagnostic_sdr_proxy(reference);
  const auto diagnostic_difference = image_difference_summary(
      candidate_proxy, reference_proxy);
  const auto linear_difference = float_target_difference_summary(
      candidate, reference);
  if (diagnostic_difference.isEmpty() || linear_difference.isEmpty()) {
    return fail(QStringLiteral(
        "HDR target diagnostic measurement failed"));
  }
  auto diagnostic_document = diagnostic_difference;
  diagnostic_document.insert(
      QStringLiteral("comparisonDomain"),
      QStringLiteral(
          "fixed-reinhard-sdr-diagnostic-proxy-not-hdr-approval"));
  const auto format_identity = QStringLiteral("%1-avif-10")
                                   .arg(settings.hdr_transfer.toLower());
  auto target_comparisons =
      evidence->metrics
          .value(QStringLiteral("targetComparisonEvidence"))
          .toMap();
  target_comparisons.insert(
      target_identity,
      QVariantMap{
          {QStringLiteral("available"), true},
          {QStringLiteral("outputColorSpace"),
           QStringLiteral("Rec.2100 %1").arg(settings.hdr_transfer)},
          {QStringLiteral("planCacheKey"), plan_cache_key},
          {QStringLiteral("artifactKind"),
           QStringLiteral("shipping-avif")},
          {QStringLiteral("candidateFullImageSha256"),
           QString::fromLatin1(image_hash(candidate))},
          {QStringLiteral("referenceFullImageSha256"),
           QString::fromLatin1(image_hash(reference))},
          {QStringLiteral("candidateVsReference"),
           diagnostic_document},
          {QStringLiteral("linearTargetDifference"),
           linear_difference},
          {QStringLiteral("hdrPerceptualApproval"), false},
      });
  evidence->metrics.insert(
      QStringLiteral("targetComparisonEvidence"), target_comparisons);

  QVariantMap format_document{
      {QStringLiteral("writerFormat"), QStringLiteral("avif")},
      {QStringLiteral("productionWriterFormat"),
       QString::fromLatin1(settings.writer_format)},
      {QStringLiteral("bitDepth"), settings.bit_depth},
      {QStringLiteral("quality"), settings.jpeg_quality},
      {QStringLiteral("transfer"), settings.hdr_transfer},
      {QStringLiteral("colorPrimaries"), 9},
      {QStringLiteral("matrixCoefficients"), 9},
      {QStringLiteral("fullRange"), true},
      {QStringLiteral("peakLuminanceNits"),
       settings.peak_luminance_nits},
      {QStringLiteral("blackLuminanceNits"),
       settings.black_luminance_nits},
      {QStringLiteral("referenceWhiteNits"),
       settings.reference_white_nits},
      {QStringLiteral("maxContentLightLevel"),
       settings.max_content_light_level},
      {QStringLiteral("maxFrameAverageLightLevel"),
       settings.max_frame_average_light_level},
      {QStringLiteral("targetPlanCacheKey"), plan_cache_key},
      {QStringLiteral("candidateVsReference"),
       diagnostic_document},
      {QStringLiteral("linearTargetDifference"),
       linear_difference},
      {QStringLiteral("backendVersion"),
       QString::fromLatin1(
           photo_editor::io::hdr_avif_backend_version())},
  };
  auto shipping_formats =
      evidence->metrics
          .value(QStringLiteral("shippingFormatEvidence"))
          .toMap();
  if (!photo_editor::io::hdr_avif_backend_available()) {
    format_document.insert(QStringLiteral("available"), false);
    format_document.insert(
        QStringLiteral("failureReason"),
        QStringLiteral("hdr_avif_backend_unavailable"));
    shipping_formats.insert(format_identity, format_document);
    evidence->metrics.insert(
        QStringLiteral("shippingFormatEvidence"), shipping_formats);
    return refresh_evidence_identity(evidence, error);
  }

  QTemporaryDir temporary_directory(
      QStringLiteral("photo-editor-migration-hdr-avif-XXXXXX"));
  if (!temporary_directory.isValid()) {
    return fail(QStringLiteral(
        "HDR AVIF evidence temporary directory could not be created"));
  }
  const auto candidate_path =
      temporary_directory.filePath(QStringLiteral("candidate.avif"));
  const auto reference_path =
      temporary_directory.filePath(QStringLiteral("reference.avif"));
  const auto candidate_write = write_prepared_hdr_avif_export_image(
      candidate, candidate_path, settings);
  const auto reference_write = write_prepared_hdr_avif_export_image(
      reference, reference_path, settings);
  if (!candidate_write.valid || !reference_write.valid) {
    format_document.insert(QStringLiteral("available"), false);
    format_document.insert(
        QStringLiteral("failureReason"),
        !candidate_write.valid
            ? candidate_write.error
            : reference_write.error);
    shipping_formats.insert(format_identity, format_document);
    evidence->metrics.insert(
        QStringLiteral("shippingFormatEvidence"), shipping_formats);
    return refresh_evidence_identity(evidence, error);
  }
  QFile candidate_file(candidate_path);
  QFile reference_file(reference_path);
  if (!candidate_file.open(QIODevice::ReadOnly) ||
      !reference_file.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral(
        "HDR AVIF evidence artifact could not be read"));
  }
  const auto candidate_bytes = candidate_file.readAll();
  const auto reference_bytes = reference_file.readAll();
  const auto candidate_inspection = photo_editor::io::inspect_hdr_avif(
      candidate_path.toStdString(), true);
  const auto reference_inspection = photo_editor::io::inspect_hdr_avif(
      reference_path.toStdString(), true);
  format_document.insert(
      QStringLiteral("candidateInspection"),
      hdr_avif_inspection_document(candidate_inspection));
  format_document.insert(
      QStringLiteral("referenceInspection"),
      hdr_avif_inspection_document(reference_inspection));
  const auto candidate_contract = hdr_avif_inspection_matches(
      candidate_inspection, settings, candidate.size());
  const auto reference_contract = hdr_avif_inspection_matches(
      reference_inspection, settings, reference.size());
  const auto candidate_pixel_verification =
      verify_hdr_avif_encoded_pixels(
          candidate,
          settings,
          candidate_inspection.encoded_rgb);
  const auto reference_pixel_verification =
      verify_hdr_avif_encoded_pixels(
          reference,
          settings,
          reference_inspection.encoded_rgb);
  const auto encoded_signal_difference =
      encoded_signal_difference_summary(
          candidate_inspection.encoded_rgb,
          reference_inspection.encoded_rgb);
  format_document.insert(
      QStringLiteral("candidatePixelVerification"),
      QVariantMap{
          {QStringLiteral("valid"),
           candidate_pixel_verification.valid},
          {QStringLiteral("meanAbsoluteDifference"),
           candidate_pixel_verification.mean_delta},
          {QStringLiteral("p99AbsoluteDifference"),
           candidate_pixel_verification.p99_delta},
          {QStringLiteral("maximumAbsoluteDifference"),
           candidate_pixel_verification.maximum_delta},
      });
  format_document.insert(
      QStringLiteral("referencePixelVerification"),
      QVariantMap{
          {QStringLiteral("valid"),
           reference_pixel_verification.valid},
          {QStringLiteral("meanAbsoluteDifference"),
           reference_pixel_verification.mean_delta},
          {QStringLiteral("p99AbsoluteDifference"),
           reference_pixel_verification.p99_delta},
          {QStringLiteral("maximumAbsoluteDifference"),
           reference_pixel_verification.maximum_delta},
      });
  if (candidate_bytes.isEmpty() || reference_bytes.isEmpty() ||
      !candidate_contract || !reference_contract ||
      !candidate_pixel_verification.valid ||
      !reference_pixel_verification.valid ||
      encoded_signal_difference.isEmpty()) {
    format_document.insert(QStringLiteral("available"), false);
    format_document.insert(
        QStringLiteral("failureReason"),
        QStringLiteral("hdr_avif_artifact_contract_failed"));
    shipping_formats.insert(format_identity, format_document);
    evidence->metrics.insert(
        QStringLiteral("shippingFormatEvidence"), shipping_formats);
    return refresh_evidence_identity(evidence, error);
  }
  const auto candidate_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          candidate_bytes, QCryptographicHash::Sha256)
          .toHex());
  const auto reference_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          reference_bytes, QCryptographicHash::Sha256)
          .toHex());
  evidence->candidate_shipping_format_bytes.insert(
      format_identity, candidate_bytes);
  evidence->reference_shipping_format_bytes.insert(
      format_identity, reference_bytes);
  format_document.insert(QStringLiteral("available"), true);
  format_document.insert(
      QStringLiteral("candidateSha256"), candidate_hash);
  format_document.insert(
      QStringLiteral("referenceSha256"), reference_hash);
  format_document.insert(
      QStringLiteral("candidateBytes"), candidate_bytes.size());
  format_document.insert(
      QStringLiteral("referenceBytes"), reference_bytes.size());
  format_document.insert(
      QStringLiteral("candidateArtifactContractVerified"), true);
  format_document.insert(
      QStringLiteral("referenceArtifactContractVerified"), true);
  format_document.insert(
      QStringLiteral("candidateRoundTrip"),
      QVariantMap{
          {QStringLiteral("comparisonDomain"),
           QStringLiteral("expected-vs-decoded-rec2100-encoded-rgb-signal")},
          {QStringLiteral("meanAbsoluteDifference"),
           candidate_pixel_verification.mean_delta},
          {QStringLiteral("p99AbsoluteDifference"),
           candidate_pixel_verification.p99_delta},
          {QStringLiteral("maximumAbsoluteDifference"),
           candidate_pixel_verification.maximum_delta},
      });
  format_document.insert(
      QStringLiteral("referenceRoundTrip"),
      QVariantMap{
          {QStringLiteral("comparisonDomain"),
           QStringLiteral("expected-vs-decoded-rec2100-encoded-rgb-signal")},
          {QStringLiteral("meanAbsoluteDifference"),
           reference_pixel_verification.mean_delta},
          {QStringLiteral("p99AbsoluteDifference"),
           reference_pixel_verification.p99_delta},
          {QStringLiteral("maximumAbsoluteDifference"),
           reference_pixel_verification.maximum_delta},
      });
  format_document.insert(
      QStringLiteral("encodedSignalDifference"),
      encoded_signal_difference);
  shipping_formats.insert(format_identity, format_document);
  evidence->metrics.insert(
      QStringLiteral("shippingFormatEvidence"), shipping_formats);
  return refresh_evidence_identity(evidence, error);
}

bool add_rendering_migration_structure_attribution(
    RenderingMigrationEvidence* evidence,
    const QString& structure_identity,
    int removed_count,
    const QString& plan_cache_key,
    const QImage& candidate,
    const QImage& reference,
    QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (evidence == nullptr || !evidence->valid ||
      structure_identity.trimmed().isEmpty() ||
      structure_identity.contains(QLatin1Char('/')) ||
      removed_count <= 0 ||
      plan_cache_key.trimmed().isEmpty() ||
      candidate.isNull() || reference.isNull() ||
      candidate.size() != reference.size()) {
    return fail(QStringLiteral(
        "valid structure attribution inputs are required"));
  }
  const auto candidate_png = encode_png(candidate);
  const auto reference_png = encode_png(reference);
  const auto candidate_round_trip =
      QImage::fromData(candidate_png, "PNG");
  const auto reference_round_trip =
      QImage::fromData(reference_png, "PNG");
  const auto difference =
      image_difference_summary(candidate, reference);
  if (candidate_png.isEmpty() || reference_png.isEmpty() ||
      candidate_round_trip.isNull() ||
      reference_round_trip.isNull() || difference.isEmpty()) {
    return fail(QStringLiteral(
        "structure attribution encoding or measurement failed"));
  }
  const auto candidate_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          candidate_png, QCryptographicHash::Sha256)
          .toHex());
  const auto reference_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          reference_png, QCryptographicHash::Sha256)
          .toHex());
  const auto difference_change_from_full = QVariantMap{
      {QStringLiteral("meanDeltaE00"),
       difference.value(QStringLiteral("meanDeltaE00"))
               .toDouble() -
           evidence->metrics
               .value(QStringLiteral("meanDeltaE00"))
               .toDouble()},
      {QStringLiteral("p95DeltaE00"),
       difference.value(QStringLiteral("p95DeltaE00"))
               .toDouble() -
           evidence->metrics
               .value(QStringLiteral("p95DeltaE00"))
               .toDouble()},
      {QStringLiteral("maximumDeltaE00"),
       difference.value(QStringLiteral("maximumDeltaE00"))
               .toDouble() -
           evidence->metrics
               .value(QStringLiteral("maximumDeltaE00"))
               .toDouble()},
  };
  auto attributions = evidence->metrics
                          .value(QStringLiteral(
                              "structureAttributionEvidence"))
                          .toMap();
  attributions.insert(
      structure_identity,
      QVariantMap{
          {QStringLiteral("available"), true},
          {QStringLiteral("removedCount"), removed_count},
          {QStringLiteral("planCacheKey"), plan_cache_key},
          {QStringLiteral("candidateSha256"), candidate_hash},
          {QStringLiteral("referenceSha256"), reference_hash},
          {QStringLiteral("candidateBytes"), candidate_png.size()},
          {QStringLiteral("referenceBytes"), reference_png.size()},
          {QStringLiteral("candidateRoundTripExact"),
           image_hash(candidate_round_trip) == image_hash(candidate)},
          {QStringLiteral("referenceRoundTripExact"),
           image_hash(reference_round_trip) == image_hash(reference)},
          {QStringLiteral("candidateVsReferenceWithoutStructure"),
           difference},
          {QStringLiteral("withoutStructureMinusFullComposite"),
           difference_change_from_full},
      });
  evidence->candidate_attribution_png_bytes.insert(
      structure_identity, candidate_png);
  evidence->reference_attribution_png_bytes.insert(
      structure_identity, reference_png);
  evidence->metrics.insert(
      QStringLiteral("structureAttributionEvidence"),
      attributions);
  return refresh_evidence_identity(evidence, error);
}

bool add_unavailable_rendering_migration_structure_attribution(
    RenderingMigrationEvidence* evidence,
    const QString& structure_identity,
    int structure_count,
    const QString& reason,
    QString* error) {
  if (evidence == nullptr || !evidence->valid ||
      structure_identity.trimmed().isEmpty() ||
      structure_identity.contains(QLatin1Char('/')) ||
      structure_count <= 0 || reason.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "valid unavailable structure attribution is required");
    }
    return false;
  }
  auto attributions = evidence->metrics
                          .value(QStringLiteral(
                              "structureAttributionEvidence"))
                          .toMap();
  attributions.insert(
      structure_identity,
      QVariantMap{
          {QStringLiteral("available"), false},
          {QStringLiteral("structureCount"), structure_count},
          {QStringLiteral("failureReason"), reason.trimmed()},
      });
  evidence->metrics.insert(
      QStringLiteral("structureAttributionEvidence"),
      attributions);
  return refresh_evidence_identity(evidence, error);
}

QByteArray canonical_rendering_migration_evidence_payload(
    const QVariantMap& evidence) {
  auto payload = evidence;
  payload.remove(QStringLiteral("evidenceSha256"));
  payload.remove(QStringLiteral("migrationPending"));
  payload.remove(QStringLiteral("approvalTime"));
  return QJsonDocument::fromVariant(payload)
      .toJson(QJsonDocument::Compact);
}

}  // namespace photo_editor::desktop
