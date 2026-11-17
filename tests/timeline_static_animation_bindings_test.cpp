#include <QCoreApplication>
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <iostream>
#include <memory>

namespace {
QString production_function(const QString& source, const QString& name) {
  const auto start = source.indexOf(QStringLiteral("    function ") + name + QLatin1Char('('));
  const auto end = source.indexOf(QStringLiteral("\n    function "), start + 1);
  return start >= 0 && end > start ? source.mid(start, end - start) : QString{};
}
QVariantMap counts(QObject* root) {
  return root->property("evaluations").value<QJSValue>().toVariant().toMap();
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const bool measure_only = app.arguments().contains(QStringLiteral("--measure-only"));
  const auto source_path = app.arguments().size() > 2
      ? app.arguments().at(2)
      : QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/TimelineView.qml");
  QFile file(source_path);
  if (!file.open(QIODevice::ReadOnly)) return 2;
  const auto source = QString::fromUtf8(file.readAll());
  QString functions;
  for (const auto* name : {"animatedClipValue", "programTrackOpacity", "programColorFadeState"}) {
    const auto function = production_function(source, QString::fromLatin1(name));
    if (function.isEmpty()) return 3;
    functions += function + QLatin1Char('\n');
  }
  const auto fixture = QStringLiteral(R"QML(
import QtQuick
QtObject {
    id: root
    property real sequencePlayheadSeconds: 0
    property var clip: ({startSeconds: 0, trimmedDurationSeconds: 10, scale: 2, keyframes: []})
    property var sequenceTrackModel: [{trackId: "lane", opacity: 0.6, automationKeyframes: []}]
    property var evaluations: ({clip: 0, track: 0, fade: 0})
    property real clipValue: { evaluations.clip += 1; return animatedClipValue(clip, "frameScale", clip.scale) }
    property real trackValue: { evaluations.track += 1; return programTrackOpacity({trackRowIndex: 0, trackId: "lane"}) }
    property var fadeValue: { evaluations.fade += 1; return programColorFadeState(clip) }
    function keyframesForClipParameter(value, parameter) { return value.keyframes || [] }
    function animatedClipValueAtLocal(value, parameter, fallback, seconds) {
        return value.keyframes.length > 0 ? fallback + seconds : fallback
    }
    function trackAutomationKeyframes(track, parameter) { return track.automationKeyframes || [] }
    function trackAutomationBaseValue(track, parameter) { return track.opacity }
    function trackAutomationValueAt(track, parameter, seconds) {
        return track.automationKeyframes.length > 0 ? track.opacity + seconds : track.opacity
    }
    function enableAnimation() {
        clip = {startSeconds: 0, trimmedDurationSeconds: 10, scale: 2,
                keyframes: [{seconds: 0, value: 2}],
                transitionInName: "Fade to White", transitionInSeconds: 2}
        sequenceTrackModel = [{trackId: "lane", opacity: 0.6,
                               automationKeyframes: [{seconds: 0, value: 0.6}]}]
    }
    function clearAnimation() {
        clip = {startSeconds: 0, trimmedDurationSeconds: 10, scale: 3, keyframes: []}
        sequenceTrackModel = [{trackId: "lane", opacity: 0.4, automationKeyframes: []}]
    }
    %1
}
)QML").arg(functions);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(fixture.toUtf8(), QUrl(QStringLiteral("file:///static-animation-bindings.qml")));
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return 4; }
  for (int tick = 1; tick <= 100; ++tick)
    root->setProperty("sequencePlayheadSeconds", static_cast<double>(tick) / 30.0);
  const auto static_counts = counts(root.get());
  std::cout << QJsonDocument::fromVariant(static_counts).toJson(QJsonDocument::Compact).toStdString() << '\n';
  if (measure_only) return 0;
  for (const auto* key : {"clip", "track", "fade"}) {
    if (static_counts.value(QString::fromLatin1(key)).toInt() != 1) {
      std::cerr << "Static properties must not subscribe to the playhead clock\n";
      return 5;
    }
  }
  if (root->property("clipValue").toDouble() != 2 || root->property("trackValue").toDouble() != 0.6)
    return 6;
  if (!QMetaObject::invokeMethod(root.get(), "enableAnimation")) return 7;
  root->setProperty("sequencePlayheadSeconds", 0.5);
  const auto fade = root->property("fadeValue").value<QJSValue>().toVariant().toMap();
  if (root->property("clipValue").toDouble() != 2.5 ||
      root->property("trackValue").toDouble() != 1.1 ||
      fade.value("amount").toDouble() != 0.75) {
    std::cerr << "Authored animation and fades must still follow the clock\n";
    return 8;
  }
  if (!QMetaObject::invokeMethod(root.get(), "clearAnimation")) return 9;
  const auto cleared_counts = counts(root.get());
  for (int tick = 1; tick <= 100; ++tick)
    root->setProperty("sequencePlayheadSeconds", static_cast<double>(tick) / 30.0);
  if (counts(root.get()) != cleared_counts || root->property("clipValue").toDouble() != 3 ||
      root->property("trackValue").toDouble() != 0.4) {
    std::cerr << "Removing animation must release clock dependencies and retain authored fallback edits\n";
    return 10;
  }
  return 0;
}
