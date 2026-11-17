#include "timeline_rig_scene.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (argc != 6) {
    std::cerr << "Usage: rig-render FILM.json WIDTH HEIGHT SECONDS PNG_OR_raw\n";
    return 2;
  }
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) return 3;
  QJsonParseError parse;
  const auto film = QJsonDocument::fromJson(file.readAll(), &parse).object();
  if (parse.error != QJsonParseError::NoError) return 4;
  const QSize size(QString::fromLocal8Bit(argv[2]).toInt(), QString::fromLocal8Bit(argv[3]).toInt());
  const auto seconds = QString::fromLocal8Bit(argv[4]).toDouble();
  const auto raw = QString::fromLocal8Bit(argv[5]) == "raw";
  const auto fps = film.value("fps").toDouble(24.0);
  const auto duration = film.value("duration").toDouble();
  if (fps <= 0 || fps > 120 || duration <= 0 || duration > 3600 || size.isEmpty()) return 5;
  struct Shot { double start, end; std::shared_ptr<const photo_editor::desktop::timeline_motion::PreparedRigScene> scene; };
  QVector<Shot> shots;
  for (const auto& entry : film.value("shots").toArray()) {
    const auto s = entry.toObject();
    QString error;
    const auto scene = photo_editor::desktop::timeline_motion::prepare_rig_scene(s.value("scene").toObject().toVariantMap(), &error);
    if (!scene) { std::cerr << error.toStdString() << '\n'; return 6; }
    shots.append({s.value("start").toDouble(), s.value("end").toDouble(), scene});
  }
  const auto draw = [&](double t) {
    for (const auto& shot : shots)
      if (t >= shot.start && t < shot.end)
        return photo_editor::desktop::timeline_motion::render_rig_scene_frame(size, *shot.scene, t-shot.start);
    return QImage{};
  };
  if (!raw) {
    const auto image = draw(seconds);
    return !image.isNull() && image.save(QString::fromLocal8Bit(argv[5])) ? 0 : 7;
  }
  const auto frames = static_cast<int>(std::llround(duration*fps));
  for (int i = 0; i < frames; ++i) {
    const auto image = draw(static_cast<double>(i)/fps).convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return 8;
    for (int row = 0; row < size.height(); ++row)
      if (std::fwrite(image.constScanLine(row), 1, static_cast<size_t>(size.width())*4U, stdout) !=
          static_cast<size_t>(size.width())*4U) return 9;
    if (i % 120 == 0) std::cerr << "Frame " << i << '/' << frames << '\n';
  }
  return 0;
}
