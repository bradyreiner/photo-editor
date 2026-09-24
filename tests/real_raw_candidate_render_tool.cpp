#include <QByteArrayView>
#include <QColorSpace>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "photo_render_utils.h"
#include "raw_image_decoder.h"
#include "raw_image_decoder_libraw.h"

namespace {

constexpr auto kToolVersion = "photo-editor-real-raw-candidate-render:v3";

struct ImageStatistics final {
  std::array<double, 4> minimum{
      std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::infinity(),
  };
  std::array<double, 4> maximum{
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),
  };
  quint64 pixel_count = 0U;
  quint64 nonfinite_sample_count = 0U;
};

[[nodiscard]] ImageStatistics image_statistics(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA32FPx4);
  auto statistics = ImageStatistics{};
  if (image.isNull()) {
    return statistics;
  }
  statistics.pixel_count =
      static_cast<quint64>(image.width()) * static_cast<quint64>(image.height());
  for (int y = 0; y < image.height(); ++y) {
    const auto* scanline =
        reinterpret_cast<const QRgbaFloat32*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const auto& pixel = scanline[x];
      const auto samples = std::array<double, 4>{
          static_cast<double>(pixel.r),
          static_cast<double>(pixel.g),
          static_cast<double>(pixel.b),
          static_cast<double>(pixel.a),
      };
      for (std::size_t channel = 0; channel < samples.size(); ++channel) {
        if (!std::isfinite(samples[channel])) {
          ++statistics.nonfinite_sample_count;
          continue;
        }
        statistics.minimum[channel] =
            std::min(statistics.minimum[channel], samples[channel]);
        statistics.maximum[channel] =
            std::max(statistics.maximum[channel], samples[channel]);
      }
    }
  }
  return statistics;
}

[[nodiscard]] bool valid_opaque_photo_statistics(
    const ImageStatistics& statistics) {
  if (statistics.pixel_count == 0U || statistics.nonfinite_sample_count != 0U) {
    return false;
  }
  const auto rgb_minimum = std::min({
      statistics.minimum[0], statistics.minimum[1], statistics.minimum[2]});
  const auto rgb_maximum = std::max({
      statistics.maximum[0], statistics.maximum[1], statistics.maximum[2]});
  return rgb_maximum > 1.0e-6 && rgb_maximum - rgb_minimum > 1.0e-6 &&
         std::abs(statistics.minimum[3] - 1.0) <= 1.0e-6 &&
         std::abs(statistics.maximum[3] - 1.0) <= 1.0e-6;
}

[[nodiscard]] QJsonObject statistics_contract(
    const ImageStatistics& statistics) {
  return {
      {QStringLiteral("pixel_count"),
       static_cast<qint64>(statistics.pixel_count)},
      {QStringLiteral("nonfinite_sample_count"),
       static_cast<qint64>(statistics.nonfinite_sample_count)},
      {QStringLiteral("red_minimum"), statistics.minimum[0]},
      {QStringLiteral("red_maximum"), statistics.maximum[0]},
      {QStringLiteral("green_minimum"), statistics.minimum[1]},
      {QStringLiteral("green_maximum"), statistics.maximum[1]},
      {QStringLiteral("blue_minimum"), statistics.minimum[2]},
      {QStringLiteral("blue_maximum"), statistics.maximum[2]},
      {QStringLiteral("alpha_minimum"), statistics.minimum[3]},
      {QStringLiteral("alpha_maximum"), statistics.maximum[3]},
  };
}

[[nodiscard]] QByteArray sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto block = file.read(1024 * 1024);
    if (block.isEmpty() && !file.atEnd()) {
      return {};
    }
    hash.addData(block);
  }
  return hash.result().toHex();
}

[[nodiscard]] QByteArray pixel_sha256_float(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA32FPx4);
  if (image.isNull()) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) * 16;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return hash.result().toHex();
}

[[nodiscard]] QByteArray pixel_sha256_rgba64(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA64);
  if (image.isNull()) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) * 8;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return hash.result().toHex();
}

[[nodiscard]] bool valid_logical_id(const QString& value) {
  if (value.isEmpty() || value.startsWith(QLatin1Char('/')) ||
      value.contains(QLatin1Char('\\')) || value.contains(QLatin1Char(':'))) {
    return false;
  }
  const auto components = value.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  for (const auto& component : components) {
    if (component.isEmpty() || component == QStringLiteral(".") ||
        component == QStringLiteral("..")) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] QJsonObject recipe_contract(const QString& decode_path) {
  return {
      {QStringLiteral("schema"),
       QStringLiteral("photo-editor-real-raw-recipe:v1")},
      {QStringLiteral("decode_path"),
       decode_path},
      {QStringLiteral("input_color_profile"), QStringLiteral("Embedded")},
      {QStringLiteral("working_color_profile"), QStringLiteral("Linear sRGB")},
      {QStringLiteral("raw_demosaic_quality"), QStringLiteral("Full")},
      {QStringLiteral("raw_highlight_recovery"), true},
      {QStringLiteral("raw_gamut_mapping"), false},
      {QStringLiteral("raw_lens_correction"), true},
      {QStringLiteral("output_color_profile"), QStringLiteral("sRGB")},
      {QStringLiteral("photographic_rendering_model"),
       QStringLiteral("aces2-ocio-v1")},
      {QStringLiteral("render_intent"), QStringLiteral("Perceptual")},
      {QStringLiteral("output_gamut_mapping_policy"),
       QStringLiteral("Perceptual")},
      {QStringLiteral("black_point_compensation"), true},
      {QStringLiteral("backend"), QStringLiteral("cpu-only")},
      {QStringLiteral("output_storage"), QStringLiteral("rgba16-png")},
  };
}

[[nodiscard]] QByteArray recipe_sha256(const QJsonObject& recipe) {
  return QCryptographicHash::hash(
             QJsonDocument(recipe).toJson(QJsonDocument::Compact),
             QCryptographicHash::Sha256)
      .toHex();
}

[[nodiscard]] int render_candidate(
    const QString& input_path,
    const QString& output_path,
    const QString& report_path,
    const QString& source_id,
    const QString& output_id,
    const QString& decode_path) {
  if (input_path.isEmpty() || output_path.isEmpty() || report_path.isEmpty() ||
      input_path == output_path || input_path == report_path ||
      output_path == report_path || !valid_logical_id(source_id) ||
      !valid_logical_id(output_id) || source_id == output_id ||
      QFile::exists(output_path) || QFile::exists(report_path) ||
      (decode_path != QStringLiteral("portable-libraw") &&
       decode_path != QStringLiteral("shipping-default"))) {
    std::cerr << "invalid_path_or_identity_contract\n";
    return EXIT_FAILURE;
  }
  const auto source_sha256 = sha256_file(input_path);
  const auto source_bytes = QFileInfo(input_path).size();
  if (source_sha256.isEmpty() || source_bytes <= 0 ||
      !photo_editor::desktop::raw_image_supported(input_path)) {
    std::cerr << "source_must_be_supported_nonempty_raw\n";
    return EXIT_FAILURE;
  }

  photo_editor::desktop::EditRecipeState state;
  state.input_color_profile = QStringLiteral("Embedded");
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = QStringLiteral("sRGB");
  state.raw_demosaic_quality = QStringLiteral("Full");
  state.raw_highlight_recovery = true;
  state.raw_gamut_mapping = false;
  state.raw_lens_correction = true;
  state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  state.render_intent = QStringLiteral("Perceptual");
  state.output_gamut_mapping_enabled = true;
  state.output_gamut_mapping_policy = QStringLiteral("Perceptual");
  state.black_point_compensation_enabled = true;
  state.source_black_point = 0.0;
  state.output_black_point = 0.0;
  state.output_headroom = 1.0;

  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  const auto decode_settings = photo_editor::desktop::RawDecodeSettings{
      .input_color_profile = QStringLiteral("Embedded"),
      .demosaic_quality = QStringLiteral("Full"),
      .highlight_recovery_enabled = true,
  };
  const auto decoded_ok =
      decode_path == QStringLiteral("portable-libraw")
          ? photo_editor::desktop::decode_libraw_scene_linear_image(
                input_path, &decoded, 0, decode_settings, &metadata)
          : photo_editor::desktop::decode_source_image(
                input_path, &decoded, 0, state, &metadata);
  const auto float_storage =
      decoded.format() == QImage::Format_RGBA32FPx4 ||
      decoded.format() == QImage::Format_RGBA16FPx4;
  const auto decoded_storage =
      decoded.format() == QImage::Format_RGBA32FPx4
          ? QStringLiteral("rgba32-float")
          : QStringLiteral("rgba16-float");
  const auto portable_decode =
      decode_path == QStringLiteral("portable-libraw");
  const auto expected_decoder =
      portable_decode
          ? metadata.decoder_backend.find(
                "LibRaw characterized linear-sRGB") == 0U
          : metadata.decoder_backend.find(
                "LibRaw camera-linear") == 0U;
  const auto expected_color_boundary =
      portable_decode
          ? metadata.pixel_encoding ==
                    photo_editor::pipeline::DecodedPixelEncoding::SceneLinearRgb &&
                metadata.camera_transform_applied &&
                decoded.colorSpace().isValid() &&
                decoded.colorSpace().primaries() ==
                    QColorSpace::Primaries::SRgb &&
                decoded.colorSpace().transferFunction() ==
                    QColorSpace::TransferFunction::Linear &&
                !photo_editor::pipeline::decoded_image_requires_engine_input_transform(
                    metadata)
          : metadata.pixel_encoding ==
                    photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb &&
                !metadata.camera_transform_applied &&
                !decoded.colorSpace().isValid() &&
                photo_editor::pipeline::decoded_image_requires_engine_input_transform(
                    metadata);
  if (!decoded_ok || decoded.isNull() || !float_storage ||
      !photo_editor::pipeline::decoded_image_color_boundary_valid(metadata) ||
      !metadata.is_raw ||
      !metadata.demosaic_applied ||
      metadata.white_balance_applied != portable_decode ||
      !expected_color_boundary || !expected_decoder) {
    std::cerr << "shipping_raw_decode_contract_failed"
              << ":decoded_ok=" << decoded_ok
              << ":null=" << decoded.isNull()
              << ":format=" << static_cast<int>(decoded.format())
              << ":decode_path=" << decode_path.toStdString()
              << ":color_valid=" << decoded.colorSpace().isValid()
              << ":primaries="
              << static_cast<int>(decoded.colorSpace().primaries())
              << ":transfer="
              << static_cast<int>(decoded.colorSpace().transferFunction())
              << ":metadata_valid=" << metadata.valid
              << ":boundary_valid="
              << photo_editor::pipeline::decoded_image_color_boundary_valid(
                     metadata)
              << ":is_raw=" << metadata.is_raw
              << ":encoding="
              << photo_editor::pipeline::decoded_pixel_encoding_name(
                     metadata.pixel_encoding)
              << ":demosaic=" << metadata.demosaic_applied
              << ":white_balance=" << metadata.white_balance_applied
              << ":camera_transform=" << metadata.camera_transform_applied
              << ":requires_engine_transform="
              << photo_editor::pipeline::decoded_image_requires_engine_input_transform(
                     metadata)
              << ":expected_boundary=" << expected_color_boundary
              << ":expected_decoder=" << expected_decoder
              << ":backend=" << metadata.decoder_backend << '\n';
    return EXIT_FAILURE;
  }
  const auto decoded_statistics = image_statistics(decoded);
  if (!valid_opaque_photo_statistics(decoded_statistics)) {
    std::cerr << "shipping_raw_decode_pixels_failed:"
              << QJsonDocument(statistics_contract(decoded_statistics))
                     .toJson(QJsonDocument::Compact)
                     .constData()
              << '\n';
    return EXIT_FAILURE;
  }
  const auto decoded_pixel_sha256 = pixel_sha256_float(decoded);
  const auto decoded_icc_sha256 = QCryptographicHash::hash(
                                      decoded.colorSpace().iccProfile(),
                                      QCryptographicHash::Sha256)
                                      .toHex();
  if (decoded_pixel_sha256.isEmpty() || decoded_icc_sha256.isEmpty()) {
    std::cerr << "decoded_provenance_hash_failed\n";
    return EXIT_FAILURE;
  }

  photo_editor::desktop::RenderBackendTelemetry telemetry;
  const auto rendered =
      photo_editor::desktop::render_output_image_with_backend(
          decoded,
          state,
          QColorSpace(QColorSpace::SRgb),
          QImage::Format_RGBA64,
          photo_editor::desktop::RenderBackendPreference::CpuOnly,
          &metadata,
          &telemetry);
  if (rendered.isNull() || rendered.size() != decoded.size() ||
      rendered.format() != QImage::Format_RGBA64 ||
      !rendered.colorSpace().isValid() ||
      rendered.colorSpace().primaries() != QColorSpace::Primaries::SRgb ||
      rendered.colorSpace().transferFunction() !=
          QColorSpace::TransferFunction::SRgb ||
      telemetry.backend != QStringLiteral("cpu")) {
    std::cerr << "shipping_raw_output_render_failed:"
              << telemetry.backend.toStdString() << ':'
              << telemetry.fallback_reason.toStdString() << '\n';
    return EXIT_FAILURE;
  }
  const auto rendered_statistics = image_statistics(rendered);
  if (!valid_opaque_photo_statistics(rendered_statistics)) {
    std::cerr << "shipping_raw_output_pixels_failed:"
              << QJsonDocument(statistics_contract(rendered_statistics))
                     .toJson(QJsonDocument::Compact)
                     .constData()
              << '\n';
    return EXIT_FAILURE;
  }

  const auto temporary_output = output_path + QStringLiteral(".partial.png");
  QFile::remove(temporary_output);
  QImageWriter writer(temporary_output, "png");
  writer.setCompression(9);
  if (!writer.write(rendered)) {
    QFile::remove(temporary_output);
    std::cerr << "candidate_png_write_failed:"
              << writer.errorString().toStdString() << '\n';
    return EXIT_FAILURE;
  }
  QImageReader verifier(temporary_output, "png");
  verifier.setAutoTransform(false);
  const auto readback = verifier.read();
  if (readback.isNull() || readback.size() != rendered.size() ||
      readback.depth() != 64 || !readback.colorSpace().isValid() ||
      readback.colorSpace().primaries() != QColorSpace::Primaries::SRgb ||
      readback.colorSpace().transferFunction() !=
          QColorSpace::TransferFunction::SRgb ||
      pixel_sha256_rgba64(readback) != pixel_sha256_rgba64(rendered)) {
    QFile::remove(temporary_output);
    std::cerr << "candidate_png_readback_contract_failed\n";
    return EXIT_FAILURE;
  }
  if (!QFile::rename(temporary_output, output_path)) {
    QFile::remove(temporary_output);
    std::cerr << "candidate_png_publish_failed\n";
    return EXIT_FAILURE;
  }

  const auto output_sha256 = sha256_file(output_path);
  const auto output_pixel_sha256 = pixel_sha256_rgba64(readback);
  const auto output_icc_sha256 = QCryptographicHash::hash(
                                     readback.colorSpace().iccProfile(),
                                     QCryptographicHash::Sha256)
                                     .toHex();
  const auto recipe = recipe_contract(decode_path);
  const auto capability =
      photo_editor::desktop::raw_camera_linear_decoder_capability();
  const auto report = QJsonObject{
      {QStringLiteral("schema"),
       QStringLiteral("photo-editor-real-raw-candidate-report:v2")},
      {QStringLiteral("tool_version"), QString::fromLatin1(kToolVersion)},
      {QStringLiteral("source_id"), source_id},
      {QStringLiteral("output_id"), output_id},
      {QStringLiteral("source_file_sha256"),
       QString::fromLatin1(source_sha256)},
      {QStringLiteral("source_byte_size"), source_bytes},
      {QStringLiteral("decoded_pixel_sha256"),
       QString::fromLatin1(decoded_pixel_sha256)},
      {QStringLiteral("decoded_icc_sha256"),
       QString::fromLatin1(decoded_icc_sha256)},
      {QStringLiteral("decoded_width"), decoded.width()},
      {QStringLiteral("decoded_height"), decoded.height()},
      {QStringLiteral("decoded_storage"), decoded_storage},
      {QStringLiteral("decoded_statistics"),
       statistics_contract(decoded_statistics)},
      {QStringLiteral("decoded_metadata"),
       QJsonObject{
           {QStringLiteral("valid"), metadata.valid},
           {QStringLiteral("is_raw"), metadata.is_raw},
           {QStringLiteral("pixel_encoding"),
            QString::fromLatin1(
                photo_editor::pipeline::decoded_pixel_encoding_name(
                    metadata.pixel_encoding))},
           {QStringLiteral("decoder_backend"),
            QString::fromStdString(metadata.decoder_backend)},
           {QStringLiteral("decoded_color_space_name"),
            QString::fromStdString(metadata.decoded_color_space_name)},
           {QStringLiteral("camera_profile_identity"),
            QString::fromStdString(metadata.camera_profile_identity)},
           {QStringLiteral("black_subtraction_applied"),
            metadata.black_subtraction_applied},
           {QStringLiteral("white_normalization_applied"),
            metadata.white_normalization_applied},
           {QStringLiteral("demosaic_applied"), metadata.demosaic_applied},
           {QStringLiteral("white_balance_applied"),
            metadata.white_balance_applied},
           {QStringLiteral("highlight_reconstruction_applied"),
            metadata.highlight_reconstruction_applied},
           {QStringLiteral("lens_correction_applied"),
            metadata.lens_correction_applied},
           {QStringLiteral("baseline_exposure_applied"),
            metadata.baseline_exposure_applied},
           {QStringLiteral("camera_transform_applied"),
            metadata.camera_transform_applied},
           {QStringLiteral("decoder_rendering_applied"),
            metadata.decoder_rendering_applied},
           {QStringLiteral("input_profile_name"),
            QString::fromStdString(metadata.input_profile_name)},
           {QStringLiteral("camera_make"),
            QString::fromStdString(metadata.camera_make)},
           {QStringLiteral("camera_model"),
            QString::fromStdString(metadata.camera_model)},
           {QStringLiteral("as_shot_temperature"),
            metadata.as_shot_temperature},
           {QStringLiteral("as_shot_tint"), metadata.as_shot_tint},
           {QStringLiteral("baseline_exposure"), metadata.baseline_exposure},
           {QStringLiteral("highlight_headroom"), metadata.highlight_headroom},
       }},
      {QStringLiteral("decoder_capability"),
       QJsonObject{
           {QStringLiteral("available"), capability.available},
           {QStringLiteral("strict_version"), capability.strict_version},
           {QStringLiteral("backend"), capability.backend},
           {QStringLiteral("compiled_version"), capability.compiled_version},
           {QStringLiteral("runtime_version"), capability.runtime_version},
           {QStringLiteral("validated_version"), capability.validated_version},
           {QStringLiteral("pixel_contract"), capability.pixel_contract},
           {QStringLiteral("applied_controls"), capability.applied_controls},
           {QStringLiteral("deferred_controls"), capability.deferred_controls},
           {QStringLiteral("maximum_raw_memory_bytes"),
            static_cast<qint64>(capability.maximum_raw_memory_bytes)},
       }},
      {QStringLiteral("output_file_sha256"),
       QString::fromLatin1(output_sha256)},
      {QStringLiteral("output_pixel_sha256"),
       QString::fromLatin1(output_pixel_sha256)},
      {QStringLiteral("output_statistics"),
       statistics_contract(rendered_statistics)},
      {QStringLiteral("output_icc_sha256"),
       QString::fromLatin1(output_icc_sha256)},
      {QStringLiteral("output_color_space"), QStringLiteral("sRGB")},
      {QStringLiteral("output_storage"), QStringLiteral("rgba16-png")},
      {QStringLiteral("recipe"), recipe},
      {QStringLiteral("recipe_sha256"),
       QString::fromLatin1(recipe_sha256(recipe))},
      {QStringLiteral("backend_requested"), QStringLiteral("cpu-only")},
      {QStringLiteral("backend_used"), telemetry.backend},
      {QStringLiteral("fallback_reason"), telemetry.fallback_reason},
      {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
  };
  QSaveFile report_file(report_path);
  if (!report_file.open(QIODevice::WriteOnly) ||
      report_file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) <
          0 ||
      !report_file.commit()) {
    QFile::remove(output_path);
    std::cerr << "candidate_report_write_failed\n";
    return EXIT_FAILURE;
  }
  std::cout << QJsonDocument(report)
                   .toJson(QJsonDocument::Compact)
                   .constData()
            << '\n';
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  QGuiApplication::setApplicationName(
      QStringLiteral("real_raw_candidate_render_tool"));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral(
          "Render one real RAW through shipping decode and ACES 2 output."));
  parser.addHelpOption();
  const QCommandLineOption input_option(
      QStringList{QStringLiteral("input")}, QStringLiteral("Input RAW path."),
      QStringLiteral("path"));
  const QCommandLineOption output_option(
      QStringList{QStringLiteral("output")},
      QStringLiteral("16-bit sRGB candidate PNG."), QStringLiteral("path"));
  const QCommandLineOption report_option(
      QStringList{QStringLiteral("report")},
      QStringLiteral("Machine-readable provenance report."),
      QStringLiteral("path"));
  const QCommandLineOption source_id_option(
      QStringList{QStringLiteral("source-id")},
      QStringLiteral("Stable source identity."), QStringLiteral("logical-id"));
  const QCommandLineOption output_id_option(
      QStringList{QStringLiteral("output-id")},
      QStringLiteral("Stable output identity."), QStringLiteral("logical-id"));
  const QCommandLineOption decode_path_option(
      QStringList{QStringLiteral("decode-path")},
      QStringLiteral("portable-libraw or shipping-default."),
      QStringLiteral("name"), QStringLiteral("portable-libraw"));
  parser.addOptions({
      input_option,
      output_option,
      report_option,
      source_id_option,
      output_id_option,
      decode_path_option,
  });
  parser.process(application);
  for (const auto* option : {
           &input_option, &output_option, &report_option, &source_id_option,
           &output_id_option,
       }) {
    if (!parser.isSet(*option)) {
      parser.showHelp(EXIT_FAILURE);
    }
  }
  return render_candidate(
      parser.value(input_option), parser.value(output_option),
      parser.value(report_option), parser.value(source_id_option),
      parser.value(output_id_option), parser.value(decode_path_option));
}
