#include "native_window_test_input.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlPropertyMap>
#include <QProcess>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>

namespace {
constexpr qint64 kRequestedPositionMs = 750;
constexpr qint64 kPositionToleranceMs = 100;

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate())
      return true;
    QThread::msleep(5);
  } while (timer.elapsed() < timeout_ms);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

QObject* find_object(QObject* root, const QString& object_name) {
  return root == nullptr ? nullptr : root->findChild<QObject*>(object_name);
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == object_name)
    return root;
  for (auto* child : root->childItems()) {
    if (auto* match = find_item(child, object_name))
      return match;
  }
  return nullptr;
}

bool is_blue_reference(const QColor& pixel) {
  return pixel.blue() > pixel.red() + 65 && pixel.blue() > pixel.green() + 35;
}

bool write_window_capture(QQuickWindow* window, const QString& output_directory,
                          const QString& file_name, QSize* captured_size = nullptr) {
  if (window == nullptr || output_directory.isEmpty() ||
      !QDir().mkpath(output_directory))
    return false;
  const QImage image = window->grabWindow();
  if (image.isNull() || !image.save(QDir(output_directory).filePath(file_name)))
    return false;
  if (captured_size != nullptr)
    *captured_size = image.size();
  return true;
}

QString make_fixture_video(const QString& directory) {
  const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty())
    return {};

  const QString path = QDir(directory).filePath(QStringLiteral("red-blue-aac.mp4"));
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(ffmpeg, {
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("color=c=red:s=320x180:r=30:d=0.5"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("color=c=blue:s=320x180:r=30:d=1.5"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
      QStringLiteral("-filter_complex"),
      QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0,format=yuv420p[v]"),
      QStringLiteral("-map"), QStringLiteral("[v]"),
      QStringLiteral("-map"), QStringLiteral("2:a:0"),
      QStringLiteral("-t"), QStringLiteral("2"),
      QStringLiteral("-r"), QStringLiteral("30"),
      QStringLiteral("-af"), QStringLiteral("volume=0.02"),
      QStringLiteral("-c:v"), QStringLiteral("libx264"),
      QStringLiteral("-preset"), QStringLiteral("ultrafast"),
      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
      QStringLiteral("-c:a"), QStringLiteral("aac"),
      QStringLiteral("-b:a"), QStringLiteral("96k"),
      QStringLiteral("-movflags"), QStringLiteral("+faststart"),
      QStringLiteral("-y"), path,
  });
  if (!process.waitForStarted(3000) || !process.waitForFinished(15000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
      QFileInfo(path).size() <= 1024) {
    std::cerr << "ffmpeg fixture generation: "
              << process.readAllStandardOutput().toStdString() << '\n';
    return {};
  }
  return path;
}

QString make_red_poster(const QString& directory) {
  const QString path = QDir(directory).filePath(QStringLiteral("red-poster.png"));
  QImage image(320, 180, QImage::Format_RGB32);
  image.fill(QColor(255, 0, 0));
  return image.save(path, "PNG") ? QUrl::fromLocalFile(path).toString() : QString{};
}

bool is_critical_qml_warning(const QString& message) {
  const QString lower = message.toLower();
  return lower.contains(QStringLiteral("referenceerror")) ||
         lower.contains(QStringLiteral("typeerror")) ||
         lower.contains(QStringLiteral("unable to assign")) ||
         lower.contains(QStringLiteral("cannot assign")) ||
         lower.contains(QStringLiteral("cannot read property")) ||
         lower.contains(QStringLiteral("is not a function"));
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlPropertyMap app_shell;
  app_shell.insert(QStringLiteral("interfaceScale"), 1.0);
  app_shell.insert(QStringLiteral("interfaceDensity"), QStringLiteral("balanced"));
  app_shell.insert(QStringLiteral("reducedMotion"), false);
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &app_shell);

  QStringList qml_warning_messages;
  QObject::connect(&engine, &QQmlEngine::warnings, &app,
                   [&qml_warning_messages](const QList<QQmlError>& warnings) {
    for (const auto& warning : warnings) {
      qml_warning_messages.push_back(warning.toString());
      std::cerr << "QML warning: " << warning.toString().toStdString() << '\n';
    }
  });

  QTemporaryDir fixture_directory;
  if (!fixture_directory.isValid())
    return fail(QStringLiteral("The Source loading fixture should have a temporary directory"));
  const QString video_path = make_fixture_video(fixture_directory.path());
  const QString poster_source = make_red_poster(fixture_directory.path());
  if (video_path.isEmpty() || poster_source.isEmpty())
    return fail(QStringLiteral("The Source loading UI proof requires an AAC red/blue video fixture"));

  const QString video_url = QUrl::fromLocalFile(video_path).toString(QUrl::FullyEncoded);
  QString qml = QStringLiteral(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml") + QStringLiteral(PHOTO_EDITOR_SOURCE_DIR) + QStringLiteral(R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    id: fixture
    objectName: "timelineSourceLoadingFixture"
    width: 680
    height: 560
    visible: true
    color: "#17191c"

    property var sourceFixture: ({
        "assetId": "source-loading-red-blue",
        "mediaType": "video",
        "sourceUrl": "__SOURCE_URL__",
        "posterSource": "__POSTER_SOURCE__",
        "filePath": "__SOURCE_PATH__",
        "durationSeconds": 2.0,
        "sourceWidth": 320,
        "sourceHeight": 180,
        "mediaAnalysisStatus": "ready"
    })

    QtObject {
        id: fakeDocument
        objectName: "sourceLoadingFakeDocument"
        property bool hasSourceClip: true
        property bool sourceTapeEnabled: false
        property int sourceTapeIndex: -1
        property var sourceTapeItems: []
        property bool sourcePatchReady: false
        property bool hasOfflineMediaItems: false
        property real sourceInSeconds: 0
        property real sourceOutSeconds: 2
        property real framesPerSecond: 30
        property real totalDurationSeconds: 2
        property string activeMediaBinId: ""
        property string activeSmartCollectionId: ""
        property var mediaBins: []
        property var smartCollections: []
        property var currentClip: ({})

        function mediaAssetVisibleInBin(assetId, binId) { return true }
        function mediaAssetMatchesSmartCollection(asset, collectionId) { return true }
        function searchMediaAsset(assetId, query, scopes, asset) {
            return { "matches": true, "matchSummary": "" }
        }
        function mediaAssetMetadata(assetId) { return ({}) }
        function inspectMediaAssetTechnicalMetadata(asset) { return ({}) }
        function setSelectedClipVideoInputTransform(profile) { return true }
    }

    QtObject {
        id: fakeCatalog
        objectName: "sourceLoadingFakeCatalog"
        property var assets: []
        property int assetCount: 0
        property int totalAssetCount: 0
        property bool loading: false
        function assetAt(index) { return ({}) }
        function assetById(assetId) { return ({}) }
        function setVisibleAssetRange(firstIndex, lastIndex) {}
    }

    Theme { id: sharedTheme }

    TimelineSourcePanel {
        id: sourcePanel
        objectName: "timelineSourceLoadingPanel"
        anchors.fill: parent
        embeddedDockHeader: true
        theme: sharedTheme
        timelineDocument: fakeDocument
        sourceClip: fixture.sourceFixture
        catalogAssets: fakeCatalog
        collapsed: false
        activeTab: "media"
        workspaceActive: true
    }
}
)qml");
  qml.replace(QStringLiteral("__SOURCE_URL__"), video_url);
  qml.replace(QStringLiteral("__POSTER_SOURCE__"), poster_source);
  qml.replace(QStringLiteral("__SOURCE_PATH__"), video_path);

  QQmlComponent component(&engine);
  component.setData(qml.toUtf8(), QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/timeline_source_loading_ui_test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    for (const auto& error : component.errors())
      std::cerr << error.toString().toStdString() << '\n';
    return fail(QStringLiteral("TimelineSourcePanel should instantiate in the native loading fixture"));
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* panel = find_object(fixture.get(), QStringLiteral("timelineSourceLoadingPanel"));
  auto* player = find_object(fixture.get(), QStringLiteral("timelineSourcePreviewPlayer"));
  auto* video_output = find_object(fixture.get(), QStringLiteral("timelineSourceVideoOutput"));
  auto* loading_label = find_item(window ? window->contentItem() : nullptr,
                                  QStringLiteral("timelineSourceFrameLoadingLabel"));
  auto* poster_image = find_item(window ? window->contentItem() : nullptr,
                                 QStringLiteral("timelineSourcePosterImage"));
  auto* video_sink = video_output
      ? video_output->property("videoSink").value<QVideoSink*>() : nullptr;
  if (!window || !panel || !player || !video_output || !loading_label || !poster_image || !video_sink)
    return fail(QStringLiteral("The real Source loading label, poster, player, and video sink should be discoverable"));

  window->resize(680, 560);
  window->show();
  activate_native_test_application();
  activate_native_test_window(window);
  const bool native_window_ready = wait_until([&] {
    return window->isExposed() && window->isActive();
  }, 10000);
  if (!native_window_ready)
    return fail(QStringLiteral("The Source UI proof needs an exposed and active native window before decoder start"));

  if (player->property("source").toUrl().isEmpty()) {
    // The media tab is intentionally active until native exposure is proven.
  } else {
    return fail(QStringLiteral("The Source decoder should remain dormant before the native window is ready"));
  }
  panel->setProperty("activeTab", QStringLiteral("source"));
  if (!QMetaObject::invokeMethod(
          panel, "seekSource", Q_ARG(QVariant, QVariant(0.75)),
          Q_ARG(QVariant, QVariant(false))))
    return fail(QStringLiteral("The Source panel should accept the requested 750 ms target"));

  const QString evidence_directory = qEnvironmentVariable(
      "PHOTO_EDITOR_TIMELINE_SOURCE_LOADING_EVIDENCE_DIR");
  if (evidence_directory.isEmpty())
    return fail(QStringLiteral("The Source UI proof needs an evidence output directory"));

  bool window_lost_foreground = false;
  const bool loading_state_seen = wait_until([&] {
    if (!window->isExposed() || !window->isActive()) {
      window_lost_foreground = true;
      return false;
    }
    const bool source_attached = player->property("source").toUrl().toLocalFile() == video_path;
    return source_attached &&
           panel->property("sourceFreshDecoderPending").toBool() &&
           loading_label->property("visible").toBool() &&
           !poster_image->property("visible").toBool();
  }, 5000);
  if (!loading_state_seen || window_lost_foreground)
    return fail(QStringLiteral("Fresh Source loading should show its loading label and hide the red poster in the active native window"));
  if (player->property("audioOutput").isNull() == false)
    return fail(QStringLiteral("Source internal decoder warmup should keep its audio output detached"));

  QSize loading_capture_size;
  if (!write_window_capture(window, evidence_directory,
                            QStringLiteral("source-loading-native.png"),
                            &loading_capture_size))
    return fail(QStringLiteral("The exposed native window should capture the Source loading state"));

  const bool decoded_target_ready = wait_until([&] {
    if (!window->isExposed() || !window->isActive()) {
      window_lost_foreground = true;
      return false;
    }
    const auto frame = video_sink->videoFrame();
    if (!frame.isValid())
      return false;
    const QImage image = frame.toImage();
    if (image.isNull())
      return false;
    const QColor pixel = image.pixelColor(image.width() / 2, image.height() / 2);
    const double position_ms = player->property("position").toDouble();
    const double completed_target =
        panel->property("sourcePrerollCompletedTargetSeconds").toDouble();
    const double completed_position =
        panel->property("sourcePrerollCompletedPositionSeconds").toDouble();
    const int seek_generation =
        panel->property("sourcePrerollCompletedSeekFrameGeneration").toInt();
    const int acknowledged_generation =
        panel->property("sourcePrerollCompletedAcknowledgedFrameGeneration").toInt();
    const double acknowledged_position_ms =
        panel->property("sourcePrerollCompletedAcknowledgedPositionMs").toDouble();
    const int frame_generation =
        panel->property("sourcePrerollCompletedFrameGeneration").toInt();
    return panel->property("sourceDecoderPrimed").toBool() &&
           !panel->property("sourceFreshDecoderPending").toBool() &&
           !panel->property("sourcePrerollActive").toBool() &&
           !panel->property("sourcePrerollFailed").toBool() &&
           panel->property("sourcePrerollCompletedFrameValid").toBool() &&
           panel->property("sourceFrameAvailable").toBool() &&
           !loading_label->property("visible").toBool() &&
           !poster_image->property("visible").toBool() &&
           player->property("playbackState").toInt() ==
               static_cast<int>(QMediaPlayer::PausedState) &&
           std::abs(position_ms - static_cast<double>(kRequestedPositionMs)) <=
               static_cast<double>(kPositionToleranceMs) &&
           std::abs(panel->property("previewSeconds").toDouble() - 0.75) <= 0.1 &&
           std::abs(completed_target - 0.75) <= 0.001 &&
           std::abs(completed_position - 0.75) <= 0.1 &&
           acknowledged_generation > seek_generation &&
           frame_generation > acknowledged_generation &&
           acknowledged_position_ms > static_cast<double>(kRequestedPositionMs) &&
           acknowledged_position_ms - static_cast<double>(kRequestedPositionMs) <=
               static_cast<double>(kPositionToleranceMs) &&
           (frame.startTime() < 0 ||
            std::abs(frame.startTime() - kRequestedPositionMs * 1000) <=
                kPositionToleranceMs * 1000) &&
           is_blue_reference(pixel);
  }, 5000);
  if (!decoded_target_ready || window_lost_foreground) {
    const auto frame = video_sink->videoFrame();
    const auto image = frame.isValid() ? frame.toImage() : QImage{};
    const QColor pixel = image.isNull() ? QColor{} :
        image.pixelColor(image.width() / 2, image.height() / 2);
    std::cerr << "Source loading UI failure: exposed=" << window->isExposed()
              << " active=" << window->isActive()
              << " source=" << player->property("source").toUrl().toString().toStdString()
              << " positionMs=" << player->property("position").toDouble()
              << " playbackState=" << player->property("playbackState").toInt()
              << " fresh=" << panel->property("sourceFreshDecoderPending").toBool()
              << " preroll=" << panel->property("sourcePrerollActive").toBool()
              << " failed=" << panel->property("sourcePrerollFailed").toBool()
              << " loadingVisible=" << loading_label->property("visible").toBool()
              << " posterVisible=" << poster_image->property("visible").toBool()
              << " frameValid=" << frame.isValid()
              << " ptsUs=" << frame.startTime()
              << " pixel=" << pixel.red() << ',' << pixel.green() << ',' << pixel.blue()
              << " previewSeconds=" << panel->property("previewSeconds").toDouble()
              << " completedTarget="
              << panel->property("sourcePrerollCompletedTargetSeconds").toDouble()
              << " completedPosition="
              << panel->property("sourcePrerollCompletedPositionSeconds").toDouble()
              << " ackMs="
              << panel->property("sourcePrerollCompletedAcknowledgedPositionMs").toDouble()
              << " seekGen="
              << panel->property("sourcePrerollCompletedSeekFrameGeneration").toInt()
              << " ackGen="
              << panel->property("sourcePrerollCompletedAcknowledgedFrameGeneration").toInt()
              << " frameGen="
              << panel->property("sourcePrerollCompletedFrameGeneration").toInt() << '\n';
    return fail(QStringLiteral("Fresh Source loading should resolve to a paused decoded blue frame at the requested position"));
  }

  QSize ready_capture_size;
  if (!write_window_capture(window, evidence_directory,
                            QStringLiteral("source-ready-blue-native.png"),
                            &ready_capture_size))
    return fail(QStringLiteral("The exposed native window should capture the decoded Source frame"));

  if (!qml_warning_messages.isEmpty()) {
    const auto critical_warning = std::find_if(
        qml_warning_messages.cbegin(), qml_warning_messages.cend(), is_critical_qml_warning);
    if (critical_warning != qml_warning_messages.cend())
      return fail(QStringLiteral("TimelineSourcePanel emitted a critical QML binding warning"));
  }

  QJsonObject receipt{
      {QStringLiteral("passed"), true},
      {QStringLiteral("nativeWindowExposed"), window->isExposed()},
      {QStringLiteral("nativeWindowActive"), window->isActive()},
      {QStringLiteral("foregroundMaintained"), !window_lost_foreground},
      {QStringLiteral("sourceUrl"), video_url},
      {QStringLiteral("sourceLoadingLabelObserved"), loading_state_seen},
      {QStringLiteral("redPosterHiddenDuringWarmup"), true},
      {QStringLiteral("audioDetachedDuringWarmup"), true},
      {QStringLiteral("requestedPositionMs"), kRequestedPositionMs},
      {QStringLiteral("playerPositionMs"), player->property("position").toDouble()},
      {QStringLiteral("framePtsUs"), video_sink->videoFrame().startTime()},
      {QStringLiteral("positionEstimated"), video_sink->videoFrame().startTime() < 0},
      {QStringLiteral("acknowledgedPositionMs"),
       panel->property("sourcePrerollCompletedAcknowledgedPositionMs").toDouble()},
      {QStringLiteral("loadingHiddenWhenReady"), !loading_label->property("visible").toBool()},
      {QStringLiteral("posterHiddenWhenReady"), !poster_image->property("visible").toBool()},
      {QStringLiteral("playbackPausedWhenReady"),
       player->property("playbackState").toInt() == static_cast<int>(QMediaPlayer::PausedState)},
      {QStringLiteral("blueFrameDisplayed"), true},
      {QStringLiteral("captureLoadingPath"),
       QDir(evidence_directory).filePath(QStringLiteral("source-loading-native.png"))},
      {QStringLiteral("captureLoadingWidth"), loading_capture_size.width()},
      {QStringLiteral("captureLoadingHeight"), loading_capture_size.height()},
      {QStringLiteral("captureReadyPath"),
       QDir(evidence_directory).filePath(QStringLiteral("source-ready-blue-native.png"))},
      {QStringLiteral("captureReadyWidth"), ready_capture_size.width()},
      {QStringLiteral("captureReadyHeight"), ready_capture_size.height()},
      {QStringLiteral("qmlWarnings"), QJsonArray::fromStringList(qml_warning_messages)},
  };
  QFile receipt_file(QDir(evidence_directory).filePath(QStringLiteral("receipt.json")));
  if (!receipt_file.open(QIODevice::WriteOnly) ||
      receipt_file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) < 0)
    return fail(QStringLiteral("The Source loading UI proof should write a receipt beside its captures"));

  std::cout << "Source loading UI passed: ready position="
            << player->property("position").toDouble() << " ms; evidence="
            << evidence_directory.toStdString() << '\n';
  return EXIT_SUCCESS;
}
