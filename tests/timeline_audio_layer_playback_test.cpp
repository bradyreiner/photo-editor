#include <QAudioDevice>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QtEndian>

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>

#include "timeline_audio_output.h"

using photo_editor::desktop::TimelineAudioOutput;

namespace {
int fail(const char* message) {
  std::cerr << "Timeline audio layer: " << message << '\n';
  return 1;
}
bool wait_for(const std::function<bool()>& predicate, int timeout_ms = 8000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) return true;
    QTest::qWait(10);
  }
  return false;
}
QString tone_file(const QString& path) {
  QByteArray pcm;
  constexpr int frames = 96000;
  for (int frame = 0; frame < frames; ++frame) {
    const auto value = qToLittleEndian(static_cast<qint16>(
        8192.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * frame / 48000.0)));
    pcm.append(reinterpret_cast<const char*>(&value), sizeof(value));
    pcm.append(reinterpret_cast<const char*>(&value), sizeof(value));
  }
  QByteArray wav("RIFF");
  const auto u32 = [&](quint32 value) { value = qToLittleEndian(value); wav.append(reinterpret_cast<const char*>(&value), 4); };
  const auto u16 = [&](quint16 value) { value = qToLittleEndian(value); wav.append(reinterpret_cast<const char*>(&value), 2); };
  u32(static_cast<quint32>(pcm.size() + 36)); wav.append("WAVEfmt ", 8); u32(16);
  u16(1); u16(2); u32(48000); u32(192000); u16(4); u16(16);
  wav.append("data", 4); u32(static_cast<quint32>(pcm.size())); wav.append(pcm);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) return {};
  return QUrl::fromLocalFile(path).toString();
}
}

int main(int argc, char** argv) {
  qputenv("QT_MEDIA_BACKEND", QByteArrayLiteral("darwin"));
  qunsetenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS");
  QGuiApplication app(argc, argv);
  if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg"),
          {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")}).isEmpty()) return 77;
  QTemporaryDir workspace;
  const auto source_url = tone_file(workspace.filePath(QStringLiteral("tone.wav")));
  if (source_url.isEmpty()) return fail("tone fixture unavailable");
  QFile file(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/TimelineView.qml"));
  if (!file.open(QIODevice::ReadOnly)) return fail("production QML unavailable");
  const auto source = QString::fromUtf8(file.readAll());
  const auto marker = source.indexOf(QStringLiteral("id: programAudioLayerRepeater"));
  const auto start = source.lastIndexOf(QStringLiteral("Repeater {"), marker);
  const auto next_model = source.indexOf(QStringLiteral("model: programBackgroundLayerDelegateModel"), marker);
  const auto end = source.lastIndexOf(QStringLiteral("Repeater {"), next_model);
  if (marker < 0 || start < 0 || end <= start) return fail("production audio delegate unavailable");
  auto production = source.mid(start, end - start);
  // Repeater delegates have visual parents distinct from QObject ownership.
  // Expose the real production output only to this fixture, without a fake sink.
  production.replace(QStringLiteral("function audioPlaybackDiagnostics()"),
      QStringLiteral("function testOutputObject() { return audioPlaybackLoader.item ? audioPlaybackLoader.item.audioOutput : null }\nfunction audioPlaybackDiagnostics()"));
  qmlRegisterType<TimelineAudioOutput>("Looksy.AudioTest", 1, 0, "TimelineAudioOutput");
  QQmlEngine engine;
  QQmlComponent component(&engine);
  const auto fixture = QStringLiteral(R"QML(
import QtQuick
import QtQuick.Window
import QtMultimedia
import Looksy.AudioTest 1.0
Window {
    id: root
    width: 64; height: 64; visible: true
    property bool sequencePlaying: false
    property real sequencePlayheadSeconds: 0.5
    property bool renderCacheActive: false
    property var programAudioDelegateHost: null
    property int audioMeterResetRevision: 0
    property var programAudioLayerModel: [{clipId: "music", trackId: "music-track", mediaType: "audio", startSeconds: 0, endSeconds: 2, sourceAudioChannels: 2}]
    property string audioSource: ""
    QtObject { id: timelineDocument; signal playbackStateChanged() }
    onSequencePlayingChanged: timelineDocument.playbackStateChanged()
    function programPlaybackSourceUrl(row) { return audioSource }
    function programLayerSourceSeconds(row) { return sequencePlayheadSeconds }
    function programAudioTrackSettings(row) { return ({}) }
    function programAudioVolume(row, settings) { return 0.01 }
    function programAudioPan(row, settings) { return 0 }
    function publishTrackMeter() {}
    function publishTrackPhase() {}
    function outputForTest() { const layer = programAudioLayerRepeater.itemAt(0); return layer ? layer.testOutputObject() : null }
    function playerCountForTest() { const layer = programAudioLayerRepeater.itemAt(0); return layer ? layer.audioPlaybackDiagnostics().nativePlayerCount : 0 }
    %1
}
)QML").arg(production);
  component.setData(fixture.toUtf8(), QUrl(QStringLiteral("file:///timeline-audio-layer-test.qml")));
  if (component.isError()) { std::cerr << component.errorString().toStdString(); return 1; }
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return 1; }
  root->setProperty("audioSource", source_url);
  root->setProperty("sequencePlayheadSeconds", 0.6);
  QPointer<TimelineAudioOutput> output;
  const auto resolve_output = [&]() {
    QVariant returned;
    QMetaObject::invokeMethod(root.get(), "outputForTest", Q_RETURN_ARG(QVariant, returned));
    output = qobject_cast<TimelineAudioOutput*>(returned.value<QObject*>());
    return !output.isNull();
  };
  if (!wait_for(resolve_output)) return fail("production audio Loader must create its output");
  if (output == nullptr || !output->pcmFallbackEnabled()) return fail("Darwin must use the PCM fallback");
  QVariant player_count;
  QMetaObject::invokeMethod(root.get(), "playerCountForTest", Q_RETURN_ARG(QVariant, player_count));
  if (player_count.toInt() != 0) return fail("PCM audio lanes must allocate no AV-backed media players");
  root->setProperty("sequencePlaying", true);
  if (!wait_for([&]() { return output->decoderDiagnostics().value(QStringLiteral("pcmFallbackDecodedBufferCount")).toULongLong() > 3 && output->rmsLeft() > 0.0001; })) {
    std::cerr << output->decoderDiagnostics().value(QStringLiteral("pcmFallbackError")).toString().toStdString();
    return fail("real decoded production-lane audio must reach nonzero post-DSP meters");
  }
  const auto device_available = !QMediaDevices::defaultAudioOutput().isNull();
  if (device_available && !wait_for([&]() { return output->decoderDiagnostics().value(QStringLiteral("sinkWrittenBytes")).toULongLong() > 0; }))
    return fail("decoded lane audio must reach the real output sink");
  const auto written = output->decoderDiagnostics().value(QStringLiteral("sinkWrittenBytes")).toULongLong();
  root->setProperty("sequencePlaying", false);
  const auto stopped = output->decoderDiagnostics().value(QStringLiteral("pcmFallbackDecodedBufferCount")).toULongLong();
  QTest::qWait(150);
  if (output->decoderDiagnostics().value(QStringLiteral("pcmFallbackActive")).toBool() ||
      output->decoderDiagnostics().value(QStringLiteral("pcmFallbackDecodedBufferCount")).toULongLong() != stopped)
    return fail("pause must stop lane decode without stale callbacks");
  root->setProperty("programAudioLayerModel", QVariantList{});
  QTest::qWait(20);
  if (!output.isNull())
    return fail("removing a lane must release its decoder/output");
  std::cout << "Production audio lane decoded nonzero PCM; sinkWrittenBytes=" << written
            << "; nativePlayers=0; pause/removal passed\n";
  return 0;
}
