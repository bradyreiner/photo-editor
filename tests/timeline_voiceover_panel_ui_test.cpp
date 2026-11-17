#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QString>
#include <QThread>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (int iteration = 0; iteration < 30; ++iteration) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(4);
  }
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool invoke(QObject* object, const char* method) {
  return QMetaObject::invokeMethod(object, method, Qt::DirectConnection);
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: 760
    height: 560
    visible: true
    color: "#17191c"

    property int playbackStartRequests: 0
    property int playbackStopRequests: 0
    property bool playbackRunning: true
    property bool playbackStartedByPanel: false

    function beginCapture() { voiceoverPanel.beginRecording() }
    function denyPermission() { fakeRecorder.denyPermission() }
    function cancelCapture() { voiceoverPanel.cancelRecording() }
    function enterPreroll() {
        fakeRecorder.preRollRemainingMs = 850
        fakeRecorder.state = "preroll"
    }
    function enterRecording() { fakeRecorder.state = "recording" }
    function prepareForNewCapture() { playbackRunning = false; voiceoverPanel.open() }

    Component.onCompleted: voiceoverPanel.open()

    QtObject {
        id: fakeRecorder
        objectName: "timelineVoiceoverFakeRecorder"
        property var inputDevices: [{ id: "fake-mic", description: "Fake microphone" }]
        property string selectedInputDeviceId: "fake-mic"
        property string state: "idle"
        property int preRollRemainingMs: 0
        property int recordedDurationMs: 0
        property string errorString: ""
        property string recoverablePath: ""
        property string captureTimelineIdentity: ""
        property string captureDocumentPath: ""
        property string nextCancellationPath: ""
        property int startCalls: 0
        property int cancelCalls: 0
        signal recordingFinished(string filePath, int durationMs)
        signal recordingCancelled(string recoverablePath)
        signal recordingFailed(string detail, string recoverablePath)

        function refreshInputDevices() {}
        function startRecording(documentPath, preRollMs, timelineIdentity) {
            startCalls += 1
            captureDocumentPath = documentPath
            captureTimelineIdentity = timelineIdentity
            errorString = ""
            state = "permissionPending"
            return true
        }
        function cancelRecording() {
            cancelCalls += 1
            recoverablePath = nextCancellationPath
            state = "idle"
            recordingCancelled(recoverablePath)
        }
        function stopRecording() { state = "finalizing" }
        function denyPermission() {
            errorString = "Microphone access was denied. Allow access in System Settings."
            state = "error"
            recordingFailed(errorString, recoverablePath)
        }
    }

    QtObject {
        id: fakeTimelineDocument
        property string timelineAssetId: "timeline-voiceover-test"
        property string savedDocumentPath: "/tmp/timeline-voiceover-test.looksy"
    }

    TimelineVoiceoverPanel {
        id: voiceoverPanel
        objectName: "timelineVoiceoverPanelPopup"
        recorder: fakeRecorder
        timelineDocument: fakeTimelineDocument
        onPlaybackStartRequested: {
            fixture.playbackStartRequests += 1
            fixture.playbackStartedByPanel = !fixture.playbackRunning
            if (fixture.playbackStartedByPanel)
                fixture.playbackRunning = true
        }
        onPlaybackStopRequested: {
            fixture.playbackStopRequests += 1
            if (fixture.playbackStartedByPanel)
                fixture.playbackRunning = false
            fixture.playbackStartedByPanel = false
        }
    }
}
)qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/timeline-voiceover-panel-fixture.qml")));

  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Voiceover panel fixture should instantiate");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  QObject* panel = fixture->findChild<QObject*>(QStringLiteral("timelineVoiceoverPanelPopup"));
  QObject* recorder = fixture->findChild<QObject*>(QStringLiteral("timelineVoiceoverFakeRecorder"));
  if (window == nullptr || panel == nullptr || recorder == nullptr)
    return fail("Voiceover panel and fake recorder should be discoverable in the fixture");

  const QString evidence_dir = qEnvironmentVariable("LOOKSY_TIMELINE_UI_EVIDENCE_DIR");
  if (!evidence_dir.isEmpty() && !QDir().mkpath(evidence_dir))
    return fail("Could not create the requested voiceover UI evidence directory");
  const auto capture_evidence = [&](const QString& file_name) {
    if (evidence_dir.isEmpty())
      return true;
    const QImage image = window->grabWindow();
    return !image.isNull() && image.save(QDir(evidence_dir).filePath(file_name));
  };

  settle();
  if (!panel->property("visible").toBool())
    return fail("Voiceover panel should open in the fixture");

  // Permission is asynchronous. Denying it must not start or stop playback that
  // was already running before the panel requested microphone access.
  if (!invoke(fixture.get(), "beginCapture"))
    return fail("Voiceover panel should invoke the fake recorder start method");
  settle();
  if (recorder->property("state").toString() != QStringLiteral("permissionPending") ||
      fixture->property("playbackStartRequests").toInt() != 0 ||
      fixture->property("playbackStopRequests").toInt() != 0 ||
      !fixture->property("playbackRunning").toBool())
    return fail("Permission-pending capture must leave existing playback untouched");
  if (!invoke(fixture.get(), "denyPermission"))
    return fail("Fake microphone denial should be delivered");
  settle();
  if (recorder->property("state").toString() != QStringLiteral("error") ||
      fixture->property("playbackStartRequests").toInt() != 0 ||
      fixture->property("playbackStopRequests").toInt() != 0 ||
      !fixture->property("playbackRunning").toBool() ||
      !panel->property("visible").toBool())
    return fail("Permission denial must preserve user playback and leave the actionable panel open");
  if (!capture_evidence(QStringLiteral("voiceover-permission-denied.png")))
    return fail("Could not capture the permission-denied voiceover panel");

  // Canceling while permission is still pending also must not claim playback
  // ownership, and an empty cancellation should close the panel.
  if (!invoke(fixture.get(), "beginCapture"))
    return fail("A second permission request should start");
  if (!invoke(fixture.get(), "cancelCapture"))
    return fail("Pending permission request should be cancelable");
  settle();
  if (fixture->property("playbackStartRequests").toInt() != 0 ||
      fixture->property("playbackStopRequests").toInt() != 0 ||
      !fixture->property("playbackRunning").toBool() || panel->property("visible").toBool())
    return fail("Canceling before permission should close without changing existing playback");

  // A successful permission callback enters pre-roll, then recording. Playback
  // should be requested exactly once and the panel should own only that start.
  if (!invoke(fixture.get(), "prepareForNewCapture"))
    return fail("Panel should reopen for a new capture");
  settle();
  if (!invoke(fixture.get(), "beginCapture"))
    return fail("Successful fake capture should begin");
  settle();
  if (fixture->property("playbackStartRequests").toInt() != 0)
    return fail("Permission prompt must finish before timeline playback starts");
  if (!invoke(fixture.get(), "enterPreroll"))
    return fail("Fake recorder should enter pre-roll");
  settle();
  if (fixture->property("playbackStartRequests").toInt() != 1 ||
      !fixture->property("playbackRunning").toBool())
    return fail("Entering pre-roll should start timeline playback once");
  if (!invoke(fixture.get(), "enterRecording"))
    return fail("Fake recorder should enter recording");
  settle();
  if (fixture->property("playbackStartRequests").toInt() != 1 ||
      recorder->property("startCalls").toInt() != 3)
    return fail("Pre-roll to recording must not duplicate timeline or recorder starts");

  // A canceled active take retains its partial file and explanatory state in
  // the still-open panel, while stopping only playback this panel started.
  const QString partial_path = QStringLiteral("/tmp/voiceover-recoverable.partial.wav");
  recorder->setProperty("nextCancellationPath", partial_path);
  if (!invoke(fixture.get(), "cancelCapture"))
    return fail("Active capture should be cancelable");
  settle();
  QObject* status = fixture->findChild<QObject*>(QStringLiteral("timelineVoiceoverStateMessage"));
  QObject* saved_path = fixture->findChild<QObject*>(QStringLiteral("timelineVoiceoverRecoverablePath"));
  const QString status_text = status == nullptr ? QString() : status->property("text").toString();
  const QString path_text = saved_path == nullptr ? QString() : saved_path->property("text").toString();
  if (fixture->property("playbackStopRequests").toInt() != 1 ||
      fixture->property("playbackRunning").toBool() || !panel->property("visible").toBool() ||
      status == nullptr || !status_text.contains(QStringLiteral("recoverable partial WAV")) ||
      saved_path == nullptr || !saved_path->property("visible").toBool() ||
      !path_text.contains(partial_path))
    return fail("Canceled active take should stop owned playback and keep the partial WAV visible");
  if (!capture_evidence(QStringLiteral("voiceover-recoverable-partial.png")))
    return fail("Could not capture the recoverable-partial voiceover panel");
  return EXIT_SUCCESS;
}
