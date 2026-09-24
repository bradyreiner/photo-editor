#include <QBuffer>
#include <QColorSpace>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption({"source", "Optional photographic PNG input", "path"});
  parser.addOption({"compression", "Qt PNG compression (-1 for default)", "value", "12"});
  parser.addOption({"iterations", "Measured iterations after one warmup", "count", "5"});
  parser.process(app);
  bool valid_compression = false;
  bool valid_iterations = false;
  const int compression = parser.value("compression").toInt(&valid_compression);
  const int iterations = parser.value("iterations").toInt(&valid_iterations);
  if (!valid_compression || compression < -1 || compression > 100 ||
      !valid_iterations || iterations < 1 || iterations > 100) {
    std::fprintf(stderr, "Invalid compression or iteration count\n");
    return 1;
  }
  QImage source;
  if (parser.isSet("source")) {
    source = QImage(parser.value("source")).convertToFormat(QImage::Format_ARGB32);
  } else {
    source = QImage(512, 384, QImage::Format_ARGB32);
    for (int y = 0; y < source.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(source.scanLine(y));
      for (int x = 0; x < source.width(); ++x) {
        row[x] = qRgba((x * 13 + y * 7) % 256, (x + y * 19) % 256,
                       (x * 31 + y) % 256, (x + y) % 256);
      }
    }
    source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  }
  if (source.isNull() || !source.colorSpace().isValid()) {
    std::fprintf(stderr, "Input must contain pixels and a valid color space\n");
    return 1;
  }
  const auto digest = QCryptographicHash::hash(
      QByteArrayView(reinterpret_cast<const char*>(source.constBits()),
                     source.sizeInBytes()), QCryptographicHash::Sha256).toHex();
  std::vector<double> encode_ms;
  std::vector<double> decode_ms;
  QJsonArray samples;
  qsizetype encoded_bytes = 0;
  for (int iteration = 0; iteration <= iterations; ++iteration) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
      return 1;
    }
    QImageWriter writer(&buffer, "png");
    writer.setOptimizedWrite(false);
    if (compression >= 0) {
      writer.setCompression(compression);
    }
    QElapsedTimer timer;
    timer.start();
    if (!writer.write(source)) {
      std::fprintf(stderr, "PNG encode failed: %s\n", qPrintable(writer.errorString()));
      return 1;
    }
    const double encode = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    buffer.close();
    timer.restart();
    const QImage decoded = QImage::fromData(bytes, "png").convertToFormat(QImage::Format_ARGB32);
    const double decode = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    if (decoded.isNull() || decoded != source || decoded.colorSpace() != source.colorSpace()) {
      std::fprintf(stderr, "PNG roundtrip changed pixels or color space\n");
      return 1;
    }
    encoded_bytes = bytes.size();
    if (iteration > 0) {
      encode_ms.push_back(encode);
      decode_ms.push_back(decode);
      samples.append(QJsonObject{{"encode_ms", encode}, {"decode_ms", decode}});
    }
  }
  const auto percentile = [](std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1, rank) - 1];
  };
  const QJsonObject report{
      {"compression", compression}, {"width", source.width()}, {"height", source.height()},
      {"input_sha256", QString::fromLatin1(digest)}, {"exact_pixels_and_profile", true},
      {"encoded_bytes", static_cast<double>(encoded_bytes)},
      {"encode_p50_ms", percentile(encode_ms, .5)}, {"encode_p95_ms", percentile(encode_ms, .95)},
      {"decode_p50_ms", percentile(decode_ms, .5)}, {"decode_p95_ms", percentile(decode_ms, .95)},
      {"samples", samples}};
  const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
  std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
  return 0;
}
