#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (argc != 3 && argc != 4) return 2;
  const auto seconds = argc == 4 ? QString::fromLocal8Bit(argv[3]).toDouble() : 218.5;
  QFile project(QString::fromLocal8Bit(argv[1]));
  QFile source(QString::fromLocal8Bit(argv[2]));
  if (!project.open(QIODevice::ReadOnly) || !source.open(QIODevice::ReadOnly)) return 3;
  const auto document = QJsonDocument::fromJson(project.readAll()).object();
  QVariantList rows;
  for (const auto& track_value : document.value("tracks").toArray()) {
    const auto track = track_value.toObject();
    for (const auto& clip_value : track.value("clips").toArray()) {
      const auto clip = clip_value.toObject();
      const auto start = clip.value("startSeconds").toDouble();
      const auto duration = clip.value("durationSeconds").toDouble();
      const auto trimmed = (clip.value("trimEndSeconds").toDouble(duration) - clip.value("trimStartSeconds").toDouble())
          / qMax(0.01, qAbs(clip.value("playbackRate").toDouble(1.0)));
      if (start <= seconds && start + trimmed > seconds)
        rows.push_back(QVariantMap{{"clip", clip.toVariantMap()}, {"track", track.toVariantMap()}});
    }
  }
  auto helper = source.readAll();
  helper.replace("id: root", "id: root\n property int buildCount: 0");
  helper.replace("const channels = Object.create(null)", "root.buildCount += 1\n const channels = Object.create(null)");
  const auto end = helper.lastIndexOf('}');
  helper.insert(end, R"QML(
    property var fixtureRows: []
    function exercise() {
        let checksum = 0
        for (let frame = 0; frame < 120; ++frame) {
            for (let i = 0; i < fixtureRows.length; ++i) {
                const row = fixtureRows[i]
                for (const parameter of ["frameScale", "frameOffsetX", "frameOffsetY", "opacity", "frameRotationDegrees", "frameCornerRadius"])
                    checksum += clipParameter(row.clip, parameter).length
                checksum += trackParameter(row.track, null, "opacity").length
            }
        }
        return checksum
    }
)QML");
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(helper, QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])));
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return 4; }
  root->setProperty("fixtureRows", rows);
  QElapsedTimer timer;
  timer.start();
  QVariant checksum;
  if (!QMetaObject::invokeMethod(root.get(), "exercise", Q_RETURN_ARG(QVariant, checksum))) return 5;
  std::cout << QJsonDocument(QJsonObject{{"milliseconds", timer.nsecsElapsed() / 1000000.0},
      {"rows", rows.size()}, {"channelBuilds", root->property("buildCount").toInt()},
      {"checksum", checksum.toInt()}}).toJson(QJsonDocument::Compact).toStdString() << '\n';
}
