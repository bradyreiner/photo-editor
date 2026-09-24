#include <QByteArrayView>
#include <QColorSpace>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtGui/qrgbafloat.h>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <vector>

#include "photo_render_utils.h"

namespace {

constexpr auto kToolVersion = "photo-editor-independent-chart-candidate-render:v1";

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

[[nodiscard]] QByteArray pixel_sha256(const QImage& source) {
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

[[nodiscard]] QJsonObject recipe_contract(const QString& render_path) {
  return {
      {QStringLiteral("schema"), QStringLiteral("photo-editor-chart-recipe:v1")},
      {QStringLiteral("input_color_profile"), QStringLiteral("Embedded")},
      {QStringLiteral("working_color_profile"), QStringLiteral("Linear sRGB")},
      {QStringLiteral("output_color_profile"), QStringLiteral("sRGB")},
      {QStringLiteral("photographic_rendering_model"),
       QStringLiteral("aces2-ocio-v1")},
      {QStringLiteral("render_intent"), QStringLiteral("Perceptual")},
      {QStringLiteral("output_gamut_mapping_policy"),
       QStringLiteral("Perceptual")},
      {QStringLiteral("black_point_compensation"), true},
      {QStringLiteral("source_black_point"), 0.0},
      {QStringLiteral("output_black_point"), 0.0},
      {QStringLiteral("output_headroom"), 1.0},
      {QStringLiteral("backend"), QStringLiteral("cpu-only")},
      {QStringLiteral("render_path"), render_path},
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
    const QString& render_path) {
  if (input_path.isEmpty() || output_path.isEmpty() || report_path.isEmpty() ||
      input_path == output_path || input_path == report_path ||
      output_path == report_path || !valid_logical_id(source_id) ||
      !valid_logical_id(output_id) || source_id == output_id) {
    std::cerr << "invalid_path_or_identity_contract\n";
    return EXIT_FAILURE;
  }
  if (QFile::exists(output_path) || QFile::exists(report_path)) {
    std::cerr << "output_already_exists\n";
    return EXIT_FAILURE;
  }
  QImageReader reader(input_path, "png");
  reader.setAutoTransform(false);
  const auto source = reader.read();
  if (source.isNull() || source.width() != 606 || source.height() != 404 ||
      !source.colorSpace().isValid() ||
      source.colorSpace().primaries() != QColorSpace::Primaries::SRgb ||
      source.colorSpace().transferFunction() !=
          QColorSpace::TransferFunction::Linear) {
    std::cerr << "source_must_be_tagged_606x404_linear_srgb\n";
    return EXIT_FAILURE;
  }
  const auto source_sha256 = sha256_file(input_path);
  const auto source_pixel_sha256 = pixel_sha256(source);
  const auto source_icc_sha256 = QCryptographicHash::hash(
                                     source.colorSpace().iccProfile(),
                                     QCryptographicHash::Sha256)
                                     .toHex();
  if (source_sha256.isEmpty() || source_pixel_sha256.isEmpty() ||
      source_icc_sha256.isEmpty()) {
    std::cerr << "source_provenance_hash_failed\n";
    return EXIT_FAILURE;
  }

  photo_editor::desktop::RenderBackendTelemetry telemetry;
  QImage rendered;
  if (render_path == QStringLiteral("desktop-export")) {
    photo_editor::desktop::EditRecipeState state;
    state.input_color_profile = QStringLiteral("Embedded");
    state.working_color_profile = QStringLiteral("Linear sRGB");
    state.preview_color_space = QStringLiteral("sRGB");
    state.output_color_space = QStringLiteral("sRGB");
    state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
    state.render_intent = QStringLiteral("Perceptual");
    state.output_gamut_mapping_enabled = true;
    state.output_gamut_mapping_policy = QStringLiteral("Perceptual");
    state.black_point_compensation_enabled = true;
    state.source_black_point = 0.0;
    state.output_black_point = 0.0;
    state.output_headroom = 1.0;
    rendered = photo_editor::desktop::render_output_image_with_backend(
        source,
        state,
        QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA64,
        photo_editor::desktop::RenderBackendPreference::CpuOnly,
        nullptr,
        &telemetry);
  } else if (render_path == QStringLiteral("engine-output")) {
    const auto device = source.convertToFormat(QImage::Format_RGBA32FPx4);
    if (device.isNull()) {
      std::cerr << "engine_output_source_unpack_failed\n";
      return EXIT_FAILURE;
    }
    std::vector<photo_editor::pipeline::ColorPixel> pixels;
    pixels.reserve(static_cast<std::size_t>(device.width() * device.height()));
    for (int y = 0; y < device.height(); ++y) {
      const auto* row =
          reinterpret_cast<const QRgbaFloat32*>(device.constScanLine(y));
      for (int x = 0; x < device.width(); ++x) {
        pixels.push_back(photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(row[x].r),
            .green = static_cast<float>(row[x].g),
            .blue = static_cast<float>(row[x].b),
            .alpha = static_cast<float>(row[x].a),
        });
      }
    }
    auto config = photo_editor::pipeline::default_color_pipeline_config();
    config.working_profile =
        photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
    config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name("sRGB");
    config.export_display_target =
        photo_editor::pipeline::make_sdr_display_target(
            config.export_output_profile);
    config.photographic_rendering_model =
        photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
    config.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
    config.output_gamut_mapping_enabled = true;
    config.output_gamut_mapping_policy =
        photo_editor::pipeline::GamutMappingPolicy::Perceptual;
    config.output_headroom = 1.0;
    config.black_point_compensation_enabled = true;
    config.source_black_point = 0.0;
    config.output_black_point = 0.0;
    photo_editor::pipeline::apply_output_transform(
        pixels,
        config,
        photo_editor::pipeline::GamutWarningTarget::ExportOutput);
    rendered = QImage(device.width(), device.height(), QImage::Format_RGBA64);
    const auto encode_srgb16 = [](float linear) {
      const auto value = std::clamp(static_cast<double>(linear), 0.0, 1.0);
      const auto encoded =
          value <= 0.0031308
              ? 12.92 * value
              : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
      return static_cast<quint16>(std::lround(
          std::clamp(encoded, 0.0, 1.0) * 65535.0));
    };
    for (int y = 0; y < rendered.height(); ++y) {
      auto* row = reinterpret_cast<QRgba64*>(rendered.scanLine(y));
      const auto row_offset = static_cast<std::size_t>(y * rendered.width());
      for (int x = 0; x < rendered.width(); ++x) {
        const auto& pixel = pixels[row_offset + static_cast<std::size_t>(x)];
        row[x] = QRgba64::fromRgba64(
            encode_srgb16(pixel.red),
            encode_srgb16(pixel.green),
            encode_srgb16(pixel.blue),
            static_cast<quint16>(std::lround(
                std::clamp(static_cast<double>(pixel.alpha), 0.0, 1.0) *
                65535.0)));
      }
    }
    rendered.setColorSpace(QColorSpace(QColorSpace::SRgb));
    telemetry.backend = QStringLiteral("cpu");
    telemetry.fallback_reason = QStringLiteral("engine_output_facade_requested");
  } else {
    std::cerr << "render_path_must_be_desktop_export_or_engine_output\n";
    return EXIT_FAILURE;
  }
  if (rendered.isNull() || rendered.size() != source.size() ||
      !rendered.colorSpace().isValid() ||
      rendered.colorSpace().primaries() != QColorSpace::Primaries::SRgb ||
      rendered.colorSpace().transferFunction() !=
          QColorSpace::TransferFunction::SRgb ||
      telemetry.backend != QStringLiteral("cpu")) {
    std::cerr << "shipping_candidate_render_failed:"
              << telemetry.backend.toStdString() << ':'
              << telemetry.fallback_reason.toStdString() << '\n';
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
  const auto verified = verifier.read();
  if (verified.isNull() || verified.size() != source.size() ||
      verified.colorSpace().primaries() != QColorSpace::Primaries::SRgb ||
      verified.colorSpace().transferFunction() !=
          QColorSpace::TransferFunction::SRgb) {
    QFile::remove(temporary_output);
    std::cerr << "candidate_png_readback_failed\n";
    return EXIT_FAILURE;
  }
  if (!QFile::rename(temporary_output, output_path)) {
    QFile::remove(temporary_output);
    std::cerr << "candidate_png_publish_failed\n";
    return EXIT_FAILURE;
  }
  const auto output_sha256 = sha256_file(output_path);
  const auto output_pixel_sha256 = pixel_sha256(verified);
  const auto output_icc_sha256 = QCryptographicHash::hash(
                                     verified.colorSpace().iccProfile(),
                                     QCryptographicHash::Sha256)
                                     .toHex();
  if (output_sha256.isEmpty() || output_pixel_sha256.isEmpty() ||
      output_icc_sha256.isEmpty()) {
    QFile::remove(output_path);
    std::cerr << "candidate_output_hash_failed\n";
    return EXIT_FAILURE;
  }
  const auto recipe = recipe_contract(render_path);
  const QJsonObject report{
      {QStringLiteral("schema"),
       QStringLiteral("photo-editor-independent-chart-candidate-report:v1")},
      {QStringLiteral("tool_version"), QString::fromLatin1(kToolVersion)},
      {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
      {QStringLiteral("renderer"),
       render_path == QStringLiteral("desktop-export")
           ? QStringLiteral("shipping-desktop-render-output-image-with-backend")
           : QStringLiteral("shipping-engine-apply-output-transform")},
      {QStringLiteral("source_id"), source_id},
      {QStringLiteral("output_id"), output_id},
      {QStringLiteral("source_file_sha256"), QString::fromLatin1(source_sha256)},
      {QStringLiteral("source_pixel_sha256"),
       QString::fromLatin1(source_pixel_sha256)},
      {QStringLiteral("source_icc_sha256"),
       QString::fromLatin1(source_icc_sha256)},
      {QStringLiteral("output_file_sha256"), QString::fromLatin1(output_sha256)},
      {QStringLiteral("output_pixel_sha256"),
       QString::fromLatin1(output_pixel_sha256)},
      {QStringLiteral("output_icc_sha256"),
       QString::fromLatin1(output_icc_sha256)},
      {QStringLiteral("output_color_space"), QStringLiteral("sRGB")},
      {QStringLiteral("output_storage"), QStringLiteral("rgba16-png")},
      {QStringLiteral("width"), verified.width()},
      {QStringLiteral("height"), verified.height()},
      {QStringLiteral("backend_requested"), QStringLiteral("cpu-only")},
      {QStringLiteral("backend_used"), telemetry.backend},
      {QStringLiteral("fallback_reason"), telemetry.fallback_reason},
      {QStringLiteral("recipe_sha256"),
       QString::fromLatin1(recipe_sha256(recipe))},
      {QStringLiteral("recipe"), recipe},
  };
  QSaveFile report_file(report_path);
  const auto report_bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
  if (!report_file.open(QIODevice::WriteOnly) ||
      report_file.write(report_bytes) != report_bytes.size() ||
      !report_file.commit()) {
    QFile::remove(output_path);
    std::cerr << "candidate_report_publish_failed\n";
    return EXIT_FAILURE;
  }
  std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString()
            << '\n';
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Render a tagged chart through the shipping CPU color path."));
  parser.addHelpOption();
  const QCommandLineOption input(
      QStringLiteral("input"), QStringLiteral("Tagged linear-sRGB chart PNG."),
      QStringLiteral("path"));
  const QCommandLineOption output(
      QStringLiteral("output"), QStringLiteral("Shipping-rendered candidate PNG."),
      QStringLiteral("path"));
  const QCommandLineOption report(
      QStringLiteral("report"), QStringLiteral("Candidate provenance report."),
      QStringLiteral("path"));
  const QCommandLineOption source_id(
      QStringLiteral("source-id"), QStringLiteral("Stable tagged source identity."),
      QStringLiteral("logical-id"));
  const QCommandLineOption output_id(
      QStringLiteral("output-id"), QStringLiteral("Stable candidate identity."),
      QStringLiteral("logical-id"));
  const QCommandLineOption render_path(
      QStringLiteral("render-path"),
      QStringLiteral("desktop-export or engine-output."),
      QStringLiteral("name"),
      QStringLiteral("desktop-export"));
  parser.addOptions({input, output, report, source_id, output_id, render_path});
  parser.process(application);
  if (!parser.isSet(input) || !parser.isSet(output) || !parser.isSet(report) ||
      !parser.isSet(source_id) || !parser.isSet(output_id)) {
    std::cerr << "--input, --output, --report, --source-id, and --output-id are required\n";
    return EXIT_FAILURE;
  }
  return render_candidate(
      parser.value(input), parser.value(output), parser.value(report),
      parser.value(source_id), parser.value(output_id), parser.value(render_path));
}
