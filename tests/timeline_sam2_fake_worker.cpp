#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <iostream>

namespace {

QString value_after(const QStringList& arguments, const QString& name) {
  const int index = arguments.indexOf(name);
  return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString{};
}

void emit_error(const QString& status, const QString& message) {
  std::cout << "{\"type\":\"error\",\"status\":\"" << status.toStdString()
            << "\",\"message\":\"" << message.toStdString() << "\"}" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList arguments = app.arguments();
#if TIMELINE_SAM2_FAKE_MODE == 1
  emit_error(QStringLiteral("unavailable"), QStringLiteral("fake worker reports missing SAM2 imports"));
  return 2;
#elif TIMELINE_SAM2_FAKE_MODE == 2
  while (true) {
    QThread::msleep(50);
  }
#else
  const QString output_directory = value_after(arguments, QStringLiteral("--mask-dir"));
  const QString corrections_path = value_after(arguments, QStringLiteral("--corrections-json"));
  bool count_ok = false;
  const int frame_count = value_after(arguments, QStringLiteral("--frame-count")).toInt(&count_ok);
  if (output_directory.isEmpty() || !count_ok || frame_count < 1) {
    emit_error(QStringLiteral("invalid_input"), QStringLiteral("fake worker arguments are malformed"));
    return 2;
  }
  if (!corrections_path.isEmpty()) {
    QFile corrections_file(corrections_path);
    if (!corrections_file.open(QIODevice::ReadOnly)) {
      emit_error(QStringLiteral("invalid_input"), QStringLiteral("correction JSON file is unreadable"));
      return 2;
    }
    const auto corrections = QJsonDocument::fromJson(corrections_file.readAll()).object()
                                .value(QStringLiteral("frames")).toArray();
    if (corrections.size() != 1 ||
        corrections.first().toObject().value(QStringLiteral("frameIndex")).toInt(-1) != 1 ||
        corrections.first().toObject().value(QStringLiteral("points")).toArray().size() != 2 ||
        corrections.first().toObject().value(QStringLiteral("points")).toArray().at(0).toObject()
                .value(QStringLiteral("label")).toInt(-1) != 1 ||
        corrections.first().toObject().value(QStringLiteral("points")).toArray().at(0).toObject()
                .value(QStringLiteral("x")).toDouble(-1) != 0.36 ||
        corrections.first().toObject().value(QStringLiteral("points")).toArray().at(0).toObject()
                .value(QStringLiteral("y")).toDouble(-1) != 0.40 ||
        corrections.first().toObject().value(QStringLiteral("points")).toArray().at(1).toObject()
                .value(QStringLiteral("label")).toInt(-1) != 0) {
      emit_error(QStringLiteral("invalid_input"), QStringLiteral("correction JSON payload did not match the expected prompt"));
      return 2;
    }
  }
  QDir output(output_directory);
  const bool all_empty = qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_SAM2_EMPTY_ALL");
  for (int index = 0; index < frame_count; ++index) {
    QImage mask(8, 6, QImage::Format_ARGB32);
    mask.fill(qRgba(255, 255, 255, 0));
    if (!all_empty && index == 0) {
      for (int y = 1; y < 5; ++y) {
        for (int x = 2; x < 6; ++x) {
          mask.setPixel(x, y, qRgba(255, 255, 255, 255));
        }
      }
    }
    const QString filename = QStringLiteral("mask_%1.png").arg(index, 6, 10, QLatin1Char('0'));
    if (!mask.save(output.filePath(filename), "PNG")) {
      emit_error(QStringLiteral("failed"), QStringLiteral("fake worker failed to save a mask"));
      return 3;
    }
    std::cout << "{\"type\":\"progress\",\"completed\":" << (index + 1)
              << ",\"total\":" << frame_count << "}" << std::endl;
  }
  std::cout << "{\"type\":\"complete\",\"frame_count\":" << frame_count << "}" << std::endl;
  return 0;
#endif
}
