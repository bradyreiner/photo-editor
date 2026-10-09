#include "job_runner.h"
#include "job_output_transaction.h"

#include "bounded_qimage_reader.h"

#include "app_bootstrap.h"
#include "export_image_encoder.h"
#include "hdr_avif_image_encoder.h"
#include "export_image_settings.h"
#include "edit_adjustment_patch.h"
#include "observability.h"
#include "timeline_asset_store.h"
#include "timeline_document_controller.h"
#include "worker_queue_registry.h"
#include "photo_cache_budget.h"
#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

#include "photo_editor/io/openexr_io.h"
#include "photo_editor/io/hdr_avif_io.h"
#include "photo_editor/io/tiff_metadata_io.h"
#include "photo_editor/pipeline/color_transform.h"

#include <QtConcurrent>

#include <QDateTime>
#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QUuid>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <span>

#include "edit_recipe_state.h"
#include "job_store.h"
#include "photo_memory_limits.h"
#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "working_image.h"

namespace photo_editor::desktop {

namespace {

struct PreviewExportDiff final {
  bool available = false;
  bool within_tolerance = false;
  int width = 0;
  int height = 0;
  int max_channel_delta = 0;
  double mean_channel_delta = 0.0;
  int tolerance = 1;
  QString status;
};

struct ExportArtifactVerification final {
  bool valid = false;
  QString format;
  QString failure_reason;
  int width = 0;
  int height = 0;
  int bit_depth = 0;
  bool pixels_verified = false;
  bool profile_verified = false;
  bool transfer_function_verified = false;
  bool metadata_verified = false;
  bool metadata_applicable = false;
  bool alpha_verified = false;
  bool orientation_verified = false;
  bool component_association_verified = false;
  bool mastering_fields_verified = false;
  bool mastering_fields_applicable = false;
  int maximum_pixel_delta = 0;
  double p99_pixel_delta = 0.0;
  double mean_pixel_delta = 0.0;
  qint64 file_bytes = 0;
  QImage decoded_image;
};

class CancellationAwareSaveDevice final : public QIODevice {
 public:
  CancellationAwareSaveDevice(
      const QString& path,
      photo_editor::pipeline::CancellationToken cancellation)
      : file_(path), cancellation_(cancellation) {}

  bool openForWrite() {
    if (cancellation_.cancelled() ||
        !file_.open(QIODevice::WriteOnly)) {
      return false;
    }
    QIODevice::open(QIODevice::WriteOnly);
    return true;
  }

  bool commit() {
    QIODevice::close();
    return !cancellation_.cancelled() && file_.commit();
  }

  [[nodiscard]] bool isSequential() const override {
    return file_.isSequential();
  }

  [[nodiscard]] qint64 size() const override {
    return file_.size();
  }

  bool seek(qint64 position) override {
    return !cancellation_.cancelled() &&
           file_.seek(position) &&
           QIODevice::seek(position);
  }

 protected:
  qint64 readData(char*, qint64) override {
    return -1;
  }

  qint64 writeData(const char* data, qint64 length) override {
    return cancellation_.cancelled()
               ? -1
               : file_.write(data, length);
  }

 private:
  QSaveFile file_;
  photo_editor::pipeline::CancellationToken cancellation_;
};

QString full_file_sha256(const QString& file_path, photo_editor::pipeline::CancellationToken cancellation = {}) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    if (cancellation.cancelled()) return {};
    const auto bytes = file.read(1024 * 1024);
    if (bytes.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(bytes);
  }
  return QString::fromLatin1(hash.result().toHex());
}

bool remove_delivery_artifact(const QString& output_path) {
  const auto removed = QFile::remove(output_path);
  QFile::remove(output_path + QStringLiteral(".integrity.json"));
  return removed;
}

QVariantMap write_delivery_integrity(
    const QString& output_path,
    photo_editor::pipeline::CancellationToken cancellation = {},
    const QString& published_path = {}) {
  const QFileInfo output(output_path);
  const auto checksum = cancellation.cancelled()
                            ? QString{}
                            : full_file_sha256(output.absoluteFilePath(), cancellation);
  QVariantMap result{
      {QStringLiteral("schema"), QStringLiteral("delivery-integrity:v1")},
      {QStringLiteral("algorithm"), QStringLiteral("sha256-full-v1")},
      {QStringLiteral("outputPath"), published_path.isEmpty() ? output.absoluteFilePath() : published_path},
      {QStringLiteral("fileBytes"), output.size()},
      {QStringLiteral("sha256"), checksum},
      {QStringLiteral("valid"),
       !cancellation.cancelled() && output.isReadable() &&
           output.size() > 0 && !checksum.isEmpty()},
  };
  if (!result.value(QStringLiteral("valid")).toBool()) {
    return result;
  }
  const auto sidecar_path = output.absoluteFilePath() + QStringLiteral(".integrity.json");
  QSaveFile sidecar(sidecar_path);
  if (sidecar.open(QIODevice::WriteOnly)) {
    const auto manifest = QJsonObject::fromVariantMap(result);
    sidecar.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    if (!cancellation.cancelled() && sidecar.commit()) {
      result.insert(QStringLiteral("sidecarPath"), published_path.isEmpty() ? sidecar_path : published_path + QStringLiteral(".integrity.json"));
    }
  }
  return result;
}

QVariantMap export_artifact_verification_to_map(
    const ExportArtifactVerification& verification,
    const ExportImageSettings& settings) {
  return {
      {QStringLiteral("schema"),
       QStringLiteral("export-artifact-verification:v2")},
      {QStringLiteral("valid"), verification.valid},
      {QStringLiteral("format"), verification.format},
      {QStringLiteral("failureReason"), verification.failure_reason},
      {QStringLiteral("width"), verification.width},
      {QStringLiteral("height"), verification.height},
      {QStringLiteral("bitDepth"), verification.bit_depth},
      {QStringLiteral("fileBytes"), verification.file_bytes},
      {QStringLiteral("pixelsVerified"),
       verification.pixels_verified},
      {QStringLiteral("maximumPixelDelta"),
       verification.maximum_pixel_delta},
      {QStringLiteral("p99PixelDelta"),
       verification.p99_pixel_delta},
      {QStringLiteral("meanPixelDelta"),
       verification.mean_pixel_delta},
      {QStringLiteral("profileVerified"), verification.profile_verified},
      {QStringLiteral("transferFunctionVerified"),
       verification.transfer_function_verified},
      {QStringLiteral("metadataVerified"),
       verification.metadata_verified},
      {QStringLiteral("metadataApplicable"),
       verification.metadata_applicable},
      {QStringLiteral("alphaVerified"), verification.alpha_verified},
      {QStringLiteral("orientationVerified"),
       verification.orientation_verified},
      {QStringLiteral("componentAssociationVerified"),
       verification.component_association_verified},
      {QStringLiteral("masteringFieldsVerified"),
       verification.mastering_fields_verified},
      {QStringLiteral("masteringFieldsApplicable"),
       verification.mastering_fields_applicable},
      {QStringLiteral("alphaBehavior"), export_alpha_behavior(settings)},
      {QStringLiteral("componentAssociation"),
       export_component_association(settings)},
      {QStringLiteral("orientationPolicy"), export_orientation_policy()},
  };
}

int qimage_component_bit_depth(const QImage& image) {
  switch (image.format()) {
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
      return 16;
    default:
      return image.isNull() ? 0 : 8;
  }
}

bool lossless_alpha_and_association_match(
    const QImage& expected,
    const QImage& decoded) {
  if (expected.isNull() || decoded.isNull() || expected.size() != decoded.size()) {
    return false;
  }
  const auto expected_rgba = expected.convertToFormat(QImage::Format_RGBA64);
  const auto decoded_rgba = decoded.convertToFormat(QImage::Format_RGBA64);
  if (expected_rgba.isNull() || decoded_rgba.isNull()) {
    return false;
  }
  constexpr auto kMaximumSamplesPerAxis = 33;
  const auto decoded_component_bits = qimage_component_bit_depth(decoded);
  const auto x_step = std::max(1, expected.width() / kMaximumSamplesPerAxis);
  const auto y_step = std::max(1, expected.height() / kMaximumSamplesPerAxis);
  for (int y = 0; y < expected.height(); y += y_step) {
    for (int x = 0; x < expected.width(); x += x_step) {
      const auto expected_pixel = expected_rgba.pixelColor(x, y).rgba64();
      const auto decoded_pixel = decoded_rgba.pixelColor(x, y).rgba64();
      if (std::abs(static_cast<int>(expected_pixel.alpha()) -
                   static_cast<int>(decoded_pixel.alpha())) > 1) {
        qWarning().noquote()
            << "Export alpha artifact mismatch at" << x << y
            << "expected alpha" << expected_pixel.alpha()
            << "decoded alpha" << decoded_pixel.alpha();
        return false;
      }
      // RGB below fully transparent alpha is undefined in several container
      // implementations. Every visible and partially transparent sample must
      // retain unassociated component values.
      const auto alpha_fraction =
          static_cast<double>(expected_pixel.alpha()) / 65535.0;
      const auto component_tolerance =
          decoded_component_bits > 8
              ? 1
              : static_cast<int>(std::ceil(
                    257.0 / std::max(alpha_fraction, 1.0 / 255.0)));
      if (expected_pixel.alpha() != 0U &&
          (std::abs(static_cast<int>(expected_pixel.red()) -
                    static_cast<int>(decoded_pixel.red())) > component_tolerance ||
           std::abs(static_cast<int>(expected_pixel.green()) -
                    static_cast<int>(decoded_pixel.green())) > component_tolerance ||
           std::abs(static_cast<int>(expected_pixel.blue()) -
                    static_cast<int>(decoded_pixel.blue())) > component_tolerance)) {
        qWarning().noquote()
            << "Export unassociated RGB artifact mismatch at" << x << y
            << "expected" << expected_pixel.red() << expected_pixel.green()
            << expected_pixel.blue() << expected_pixel.alpha()
            << "decoded" << decoded_pixel.red() << decoded_pixel.green()
            << decoded_pixel.blue() << decoded_pixel.alpha();
        return false;
      }
    }
  }
  return true;
}

bool encoded_pixels_match(
    const QImage& expected,
    const QImage& decoded,
    bool lossy,
    int* maximum_delta,
    double* mean_delta) {
  if (expected.isNull() || decoded.isNull() ||
      expected.size() != decoded.size()) {
    return false;
  }
  const auto expected_rgba =
      expected.convertToFormat(
          QImage::Format_RGBA64);
  const auto decoded_rgba =
      decoded.convertToFormat(
          QImage::Format_RGBA64);
  if (expected_rgba.isNull() ||
      decoded_rgba.isNull()) {
    return false;
  }
  constexpr auto kMaximumSamplesPerAxis = 65;
  const auto x_step =
      std::max(
          1,
          expected.width() /
              kMaximumSamplesPerAxis);
  const auto y_step =
      std::max(
          1,
          expected.height() /
              kMaximumSamplesPerAxis);
  auto maximum = 0;
  auto total = std::uint64_t{0};
  auto samples = std::uint64_t{0};
  for (int y = 0; y < expected.height();
       y += y_step) {
    for (int x = 0; x < expected.width();
         x += x_step) {
      const auto expected_pixel =
          expected_rgba.pixelColor(x, y)
              .rgba64();
      const auto decoded_pixel =
          decoded_rgba.pixelColor(x, y)
              .rgba64();
      for (const auto delta : {
               std::abs(
                   static_cast<int>(
                       expected_pixel.red()) -
                   static_cast<int>(
                       decoded_pixel.red())),
               std::abs(
                   static_cast<int>(
                       expected_pixel.green()) -
                   static_cast<int>(
                       decoded_pixel.green())),
               std::abs(
                   static_cast<int>(
                       expected_pixel.blue()) -
                   static_cast<int>(
                       decoded_pixel.blue())),
           }) {
        maximum = std::max(maximum, delta);
        total +=
            static_cast<std::uint64_t>(
                delta);
        ++samples;
      }
    }
  }
  const auto mean =
      samples == 0U
          ? 0.0
          : static_cast<double>(total) /
                static_cast<double>(samples);
  if (maximum_delta != nullptr) {
    *maximum_delta = maximum;
  }
  if (mean_delta != nullptr) {
    *mean_delta = mean;
  }
  // Deltas are measured in 16-bit component code values. Lossless
  // paths admit only a one-code rounding difference; JPEG admits
  // bounded codec error while still proving the decoded artifact
  // corresponds to the rendered pixels.
  if (lossy) {
    return maximum <= 16384 &&
           mean <= 4096.0;
  }
  if (qimage_component_bit_depth(
          decoded) <= 8) {
    return maximum <= 128 &&
           mean <= 32.0;
  }
  return maximum <= 1;
}

float read_little_endian_float(const char* source) {
  const auto bits = qFromLittleEndian<quint32>(
      reinterpret_cast<const uchar*>(source));
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool float_rgb_pixels_match(
    const QImage& expected,
    std::span<const float> decoded_rgb,
    int decoded_channels,
    double maximum_tolerance,
    double mean_tolerance,
    int* maximum_delta,
    double* mean_delta) {
  const auto expected_float =
      expected.format() == QImage::Format_RGBA32FPx4
          ? expected
          : expected.convertToFormat(
                QImage::Format_RGBA32FPx4);
  if (expected_float.isNull() ||
      decoded_channels < 3 ||
      decoded_rgb.size() <
          static_cast<std::size_t>(
              expected.width()) *
              static_cast<std::size_t>(
                  expected.height()) *
              static_cast<std::size_t>(
                  decoded_channels)) {
    return false;
  }
  constexpr auto kMaximumSamplesPerAxis = 65;
  const auto x_step =
      std::max(
          1,
          expected.width() /
              kMaximumSamplesPerAxis);
  const auto y_step =
      std::max(
          1,
          expected.height() /
              kMaximumSamplesPerAxis);
  auto maximum = 0.0;
  auto total = 0.0;
  auto samples = std::uint64_t{0};
  for (int y = 0; y < expected.height();
       y += y_step) {
    const auto* expected_line =
        reinterpret_cast<const float*>(
            expected_float.constScanLine(y));
    for (int x = 0; x < expected.width();
         x += x_step) {
      const auto expected_offset = x * 4;
      const auto decoded_offset =
          (static_cast<std::size_t>(y) *
               static_cast<std::size_t>(
                   expected.width()) +
           static_cast<std::size_t>(x)) *
          static_cast<std::size_t>(
              decoded_channels);
      for (auto channel = 0; channel < 3;
           ++channel) {
        const auto expected_value =
            expected_line[
                expected_offset + channel];
        const auto decoded_value =
            decoded_rgb[
                decoded_offset +
                static_cast<std::size_t>(
                    channel)];
        if (!std::isfinite(expected_value) ||
            !std::isfinite(decoded_value)) {
          return false;
        }
        const auto delta = std::abs(
            static_cast<double>(
                expected_value) -
            static_cast<double>(
                decoded_value));
        maximum = std::max(maximum, delta);
        total += delta;
        ++samples;
      }
    }
  }
  const auto mean =
      samples == 0U
          ? 0.0
          : total /
                static_cast<double>(samples);
  if (maximum_delta != nullptr) {
    *maximum_delta = static_cast<int>(
        std::lround(
            std::min(maximum, 1.0) *
            65535.0));
  }
  if (mean_delta != nullptr) {
    *mean_delta = mean * 65535.0;
  }
  return maximum <= maximum_tolerance &&
         mean <= mean_tolerance;
}

ExportArtifactVerification verify_standard_export_artifact(
    const QString& output_path,
    const QImage& rendered_output,
    const ExportImageSettings& settings,
    const QJsonObject& export_metadata) {
  ExportArtifactVerification verification;
  verification.format = settings.format_name;
  verification.file_bytes = QFileInfo(output_path).size();
  const ScopedQImageReaderAllocationLimit allocation_limit(
      rendered_output.size(),
      settings.bit_depth > 8 ? 8 : 4);
  if (!allocation_limit.valid()) {
    verification.failure_reason =
        QStringLiteral("encoded_artifact_exceeds_bounded_decode_memory");
    return verification;
  }
  QImageReader reader(output_path);
  reader.setAutoTransform(false);
  if (verification.file_bytes <= 0 || !reader.canRead()) {
    verification.failure_reason = QStringLiteral("encoded_artifact_is_not_readable");
    return verification;
  }
  verification.orientation_verified =
      reader.transformation() == QImageIOHandler::TransformationNone;
  const auto encoded_description =
      settings.writer_format == "tif"
          ? QString{}
          : reader.text(
                QStringLiteral(
                    "Description"));
  verification.decoded_image = reader.read();
  if (verification.decoded_image.isNull()) {
    verification.failure_reason = QStringLiteral("encoded_artifact_decode_failed");
    return verification;
  }
  verification.width = verification.decoded_image.width();
  verification.height = verification.decoded_image.height();
  verification.bit_depth = qimage_component_bit_depth(verification.decoded_image);
  const auto dimensions_match =
      verification.decoded_image.size() == rendered_output.size();
  const auto bit_depth_matches = verification.bit_depth == settings.bit_depth;
  verification.pixels_verified =
      encoded_pixels_match(
          rendered_output,
          verification.decoded_image,
          settings.writer_format == "jpg",
          &verification.maximum_pixel_delta,
          &verification.mean_pixel_delta);
  verification.profile_verified =
      !settings.embed_profile ||
      (rendered_output.colorSpace().isValid() &&
       verification.decoded_image.colorSpace().isValid() &&
       rendered_output.colorSpace() == verification.decoded_image.colorSpace());
  verification.transfer_function_verified =
      !settings.embed_profile ||
      (rendered_output.colorSpace().isValid() &&
       verification.decoded_image.colorSpace().isValid() &&
       rendered_output.colorSpace() ==
           verification.decoded_image
               .colorSpace());
  verification.metadata_applicable = true;
  const auto expected_description =
      QStringLiteral(
          "Software=Looksy;Asset=%1")
          .arg(
              export_metadata
                  .value(
                      QStringLiteral(
                          "assetId"))
                  .toString());
  if (settings.writer_format == "tif") {
    std::string software;
    std::string description;
    verification.metadata_verified =
        photo_editor::io::
            read_tiff_export_metadata(
                output_path.toStdString(),
                &software,
                &description) &&
        (software == "Looksy" || software == "Framehouse") &&
        QString::fromStdString(
            description) ==
            expected_description;
  } else {
    verification.metadata_verified =
        encoded_description ==
        expected_description;
  }
  if (!verification.metadata_verified) {
    qWarning().noquote()
        << "Export metadata artifact mismatch for"
        << settings.writer_format
        << "keys" << reader.textKeys()
        << "description"
        << encoded_description;
  }
  verification.mastering_fields_verified = true;
  if (settings.flatten_alpha) {
    verification.alpha_verified =
        !verification.decoded_image.hasAlphaChannel();
    verification.component_association_verified =
        verification.alpha_verified;
  } else {
    verification.alpha_verified =
        verification.decoded_image.hasAlphaChannel();
    verification.component_association_verified =
        (settings.writer_format == "png" || settings.writer_format == "tif") &&
        lossless_alpha_and_association_match(
            rendered_output,
            verification.decoded_image);
  }
  verification.valid =
      dimensions_match && bit_depth_matches &&
      verification.pixels_verified &&
      verification.profile_verified &&
      verification.transfer_function_verified &&
      verification.metadata_verified &&
      verification.alpha_verified && verification.orientation_verified &&
      verification.component_association_verified;
  if (!verification.valid) {
    verification.failure_reason = QStringLiteral(
        "artifact_contract_failed:dimensions=%1,bit_depth=%2,pixels=%3,profile=%4,transfer=%5,metadata=%6,alpha=%7,orientation=%8,association=%9")
                                      .arg(dimensions_match)
                                      .arg(bit_depth_matches)
                                      .arg(verification.pixels_verified)
                                      .arg(verification.profile_verified)
                                      .arg(
                                          verification
                                              .transfer_function_verified)
                                      .arg(
                                          verification
                                              .metadata_verified)
                                      .arg(verification.alpha_verified)
                                      .arg(verification.orientation_verified)
                                      .arg(verification.component_association_verified);
  }
  return verification;
}

ExportArtifactVerification verify_pfm_export_artifact(
    const QString& output_path,
    const QImage& rendered_output,
    const ExportImageSettings& settings) {
  ExportArtifactVerification verification;
  verification.format = settings.format_name;
  verification.file_bytes = QFileInfo(output_path).size();
  QFile file(output_path);
  if (verification.file_bytes <= 0 || !file.open(QIODevice::ReadOnly) ||
      file.readLine().trimmed() != QByteArrayLiteral("PF")) {
    verification.failure_reason = QStringLiteral("pfm_header_invalid");
    return verification;
  }
  const auto dimensions = file.readLine().simplified().split(' ');
  bool width_ok = false;
  bool height_ok = false;
  verification.width = dimensions.value(0).toInt(&width_ok);
  verification.height = dimensions.value(1).toInt(&height_ok);
  bool scale_ok = false;
  const auto scale = file.readLine().trimmed().toDouble(&scale_ok);
  const auto pixel_bytes = file.readAll();
  verification.bit_depth = 32;
  verification.profile_verified = !settings.embed_profile;
  verification.transfer_function_verified =
      scale_ok && scale < 0.0;
  const auto expected_float =
      rendered_output.format() ==
              QImage::Format_RGBA32FPx4
          ? rendered_output
          : rendered_output.convertToFormat(
                QImage::Format_RGBA32FPx4);
  const auto expected_payload_bytes =
      verification.width *
      verification.height * 3 *
      static_cast<int>(sizeof(float));
  auto maximum = 0.0;
  auto total = 0.0;
  auto samples = std::uint64_t{0};
  auto payload_valid =
      !expected_float.isNull() &&
      pixel_bytes.size() ==
          expected_payload_bytes;
  if (payload_valid) {
    for (int file_y = 0;
         file_y < verification.height;
         ++file_y) {
      const auto image_y =
          verification.height - 1 - file_y;
      const auto* expected_line =
          reinterpret_cast<const float*>(
              expected_float.constScanLine(
                  image_y));
      for (int x = 0;
           x < verification.width;
           ++x) {
        for (auto channel = 0;
             channel < 3;
             ++channel) {
          const auto payload_offset =
              ((file_y *
                    verification.width +
                x) *
                   3 +
               channel) *
              static_cast<int>(
                  sizeof(float));
          const auto decoded_value =
              read_little_endian_float(
                  pixel_bytes.constData() +
                  payload_offset);
          const auto expected_value =
              expected_line[x * 4 +
                            channel];
          if (!std::isfinite(
                  decoded_value) ||
              !std::isfinite(
                  expected_value)) {
            payload_valid = false;
            break;
          }
          const auto delta = std::abs(
              static_cast<double>(
                  decoded_value) -
              static_cast<double>(
                  expected_value));
          maximum =
              std::max(maximum, delta);
          total += delta;
          ++samples;
        }
        if (!payload_valid) {
          break;
        }
      }
      if (!payload_valid) {
        break;
      }
    }
  }
  verification.maximum_pixel_delta =
      static_cast<int>(
          std::lround(
              std::min(maximum, 1.0) *
              65535.0));
  verification.mean_pixel_delta =
      samples == 0U
          ? 0.0
          : total /
                static_cast<double>(
                    samples) *
                65535.0;
  verification.pixels_verified =
      payload_valid && maximum <= 1.0e-6;
  verification.metadata_verified = true;
  verification.mastering_fields_verified =
      true;
  verification.alpha_verified = export_alpha_behavior(settings) ==
                                QStringLiteral("drop-unassociated-alpha");
  verification.orientation_verified = true;
  verification.component_association_verified = true;
  verification.valid =
      width_ok && height_ok && scale_ok && scale < 0.0 &&
      verification.width == rendered_output.width() &&
      verification.height == rendered_output.height() &&
      verification.pixels_verified &&
      verification.profile_verified &&
      verification.transfer_function_verified &&
      verification.metadata_verified &&
      verification.alpha_verified;
  if (!verification.valid) {
    verification.failure_reason = QStringLiteral("pfm_artifact_contract_failed");
  }
  return verification;
}

ExportArtifactVerification verify_openexr_export_artifact(
    const QString& output_path,
    const QImage& rendered_output,
    const ExportImageSettings& settings) {
  ExportArtifactVerification verification;
  verification.format = settings.format_name;
  verification.file_bytes = QFileInfo(output_path).size();
  const auto inspection = photo_editor::io::inspect_openexr(
      output_path.toStdString(),
      true);
  verification.width = inspection.width;
  verification.height = inspection.height;
  verification.bit_depth = inspection.channel_type == "half" ? 16 : 32;
  verification.profile_verified =
      !settings.embed_profile &&
      !inspection.color_space_name.empty() &&
      (settings.scene_linear_color_space.isEmpty() ||
       QString::fromStdString(inspection.color_space_name) ==
           settings.scene_linear_color_space);
  verification.transfer_function_verified =
      inspection.transfer_function ==
      "linear";
  verification.metadata_applicable = true;
  verification.metadata_verified =
      !inspection.rendering_model.empty() &&
      !inspection.transform_provenance.empty() &&
      !inspection.color_space_name.empty();
  verification.mastering_fields_verified =
      true;
  verification.pixels_verified =
      float_rgb_pixels_match(
          rendered_output,
          inspection.rgba_pixels,
          4,
          settings.bit_depth == 16
              ? 0.0025
              : 1.0e-6,
          settings.bit_depth == 16
              ? 0.0005
              : 1.0e-7,
          &verification
               .maximum_pixel_delta,
          &verification.mean_pixel_delta);
  verification.alpha_verified = inspection.has_alpha;
  verification.orientation_verified = true;
  verification.component_association_verified = inspection.has_alpha;
  verification.valid =
      verification.file_bytes > 0 && inspection.valid &&
      inspection.width == rendered_output.width() &&
      inspection.height == rendered_output.height() &&
      verification.bit_depth == settings.bit_depth &&
      verification.pixels_verified &&
      verification.profile_verified &&
      verification.transfer_function_verified &&
      verification.metadata_verified &&
      verification.alpha_verified &&
      inspection.non_finite_sample_count == 0U;
  if (!verification.valid) {
    verification.failure_reason = inspection.failure_reason.empty()
                                      ? QStringLiteral("openexr_artifact_contract_failed")
                                      : QString::fromStdString(inspection.failure_reason);
  }
  return verification;
}

ExportArtifactVerification verify_hdr_avif_export_artifact(
    const QString& output_path,
    const QImage& rendered_output,
    const ExportImageSettings& settings) {
  ExportArtifactVerification verification;
  verification.format = settings.format_name;
  verification.file_bytes = QFileInfo(output_path).size();
  const auto inspection = photo_editor::io::inspect_hdr_avif(
      output_path.toStdString(),
      true);
  verification.width = inspection.width;
  verification.height = inspection.height;
  verification.bit_depth = inspection.bit_depth;
  const auto expected_transfer =
      settings.hdr_transfer.compare(QStringLiteral("HLG"), Qt::CaseInsensitive) == 0
          ? 18
          : 16;
  verification.profile_verified =
      !settings.embed_profile &&
      inspection.color_primaries == 9;
  verification.transfer_function_verified =
      inspection.transfer_characteristics == expected_transfer &&
      inspection.matrix_coefficients == 9 &&
      inspection.full_range;
  verification.metadata_applicable = true;
  verification.metadata_verified =
      inspection.color_primaries == 9 &&
      inspection.matrix_coefficients == 9 &&
      inspection.full_range;
  verification.mastering_fields_applicable =
      true;
  verification.mastering_fields_verified =
      inspection.has_mastering_display &&
      inspection.max_content_light_level == settings.max_content_light_level &&
      inspection.max_frame_average_light_level ==
          settings.max_frame_average_light_level &&
      std::abs(
          inspection
                  .mastering_peak_luminance_nits -
              settings
                  .peak_luminance_nits) <=
          0.1 &&
      std::abs(
          inspection
                  .mastering_black_luminance_nits -
              settings
                  .black_luminance_nits) <=
          0.0001;
  const auto pixel_verification =
      verify_hdr_avif_encoded_pixels(
          rendered_output,
          settings,
          inspection.encoded_rgb);
  verification.pixels_verified = pixel_verification.valid;
  verification.maximum_pixel_delta =
      static_cast<int>(std::lround(
          std::min(pixel_verification.maximum_delta, 1.0) *
          65535.0));
  verification.p99_pixel_delta =
      pixel_verification.p99_delta * 65535.0;
  verification.mean_pixel_delta =
      pixel_verification.mean_delta * 65535.0;
  verification.alpha_verified = settings.flatten_alpha;
  verification.orientation_verified = true;
  verification.component_association_verified = settings.flatten_alpha;
  verification.valid =
      verification.file_bytes > 0 && inspection.valid &&
      inspection.width == rendered_output.width() &&
      inspection.height == rendered_output.height() &&
      inspection.bit_depth == settings.bit_depth &&
      verification.pixels_verified &&
      verification.profile_verified &&
      verification.transfer_function_verified &&
      verification.metadata_verified &&
      verification.mastering_fields_verified &&
      verification.alpha_verified;
  if (!verification.valid) {
    verification.failure_reason = inspection.failure_reason.empty()
                                      ? QStringLiteral("hdr_avif_artifact_contract_failed")
                                      : QString::fromStdString(inspection.failure_reason);
  }
  return verification;
}

ExportArtifactVerification verify_export_artifact(
    const QString& output_path,
    const QImage& rendered_output,
    const ExportImageSettings& settings,
    const QJsonObject& export_metadata) {
  if (settings.writer_format == "pfm") {
    return verify_pfm_export_artifact(output_path, rendered_output, settings);
  }
  if (settings.writer_format == "exr") {
    return verify_openexr_export_artifact(output_path, rendered_output, settings);
  }
  if (settings.writer_format == "avif-hdr") {
    return verify_hdr_avif_export_artifact(output_path, rendered_output, settings);
  }
  return verify_standard_export_artifact(
      output_path,
      rendered_output,
      settings,
      export_metadata);
}

QString iso_now() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

qint64 queue_wait_msecs_since(const QString& created_at, const QDateTime& reference_time) {
  const auto created = QDateTime::fromString(created_at.trimmed(), Qt::ISODate);
  if (!created.isValid() || !reference_time.isValid()) {
    return 0;
  }
  return qMax<qint64>(0, created.toUTC().msecsTo(reference_time.toUTC()));
}

qint64 queue_wait_msecs_for_job(const PersistentJobRecord& job) {
  return queue_wait_msecs_since(job.created_at, QDateTime::currentDateTimeUtc());
}

QString worker_pool_id_for_job_kind(const QString& job_kind) {
  if (job_kind == QStringLiteral("catalog_import")) {
    return QStringLiteral("catalog-import");
  }
  if (job_kind == QStringLiteral("timeline_render")) {
    return QStringLiteral("timeline-media");
  }
  if (job_kind == QStringLiteral("metadata_extraction") ||
      job_kind == QStringLiteral("metadata_extract")) {
    return QStringLiteral("metadata-extraction");
  }
  if (job_kind == QStringLiteral("carousel_render")) {
    return QStringLiteral("carousel-render");
  }
  return QStringLiteral("export");
}

QString worker_surface_for_job_kind(const QString& job_kind) {
  if (job_kind == QStringLiteral("catalog_import") ||
      job_kind == QStringLiteral("metadata_extraction") ||
      job_kind == QStringLiteral("metadata_extract")) {
    return QStringLiteral("library");
  }
  if (job_kind == QStringLiteral("timeline_render")) {
    return QStringLiteral("timeline");
  }
  if (job_kind == QStringLiteral("carousel_render")) {
    return QStringLiteral("carousel");
  }
  return QStringLiteral("export");
}

QString cancellation_reason_for_job(const PersistentJobRecord& job) {
  const auto explicit_reason =
      job.metadata_json.value(QStringLiteral("cancellationReason")).toString().trimmed();
  if (!explicit_reason.isEmpty()) {
    return explicit_reason;
  }
  const auto cancel_action = job.metadata_json.value(QStringLiteral("cancelAction")).toString().trimmed();
  if (!cancel_action.isEmpty()) {
    return cancel_action;
  }
  if (job.metadata_json.value(QStringLiteral("cancellationRequested")).toBool() ||
      job.status == QStringLiteral("Canceling")) {
    return QStringLiteral("cooperative_cancel_requested");
  }
  if (job.status == QStringLiteral("Canceled")) {
    return QStringLiteral("canceled");
  }
  return {};
}

QString normalized_local_path(const QString& path) {
  const auto trimmed = path.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  const QUrl url(trimmed);
  if (url.isValid() && url.isLocalFile()) {
    return QDir::cleanPath(url.toLocalFile());
  }

  return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

QString sanitize_token(const QString& value, const QString& fallback) {
  auto cleaned = value.trimmed();
  cleaned.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
  cleaned.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));
  cleaned.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
  return cleaned.isEmpty() ? fallback : cleaned;
}

QString preset_token(const QString& preset) {
  return sanitize_token(preset.toLower(), QStringLiteral("export"));
}

QString display_label_for_job(const PersistentJobRecord& job) {
  const auto trimmed = job.display_label.trimmed();
  if (!trimmed.isEmpty()) {
    return trimmed;
  }

  const QFileInfo source_info(job.source_path);
  if (!source_info.completeBaseName().isEmpty()) {
    return source_info.completeBaseName();
  }

  if (!source_info.fileName().isEmpty()) {
    return source_info.fileName();
  }

  return QStringLiteral("Export");
}

bool is_photo_media(const QString& media_type) {
  return media_type.trimmed().compare(QStringLiteral("photo"), Qt::CaseInsensitive) == 0;
}

bool is_original_copy_preset(const QString& preset) {
  return preset.trimmed().compare(QStringLiteral("Original Copy"), Qt::CaseInsensitive) == 0;
}

bool should_render_photo_job(const PersistentJobRecord& job) {
  return is_photo_media(job.media_type) && !is_original_copy_preset(job.preset_name);
}

void apply_export_crop_constraint(EditRecipeState* state, const QJsonObject& metadata) {
  if (state == nullptr) {
    return;
  }
  const auto crop = metadata.value(QStringLiteral("exportCrop")).toObject();
  if (crop.isEmpty() || !crop.value(QStringLiteral("enabled")).toBool(true)) {
    return;
  }

  const auto preset = crop.value(QStringLiteral("preset")).toString().trimmed();
  const auto has_rect = crop.value(QStringLiteral("width")).toDouble() > 0.0 &&
                        crop.value(QStringLiteral("height")).toDouble() > 0.0;
  if (!preset.isEmpty() && preset.compare(QStringLiteral("Current"), Qt::CaseInsensitive) != 0) {
    state->crop_rect_x = 0.0;
    state->crop_rect_y = 0.0;
    state->crop_rect_width = 0.0;
    state->crop_rect_height = 0.0;
    state->crop_zoom = 1.0;
    state->crop_preset = preset.compare(QStringLiteral("Original"), Qt::CaseInsensitive) == 0
                             ? QStringLiteral("Original")
                             : preset;
  }

  if (has_rect) {
    const auto width = qBound(0.01, crop.value(QStringLiteral("width")).toDouble(), 1.0);
    const auto height = qBound(0.01, crop.value(QStringLiteral("height")).toDouble(), 1.0);
    state->crop_rect_x = qBound(0.0, crop.value(QStringLiteral("x")).toDouble(), 1.0 - width);
    state->crop_rect_y = qBound(0.0, crop.value(QStringLiteral("y")).toDouble(), 1.0 - height);
    state->crop_rect_width = width;
    state->crop_rect_height = height;
    state->crop_preset = QStringLiteral("Original");
    state->crop_zoom = 1.0;
  }
}

QString resolved_source_path(const PersistentJobRecord& job, bool* uses_preview) {
  const auto preview_path = normalized_local_path(job.edited_preview_source);
  const auto source_path = normalized_local_path(job.source_path);
  const auto prefer_preview = is_photo_media(job.media_type) && !is_original_copy_preset(job.preset_name);

  if (uses_preview != nullptr) {
    *uses_preview = false;
  }

  if (prefer_preview && !preview_path.isEmpty() && QFileInfo::exists(preview_path)) {
    if (uses_preview != nullptr) {
      *uses_preview = true;
    }
    return preview_path;
  }

  if (!source_path.isEmpty() && QFileInfo::exists(source_path)) {
    return source_path;
  }

  if (!preview_path.isEmpty() && QFileInfo::exists(preview_path)) {
    if (uses_preview != nullptr) {
      *uses_preview = true;
    }
    return preview_path;
  }

  return {};
}

QColorSpace export_target_color_space(
    const RenderEditState& state,
    const ExportImageSettings& options) {
  if (options.scene_linear) {
    return working_color_space(state.working_color_profile);
  }
  if (options.hdr_delivery) {
    return QColorSpace(
        QColorSpace::Primaries::Bt2020,
        QColorSpace::TransferFunction::Linear);
  }
  return preview_color_space_for_name(normalized_output_color_space_name(state.output_color_space));
}

void append_pfm_float(QByteArray* data, float value) {
  if (!std::isfinite(value)) {
    value = 0.0F;
  }
  quint32 bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const auto little_endian_bits = qToLittleEndian(bits);
  data->append(reinterpret_cast<const char*>(&little_endian_bits), sizeof(little_endian_bits));
}

bool write_pfm_image(
    const QImage& image,
    const QString& output_path,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (cancellation.cancelled() || image.isNull() ||
      image.width() <= 0 || image.height() <= 0) {
    return false;
  }

  const auto float_image = image.format() == QImage::Format_RGBA32FPx4
                               ? image
                               : image.convertToFormat(QImage::Format_RGBA32FPx4);
  if (float_image.isNull()) {
    return false;
  }

  QByteArray data;
  data.reserve(64 + float_image.width() * float_image.height() * 3 * static_cast<int>(sizeof(float)));
  data.append("PF\n");
  data.append(QByteArray::number(float_image.width()));
  data.append(' ');
  data.append(QByteArray::number(float_image.height()));
  data.append("\n-1.0\n");
  for (int y = float_image.height() - 1; y >= 0; --y) {
    if (cancellation.cancelled()) {
      return false;
    }
    const auto* scanline = reinterpret_cast<const float*>(float_image.constScanLine(y));
    for (int x = 0; x < float_image.width(); ++x) {
      const auto* pixel = scanline + (x * 4);
      append_pfm_float(&data, pixel[0]);
      append_pfm_float(&data, pixel[1]);
      append_pfm_float(&data, pixel[2]);
    }
  }

  if (cancellation.cancelled()) {
    return false;
  }
  CancellationAwareSaveDevice file(output_path, cancellation);
  if (!file.openForWrite()) {
    return false;
  }
  return file.write(data) == data.size() &&
         !cancellation.cancelled() &&
         file.commit();
}

bool write_openexr_image(
    const QImage& image,
    const QString& output_path,
    const RenderEditState& state,
    const ExportImageSettings& options,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (cancellation.cancelled() || image.isNull() ||
      image.width() <= 0 || image.height() <= 0 ||
      options.writer_format != "exr") {
    return false;
  }
  auto float_image = image.format() == QImage::Format_RGBA32FPx4
                         ? image
                         : image.convertToFormat(QImage::Format_RGBA32FPx4);
  if (float_image.isNull() ||
      float_image.bytesPerLine() % static_cast<qsizetype>(sizeof(float)) != 0) {
    return false;
  }
  const auto working_profile =
      photo_editor::pipeline::working_color_profile_from_name(
          state.working_color_profile.toStdString());
  auto export_color_space = working_profile.color_space;
  if (options.scene_linear_color_space == QStringLiteral("ACES2065-1")) {
    export_color_space = photo_editor::pipeline::make_aces2065_1_color_space();
  } else if (options.scene_linear_color_space == QStringLiteral("ACEScg")) {
    export_color_space = photo_editor::pipeline::make_acescg_color_space();
  }
  const auto explicit_interchange =
      export_color_space.name != working_profile.color_space.name;
  if (explicit_interchange) {
    float_image.detach();
    const auto transform = photo_editor::pipeline::compile_builtin_color_transform(
        working_profile.color_space,
        export_color_space);
    for (int y = 0; y < float_image.height(); ++y) {
      if (cancellation.cancelled()) {
        return false;
      }
      auto* scanline = reinterpret_cast<float*>(float_image.scanLine(y));
      if (scanline == nullptr) {
        return false;
      }
      for (int x = 0; x < float_image.width(); ++x) {
        auto* pixel = scanline + x * 4;
        const auto transformed = photo_editor::pipeline::multiply_matrix3_vector(
            transform.matrix,
            {pixel[0], pixel[1], pixel[2]});
        if (!std::isfinite(transformed[0]) ||
            !std::isfinite(transformed[1]) ||
            !std::isfinite(transformed[2])) {
          return false;
        }
        pixel[0] = static_cast<float>(transformed[0]);
        pixel[1] = static_cast<float>(transformed[1]);
        pixel[2] = static_cast<float>(transformed[2]);
      }
    }
  }
  const auto component_count = static_cast<std::size_t>(
      float_image.bytesPerLine() / static_cast<qsizetype>(sizeof(float)) *
      float_image.height());
  const auto* components = reinterpret_cast<const float*>(
      float_image.constBits());
  std::string failure_reason;
  const auto provenance = explicit_interchange
                              ? std::string(
                                    "scene-linear-interchange:v1|display-transform:bypassed|") +
                                    "source-profile:" + working_profile.profile_name +
                                    "|destination-profile:" + export_color_space.name +
                                    "|recipe-renderer:" +
                                    state.photographic_rendering_model.toStdString()
                              : std::string(
                                    "scene-linear-working:v1|display-transform:bypassed|") +
                                    "working-profile:" + working_profile.profile_name +
                                    "|recipe-renderer:" +
                                    state.photographic_rendering_model.toStdString();
  if (cancellation.cancelled()) {
    return false;
  }
  const auto wrote = photo_editor::io::write_openexr_rgba(
      output_path.toStdString(),
      std::span<const float>(components, component_count),
      photo_editor::io::OpenExrWriteOptions{
          .width = float_image.width(),
          .height = float_image.height(),
          .row_stride_floats = static_cast<std::size_t>(
              float_image.bytesPerLine() /
              static_cast<qsizetype>(sizeof(float))),
          .half_float = options.bit_depth == 16,
          .include_alpha = !options.flatten_alpha,
          .color_space = export_color_space,
          .white_luminance_nits = 100.0F,
          .rendering_model = explicit_interchange
                                 ? "scene-linear-interchange-v1"
                                 : "scene-linear-working-v1",
          .transform_provenance = provenance,
          .cancellation_requested =
              [cancellation]() {
                return cancellation.cancelled();
              },
      },
      &failure_reason);
  if (!wrote) {
    qWarning().noquote()
        << "OpenEXR export failed:" << QString::fromStdString(failure_reason);
  }
  return wrote && !cancellation.cancelled();
}

bool write_hdr_avif_image(
    const QImage& image,
    const QString& output_path,
    const ExportImageSettings& options,
    photo_editor::pipeline::CancellationToken cancellation) {
  const auto encoded = write_prepared_hdr_avif_export_image(
      image, output_path, options, cancellation);
  if (!encoded.valid) {
    qWarning().noquote()
        << "HDR AVIF export failed:" << encoded.error;
  }
  return encoded.valid && !cancellation.cancelled();
}

int preview_export_diff_tolerance_for_preset(const QString& preset) {
  const auto normalized = preset.section(QLatin1Char('|'), 0, 0).trimmed();
  if (normalized.compare(QStringLiteral("Quick Web"), Qt::CaseInsensitive) == 0) {
    return 18;
  }
  if (normalized.compare(QStringLiteral("Client Preview"), Qt::CaseInsensitive) == 0) {
    return 12;
  }
  if (normalized.compare(QStringLiteral("Web Proof"), Qt::CaseInsensitive) == 0) {
    return 10;
  }
  return 1;
}

QVariantMap preview_export_diff_to_map(
    const PreviewExportDiff& diff,
    const QJsonObject& export_preview_job = {}) {
  QVariantMap value;
  value.insert(QStringLiteral("available"), diff.available);
  value.insert(QStringLiteral("withinTolerance"), diff.within_tolerance);
  value.insert(QStringLiteral("width"), diff.width);
  value.insert(QStringLiteral("height"), diff.height);
  value.insert(QStringLiteral("maxChannelDelta"), diff.max_channel_delta);
  value.insert(QStringLiteral("meanChannelDelta"), diff.mean_channel_delta);
  value.insert(QStringLiteral("tolerance"), diff.tolerance);
  value.insert(QStringLiteral("status"), diff.status);
  if (!export_preview_job.isEmpty()) {
    value.insert(
        QStringLiteral("exportPreviewJobId"),
        export_preview_job.value(QStringLiteral("jobId")).toString());
    value.insert(
        QStringLiteral("exportPreviewJobKind"),
        export_preview_job.value(QStringLiteral("jobKind")).toString());
    value.insert(
        QStringLiteral("exportPreviewStage"),
        export_preview_job.value(QStringLiteral("stage")).toString());
    value.insert(
        QStringLiteral("exportPreviewStateSignature"),
        export_preview_job.value(QStringLiteral("stateSignature")).toString());
  }
  return value;
}

QString sha1_hex(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString json_sha1(const QJsonObject& payload) {
  return sha1_hex(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

QVariantMap export_trace_mask_source_payload(QVariantMap mask) {
  for (const auto& key : {
           QStringLiteral("mask_source_id"),
           QStringLiteral("mask_set_id"),
           QStringLiteral("mask_set_name"),
           QStringLiteral("mask_source_kind"),
           QStringLiteral("mask_source_operation"),
           QStringLiteral("mask_source_schema_version"),
           QStringLiteral("mask_source_count"),
           QStringLiteral("mask_source_ids"),
           QStringLiteral("mask_source_hash"),
           QStringLiteral("mask_alpha_trace_hash"),
           QStringLiteral("mask_alpha_trace_signature"),
           QStringLiteral("mask_alpha_provider_contract"),
           QStringLiteral("mask_alpha_provider"),
           QStringLiteral("mask_alpha_full_resolution_available"),
           QStringLiteral("mask_alpha_deterministic"),
           QStringLiteral("mask_alpha_target_width"),
           QStringLiteral("mask_alpha_target_height"),
           QStringLiteral("mask_alpha_provider_signature"),
           QStringLiteral("mask_alpha_component_providers"),
           QStringLiteral("mask_source_revision_hash"),
           QStringLiteral("mask_alpha_raster_width"),
           QStringLiteral("mask_alpha_raster_height"),
           QStringLiteral("mask_source_point_count"),
           QStringLiteral("mask_source_stroke_count"),
           QStringLiteral("mask_work_state_revision"),
       }) {
    mask.remove(key);
  }

  for (const auto& key : {
           QStringLiteral("segmentationData"),
           QStringLiteral("depthData"),
           QStringLiteral("generatedMaskCleanupSourceData"),
       }) {
    const auto bytes = mask.value(key).toByteArray();
    if (!bytes.isEmpty()) {
      QVariantMap fingerprint;
      fingerprint.insert(QStringLiteral("bytes"), bytes.size());
      fingerprint.insert(QStringLiteral("sha1"), sha1_hex(bytes));
      mask.insert(key, fingerprint);
    }
  }
  return mask;
}

QString export_mask_alpha_provider_for_mask(const QVariantMap& mask) {
  const auto kind = mask.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind == QStringLiteral("brush") &&
      (!mask.value(QStringLiteral("brushPoints")).toList().isEmpty() ||
       !mask.value(QStringLiteral("brushStrokes")).toList().isEmpty())) {
    return QStringLiteral("brush-stroke-replay");
  }

  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto has_raster_payload =
      width > 0 && height > 0 && data.size() == static_cast<qsizetype>(width * height);
  if (has_raster_payload || kind == QStringLiteral("alpha") ||
      kind == QStringLiteral("subject") || kind == QStringLiteral("object") ||
      kind == QStringLiteral("sky") || kind == QStringLiteral("background")) {
    return has_raster_payload ? QStringLiteral("raster-payload")
                              : QStringLiteral("raster-missing");
  }

  if (kind == QStringLiteral("linear") || kind == QStringLiteral("radial") ||
      kind == QStringLiteral("ellipse") || kind == QStringLiteral("rectangle") ||
      kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso") ||
      kind == QStringLiteral("luminance_range") || kind == QStringLiteral("color_range") ||
      kind == QStringLiteral("depth_range")) {
    return QStringLiteral("procedural-geometry");
  }

  return QStringLiteral("unavailable");
}

QString export_source_revision_hash(const QString& source_path) {
  const QFileInfo source_info(source_path);
  if (!source_info.exists()) {
    return {};
  }

  QJsonObject payload;
  payload.insert(QStringLiteral("version"), QStringLiteral("export-source-revision-v1"));
  payload.insert(QStringLiteral("path"), source_info.absoluteFilePath());
  payload.insert(QStringLiteral("size"), QString::number(source_info.size()));
  payload.insert(
      QStringLiteral("lastModified"),
      source_info.lastModified().toUTC().toString(Qt::ISODateWithMs));
  return json_sha1(payload);
}

QJsonObject export_mask_alpha_trace_metadata(
    const QString& recipe_json,
    const QString& source_path) {
  const auto decoded =
      parse_adjustment_payload_or_legacy_recipe(
          recipe_json);
  const auto state =
      decoded.state.value_or(EditRecipeState{});
  if (state.mask_stack.isEmpty()) {
    return {};
  }

  const auto source_revision_hash = export_source_revision_hash(source_path);
  QJsonArray component_ids;
  QJsonArray component_hashes;
  QJsonArray component_signatures;
  QJsonArray component_providers;
  auto ordinal = 1;
  auto max_width = 0;
  auto max_height = 0;
  for (const auto& value : state.mask_stack) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask.value(QStringLiteral("enabled"), true).toBool()) {
      continue;
    }

    const auto source_id = mask.value(QStringLiteral("id")).toString().simplified();
    if (source_id.isEmpty()) {
      continue;
    }

    const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (width > 0 && height > 0) {
      max_width = std::max(max_width, width);
      max_height = std::max(max_height, height);
    }
    const auto provider = export_mask_alpha_provider_for_mask(mask);
    const auto provider_available =
        provider != QStringLiteral("unavailable") && provider != QStringLiteral("raster-missing");

    auto source_payload = QJsonObject::fromVariantMap(export_trace_mask_source_payload(mask));
    source_payload.insert(QStringLiteral("ordinal"), ordinal - 1);
    const auto source_hash = json_sha1(source_payload);

    QJsonObject trace_payload;
    trace_payload.insert(QStringLiteral("maskSourceHash"), source_hash);
    trace_payload.insert(QStringLiteral("alphaProvider"), provider);
    trace_payload.insert(QStringLiteral("alphaFullResolutionAvailable"), provider_available);
    trace_payload.insert(QStringLiteral("alphaDeterministic"), provider_available);
    if (!source_revision_hash.isEmpty()) {
      trace_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
    }
    if (width > 0 && height > 0) {
      trace_payload.insert(QStringLiteral("rasterWidth"), width);
      trace_payload.insert(QStringLiteral("rasterHeight"), height);
    }
    const auto trace_hash = json_sha1(trace_payload);
    component_ids.append(source_id);
    component_hashes.append(QStringLiteral("sha1:%1").arg(trace_hash));
    component_providers.append(provider);
    component_signatures.append(
        QStringLiteral("mask:%1|source:sha1:%2|revision:%3|raster:%4x%5|provider:%6|full:%7")
            .arg(source_id)
            .arg(source_hash)
            .arg(source_revision_hash.isEmpty()
                     ? QStringLiteral("none")
                      : QStringLiteral("sha1:%1").arg(source_revision_hash))
            .arg(width)
            .arg(height)
            .arg(provider)
            .arg(provider_available ? QStringLiteral("true") : QStringLiteral("false")));
  }

  if (component_ids.isEmpty()) {
    return {};
  }

  QJsonObject source_payload;
  source_payload.insert(QStringLiteral("context"), QStringLiteral("export-render"));
  source_payload.insert(QStringLiteral("componentSignatures"), component_signatures);
  source_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
  const auto source_hash = json_sha1(source_payload);

  QJsonObject trace_payload;
  trace_payload.insert(QStringLiteral("maskSourceHash"), source_hash);
  trace_payload.insert(QStringLiteral("componentHashes"), component_hashes);
  trace_payload.insert(QStringLiteral("componentProviders"), component_providers);
  trace_payload.insert(QStringLiteral("alphaProvider"), QStringLiteral("export-render"));
  const auto export_available =
      component_providers.size() == component_ids.size() &&
      !component_providers.contains(QStringLiteral("unavailable")) &&
      !component_providers.contains(QStringLiteral("raster-missing"));
  trace_payload.insert(QStringLiteral("alphaFullResolutionAvailable"), export_available);
  trace_payload.insert(QStringLiteral("alphaDeterministic"), export_available);
  if (!source_revision_hash.isEmpty()) {
    trace_payload.insert(QStringLiteral("sourceRevisionHash"), source_revision_hash);
  }
  if (max_width > 0 && max_height > 0) {
    trace_payload.insert(QStringLiteral("rasterWidth"), max_width);
    trace_payload.insert(QStringLiteral("rasterHeight"), max_height);
  }
  const auto trace_hash = json_sha1(trace_payload);

  QJsonObject fields;
  fields.insert(QStringLiteral("trace_context"), QStringLiteral("export-render"));
  fields.insert(QStringLiteral("mask_source_count"), component_ids.size());
  fields.insert(QStringLiteral("mask_source_ids"), component_ids);
  fields.insert(QStringLiteral("mask_source_hash"), QStringLiteral("sha1:%1").arg(source_hash));
  fields.insert(QStringLiteral("mask_alpha_trace_hash"), QStringLiteral("sha1:%1").arg(trace_hash));
  fields.insert(
      QStringLiteral("mask_alpha_provider_contract"),
      QStringLiteral("shared-mask-alpha-provider-v1"));
  fields.insert(QStringLiteral("mask_alpha_provider"), QStringLiteral("export-render"));
  fields.insert(QStringLiteral("mask_alpha_full_resolution_available"), export_available);
  fields.insert(QStringLiteral("mask_alpha_deterministic"), export_available);
  fields.insert(QStringLiteral("mask_alpha_component_providers"), component_providers);
  fields.insert(
      QStringLiteral("mask_alpha_provider_signature"),
      QStringLiteral("contract:shared-mask-alpha-provider-v1|provider:export-render|full:%1|deterministic:%2|components:%3|target:%4x%5")
          .arg(export_available ? QStringLiteral("true") : QStringLiteral("false"),
               export_available ? QStringLiteral("true") : QStringLiteral("false"))
          .arg(component_ids.size())
          .arg(max_width)
          .arg(max_height));
  fields.insert(
      QStringLiteral("mask_alpha_trace_signature"),
      QStringLiteral("export-render|sources:%1|source:sha1:%2|revision:%3|raster:%4x%5|provider:export-render|full:%6")
          .arg(component_ids.size())
          .arg(source_hash)
          .arg(source_revision_hash.isEmpty()
                   ? QStringLiteral("none")
                   : QStringLiteral("sha1:%1").arg(source_revision_hash))
          .arg(max_width)
          .arg(max_height)
          .arg(export_available ? QStringLiteral("true") : QStringLiteral("false")));
  if (!source_revision_hash.isEmpty()) {
    fields.insert(
        QStringLiteral("mask_source_revision_hash"),
        QStringLiteral("sha1:%1").arg(source_revision_hash));
  }
  if (max_width > 0 && max_height > 0) {
    fields.insert(QStringLiteral("mask_alpha_raster_width"), max_width);
    fields.insert(QStringLiteral("mask_alpha_raster_height"), max_height);
  }
  return fields;
}

QString export_render_state_signature(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& mask_alpha_trace,
    const QJsonObject& export_image_settings) {
  QJsonObject payload;
  payload.insert(QStringLiteral("jobId"), job.job_id);
  payload.insert(QStringLiteral("assetId"), job.asset_id);
  payload.insert(QStringLiteral("sourcePath"), source_path);
  payload.insert(QStringLiteral("outputPath"), output_path);
  payload.insert(
      QStringLiteral("adjustmentPayload"),
      job.recipe_json);
  payload.insert(QStringLiteral("preset"), job.preset_name);
  payload.insert(QStringLiteral("metadata"), job.metadata_json);
  payload.insert(QStringLiteral("renderPhoto"), render_photo);
  payload.insert(QStringLiteral("maskAlphaTrace"), mask_alpha_trace);
  payload.insert(QStringLiteral("exportImageSettings"), export_image_settings);
  return QStringLiteral("sha1:%1").arg(QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(payload).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex()));
}

QJsonObject export_render_job_metadata(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo) {
  const auto mask_alpha_trace =
      render_photo ? export_mask_alpha_trace_metadata(job.recipe_json, source_path) : QJsonObject{};
  const auto export_image_settings =
      render_photo ? export_image_settings_metadata(export_image_settings_for_preset(job.preset_name))
                   : QJsonObject{};
  const auto signature = export_render_state_signature(
      job,
      source_path,
      output_path,
      render_photo,
      mask_alpha_trace,
      export_image_settings);
  const auto signature_token =
      signature.mid(signature.indexOf(QLatin1Char(':')) + 1, 12);
  QJsonObject metadata;
  metadata.insert(
      QStringLiteral("jobId"),
      QStringLiteral("export_render-%1-%2").arg(job.job_id, signature_token));
  metadata.insert(
      QStringLiteral("jobKind"),
      render_photo ? QStringLiteral("full_resolution_export") : QStringLiteral("file_export_copy"));
  metadata.insert(QStringLiteral("stage"), QStringLiteral("export_render"));
  metadata.insert(QStringLiteral("assetId"), job.asset_id);
  metadata.insert(QStringLiteral("sourcePath"), source_path);
  metadata.insert(QStringLiteral("outputPath"), output_path);
  metadata.insert(QStringLiteral("preset"), job.preset_name);
  metadata.insert(QStringLiteral("stateSignature"), signature);
  metadata.insert(QStringLiteral("queuedAt"), job.created_at);
  metadata.insert(QStringLiteral("renderPhoto"), render_photo);
  if (!export_image_settings.isEmpty()) {
    metadata.insert(QStringLiteral("exportImageSettings"), export_image_settings);
    metadata.insert(
        QStringLiteral("format"),
        export_image_settings.value(QStringLiteral("format")).toString());
    metadata.insert(
        QStringLiteral("bitDepth"),
        export_image_settings.value(QStringLiteral("bitDepth")).toInt());
    metadata.insert(
        QStringLiteral("flattenAlpha"),
        export_image_settings.value(QStringLiteral("flattenAlpha")).toBool());
    metadata.insert(
        QStringLiteral("embedProfile"),
        export_image_settings.value(QStringLiteral("embedProfile")).toBool());
    metadata.insert(
        QStringLiteral("sceneLinear"),
        export_image_settings.value(QStringLiteral("sceneLinear")).toBool());
    metadata.insert(
        QStringLiteral("preservesOverRange"),
        export_image_settings.value(QStringLiteral("preservesOverRange")).toBool());
    metadata.insert(
        QStringLiteral("outputEncoding"),
        export_image_settings.value(QStringLiteral("outputEncoding")).toString());
    metadata.insert(
        QStringLiteral("headroomPolicy"),
        export_image_settings.value(QStringLiteral("headroomPolicy")).toString());
  }
  if (!mask_alpha_trace.isEmpty()) {
    metadata.insert(QStringLiteral("maskAlphaTrace"), mask_alpha_trace);
  }
  return metadata;
}

QString export_preview_state_signature(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& export_render_job,
    int tolerance) {
  QJsonObject payload;
  payload.insert(QStringLiteral("jobId"), job.job_id);
  payload.insert(QStringLiteral("assetId"), job.asset_id);
  payload.insert(QStringLiteral("sourcePath"), source_path);
  payload.insert(QStringLiteral("outputPath"), output_path);
  payload.insert(
      QStringLiteral("adjustmentPayload"),
      job.recipe_json);
  payload.insert(QStringLiteral("preset"), job.preset_name);
  payload.insert(QStringLiteral("renderPhoto"), render_photo);
  payload.insert(QStringLiteral("tolerance"), tolerance);
  payload.insert(QStringLiteral("exportRenderJob"), export_render_job);
  return QStringLiteral("sha1:%1").arg(QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(payload).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex()));
}

QJsonObject export_preview_job_metadata(
    const PersistentJobRecord& job,
    const QString& source_path,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& export_render_job) {
  const auto tolerance = render_photo ? preview_export_diff_tolerance_for_preset(job.preset_name) : 0;
  const auto signature = export_preview_state_signature(
      job,
      source_path,
      output_path,
      render_photo,
      export_render_job,
      tolerance);
  const auto signature_token =
      signature.mid(signature.indexOf(QLatin1Char(':')) + 1, 12);
  QJsonObject metadata;
  metadata.insert(
      QStringLiteral("jobId"),
      QStringLiteral("export_preview-%1-%2").arg(job.job_id, signature_token));
  metadata.insert(
      QStringLiteral("jobKind"),
      render_photo ? QStringLiteral("full_resolution_export_preview")
                   : QStringLiteral("file_export_copy_preview"));
  metadata.insert(QStringLiteral("stage"), QStringLiteral("export_preview_diff"));
  metadata.insert(QStringLiteral("assetId"), job.asset_id);
  metadata.insert(QStringLiteral("sourcePath"), source_path);
  metadata.insert(QStringLiteral("outputPath"), output_path);
  metadata.insert(QStringLiteral("preset"), job.preset_name);
  metadata.insert(QStringLiteral("stateSignature"), signature);
  metadata.insert(QStringLiteral("queuedAt"), job.created_at);
  metadata.insert(QStringLiteral("renderPhoto"), render_photo);
  metadata.insert(QStringLiteral("tolerance"), tolerance);
  metadata.insert(
      QStringLiteral("exportRenderJobId"),
      export_render_job.value(QStringLiteral("jobId")).toString());
  return metadata;
}

PreviewExportDiff compare_preview_to_export(
    const QImage& preview_image,
    const QImage& exported_image,
    int tolerance) {
  PreviewExportDiff diff;
  diff.tolerance = tolerance;
  if (preview_image.isNull() || exported_image.isNull()) {
    diff.status = QStringLiteral("unavailable");
    return diff;
  }

  diff.available = true;
  diff.width = exported_image.width();
  diff.height = exported_image.height();
  if (preview_image.size() != exported_image.size()) {
    diff.max_channel_delta = 255;
    diff.status = QStringLiteral("size-mismatch");
    return diff;
  }

  const auto preview_argb = preview_image.convertToFormat(QImage::Format_ARGB32);
  const auto exported_argb = exported_image.convertToFormat(QImage::Format_ARGB32);
  qint64 total_delta = 0;
  qint64 channel_count = 0;
  for (int y = 0; y < preview_argb.height(); ++y) {
    for (int x = 0; x < preview_argb.width(); ++x) {
      const auto preview_pixel = preview_argb.pixelColor(x, y);
      const auto exported_pixel = exported_argb.pixelColor(x, y);
      const auto red_delta = qAbs(preview_pixel.red() - exported_pixel.red());
      const auto green_delta = qAbs(preview_pixel.green() - exported_pixel.green());
      const auto blue_delta = qAbs(preview_pixel.blue() - exported_pixel.blue());
      diff.max_channel_delta = std::max(diff.max_channel_delta, red_delta);
      diff.max_channel_delta = std::max(diff.max_channel_delta, green_delta);
      diff.max_channel_delta = std::max(diff.max_channel_delta, blue_delta);
      total_delta += red_delta + green_delta + blue_delta;
      channel_count += 3;
    }
  }

  diff.mean_channel_delta = channel_count > 0 ? static_cast<double>(total_delta) / static_cast<double>(channel_count) : 0.0;
  diff.within_tolerance = diff.max_channel_delta <= diff.tolerance;
  diff.status = diff.within_tolerance ? QStringLiteral("matched") : QStringLiteral("differs");
  return diff;
}

QImage apply_export_output_sharpening(
    const QImage& input,
    const QString& mode,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (mode == QStringLiteral("off") || input.isNull()) return input;
  const auto source = input.convertToFormat(QImage::Format_ARGB32);
  if (source.isNull()) return {};
  auto output = source.copy();
  const auto amount = mode == QStringLiteral("print") ? 0.34 : 0.22;
  const auto clamp_channel = [](double value) {
    return qBound(0, static_cast<int>(std::lround(value)), 255);
  };
  for (int y = 0; y < source.height(); ++y) {
    if (cancellation.cancelled()) return {};
    auto* output_row = reinterpret_cast<QRgb*>(output.scanLine(y));
    for (int x = 0; x < source.width(); ++x) {
      const auto original = source.pixel(x, y);
      double blur_red = 0.0;
      double blur_green = 0.0;
      double blur_blue = 0.0;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        const auto sample_y = qBound(0, y + offset_y, source.height() - 1);
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto sample_x = qBound(0, x + offset_x, source.width() - 1);
          const auto sample = source.pixel(sample_x, sample_y);
          const auto weight = (offset_x == 0 ? 2 : 1) * (offset_y == 0 ? 2 : 1);
          blur_red += qRed(sample) * weight;
          blur_green += qGreen(sample) * weight;
          blur_blue += qBlue(sample) * weight;
        }
      }
      const auto red = qRed(original) + amount * (qRed(original) - blur_red / 16.0);
      const auto green = qGreen(original) + amount * (qGreen(original) - blur_green / 16.0);
      const auto blue = qBlue(original) + amount * (qBlue(original) - blur_blue / 16.0);
      output_row[x] = qRgba(clamp_channel(red), clamp_channel(green), clamp_channel(blue), qAlpha(original));
    }
  }
  return output;
}

QString output_suffix_for_preset(const QString& preset, bool rendered_output) {
  if (!rendered_output) {
    return {};
  }
  const auto options = export_image_settings_for_preset(preset);
  if (options.writer_format == "jpg") {
    return QStringLiteral("jpg");
  }
  if (options.writer_format == "tif") {
    return QStringLiteral("tif");
  }
  if (options.writer_format == "pfm") {
    return QStringLiteral("pfm");
  }
  if (options.writer_format == "exr") {
    return QStringLiteral("exr");
  }
  if (options.writer_format == "avif-hdr") {
    return QStringLiteral("avif");
  }
  return QStringLiteral("png");
}

bool render_photo_export(
    const QString& source_path,
    const QString& recipe_json,
    const QString& preset,
    const QJsonObject& export_metadata,
    const QString& output_path,
    RenderBackendTelemetry* telemetry = nullptr,
    QImage* rendered_output = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {}) {
  if (cancellation.cancelled()) {
    return false;
  }
  const auto decoded =
      parse_adjustment_payload_or_legacy_recipe(
          recipe_json);
  if (!decoded.ok()) {
    return false;
  }
  auto state = *decoded.state;
  apply_export_crop_constraint(&state, export_metadata);
  const auto options =
      export_image_settings_for_preset(preset);
  if (options.hdr_delivery) {
    state.output_color_space =
        QStringLiteral("Rec.2020");
    state.photographic_rendering_model =
        QStringLiteral("aces2-ocio-v1");
    state.render_intent =
        QStringLiteral("Perceptual");
    state.export_icc_profile_asset = {};
  }
  const auto uncropped_size =
      source_image_size(source_path);
  if (!uncropped_size.isValid()) {
    return false;
  }
  photo_editor::pipeline::DecodedImageMetadata metadata;
  const auto decode_preflight =
      raw_image_supported(source_path)
          ? QStringLiteral(
                "raw-metadata-only-before-plan")
          : QStringLiteral(
                "size-only-before-plan");
  if (raw_image_supported(source_path)) {
    const auto raw_settings =
        RawDecodeSettings{
            .input_transform_enabled =
                state.raw_input_transform_enabled,
            .input_color_profile =
                state.input_color_profile,
            .demosaic_quality =
                state.raw_demosaic_quality,
            .exposure = state.raw_exposure,
            .hot_dead_pixel_cleanup_amount =
                state.raw_hot_dead_pixel_cleanup,
            .false_color_suppression_amount =
                state.raw_false_color_suppression,
            .highlight_recovery_enabled =
                state.raw_highlight_recovery,
            .white_balance_override_enabled =
                state.raw_white_balance_override,
            .white_balance_temperature =
                state.raw_white_balance_temperature,
            .white_balance_tint =
                state.raw_white_balance_tint,
        };
    if (!read_raw_decoded_metadata(
            source_path,
            &metadata,
            raw_settings)) {
      return false;
    }
    if (cancellation.cancelled()) {
      return false;
    }
  }

  const auto crop_rect =
      crop_rect_for_image(
          uncropped_size,
          state);
  const auto crop_pixel_count =
      static_cast<std::uint64_t>(crop_rect.width()) *
      static_cast<std::uint64_t>(crop_rect.height());
  if (crop_rect.isEmpty() ||
      crop_pixel_count >
          kMaximumPhotoWorkingSurfaceBytes /
              sizeof(photo_editor::pipeline::ColorPixel)) {
    if (telemetry != nullptr) {
      telemetry->fallback_reason =
          QStringLiteral(
              "export_working_surface_memory_ceiling");
    }
    return false;
  }
  const auto camera_profile_source_area = QRectF(
      static_cast<double>(crop_rect.x()) / static_cast<double>(uncropped_size.width()),
      static_cast<double>(crop_rect.y()) / static_cast<double>(uncropped_size.height()),
      static_cast<double>(crop_rect.width()) / static_cast<double>(uncropped_size.width()),
      static_cast<double>(crop_rect.height()) / static_cast<double>(uncropped_size.height()));
  std::optional<photo_editor::pipeline::AssetIdentifier>
      export_profile;
  if (!state.export_icc_profile_asset.asset_id.trimmed().isEmpty()) {
    export_profile =
        photo_editor::pipeline::AssetIdentifier{
            state.export_icc_profile_asset.asset_id
                .trimmed()
                .toStdString()};
  } else if (!state.export_icc_profile_asset.payload_sha256
                  .trimmed()
                  .isEmpty()) {
    export_profile =
        photo_editor::pipeline::AssetIdentifier{
            "sha256:" +
            state.export_icc_profile_asset.payload_sha256
                .trimmed().toStdString()};
  }
  const auto output_name =
      state.output_color_space.trimmed().toLower();
  const auto rendering_target =
      options.hdr_delivery
          ? (options.hdr_transfer.compare(
                 QStringLiteral("HLG"),
                 Qt::CaseInsensitive) == 0
                 ? photo_editor::pipeline::DisplayRenderingTarget::HdrHlg
                 : photo_editor::pipeline::DisplayRenderingTarget::HdrPq)
          : (output_name.contains(QStringLiteral("display p3"))
                 ? photo_editor::pipeline::DisplayRenderingTarget::
                       DisplayP3Simulation
                 : photo_editor::pipeline::DisplayRenderingTarget::
                       SrgbSimulation);
  const auto plan_admission =
      RenderPlanStore::shared().admit(
          export_metadata.value(QStringLiteral("assetId"))
              .toString(),
          source_path,
          state,
          uncropped_size,
          photo_editor::pipeline::RenderPurpose::Export,
          rendering_target,
          export_profile,
          metadata.valid ? &metadata : nullptr);
  if (telemetry != nullptr) {
    telemetry->canonical_plan_cache_key =
        plan_admission.plan_cache_key;
    telemetry->canonical_recipe_cache_key =
        plan_admission.recipe_cache_key;
    telemetry->canonical_plan_error = plan_admission.error;
    telemetry->decode_preflight =
        decode_preflight;
  }
  if (normalized_photographic_rendering_model_name(
          state.photographic_rendering_model) ==
          QStringLiteral("aces2-ocio-v1") &&
      !plan_admission.admitted) {
    return false;
  }
  QImage image;
  if (options.hdr_delivery) {
    const auto target =
        hdr_display_target_for_export_settings(options);
    image = render_output_file(
        source_path,
        uncropped_size,
        crop_rect,
        state,
        export_target_color_space(state, options),
        options.image_format,
        metadata.valid ? &metadata : nullptr,
        telemetry,
        RenderMaskOptions{
            .rebuild_editable_masks_at_output_resolution = true,
            .camera_profile_source_area = camera_profile_source_area,
        },
        photo_editor::pipeline::RemovalQuality::Final,
        RenderOutputPolicy::DisplayReferred,
        &target,
        plan_admission.plan.has_value()
            ? &*plan_admission.plan
            : nullptr,
        cancellation);
  } else {
    image = render_output_file(
        source_path,
        uncropped_size,
        crop_rect,
        state,
        export_target_color_space(state, options),
        options.image_format,
        metadata.valid ? &metadata : nullptr,
        telemetry,
        RenderMaskOptions{
            .rebuild_editable_masks_at_output_resolution = true,
            .camera_profile_source_area = camera_profile_source_area,
        },
        photo_editor::pipeline::RemovalQuality::Final,
        options.scene_linear ? RenderOutputPolicy::SceneLinearWorking
                             : RenderOutputPolicy::DisplayReferred,
        nullptr,
        plan_admission.plan.has_value()
            ? &*plan_admission.plan
            : nullptr,
        cancellation);
  }
  if (telemetry != nullptr) {
    telemetry->canonical_plan_cache_key =
        plan_admission.plan_cache_key;
    telemetry->canonical_recipe_cache_key =
        plan_admission.recipe_cache_key;
    telemetry->canonical_plan_error = plan_admission.error;
    telemetry->decode_preflight =
        decode_preflight;
  }
  if (cancellation.cancelled() || image.isNull()) {
    return false;
  }
  if (options.max_edge > 0 && (image.width() > options.max_edge || image.height() > options.max_edge)) {
    image = image.scaled(options.max_edge, options.max_edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  image = apply_export_output_sharpening(
      image, export_output_sharpening_for_preset(preset), cancellation);
  if (cancellation.cancelled() || image.isNull()) return false;
  image = prepare_image_for_export_encoding(
      image, options);
  if (cancellation.cancelled() || image.isNull()) {
    return false;
  }
  if (rendered_output != nullptr) {
    *rendered_output = image;
  }

  if (options.writer_format == "pfm") {
    return write_pfm_image(image, output_path, cancellation);
  }
  if (options.writer_format == "exr") {
    return write_openexr_image(
        image, output_path, state, options, cancellation);
  }
  if (options.writer_format == "avif-hdr") {
    return write_hdr_avif_image(
        image, output_path, options, cancellation);
  }

  const auto description =
      QStringLiteral(
          "Software=Looksy;Asset=%1")
          .arg(
              export_metadata
                  .value(
                      QStringLiteral(
                          "assetId"))
                  .toString());
  const auto encoded =
      encode_prepared_standard_export_image(
          image, options, description);
  if (!encoded.valid || cancellation.cancelled()) {
    return false;
  }
  CancellationAwareSaveDevice file(
      output_path, cancellation);
  if (!file.openForWrite() ||
      file.write(encoded.bytes) !=
          encoded.bytes.size() ||
      cancellation.cancelled() ||
      !file.commit()) {
    return false;
  }
  if (options.writer_format == "tif") {
    if (cancellation.cancelled()) {
      QFile::remove(output_path);
      return false;
    }
    std::string failure_reason;
    const auto wrote_metadata =
        photo_editor::io::
            write_tiff_export_metadata(
                output_path.toStdString(),
                "Looksy",
                description.toStdString(),
                &failure_reason);
    if (!wrote_metadata) {
      qWarning().noquote()
          << "TIFF export metadata failed:"
          << QString::fromStdString(
                 failure_reason);
    }
    return wrote_metadata && !cancellation.cancelled();
  }
  return true;
}

QString output_file_path_for_job(
    const PersistentJobRecord& job,
    const QString& source_path,
    bool rendered_output) {
  const QFileInfo source_info(source_path);
  const auto safe_label = sanitize_token(display_label_for_job(job), QStringLiteral("export"));
  const auto suffix = rendered_output ? output_suffix_for_preset(job.preset_name, true) : source_info.suffix().toLower();
  const auto extension = suffix.isEmpty() ? QString{} : QStringLiteral(".%1").arg(suffix);

  auto stem = safe_label;
  if (!is_original_copy_preset(job.preset_name)) {
    stem += QStringLiteral("-%1").arg(preset_token(job.preset_name));
  }

  const QDir output_dir(job.destination_path);
  auto candidate = output_dir.filePath(stem + extension);
  if (!QFileInfo::exists(candidate)) {
    return candidate;
  }

  for (int attempt = 2; attempt < 1000; ++attempt) {
    const auto numbered = output_dir.filePath(QStringLiteral("%1-%2%3").arg(stem).arg(attempt).arg(extension));
    if (!QFileInfo::exists(numbered)) {
      return numbered;
    }
  }

  return {};
}

int queued_job_priority_rank(const PersistentJobRecord& job) {
  const auto requested_priority = [](const PersistentJobRecord& job) {
    const auto priority =
        job.metadata_json.value(QStringLiteral("priority")).toString().trimmed().toLower();
    return priority == QStringLiteral("high") || priority == QStringLiteral("low")
               ? priority
               : QStringLiteral("normal");
  };
  const auto workflow = [](const PersistentJobRecord& job) {
    const auto purpose = job.metadata_json.value(QStringLiteral("purpose"))
                             .toString().trimmed().toLower();
    const auto stage = job.metadata_json.value(QStringLiteral("stage"))
                           .toString().trimmed().toLower();
    return purpose.contains(QStringLiteral("preview")) ||
                   stage.contains(QStringLiteral("export_preview"))
               ? QStringLiteral("export-preview")
               : job.job_kind;
  };
  const auto affected_id = [](const PersistentJobRecord& job) {
    const auto timeline_id = job.metadata_json.value(QStringLiteral("timelineId"))
                                 .toString().trimmed();
    return timeline_id.isEmpty() ? job.asset_id : timeline_id;
  };
  return WorkerQueueRegistry::priorityDecision(
      workflow(job), worker_surface_for_job_kind(job.job_kind), affected_id(job), requested_priority(job))
      .value(QStringLiteral("effectiveRank")).toInt();
}

bool queued_job_less_than(const PersistentJobRecord& lhs, const PersistentJobRecord& rhs) {
  const auto left = queued_job_priority_rank(lhs), right = queued_job_priority_rank(rhs);
  return left == right ? job_order_less_than(lhs, rhs) : left > right;
}

QVariantMap run_export_work(
    const QString& source_path,
    const QString& recipe_json,
    const QString& preset_name,
    const QJsonObject& export_metadata,
    const QString& output_path,
    bool render_photo,
    const QJsonObject& export_render_job,
    const QJsonObject& export_preview_job,
    const std::shared_ptr<std::atomic_bool>& cancellation_requested) {
  QElapsedTimer timer;
  timer.start();
  const auto resource_policy = export_metadata
                                   .value(QStringLiteral("resourcePolicy"))
                                   .toString(QStringLiteral("playback-first"));
  auto* worker_thread = QThread::currentThread();
  const auto previous_priority = worker_thread->priority();
  worker_thread->setPriority(
      resource_policy == QStringLiteral("export-first")
          ? QThread::NormalPriority
          : resource_policy == QStringLiteral("balanced")
                ? QThread::LowPriority
                : QThread::LowestPriority);
  QVariantMap result;
  result.insert(QStringLiteral("success"), false);
  result.insert(QStringLiteral("outputPath"), output_path);
  result.insert(QStringLiteral("backend"), render_photo ? QStringLiteral("unknown") : QStringLiteral("copy"));
  result.insert(QStringLiteral("fallbackReason"), QString());
  result.insert(QStringLiteral("exportRenderJob"), export_render_job.toVariantMap());
  result.insert(QStringLiteral("exportPreviewJob"), export_preview_job.toVariantMap());
  const auto cancellation = photo_editor::pipeline::CancellationToken{
      .cancellation_requested = cancellation_requested.get(),
  };
  result.insert(QStringLiteral("canceled"), false);

  QDir().mkpath(QFileInfo(output_path).absolutePath());
  const auto output_info = QFileInfo(output_path);
  const auto staging_path = output_info.dir().filePath(
      QStringLiteral(".%1.partial-%2%3")
          .arg(
              output_info.completeBaseName(),
              QUuid::createUuid().toString(QUuid::WithoutBraces),
              output_info.suffix().isEmpty()
                  ? QString{}
                  : QStringLiteral(".") + output_info.suffix()));
  const auto discard_staging = [&]() {
    QFile::remove(staging_path);
    QFile::remove(staging_path + QStringLiteral(".integrity.json"));
  };
  discard_staging();
  RenderBackendTelemetry telemetry;
  QImage rendered_output;
  const auto write_succeeded = !cancellation.cancelled() &&
                               (render_photo
                                   ? render_photo_export(
                                         source_path,
                                         recipe_json,
                                         preset_name,
                                         export_metadata,
                                         staging_path,
                                         &telemetry,
                                         &rendered_output,
                                         cancellation)
                                   : QFile::copy(source_path, staging_path));
  if (render_photo) {
    result.insert(
        QStringLiteral("backend"),
        telemetry.backend.isEmpty() ? QStringLiteral("unknown") : telemetry.backend);
    result.insert(QStringLiteral("fallbackReason"), telemetry.fallback_reason);
    result.insert(
        QStringLiteral("canonicalPlanCacheKey"),
        telemetry.canonical_plan_cache_key);
    result.insert(
        QStringLiteral("canonicalRecipeCacheKey"),
        telemetry.canonical_recipe_cache_key);
    result.insert(
        QStringLiteral("canonicalPlanError"),
        telemetry.canonical_plan_error);
    result.insert(
        QStringLiteral("decodePreflight"),
        telemetry.decode_preflight);
    result.insert(
        QStringLiteral("decodeExecution"),
        telemetry.decode_execution);
    result.insert(
        QStringLiteral("completedRenderStages"),
        telemetry.completed_render_stages);
    result.insert(
        QStringLiteral("executorSubmissionCount"),
        telemetry.executor_submission_count);
    result.insert(
        QStringLiteral("geometryExecutorCacheHit"),
        telemetry.geometry_executor_cache_hit);
    if (!write_succeeded &&
        !telemetry.canonical_plan_error.trimmed().isEmpty()) {
      result.insert(
          QStringLiteral("errorMessage"),
          QStringLiteral("Canonical render plan rejected export: %1")
              .arg(telemetry.canonical_plan_error));
    }
  }
  auto success = write_succeeded && !cancellation.cancelled();
  auto artifact_verification = ExportArtifactVerification{};
  if (write_succeeded && render_photo) {
    const auto export_settings = export_image_settings_for_preset(preset_name);
    artifact_verification = verify_export_artifact(
        staging_path,
        rendered_output,
        export_settings,
        export_render_job);
    result.insert(
        QStringLiteral("artifactVerification"),
        export_artifact_verification_to_map(
            artifact_verification,
            export_settings));
    success = artifact_verification.valid;
    if (!success) {
      discard_staging();
    }
  }
  result.insert(QStringLiteral("success"), success);
  if (success && render_photo) {
    const auto tolerance = export_preview_job.value(QStringLiteral("tolerance"))
                               .toInt(preview_export_diff_tolerance_for_preset(preset_name));
    const auto preview_export_diff = compare_preview_to_export(
        rendered_output,
        artifact_verification.decoded_image.isNull()
            ? QImage(staging_path)
            : artifact_verification.decoded_image,
        tolerance);
    result.insert(
        QStringLiteral("previewExportDiff"),
        preview_export_diff_to_map(preview_export_diff, export_preview_job));
  }
  if (!success &&
      result.value(QStringLiteral("errorMessage"))
          .toString()
          .trimmed()
          .isEmpty()) {
    result.insert(
        QStringLiteral("errorMessage"),
        render_photo && write_succeeded
            ? QStringLiteral("Encoded export failed artifact verification: %1")
                  .arg(artifact_verification.failure_reason)
            : (render_photo ? QStringLiteral("Unable to render the exported photo.")
                            : QStringLiteral("Unable to write the exported file.")));
  }
  if (success && !cancellation.cancelled()) {
    if (QFileInfo::exists(output_path) ||
        !QFile::rename(staging_path, output_path)) {
      success = false;
      result.insert(QStringLiteral("success"), false);
      result.insert(
          QStringLiteral("errorMessage"),
          QStringLiteral("The completed export could not be published atomically."));
    }
  }
  if (success && !cancellation.cancelled()) {
    const auto output_integrity =
        write_delivery_integrity(output_path, cancellation);
    result.insert(QStringLiteral("outputIntegrity"), output_integrity);
    if (!output_integrity.value(QStringLiteral("valid")).toBool()) {
      success = false;
      result.insert(QStringLiteral("success"), false);
      result.insert(
          QStringLiteral("errorMessage"),
          QStringLiteral("The output was written but could not be checksum-verified."));
    }
  }
  if (cancellation.cancelled()) {
    success = false;
    result.insert(QStringLiteral("success"), false);
    result.insert(QStringLiteral("canceled"), true);
    result.insert(
        QStringLiteral("errorMessage"),
        QStringLiteral("Canceled before the export artifact was published."));
    discard_staging();
    remove_delivery_artifact(output_path);
  } else if (!success) {
    discard_staging();
  }
  result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  result.insert(QStringLiteral("resourcePolicy"), resource_policy);
  result.insert(QStringLiteral("resourcePolicyAffectsQuality"), false);
  worker_thread->setPriority(
      previous_priority == QThread::InheritPriority
          ? QThread::NormalPriority
          : previous_priority);
  return result;
}

QVariantMap run_timeline_render_work(
    const QString& document_path,
    const QString& output_path,
    const QString& timeline_id,
    const QString& resource_policy,
    const std::shared_ptr<std::atomic_bool>& cancellation) {
  QElapsedTimer timer;
  timer.start();
  QVariantMap result;
  result.insert(QStringLiteral("success"), false);

  TimelineDocumentController controller;
  if (!controller.loadTimeline(document_path)) {
    result.insert(QStringLiteral("errorMessage"), controller.status());
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }
  controller.setBackgroundRenderResourcePolicy(resource_policy);
  controller.setRenderCancellationToken(cancellation);
  const auto canceled = [&]() { return cancellation && cancellation->load(std::memory_order_relaxed); };
  const auto fail = [&](const QString& message) {
    result.insert(QStringLiteral("canceled"), canceled());
    result.insert(QStringLiteral("errorMessage"), message);
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  };
  if (canceled()) return fail(QStringLiteral("Timeline render canceled before staging."));
  if (!QDir().mkpath(QFileInfo(output_path).absolutePath()))
    return fail(QStringLiteral("Unable to create the timeline delivery folder."));
  JobOutputTransaction delivery(output_path);
  if (!delivery.valid()) return fail(QStringLiteral("Unable to stage timeline delivery."));
  const auto staged_output = delivery.stagedPath(QFileInfo(output_path).fileName());

  if (!controller.renderCurrentTimeline(staged_output)) {
    return fail(canceled() ? QStringLiteral("Timeline render canceled.") : controller.status());
  }
  const auto output_integrity = write_delivery_integrity(staged_output,
      {.cancellation_requested = cancellation.get()}, output_path);
  if (!output_integrity.value(QStringLiteral("valid")).toBool())
    return fail(QStringLiteral("The staged timeline output could not be checksum-verified."));
  if (canceled()) return fail(QStringLiteral("Timeline render canceled before publication."));
  const auto publication = delivery.publish(canceled);
  if (!publication.value("ok").toBool()) return fail(publication.value("error").toString());
  result.insert(QStringLiteral("outputPath"), output_path);
  result.insert(QStringLiteral("deliveryArtifacts"), delivery.artifacts());

  const auto poster_source = controller.generateVideoPoster(output_path, timeline_id);
  if (!poster_source.isEmpty()) {
    result.insert(QStringLiteral("posterPath"), QUrl(poster_source).toLocalFile());
  }
  result.insert(QStringLiteral("outputIntegrity"), output_integrity);
  if (canceled()) {
    result.insert(QStringLiteral("publicationRollback"), delivery.rollback());
    result.remove(QStringLiteral("outputPath"));
    return fail(QStringLiteral("Timeline delivery canceled during verification."));
  }
  if (!output_integrity.value(QStringLiteral("valid")).toBool()) {
    result.insert(
        QStringLiteral("errorMessage"),
        QStringLiteral("The timeline output was written but could not be checksum-verified."));
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }
  result.insert(QStringLiteral("success"), true);
  result.insert(QStringLiteral("resourcePolicy"), resource_policy);
  result.insert(QStringLiteral("resourcePolicyAffectsQuality"), false);
  result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  return result;
}

bool save_document_object(const QString& document_path, const QJsonObject& document, QString* error_message) {
  if (document_path.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline document path is missing.");
    }
    return false;
  }

  QDir().mkpath(QFileInfo(document_path).absolutePath());
  QSaveFile file(document_path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to write the saved timeline document.");
    }
    return false;
  }

  file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to commit the saved timeline document.");
    }
    return false;
  }

  return true;
}

bool update_timeline_render_artifacts(
    const PersistentJobRecord& job,
    const QString& poster_path,
    QString* error_message) {
  const auto timeline_id = job.metadata_json.value(QStringLiteral("timelineId")).toString().trimmed();
  if (timeline_id.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render job is missing its timeline id.");
    }
    return false;
  }

  const auto canonical_path = job.metadata_json.value(QStringLiteral("canonicalDocumentPath")).toString().trimmed();
  const auto editable_path = canonical_path.isEmpty() ? job.source_path : canonical_path;
  auto records = loadTimelineAssetRecords();
  const auto updated_at = iso_now();
  TimelineAssetRecord persisted_record;
  auto updated_existing = false;
  for (auto& record : records) {
    if (record.timeline_id != timeline_id) {
      continue;
    }

    record.asset_id = job.asset_id.isEmpty() ? QStringLiteral("timeline:%1").arg(timeline_id) : job.asset_id;
    if (canonical_path.isEmpty()) record.name = job.display_label;
    record.document_path = editable_path;
    record.render_path = job.output_path;
    if (!poster_path.isEmpty()) {
      record.poster_path = poster_path;
    }
    if (record.created_at.isEmpty()) {
      record.created_at = job.created_at;
    }
    record.updated_at = updated_at;
    persisted_record = record;
    updated_existing = true;
    break;
  }

  if (!updated_existing) {
    persisted_record.asset_id = job.asset_id.isEmpty() ? QStringLiteral("timeline:%1").arg(timeline_id) : job.asset_id;
    persisted_record.timeline_id = timeline_id;
    persisted_record.name = job.display_label;
    persisted_record.document_path = editable_path;
    persisted_record.render_path = job.output_path;
    persisted_record.poster_path = poster_path.isEmpty()
                                       ? job.metadata_json.value(QStringLiteral("posterPath")).toString().trimmed()
                                       : poster_path;
    persisted_record.duration_seconds = job.metadata_json.value(QStringLiteral("durationSeconds")).toDouble();
    persisted_record.created_at = job.created_at;
    persisted_record.updated_at = updated_at;
    records.push_back(persisted_record);
  }

  if (!saveTimelineAssetRecords(records)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render completed, but the gallery index could not be updated.");
    }
    return false;
  }

  // Snapshot jobs must never overwrite either the immutable render input or
  // a newer canonical edit/backup when the older render finishes.
  if (!canonical_path.isEmpty()) return true;

  QJsonObject document;
  QFile file(job.source_path);
  if (file.exists() && file.open(QIODevice::ReadOnly)) {
    const auto parsed = QJsonDocument::fromJson(file.readAll());
    if (parsed.isObject()) {
      document = parsed.object();
    }
  }
  if (document.isEmpty()) {
    document = loadTimelineDocumentBackup(timeline_id);
  }
  if (document.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render completed, but the saved document could not be refreshed.");
    }
    return false;
  }

  document.insert(QStringLiteral("renderPath"), job.output_path);
  if (!persisted_record.poster_path.isEmpty()) {
    document.insert(QStringLiteral("posterPath"), persisted_record.poster_path);
  }
  document.insert(QStringLiteral("updatedAt"), updated_at);

  QString document_error;
  if (!save_document_object(job.source_path, document, &document_error)) {
    if (error_message != nullptr) {
      *error_message = document_error;
    }
    return false;
  }

  if (!saveTimelineDocumentBackup(persisted_record, document)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline render completed, but the catalog backup could not be refreshed.");
    }
    return false;
  }

  return true;
}

QString derive_drive_id(const QString& folder_path) {
  const auto digest = QCryptographicHash::hash(folder_path.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QStringLiteral("drive-%1").arg(QString::fromLatin1(digest.left(12)));
}

QString derive_drive_name(const QString& folder_path) {
  const QFileInfo info(folder_path);
  if (!info.fileName().isEmpty()) {
    return info.fileName();
  }
  return folder_path;
}

QString import_program() {
  return AppBootstrap::helperProgram(QStringLiteral("import_catalog"));
}

QStringList import_arguments(const QString& folder_path, const QString& import_mode) {
  const auto drive_id = derive_drive_id(folder_path);
  const auto drive_name = derive_drive_name(folder_path);

  return AppBootstrap::helperArguments(
      QStringLiteral("import_catalog"),
      {folder_path, drive_id, drive_name, import_mode});
}

QString selection_manifest_path(const QJsonObject& metadata_json) {
  const auto selected_paths = metadata_json.value(QStringLiteral("selectedPaths")).toArray();
  if (selected_paths.isEmpty()) {
    return {};
  }

  auto temp_root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (temp_root.isEmpty()) {
    temp_root = QDir::tempPath();
  }

  const auto manifest_path = QDir(temp_root).filePath(
      QStringLiteral("photo-editor-import-runner-%1.txt").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  QFile manifest_file(manifest_path);
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return {};
  }

  for (const auto& value : selected_paths) {
    const auto path = normalized_local_path(value.toString());
    if (path.isEmpty()) {
      continue;
    }
    manifest_file.write(path.toUtf8());
    manifest_file.write("\n");
  }
  manifest_file.close();
  return manifest_path;
}

void remove_import_partial_files(const QString& source_path, const QJsonObject& metadata_json) {
  const auto preflight = metadata_json.value(QStringLiteral("importPreflight")).toObject();
  const QStringList storage_roots = {
      preflight.value(QStringLiteral("photoLibraryRoot")).toString(),
      preflight.value(QStringLiteral("backupRoot")).toString(),
  };
  for (const auto& storage_root : storage_roots) {
    if (storage_root.isEmpty()) {
      continue;
    }
    const auto drive_root = QDir(storage_root).filePath(derive_drive_id(source_path));
    if (!QFileInfo::exists(drive_root)) {
      continue;
    }
    QDirIterator iterator(
        drive_root,
        QStringList{QStringLiteral("*.virel-partial")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      QFile::remove(iterator.next());
    }
  }
}

QVariantMap run_import_work(
    const QString& job_id,
    const QString& source_path,
    const QJsonObject& metadata_json,
    int metadata_threads,
    const std::shared_ptr<std::atomic_bool>& cancellation) {
  QElapsedTimer timer;
  timer.start();
  QVariantMap result;
  result.insert(QStringLiteral("success"), false);

  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    result.insert(QStringLiteral("errorMessage"), environment_error);
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }

  if (metadata_threads > 0)
    environment.insert(QStringLiteral("PHOTO_EDITOR_IMPORT_METADATA_THREADS"), QString::number(metadata_threads));

  const auto import_mode = metadata_json.value(QStringLiteral("importMode")).toString().trimmed().isEmpty()
                               ? QStringLiteral("referenced")
                               : metadata_json.value(QStringLiteral("importMode")).toString().trimmed();
  auto arguments = import_arguments(source_path, import_mode);
  const auto manifest_path = selection_manifest_path(metadata_json);
  if (metadata_json.value(QStringLiteral("selectionCount")).toInt() > 0 && manifest_path.isEmpty()) {
    result.insert(QStringLiteral("errorMessage"), QStringLiteral("Unable to stage the selected photos for import."));
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }
  arguments.push_back(manifest_path.isEmpty() ? QStringLiteral("-") : manifest_path);
  const auto reviewed_preflight = metadata_json.value(QStringLiteral("importPreflight")).toObject();
  const auto photo_library_root =
      reviewed_preflight.value(QStringLiteral("photoLibraryRoot")).toString().trimmed();
  const auto backup_root =
      reviewed_preflight.value(QStringLiteral("backupRoot")).toString().trimmed();
  const auto organization_template =
      reviewed_preflight.value(QStringLiteral("organizationTemplate")).toString().trimmed();
  const auto raw_jpeg_policy =
      reviewed_preflight.value(QStringLiteral("rawJpegPolicy")).toString().trimmed();
  const auto duplicate_policy =
      metadata_json.value(QStringLiteral("duplicatePolicy")).toString().trimmed();
  arguments.push_back(photo_library_root.isEmpty() ? QStringLiteral("-") : photo_library_root);
  arguments.push_back(backup_root.isEmpty() ? QStringLiteral("-") : backup_root);
  arguments.push_back(organization_template.isEmpty() ? QStringLiteral("source") : organization_template);
  arguments.push_back(raw_jpeg_policy.isEmpty() ? QStringLiteral("both") : raw_jpeg_policy);
  arguments.push_back(duplicate_policy.isEmpty() ? QStringLiteral("skip") : duplicate_policy);

  QProcess process;
  process.setWorkingDirectory(QDir::currentPath());
  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  WorkerQueueRegistry::configureBackgroundProcess(&process, QStringLiteral("catalog_import"));
  process.start(import_program(), arguments);

  const auto started = process.waitForStarted(5000);
  auto finished = false;
  auto canceled = false;
  auto paused = false;
  while (started && process.state() != QProcess::NotRunning) {
    if (process.waitForFinished(100)) {
      finished = true;
      break;
    }

    const auto records = loadPersistedJobRecords();
    const auto record_it = std::find_if(
        records.cbegin(),
        records.cend(),
        [&](const auto& record) { return record.job_id == job_id; });
    canceled = record_it != records.cend() &&
               (record_it->status == QStringLiteral("Canceling") ||
                record_it->metadata_json.value(QStringLiteral("cancellationRequested")).toBool());
    paused = record_it != records.cend() &&
             (record_it->status == QStringLiteral("Pausing") ||
              record_it->metadata_json.value(QStringLiteral("pauseRequested")).toBool());
    canceled = canceled || (cancellation && cancellation->load(std::memory_order_relaxed) && !paused);
    if (!canceled && !paused) {
      continue;
    }

    process.terminate();
    if (!process.waitForFinished(2000)) {
      process.kill();
      process.waitForFinished(2000);
    }
    finished = true;
    break;
  }
  finished = started && process.state() == QProcess::NotRunning;
  const auto output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  if (!manifest_path.isEmpty()) {
    QFile::remove(manifest_path);
  }
  remove_import_partial_files(source_path, metadata_json);

  if (canceled) {
    result.insert(QStringLiteral("canceled"), true);
    result.insert(QStringLiteral("errorMessage"), QStringLiteral("Import canceled safely."));
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }
  if (paused) {
    result.insert(QStringLiteral("paused"), true);
    result.insert(QStringLiteral("errorMessage"), QStringLiteral("Import paused safely."));
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }

  if (!started || !finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    result.insert(
        QStringLiteral("errorMessage"),
        output.isEmpty() ? QStringLiteral("Import failed.") : output);
    result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
    return result;
  }

  result.insert(QStringLiteral("success"), true);
  auto import_result = metadata_json.value(QStringLiteral("importPreflight")).toObject();
  const auto marker = QStringLiteral("VIREL_IMPORT_RESULT=");
  for (const auto& line : output.split(QLatin1Char('\n'))) {
    const auto marker_index = line.indexOf(marker);
    if (marker_index < 0) {
      continue;
    }
    const auto payload = QJsonDocument::fromJson(line.mid(marker_index + marker.size()).trimmed().toUtf8());
    if (!payload.isObject()) {
      continue;
    }
    const auto worker_result = payload.object();
    import_result.insert(QStringLiteral("schema"), QStringLiteral("import-result:v1"));
    import_result.insert(QStringLiteral("importedCount"), worker_result.value(QStringLiteral("importedCount")));
    import_result.insert(QStringLiteral("resumedCount"), worker_result.value(QStringLiteral("resumedCount")));
    import_result.insert(QStringLiteral("failureCount"), worker_result.value(QStringLiteral("failureCount")));
    // Preserve the worker's backup receipt alongside its import counts. The
    // preflight says what was requested, not what was actually verified.
    for (const auto& key : {QStringLiteral("backedUpCount"), QStringLiteral("reusedBackupCount"),
                           QStringLiteral("verificationAlgorithm"), QStringLiteral("backupDestination")}) {
      if (worker_result.contains(key)) import_result.insert(key, worker_result.value(key));
    }
    const auto worker_duplicate_count =
        worker_result.value(QStringLiteral("skippedDuplicateCount")).toInt();
    import_result.insert(
        QStringLiteral("duplicateCount"),
        import_result.value(QStringLiteral("duplicateCount")).toInt() +
            worker_duplicate_count);
    import_result.insert(
        QStringLiteral("workerSkippedDuplicateCount"),
        worker_duplicate_count);
    const auto worker_unsupported = worker_result.value(QStringLiteral("skippedUnsupported")).toInt();
    const auto preflight_unsupported = import_result.value(QStringLiteral("unsupportedCount")).toInt();
    import_result.insert(
        QStringLiteral("unsupportedCount"),
        qMax(worker_unsupported, preflight_unsupported));
    import_result.insert(QStringLiteral("skippedUnsupported"), worker_unsupported);
  }
  const auto skipped_count =
      (metadata_json.value(QStringLiteral("duplicatePolicy")).toString() == QStringLiteral("skip")
           ? import_result.value(QStringLiteral("duplicateCount")).toInt()
           : 0) +
      import_result.value(QStringLiteral("unsupportedCount")).toInt() +
      import_result.value(QStringLiteral("unreadableCount")).toInt();
  import_result.insert(QStringLiteral("skippedCount"), skipped_count);
  result.insert(QStringLiteral("importResult"), import_result.toVariantMap());
  if (!output.isEmpty()) {
    result.insert(QStringLiteral("resultMessage"), output);
  }
  result.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  return result;
}

}  // namespace

JobRunner::JobRunner(QObject* parent) : QObject(parent) {
  connect(&cancellation_watcher_, &QFutureWatcher<QVector<JobCancellationReadResult>>::finished,
          this, &JobRunner::finishCancellationObservation);
  connect(&queue_watcher_, &QFutureWatcher<JobQueueReadResult>::finished,
          this, &JobRunner::finishQueueObservation);
  connect(&recovery_watcher_, &QFutureWatcher<QString>::finished, this, [this] {
    const auto error = recovery_watcher_.result();
    recovering_ = false;
    ready_ = error.isEmpty();
    emit startupRecoveryFinished(error);
    if (ready_) wake();
    else {
      observability::logWarning(photoEditorJobsLog(), QStringLiteral("job.recovery.failed"),
          {{QStringLiteral("error"), error}});
      // Keep dispatch closed. Retry recovery itself, never an ordinary scan.
      if (!wake_timer_.isActive()) wake_timer_.start(1000);
    }
  });
  wake_timer_.setSingleShot(true);
  connect(&wake_timer_, &QTimer::timeout, this, &JobRunner::processNextJob);
  connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, this, &JobRunner::wake);
}

JobAdmissionBudget JobRunner::admissionBudget() const {
  static const quint64 installed_memory = []() -> quint64 {
#if defined(Q_OS_MACOS)
    uint64_t bytes = 0;
    size_t size = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) return bytes;
#elif defined(Q_OS_UNIX)
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) return static_cast<quint64>(pages) * static_cast<quint64>(page_size);
#endif
    // Unknown capacity and platforms without file-identity proof stay serial.
    return 0;
  }();
  const auto pressure = photo_cache_budget_snapshot().value(QStringLiteral("pressure")).toString();
  const bool foreground = WorkerQueueRegistry::userIntentSnapshot().value(QStringLiteral("latencySensitive")).toBool();
  return job_admission_budget(QThread::idealThreadCount(), installed_memory,
                              pressure != QStringLiteral("normal"), foreground);
}

bool JobRunner::refreshAttemptResources(const AttemptPtr& attempt, const PersistentJobRecord& job) {
  if (!isCurrentAttempt(attempt)) return false;
  if (job_execution_specification(job) != attempt->resources.specification) {
    releaseAttempt(attempt);
    return false;
  }
  const auto refreshed = inspect_job_resources(job);
  QVector<JobResourceClaim> active;
  for (const auto& other : active_attempts_) if (other != attempt) active.push_back(other->resources);
  if (!admit_job_resources(refreshed, active, admissionBudget())) {
    releaseAttempt(attempt);
    return false;
  }
  attempt->resources = refreshed;
  return true;
}

JobRunner::AttemptPtr JobRunner::beginAttempt(const PersistentJobRecord& job) {
  if (active_attempts_.contains(job.job_id)) return {};
  auto attempt = std::make_shared<Attempt>();
  attempt->job_id = job.job_id;
  attempt->job_kind = job.job_kind;
  attempt->queue_wait_ms = queue_wait_msecs_for_job(job);
  attempt->timer.start();
  attempt->watcher = new QFutureWatcher<QVariantMap>(this);
  active_attempts_.insert(job.job_id, attempt);
  // The map owns the attempt. A weak callback avoids a watcher/attempt cycle;
  // the locked reference keeps it alive through completion and map removal.
  connect(attempt->watcher.data(), &QFutureWatcher<QVariantMap>::finished, this,
          [this, weak = std::weak_ptr<Attempt>(attempt)] {
            if (auto finished = weak.lock()) handleAsyncJobFinished(finished);
          });
  return attempt;
}

bool JobRunner::isCurrentAttempt(const AttemptPtr& attempt) const {
  return attempt && active_attempts_.value(attempt->job_id) == attempt;
}

void JobRunner::releaseAttempt(const AttemptPtr& attempt) {
  // A late callback for an older retry must not release its replacement.
  if (!isCurrentAttempt(attempt)) return;
  active_attempts_.remove(attempt->job_id);
  QTimer::singleShot(0, this, &JobRunner::processNextJob);
}

void JobRunner::observeActiveJobCancellation() {
  if (active_attempts_.isEmpty()) return;
  if (cancellation_reading_) {
    cancellation_pending_ = true;
    return;
  }
  QVector<JobCancellationReadResult> attempts;
  attempts.reserve(active_attempts_.size());
  for (const auto& attempt : active_attempts_) {
    attempts.push_back({attempt->job_id, attempt->cancellation, {}, false});
  }
  cancellation_reading_ = true;
  // One authoritative read covers all captured attempts. Workers retain only
  // IDs and tokens, so late replies cannot cancel a retry with the same ID.
  cancellation_watcher_.setFuture(QtConcurrent::run([attempts = std::move(attempts)]() mutable {
    QString read_error;
    QVector<PersistentJobRecord> records;
    try {
      records = loadPersistedJobRecords(&read_error, false);
    } catch (const std::exception& error) {
      read_error = QString::fromUtf8(error.what());
    } catch (...) {
      read_error = QStringLiteral("Unable to read job cancellation state.");
    }
    for (auto& attempt : attempts) {
      attempt.error = read_error;
      if (!read_error.isEmpty()) continue;
      const auto record = std::find_if(records.cbegin(), records.cend(),
          [&attempt](const auto& value) { return value.job_id == attempt.job_id; });
      attempt.requested = record == records.cend() ||
          record->status == QStringLiteral("Canceling") ||
          record->status == QStringLiteral("Pausing") ||
          record->metadata_json.value(QStringLiteral("cancellationRequested")).toBool() ||
          record->metadata_json.value(QStringLiteral("pauseRequested")).toBool();
    }
    return attempts;
  }));
}

void JobRunner::finishCancellationObservation() {
  const auto results = cancellation_watcher_.result();
  cancellation_reading_ = false;
  QString retry_error;
  for (const auto& result : results) {
    const auto current = active_attempts_.value(result.job_id);
    if (!current || result.cancellation != current->cancellation) continue;
    if (!result.error.isEmpty()) retry_error = result.error;
    else if (result.requested) current->cancellation->store(true, std::memory_order_relaxed);
  }
  if (cancellation_pending_) {
    cancellation_pending_ = false;
    wake();
  } else if (!retry_error.isEmpty()) {
    // A failed read is not proof of deletion or cancellation.
    observability::logWarning(photoEditorJobsLog(), QStringLiteral("job.cancellation.read_failed"),
        {{QStringLiteral("error"), retry_error}});
    if (!wake_timer_.isActive()) wake_timer_.start(1000);
  }
}

void JobRunner::initializeFromStore() {
  if (ready_ || recovering_) return;
  recovery_requested_ = true;
  recovering_ = true;
  recovery_watcher_.setFuture(QtConcurrent::run([] {
    QString error;
    try {
      (void)recoverInterruptedJobRecords(&error, true);
    } catch (const std::exception& exception) {
      error = QString::fromUtf8(exception.what());
    } catch (...) {
      error = QStringLiteral("Unable to recover interrupted jobs.");
    }
    return error;
  }));
}

void JobRunner::wake() {
  if (!ready_ && (!recovery_requested_ || recovering_)) return;
  // A notification burst needs one scan, not one blocking helper per signal.
  // A fresh notification may bring higher-priority work, so it expedites a
  // deferred retry without multiplying scheduled callbacks.
  if (!wake_timer_.isActive() || wake_timer_.interval() != 0) wake_timer_.start(0);
}

void JobRunner::processNextJob() {
  if (!ready_) {
    if (recovery_requested_) initializeFromStore();
    return;
  }
  if (!active_attempts_.isEmpty()) {
    observeActiveJobCancellation();
    const auto budget = admissionBudget();
    const bool exclusive = std::any_of(active_attempts_.cbegin(), active_attempts_.cend(),
        [](const auto& attempt) { return attempt->resources.lane == JobResourceLane::Exclusive; });
    if (exclusive || active_attempts_.size() >= 2 ||
        (budget.maximum_jobs == 1 && !budget.temporarily_limited)) return;
    // Under temporary pressure, read once to discover whether eligible work is
    // waiting. Only a blocked eligible job schedules a retry; idle queues do not
    // poll, and permanently serial hosts retain notification-driven observation.
  }
  if (queue_reading_) {
    queue_pending_ = true;
    return;
  }
  queue_reading_ = true;
  // Observing the queue must not wait for database helpers on the UI thread or
  // migrate legacy history. Startup recovery retains ownership of migration.
  // No worker captures this QObject, so shutdown does not wait on a helper.
  QVector<JobResourceClaim> active_resources;
  QStringList active_ids;
  for (const auto& attempt : active_attempts_) {
    active_resources.push_back(attempt->resources);
    active_ids.push_back(attempt->job_id);
  }
  auto inspection_budget = admissionBudget();
  if (inspection_budget.temporarily_limited) inspection_budget.maximum_jobs = 2;
  queue_watcher_.setFuture(QtConcurrent::run([active_resources, active_ids, inspection_budget] {
    JobQueueReadResult result;
    try {
      result.records = loadPersistedJobRecords(&result.error, false);
      if (result.error.isEmpty()) {
        struct Candidate { const PersistentJobRecord* job; int priority; };
        QVector<Candidate> candidates;
        for (const auto& job : std::as_const(result.records)) {
          if (job.status == QStringLiteral("Queued") && !active_ids.contains(job.job_id) &&
              (job.job_kind == QStringLiteral("asset_export") || job.job_kind == QStringLiteral("catalog_import") ||
               job.job_kind == QStringLiteral("timeline_render")))
            candidates.push_back({&job, queued_job_priority_rank(job)});
        }
        // Precomputed ranks make the worker comparator stable even if foreground
        // intent changes while this background scan is running.
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
          return a.priority == b.priority ? job_order_less_than(*a.job, *b.job) : a.priority > b.priority;
        });
        for (const auto& candidate : candidates) {
          const auto& job = *candidate.job;
          const auto lane = potential_job_resource_lane(job);
          if (!active_resources.isEmpty() && (lane == JobResourceLane::Exclusive ||
              std::any_of(active_resources.cbegin(), active_resources.cend(),
                  [lane](const auto& current) { return current.lane == lane || current.lane == JobResourceLane::Exclusive; })))
            continue;
          const auto resources = inspect_job_resources(job, active_resources);
          result.resources.insert(job.job_id, resources);
          if (admit_job_resources(resources, active_resources, inspection_budget)) break;
        }
      }
    } catch (const std::exception& error) {
      result.error = QString::fromUtf8(error.what());
    } catch (...) {
      result.error = QStringLiteral("Unable to read the job queue.");
    }
    return result;
  }));
}

void JobRunner::finishQueueObservation() {
  const auto result = queue_watcher_.result();
  queue_reading_ = false;
  if (!result.error.isEmpty()) {
    observability::logWarning(photoEditorJobsLog(), QStringLiteral("job.queue.read_failed"),
        {{QStringLiteral("error"), result.error}});
  }
  if (queue_pending_ || (wake_timer_.isActive() && wake_timer_.interval() == 0)) {
    // A newer notification invalidates this snapshot. Read the latest queue
    // once, rather than starting a job from history superseded during the read.
    queue_pending_ = false;
    wake();
    return;
  }
  if (!result.error.isEmpty()) {
    // Legacy fallback on failure is not authority to execute queued work.
    if (!wake_timer_.isActive()) wake_timer_.start(1000);
    return;
  }

  QVector<PersistentJobRecord> queued_jobs;
  for (const auto& record : result.records) {
    if ((record.job_kind == QStringLiteral("asset_export") ||
         record.job_kind == QStringLiteral("catalog_import") ||
         record.job_kind == QStringLiteral("timeline_render")) &&
        record.status == QStringLiteral("Queued")) {
      queued_jobs.push_back(record);
    }
  }

  if (queued_jobs.isEmpty()) {
    return;
  }

  std::sort(queued_jobs.begin(), queued_jobs.end(), queued_job_less_than);
  QVector<JobResourceClaim> active;
  for (const auto& attempt : active_attempts_) active.push_back(attempt->resources);
  const auto budget = admissionBudget();
  bool deferred = false;
  bool capacity_deferred = false;
  for (const auto& job : queued_jobs) {
    if (active_attempts_.contains(job.job_id)) continue;
    if (!result.resources.contains(job.job_id)) {
      // A completion or changed foreground priority may make an uninspected
      // candidate the next standalone job. Refresh instead of guessing its claim.
      if (active.isEmpty()) { wake(); return; }
      continue;
    }
    const auto resources = result.resources.value(job.job_id);
    if (!admit_job_resources(resources, active, budget)) {
      capacity_deferred = capacity_deferred || (budget.temporarily_limited &&
          admit_job_resources(resources, active, {2, budget.additional_memory_bytes, false}));
      continue;
    }
    const auto purpose = job.metadata_json.value(QStringLiteral("purpose"))
                             .toString().trimmed().toLower();
    const auto stage = job.metadata_json.value(QStringLiteral("stage"))
                           .toString().trimmed().toLower();
    const auto scheduling_workflow =
        purpose.contains(QStringLiteral("preview")) ||
                stage.contains(QStringLiteral("export_preview"))
            ? QStringLiteral("export-preview")
            : job.job_kind;
    const auto timeline_id = job.metadata_json.value(QStringLiteral("timelineId"))
                                 .toString().trimmed();
    const auto scheduling_decision = WorkerQueueRegistry::priorityDecision(
        scheduling_workflow,
        worker_surface_for_job_kind(job.job_kind),
        timeline_id.isEmpty() ? job.asset_id : timeline_id,
        job.metadata_json.value(QStringLiteral("priority")).toString());
    if (scheduling_decision.value(QStringLiteral("deferred")).toBool()) {
      deferred = true;
      continue;
    }
    const auto attempt = beginAttempt(job);
    if (!attempt) continue;
    attempt->resources = resources;
    observability::logInfo(
        photoEditorJobsLog(),
        QStringLiteral("job.started"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), job.job_kind},
            {QStringLiteral("asset_id"), job.asset_id},
            {QStringLiteral("source_path"), job.source_path},
            {QStringLiteral("queue_wait_ms"), attempt->queue_wait_ms},
            {QStringLiteral("active_jobs"), active_attempts_.size()},
            {QStringLiteral("resource_lane"), static_cast<int>(resources.lane)},
            {QStringLiteral("additional_memory_reserve_bytes"), resources.additional_memory_bytes},
            {QStringLiteral("effective_priority"),
             scheduling_decision.value(QStringLiteral("effectivePriority"))},
            {QStringLiteral("priority_reason"),
             scheduling_decision.value(QStringLiteral("reason"))},
        });
    WorkerQueueRegistry::logSample(
        worker_pool_id_for_job_kind(job.job_kind),
        job.job_kind,
        worker_surface_for_job_kind(job.job_kind),
        static_cast<int>(std::count_if(active_attempts_.cbegin(), active_attempts_.cend(),
            [&job](const auto& current) { return current->job_kind == job.job_kind; })),
        static_cast<int>(qMax<qsizetype>(0, queued_jobs.size() - 1)),
        job.job_kind == QStringLiteral("asset_export") ? 2 : 1,
        attempt->queue_wait_ms);
    if (job.job_kind == QStringLiteral("catalog_import")) {
      QTimer::singleShot(0, this, [this, attempt]() { if (isCurrentAttempt(attempt)) runImportJob(attempt); });
      return;
    }
    if (job.job_kind == QStringLiteral("timeline_render")) {
      QTimer::singleShot(0, this, [this, attempt]() { if (isCurrentAttempt(attempt)) runTimelineRenderJob(attempt); });
      return;
    }
    QTimer::singleShot(0, this, [this, attempt]() { if (isCurrentAttempt(attempt)) runExportJob(attempt); });
    return;
  }
  if (deferred) wake_timer_.start(120);
  else if (capacity_deferred) wake_timer_.start(500);
}

void JobRunner::runExportJob(const AttemptPtr& attempt) {
  if (!isCurrentAttempt(attempt)) return;
  const auto& job_id = attempt->job_id;
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    releaseAttempt(attempt);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    releaseAttempt(attempt);
    return;
  }
  if (!refreshAttemptResources(attempt, job)) return;

  job.status = QStringLiteral("Running");
  job.progress = 20;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  const auto render_photo = should_render_photo_job(job);
  bool uses_preview = false;
  const auto source_path = render_photo ? normalized_local_path(job.source_path) : resolved_source_path(job, &uses_preview);
  if (source_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The export source is no longer available.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    releaseAttempt(attempt);
    return;
  }

  const auto destination_path = normalized_local_path(job.destination_path);
  if (destination_path.isEmpty() ||
      (!QDir(destination_path).exists() && !QDir().mkpath(destination_path)) ||
      !QFileInfo(destination_path).isWritable()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The export destination is unavailable or not writable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    releaseAttempt(attempt);
    return;
  }

  auto output_path = normalized_local_path(job.output_path);
  if (output_path.isEmpty() || QFileInfo::exists(output_path)) {
    const auto planned_path = output_path;
    output_path = output_file_path_for_job(job, source_path, render_photo || uses_preview);
    if (!planned_path.isEmpty() && output_path != planned_path) {
      job.metadata_json.insert(QStringLiteral("plannedOutputCollisionAtRun"), true);
      job.metadata_json.insert(QStringLiteral("plannedOutputPath"), planned_path);
      job.metadata_json.insert(QStringLiteral("resolvedOutputPath"), output_path);
    }
  }
  if (output_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("Unable to allocate an export file name.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    releaseAttempt(attempt);
    return;
  }
  job.output_path = output_path;

  // Persisted jobs created before canonical plan admission did not mirror the
  // stable source identity into their render metadata. Repair them at the
  // execution boundary so retries compile the same plan as newly queued jobs.
  job.metadata_json.insert(QStringLiteral("assetId"), job.asset_id);
  const auto export_render_job = export_render_job_metadata(job, source_path, output_path, render_photo);
  job.metadata_json.insert(QStringLiteral("exportRenderJob"), export_render_job);
  const auto export_preview_job =
      export_preview_job_metadata(job, source_path, output_path, render_photo, export_render_job);
  job.metadata_json.insert(QStringLiteral("exportPreviewJob"), export_preview_job);
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  attempt->watcher->setFuture(QtConcurrent::run(
      run_export_work,
      source_path,
      job.recipe_json,
      job.preset_name,
      job.metadata_json,
      output_path,
      render_photo,
      export_render_job,
      export_preview_job,
      attempt->cancellation));
  wake();
}

void JobRunner::handleAsyncJobFinished(const AttemptPtr& attempt) {
  if (!isCurrentAttempt(attempt)) return;
  const auto release = qScopeGuard([this, attempt] { releaseAttempt(attempt); });
  const auto job_id = attempt->job_id;
  const auto job_kind = attempt->job_kind;
  const auto queue_wait_ms = attempt->queue_wait_ms;
  const auto total_elapsed_ms = attempt->timer.isValid() ? attempt->timer.elapsed() : 0;
  const auto result = attempt->watcher->result();
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    return;
  }

  auto job = *job_it;
  const auto cancellation_requested =
      job.status == QStringLiteral("Canceling") ||
      job.metadata_json.value(QStringLiteral("cancellationRequested")).toBool();
  const auto pause_requested =
      job.status == QStringLiteral("Pausing") ||
      job.metadata_json.value(QStringLiteral("pauseRequested")).toBool();
  const auto success = result.value(QStringLiteral("success")).toBool();
  const auto worker_elapsed_ms = result.value(QStringLiteral("elapsedMs")).toLongLong();
  const auto backend = result.value(QStringLiteral("backend")).toString().trimmed();
  const auto fallback_reason = result.value(QStringLiteral("fallbackReason")).toString().trimmed();
  const auto export_render_job = result.value(QStringLiteral("exportRenderJob")).toMap();
  const auto export_render_job_id =
      export_render_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto persisted_export_render_job =
      job.metadata_json.value(QStringLiteral("exportRenderJob")).toObject().toVariantMap();
  const auto persisted_export_render_job_id =
      persisted_export_render_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto export_preview_job = result.value(QStringLiteral("exportPreviewJob")).toMap();
  const auto export_preview_job_id =
      export_preview_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto persisted_export_preview_job =
      job.metadata_json.value(QStringLiteral("exportPreviewJob")).toObject().toVariantMap();
  const auto persisted_export_preview_job_id =
      persisted_export_preview_job.value(QStringLiteral("jobId")).toString().trimmed();
  const auto render_photo_result =
      export_render_job.value(QStringLiteral("renderPhoto")).toBool();
  const auto artifact_verification =
      result.value(QStringLiteral("artifactVerification")).toMap();
  for (const auto* key : {
           "canonicalPlanCacheKey",
           "canonicalRecipeCacheKey",
           "canonicalPlanError",
           "decodePreflight",
           "decodeExecution",
       }) {
    const auto name = QString::fromLatin1(key);
    const auto value = result.value(name).toString();
    if (!value.isEmpty()) {
      job.metadata_json.insert(name, value);
    }
  }
  const auto completed_render_stages =
      result.value(
                QStringLiteral("completedRenderStages"))
          .toStringList();
  if (!completed_render_stages.isEmpty()) {
    job.metadata_json.insert(
        QStringLiteral("completedRenderStages"),
        QJsonArray::fromStringList(
            completed_render_stages));
    job.metadata_json.insert(
        QStringLiteral("geometryExecutorCacheHit"),
        result.value(
                  QStringLiteral(
                      "geometryExecutorCacheHit"))
            .toBool());
  }
  if (result.contains(
          QStringLiteral(
              "executorSubmissionCount"))) {
    job.metadata_json.insert(
        QStringLiteral(
            "executorSubmissionCount"),
        result.value(
            QStringLiteral(
                "executorSubmissionCount"))
            .toInt());
  }
  if (!artifact_verification.isEmpty()) {
    job.metadata_json.insert(
        QStringLiteral("artifactVerification"),
        QJsonObject::fromVariantMap(artifact_verification));
  }
  const auto output_integrity = result.value(QStringLiteral("outputIntegrity")).toMap();
  if (!output_integrity.isEmpty()) {
    job.metadata_json.insert(
        QStringLiteral("outputIntegrity"),
        QJsonObject::fromVariantMap(output_integrity));
  }
  const auto delivery_artifacts = result.value(QStringLiteral("deliveryArtifacts")).toList();
  if (!delivery_artifacts.isEmpty())
    job.metadata_json.insert(QStringLiteral("deliveryArtifacts"), QJsonArray::fromVariantList(delivery_artifacts));
  const auto revoke_timeline_delivery = [&]() {
    const auto revoked = JobOutputTransaction::rollbackArtifacts(delivery_artifacts);
    job.metadata_json.insert(QStringLiteral("deliveryRollback"), QJsonObject::fromVariantMap(revoked));
  };
  const auto export_render_mask_trace =
      export_render_job.value(QStringLiteral("maskAlphaTrace")).toMap();
  const auto stale_export_render_result =
      job_kind == QStringLiteral("asset_export") &&
      (export_render_job_id.isEmpty() ||
       persisted_export_render_job_id.isEmpty() ||
       export_render_job_id != persisted_export_render_job_id);
  const auto stale_export_preview_result =
      job_kind == QStringLiteral("asset_export") &&
      success &&
      render_photo_result &&
      (export_preview_job_id.isEmpty() ||
       persisted_export_preview_job_id.isEmpty() ||
       export_preview_job_id != persisted_export_preview_job_id);
  const auto cancellation_reason = cancellation_reason_for_job(job);
  if (pause_requested) {
    const auto output_path = result.value(QStringLiteral("outputPath")).toString().trimmed();
    if ((job.job_kind == QStringLiteral("asset_export") ||
         job.job_kind == QStringLiteral("timeline_render")) &&
        !output_path.isEmpty()) {
      if (job.job_kind == QStringLiteral("timeline_render")) revoke_timeline_delivery();
      else remove_delivery_artifact(output_path);
    }
    const auto poster_path = result.value(QStringLiteral("posterPath")).toString().trimmed();
    if (job.job_kind == QStringLiteral("timeline_render") && !poster_path.isEmpty()) {
      remove_delivery_artifact(poster_path);
    }
    job.status = QStringLiteral("Paused");
    job.error_message = job.job_kind == QStringLiteral("catalog_import")
                            ? QStringLiteral("Paused safely. Resume to replay the reviewed import.")
                            : job.job_kind == QStringLiteral("timeline_render")
                                  ? QStringLiteral("Paused safely. Resume to render this timeline again.")
                                  : QStringLiteral("Paused safely. Resume to render this export again.");
    job.progress = 0;
    job.metadata_json.insert(QStringLiteral("pausedAt"), iso_now());
  } else if (cancellation_requested) {
    const auto output_path = result.value(QStringLiteral("outputPath")).toString().trimmed();
    if (!output_path.isEmpty()) {
      if (job.job_kind == QStringLiteral("timeline_render")) revoke_timeline_delivery();
      else remove_delivery_artifact(output_path);
    }
    const auto poster_path = result.value(QStringLiteral("posterPath")).toString().trimmed();
    if (!poster_path.isEmpty()) {
      remove_delivery_artifact(poster_path);
    }
    job.status = QStringLiteral("Canceled");
    job.error_message = QStringLiteral("Canceled before completion.");
    job.progress = 0;
    job.metadata_json.insert(QStringLiteral("canceled"), true);
    job.metadata_json.insert(QStringLiteral("canceledAt"), iso_now());
    job.metadata_json.insert(
        QStringLiteral("cancellationReason"),
        cancellation_reason.isEmpty() ? QStringLiteral("cooperative_cancel_requested") : cancellation_reason);
  } else if (stale_export_render_result || stale_export_preview_result) {
    const auto output_path = result.value(QStringLiteral("outputPath")).toString().trimmed();
    if (!output_path.isEmpty()) {
      remove_delivery_artifact(output_path);
    }
    job.status = QStringLiteral("Failed");
    job.error_message =
        stale_export_preview_result
            ? QStringLiteral("Export preview result was discarded because the job changed.")
            : QStringLiteral("Export render result was discarded because the job changed.");
    job.progress = 0;
    job.metadata_json.insert(QStringLiteral("staleExportRenderResult"), stale_export_render_result);
    job.metadata_json.insert(
        QStringLiteral("staleExportRenderJobId"),
        export_render_job_id);
    job.metadata_json.insert(
        QStringLiteral("staleExportPreviewResult"),
        stale_export_preview_result);
    job.metadata_json.insert(
        QStringLiteral("staleExportPreviewJobId"),
        export_preview_job_id);
  } else if (success) {
    job.output_path = result.value(QStringLiteral("outputPath")).toString();
    if (!export_render_job.isEmpty()) {
      job.metadata_json.insert(
          QStringLiteral("exportRenderJob"),
          QJsonObject::fromVariantMap(export_render_job));
    }
    if (!export_preview_job.isEmpty()) {
      job.metadata_json.insert(
          QStringLiteral("exportPreviewJob"),
          QJsonObject::fromVariantMap(export_preview_job));
    }
    const auto preview_export_diff = result.value(QStringLiteral("previewExportDiff")).toMap();
    if (!preview_export_diff.isEmpty()) {
      job.metadata_json.insert(
          QStringLiteral("previewExportDiff"),
          QJsonObject::fromVariantMap(preview_export_diff));
    }
    if (job.job_kind == QStringLiteral("catalog_import")) {
      const auto import_result = result.value(QStringLiteral("importResult")).toMap();
      if (!import_result.isEmpty()) {
        job.metadata_json.insert(
            QStringLiteral("importResult"),
            QJsonObject::fromVariantMap(import_result));
      }
      const auto result_message = result.value(QStringLiteral("resultMessage")).toString().trimmed();
      if (!result_message.isEmpty()) {
        job.metadata_json.insert(QStringLiteral("resultMessage"), result_message);
      }
      job.status = QStringLiteral("Completed");
      job.error_message.clear();
      job.progress = 100;
    } else if (job.job_kind == QStringLiteral("timeline_render")) {
      const auto poster_path = result.value(QStringLiteral("posterPath")).toString().trimmed();
      QString finalize_error;
      if (!update_timeline_render_artifacts(job, poster_path, &finalize_error)) {
        job.status = QStringLiteral("Failed");
        job.error_message = finalize_error;
        job.progress = 0;
      } else {
        if (!poster_path.isEmpty()) {
          job.metadata_json.insert(QStringLiteral("posterPath"), poster_path);
        }
        job.status = QStringLiteral("Completed");
        job.error_message.clear();
        job.progress = 100;
      }
    } else {
      const auto delivery_plan = job.metadata_json.value(QStringLiteral("deliveryPlan")).toObject();
      const auto artifact = job.metadata_json.value(QStringLiteral("artifactVerification")).toObject();
      QJsonObject delivery_summary;
      delivery_summary.insert(QStringLiteral("schema"), QStringLiteral("export-delivery-summary:v1"));
      delivery_summary.insert(QStringLiteral("outputPath"), job.output_path);
      delivery_summary.insert(QStringLiteral("outputFileName"), QFileInfo(job.output_path).fileName());
      delivery_summary.insert(QStringLiteral("preset"), job.preset_name);
      delivery_summary.insert(
          QStringLiteral("format"),
          artifact.value(QStringLiteral("format")).toString(
              delivery_plan.value(QStringLiteral("format")).toString()));
      delivery_summary.insert(QStringLiteral("bitDepth"), artifact.value(QStringLiteral("bitDepth")).toInt());
      delivery_summary.insert(QStringLiteral("width"), artifact.value(QStringLiteral("width")).toInt());
      delivery_summary.insert(QStringLiteral("height"), artifact.value(QStringLiteral("height")).toInt());
      delivery_summary.insert(QStringLiteral("fileBytes"), QFileInfo(job.output_path).size());
      delivery_summary.insert(
          QStringLiteral("outputProfile"),
          delivery_plan.value(QStringLiteral("outputProfile")).toString(QStringLiteral("Original profile")));
      delivery_summary.insert(
          QStringLiteral("profileVerified"),
          artifact.isEmpty() || artifact.value(QStringLiteral("profileVerified")).toBool());
      delivery_summary.insert(
          QStringLiteral("metadataPolicy"),
          delivery_plan.value(QStringLiteral("metadataPolicy")).toString(
              QStringLiteral("Original file metadata preserved by byte-for-byte copy.")));
      delivery_summary.insert(
          QStringLiteral("sharpeningPolicy"),
          delivery_plan.value(QStringLiteral("sharpeningPolicy")).toString(
              QStringLiteral("No additional output sharpening.")));
      delivery_summary.insert(QStringLiteral("artifactVerified"), artifact.isEmpty() || artifact.value(QStringLiteral("valid")).toBool());
      job.metadata_json.insert(QStringLiteral("deliverySummary"), delivery_summary);
      job.status = QStringLiteral("Completed");
      job.error_message.clear();
      job.progress = 100;
    }
  } else {
    job.status = QStringLiteral("Failed");
    job.error_message = result.value(QStringLiteral("errorMessage")).toString().trimmed();
    if (job.error_message.isEmpty()) {
      job.error_message = job.job_kind == QStringLiteral("timeline_render")
                              ? QStringLiteral("The timeline render job did not complete.")
                              : (job.job_kind == QStringLiteral("catalog_import")
                                     ? QStringLiteral("Import failed.")
                                     : QStringLiteral("The export job did not complete."));
    }
    job.progress = 0;
  }

  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);
  WorkerQueueRegistry::logSummary(
      worker_pool_id_for_job_kind(job_kind),
      job_kind,
      worker_surface_for_job_kind(job_kind),
      static_cast<int>(std::count_if(active_attempts_.cbegin(), active_attempts_.cend(),
          [&job_kind](const auto& current) { return current->job_kind == job_kind; })) - 1,
      0,
      job_kind == QStringLiteral("asset_export") ? 2 : 1,
      queue_wait_ms,
      worker_elapsed_ms,
      job.status == QStringLiteral("Completed") ? 1 : 0,
      cancellation_requested ? 1 : 0);
  QVariantMap event_fields{
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), job_kind},
            {QStringLiteral("asset_id"), job.asset_id},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("backend"), backend},
            {QStringLiteral("fallback_reason"), fallback_reason},
            {QStringLiteral("export_render_job_id"), export_render_job_id},
            {QStringLiteral("export_render_job_kind"),
             export_render_job.value(QStringLiteral("jobKind")).toString()},
            {QStringLiteral("export_render_stage"),
             export_render_job.value(QStringLiteral("stage")).toString()},
            {QStringLiteral("export_render_state_signature"),
             export_render_job.value(QStringLiteral("stateSignature")).toString()},
            {QStringLiteral("mask_source_count"),
             export_render_mask_trace.value(QStringLiteral("mask_source_count")).toInt()},
            {QStringLiteral("mask_source_ids"),
             export_render_mask_trace.value(QStringLiteral("mask_source_ids")).toList()},
            {QStringLiteral("mask_source_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_source_hash")).toString()},
            {QStringLiteral("mask_alpha_trace_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString()},
            {QStringLiteral("mask_alpha_trace_signature"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString()},
            {QStringLiteral("mask_source_revision_hash"),
             export_render_mask_trace.value(QStringLiteral("mask_source_revision_hash")).toString()},
            {QStringLiteral("mask_alpha_raster_width"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt()},
            {QStringLiteral("mask_alpha_raster_height"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt()},
            {QStringLiteral("render_stage_telemetry_contract"),
             QStringLiteral("layer-mask-render-stage-telemetry-v1")},
            {QStringLiteral("layer_recipe_stage"), QStringLiteral("layer_recipe_evaluation")},
            {QStringLiteral("mask_alpha_stage"), QStringLiteral("mask_alpha_retrieval")},
            {QStringLiteral("mask_alpha_retrieval_provider"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_provider")).toString()},
            {QStringLiteral("mask_alpha_retrieval_signature"),
             export_render_mask_trace.value(QStringLiteral("mask_alpha_trace_signature")).toString()},
            {QStringLiteral("layer_composite_stage"), QStringLiteral("layer_composite")},
            {QStringLiteral("thumbnail_render_stage"), QStringLiteral("thumbnail_render")},
            {QStringLiteral("final_export_stage"), QStringLiteral("final_export")},
            {QStringLiteral("final_export_ms"), worker_elapsed_ms},
            {QStringLiteral("export_preview_job_id"), export_preview_job_id},
            {QStringLiteral("export_preview_job_kind"),
             export_preview_job.value(QStringLiteral("jobKind")).toString()},
            {QStringLiteral("export_preview_stage"),
             export_preview_job.value(QStringLiteral("stage")).toString()},
            {QStringLiteral("export_preview_state_signature"),
             export_preview_job.value(QStringLiteral("stateSignature")).toString()},
            {QStringLiteral("queue_wait_ms"), queue_wait_ms},
            {QStringLiteral("cancellation_requested"), cancellation_requested},
            {QStringLiteral("cancellation_reason"), cancellation_reason},
            {QStringLiteral("worker_elapsed_ms"), worker_elapsed_ms},
            {QStringLiteral("elapsed_ms"), total_elapsed_ms},
            {QStringLiteral("output_path"), job.output_path},
            {QStringLiteral("error"), job.error_message},
  };
  if (job.status == QStringLiteral("Completed")) {
    observability::logInfo(
        photoEditorJobsLog(),
        QStringLiteral("job.completed"),
        event_fields);
  } else {
    event_fields.insert(
        QStringLiteral("stale_export_render_result"), stale_export_render_result);
    event_fields.insert(
        QStringLiteral("stale_export_preview_result"), stale_export_preview_result);
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        event_fields);
  }
}

void JobRunner::runImportJob(const AttemptPtr& attempt) {
  if (!isCurrentAttempt(attempt)) return;
  const auto& job_id = attempt->job_id;
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    releaseAttempt(attempt);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    releaseAttempt(attempt);
    return;
  }
  if (!refreshAttemptResources(attempt, job)) return;

  const auto source_path = normalized_local_path(job.source_path);
  if (source_path.isEmpty() || !QFileInfo::exists(source_path)) {
    const auto queue_wait_ms = attempt->queue_wait_ms;
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The selected import source is unavailable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    releaseAttempt(attempt);
    observability::logWarning(
        photoEditorJobsLog(),
        QStringLiteral("job.failed"),
        {
            {QStringLiteral("job_id"), job.job_id},
            {QStringLiteral("job_kind"), QStringLiteral("catalog_import")},
            {QStringLiteral("status"), job.status},
            {QStringLiteral("queue_wait_ms"), queue_wait_ms},
            {QStringLiteral("cancellation_requested"), false},
            {QStringLiteral("cancellation_reason"), QString()},
            {QStringLiteral("elapsed_ms"), attempt->timer.isValid() ? attempt->timer.elapsed() : 0},
            {QStringLiteral("error"), job.error_message},
        });
    return;
  }

  job.status = QStringLiteral("Running");
  job.progress = 15;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  attempt->watcher->setFuture(QtConcurrent::run(
      run_import_work,
      job.job_id,
      source_path,
      job.metadata_json,
      attempt->resources.import_metadata_threads,
      attempt->cancellation));
  wake();
}

void JobRunner::runTimelineRenderJob(const AttemptPtr& attempt) {
  if (!isCurrentAttempt(attempt)) return;
  const auto& job_id = attempt->job_id;
  const auto records = loadPersistedJobRecords();
  auto job_it = std::find_if(
      records.cbegin(),
      records.cend(),
      [&](const auto& record) { return record.job_id == job_id; });

  if (job_it == records.cend()) {
    releaseAttempt(attempt);
    return;
  }

  auto job = *job_it;
  if (job.status != QStringLiteral("Queued")) {
    releaseAttempt(attempt);
    return;
  }
  if (!refreshAttemptResources(attempt, job)) return;

  const auto source_path = normalized_local_path(job.source_path);
  if (source_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The saved timeline document is unavailable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    releaseAttempt(attempt);
    return;
  }

  const auto output_path = normalized_local_path(job.output_path);
  if (output_path.isEmpty()) {
    job.status = QStringLiteral("Failed");
    job.error_message = QStringLiteral("The timeline render output path is unavailable.");
    job.progress = 0;
    job.updated_at = iso_now();
    (void)upsertPersistedJobRecord(job, nullptr);
    releaseAttempt(attempt);
    return;
  }

  job.source_path = source_path;
  job.output_path = output_path;
  job.status = QStringLiteral("Running");
  job.progress = 20;
  job.updated_at = iso_now();
  (void)upsertPersistedJobRecord(job, nullptr);

  attempt->watcher->setFuture(QtConcurrent::run(
      run_timeline_render_work,
      source_path,
      output_path,
      job.metadata_json.value(QStringLiteral("timelineId")).toString().trimmed(),
      job.metadata_json.value(QStringLiteral("resourcePolicy"))
          .toString(QStringLiteral("playback-first")),
      attempt->cancellation));
  wake();
}

}  // namespace photo_editor::desktop
