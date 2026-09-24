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
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

struct TagResult final {
  bool success = false;
  QString failure_reason;
  QJsonObject report;
};

constexpr auto kTaggerVersion = "photo-editor-independent-chart-profile-tag:v1";

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
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) * 4;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return hash.result().toHex();
}

[[nodiscard]] TagResult tag_linear_srgb_png(
    const QString& input_path,
    const QString& output_path,
    const QString& report_path,
    const QString& source_id,
    const QString& output_id) {
  const auto fail = [](QString reason) {
    return TagResult{.failure_reason = std::move(reason)};
  };
  if (input_path.isEmpty() || output_path.isEmpty() || report_path.isEmpty() ||
      input_path == output_path || output_path == report_path ||
      input_path == report_path) {
    return fail(QStringLiteral("invalid_path_contract"));
  }
  if (!valid_logical_id(source_id) || !valid_logical_id(output_id) ||
      source_id == output_id) {
    return fail(QStringLiteral("invalid_logical_id_contract"));
  }
  if (QFile::exists(output_path) || QFile::exists(report_path)) {
    return fail(QStringLiteral("output_already_exists"));
  }
  QImageReader reader(input_path, "png");
  reader.setAutoTransform(false);
  auto source = reader.read();
  if (source.isNull()) {
    return fail(QStringLiteral("source_png_decode_failed:%1").arg(reader.errorString()));
  }
  if (source.colorSpace().isValid()) {
    return fail(QStringLiteral("source_must_be_untagged"));
  }
  if (source.width() != 606 || source.height() != 404 || source.depth() != 32) {
    return fail(QStringLiteral("source_content_contract_mismatch"));
  }
  const auto source_file_sha256 = sha256_file(input_path);
  const auto source_pixel_sha256 = pixel_sha256(source);
  if (source_file_sha256.isEmpty() || source_pixel_sha256.isEmpty()) {
    return fail(QStringLiteral("source_hash_failed"));
  }

  auto tagged = source.convertToFormat(QImage::Format_RGBA8888);
  const QColorSpace linear_srgb(QColorSpace::SRgbLinear);
  if (!linear_srgb.isValid()) {
    return fail(QStringLiteral("linear_srgb_profile_unavailable"));
  }
  tagged.setColorSpace(linear_srgb);
  const auto temporary_output = output_path + QStringLiteral(".partial.png");
  QFile::remove(temporary_output);
  QImageWriter writer(temporary_output, "png");
  writer.setCompression(9);
  if (!writer.write(tagged)) {
    QFile::remove(temporary_output);
    return fail(QStringLiteral("tagged_png_write_failed:%1").arg(writer.errorString()));
  }

  QImageReader verification_reader(temporary_output, "png");
  verification_reader.setAutoTransform(false);
  const auto verified = verification_reader.read();
  const auto verified_space = verified.colorSpace();
  const auto output_pixel_sha256 = pixel_sha256(verified);
  if (verified.isNull() || !verified_space.isValid() ||
      verified_space.primaries() != QColorSpace::Primaries::SRgb ||
      verified_space.transferFunction() != QColorSpace::TransferFunction::Linear) {
    QFile::remove(temporary_output);
    return fail(QStringLiteral("tagged_png_profile_verification_failed"));
  }
  if (verified.size() != source.size() ||
      output_pixel_sha256 != source_pixel_sha256) {
    QFile::remove(temporary_output);
    return fail(QStringLiteral("tagged_png_changed_pixel_samples"));
  }
  const auto profile_payload = verified_space.iccProfile();
  if (profile_payload.isEmpty()) {
    QFile::remove(temporary_output);
    return fail(QStringLiteral("tagged_png_icc_payload_missing"));
  }
  if (!QFile::rename(temporary_output, output_path)) {
    QFile::remove(temporary_output);
    return fail(QStringLiteral("tagged_png_publish_failed"));
  }
  const auto output_file_sha256 = sha256_file(output_path);
  if (output_file_sha256.isEmpty()) {
    QFile::remove(output_path);
    return fail(QStringLiteral("tagged_png_hash_failed"));
  }
  const auto profile_sha256 = QCryptographicHash::hash(
      profile_payload, QCryptographicHash::Sha256).toHex();
  const QJsonObject report{
      {QStringLiteral("schema"),
       QStringLiteral("photo-editor-independent-chart-tag-report:v1")},
      {QStringLiteral("tagger_version"), QString::fromLatin1(kTaggerVersion)},
      {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
      {QStringLiteral("source_id"), source_id},
      {QStringLiteral("output_id"), output_id},
      {QStringLiteral("source_file_sha256"), QString::fromLatin1(source_file_sha256)},
      {QStringLiteral("output_file_sha256"), QString::fromLatin1(output_file_sha256)},
      {QStringLiteral("source_pixel_sha256"), QString::fromLatin1(source_pixel_sha256)},
      {QStringLiteral("output_pixel_sha256"), QString::fromLatin1(output_pixel_sha256)},
      {QStringLiteral("icc_profile_sha256"), QString::fromLatin1(profile_sha256)},
      {QStringLiteral("declared_color_space"), QStringLiteral("Linear sRGB")},
      {QStringLiteral("profile_primaries"), QStringLiteral("sRGB")},
      {QStringLiteral("profile_transfer"), QStringLiteral("linear")},
      {QStringLiteral("width"), source.width()},
      {QStringLiteral("height"), source.height()},
      {QStringLiteral("pixel_samples_unchanged"), true},
      {QStringLiteral("tag_only"), true},
  };
  QSaveFile report_file(report_path);
  if (!report_file.open(QIODevice::WriteOnly) ||
      report_file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0 ||
      !report_file.commit()) {
    QFile::remove(output_path);
    return fail(QStringLiteral("tag_report_publish_failed"));
  }
  return TagResult{.success = true, .report = report};
}

[[nodiscard]] int run_self_test() {
  QTemporaryDir directory;
  if (!directory.isValid()) {
    std::cerr << "unable to create profile-tag self-test directory\n";
    return EXIT_FAILURE;
  }
  const auto input_path = directory.filePath(QStringLiteral("source.png"));
  const auto output_path = directory.filePath(QStringLiteral("tagged.png"));
  const auto report_path = directory.filePath(QStringLiteral("report.json"));
  const auto source_id = QStringLiteral("members/checker24/TestCamera/wb1i1e1-raw.png");
  const auto output_id =
      QStringLiteral("references/checker24/TestCamera/wb1i1e1-linear-srgb.png");
  QImage source(606, 404, QImage::Format_RGB32);
  for (int y = 0; y < source.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(source.scanLine(y));
    for (int x = 0; x < source.width(); ++x) {
      row[x] = qRgb((x * 17 + y * 3) % 256,
                    (x * 5 + y * 13) % 256,
                    (x * 7 + y * 11) % 256);
    }
  }
  if (!source.save(input_path, "png")) {
    std::cerr << "unable to write profile-tag self-test source\n";
    return EXIT_FAILURE;
  }
  const auto result = tag_linear_srgb_png(
      input_path, output_path, report_path, source_id, output_id);
  if (!result.success || !QFile::exists(output_path) || !QFile::exists(report_path) ||
      result.report.value(QStringLiteral("source_id")).toString() != source_id ||
      result.report.value(QStringLiteral("output_id")).toString() != output_id ||
      !result.report.value(QStringLiteral("pixel_samples_unchanged")).toBool() ||
      result.report.value(QStringLiteral("source_pixel_sha256")).toString() !=
          result.report.value(QStringLiteral("output_pixel_sha256")).toString()) {
    std::cerr << "profile-tag self-test failed: "
              << result.failure_reason.toStdString() << '\n';
    return EXIT_FAILURE;
  }
  const auto retagged = tag_linear_srgb_png(output_path,
                                             directory.filePath(QStringLiteral("retagged.png")),
                                             directory.filePath(QStringLiteral("retagged.json")),
                                             output_id,
                                             QStringLiteral("references/retagged.png"));
  if (retagged.success || retagged.failure_reason != QStringLiteral("source_must_be_untagged")) {
    std::cerr << "profile-tag tool must reject already tagged sources\n";
    return EXIT_FAILURE;
  }
  std::cout << "independent_chart_profile_tag_self_test=passed\n";
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Attach a verified linear-sRGB ICC tag without changing chart pixels."));
  parser.addHelpOption();
  const QCommandLineOption self_test(QStringLiteral("self-test"));
  const QCommandLineOption input(
      QStringLiteral("input"), QStringLiteral("Untagged Middlebury raw.png member."),
      QStringLiteral("path"));
  const QCommandLineOption output(
      QStringLiteral("output"), QStringLiteral("Derived tagged PNG."),
      QStringLiteral("path"));
  const QCommandLineOption report(
      QStringLiteral("report"), QStringLiteral("Machine-readable provenance report."),
      QStringLiteral("path"));
  const QCommandLineOption source_id(
      QStringLiteral("source-id"), QStringLiteral("Stable source member identity."),
      QStringLiteral("logical-id"));
  const QCommandLineOption output_id(
      QStringLiteral("output-id"), QStringLiteral("Stable derived image identity."),
      QStringLiteral("logical-id"));
  parser.addOptions({self_test, input, output, report, source_id, output_id});
  parser.process(application);
  if (parser.isSet(self_test)) {
    return run_self_test();
  }
  if (!parser.isSet(input) || !parser.isSet(output) || !parser.isSet(report) ||
      !parser.isSet(source_id) || !parser.isSet(output_id)) {
    std::cerr << "--input, --output, --report, --source-id, and --output-id are required\n";
    return EXIT_FAILURE;
  }
  const auto result = tag_linear_srgb_png(
      parser.value(input), parser.value(output), parser.value(report),
      parser.value(source_id), parser.value(output_id));
  if (!result.success) {
    std::cerr << result.failure_reason.toStdString() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << QJsonDocument(result.report).toJson(QJsonDocument::Compact).toStdString()
            << '\n';
  return EXIT_SUCCESS;
}
