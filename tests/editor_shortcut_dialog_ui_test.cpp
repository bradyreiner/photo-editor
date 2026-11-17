#include "app_shell_controller.h"

#include <QApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <iostream>
#include <memory>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool set_sequence(QObject* dialog, const QString& sequence) {
  QVariant result;
  const bool invoked = QMetaObject::invokeMethod(dialog, "setShortcutSequence",
      Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QStringLiteral("timeline-razor")),
      Q_ARG(QVariant, sequence));
  return invoked && result.toBool();
}
}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Temporary profile should be available");
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temporary.path().toUtf8());
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyShortcutUiTests"));
  QCoreApplication::setApplicationName(QStringLiteral("ShortcutDialog"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
  photo_editor::desktop::AppShellController controller;
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &controller);
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import "../apps/desktop/qml/components" as Components
ApplicationWindow {
    width: 1000; height: 900; visible: true
    color: "#17191c"
    Components.KeyboardShortcutsDialog { id: shortcuts; objectName: "testedShortcuts" }
    Components.TimelineDeliveryToolsPanel {
        objectName: "testedDelivery"
        anchors.centerIn: parent
        visible: false
        timelineDocument: QtObject {
            property bool projectPackageBusy: false
            property string projectPackageStatus: ""
            property var projectPackageResult: ({})
            property bool outputQcBusy: false
            property bool hasActiveTimeline: true
            property var outputQcResult: ({success: true, decodeOk: true,
                message: "Review detected spans", videoStreamsScanned: 1, audioStreamsScanned: 2,
                signalScanComplete: false, signalScanWarning: "An additional stream could not be scanned.",
                streams: [{index: 0, type: "video", codec: "h264", width: 1920, height: 1080},
                    {index: 1, type: "audio", codec: "aac", sampleRate: 48000, channels: 2},
                    {index: 2, type: "audio", codec: "aac", sampleRate: 48000, channels: 2}],
                silenceSegments: [{streamLabel: "Audio stream 3", startSeconds: 0, endSeconds: 2}]})
        }
    }
    Component.onCompleted: shortcuts.open()
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/shortcut_ui_host.qml")));
  std::unique_ptr<QObject> host(component.create());
  if (!host) {
    std::cerr << component.errorString().toStdString();
    return fail("Shortcut dialog and delivery panel should instantiate");
  }
  auto* dialog = host->findChild<QObject*>(QStringLiteral("testedShortcuts"));
  auto* delivery = host->findChild<QObject*>(QStringLiteral("testedDelivery"));
  if (!dialog || !delivery) return fail("Both production panels should exist");
  QTest::qWait(100);
  if (dialog->property("activeShortcutPreset").toString() != QStringLiteral("app-native"))
    return fail("A clean profile should identify the native preset");
  for (const auto* preset : {"app-native", "lightroom", "photoshop", "capture-one"}) {
    QVariant conflicts;
    if (!QMetaObject::invokeMethod(dialog, "shortcutPresetWouldConflict",
        Q_RETURN_ARG(QVariant, conflicts), Q_ARG(QVariant, QString::fromLatin1(preset))) ||
        conflicts.toBool())
      return fail("Each offered preset must be compatible with fixed commands on a clean profile");
  }
  const auto before = controller.shortcutOverrides();
  if (set_sequence(dialog, QStringLiteral("Cmd+S")) ||
      !dialog->property("shortcutEditorStatus").toString().contains(QStringLiteral("Save")) ||
      controller.shortcutOverrides() != before)
    return fail("Dialog must name Save and preserve settings when an alias collides");
  if (set_sequence(dialog, QStringLiteral("Ctrl+Q, C")) ||
      !dialog->property("shortcutEditorStatus").toString().contains(QStringLiteral("Quit")) ||
      controller.shortcutOverrides() != before)
    return fail("Dialog must reject a chord whose prefix invokes Quit");
  if (set_sequence(dialog, QStringLiteral("Ctrl+NotAKey")) ||
      !dialog->property("shortcutEditorStatus").toString().contains(QStringLiteral("valid shortcut")))
    return fail("Invalid key text must produce actionable feedback");
  if (!set_sequence(dialog, QStringLiteral("Ctrl+Alt+9")) ||
      controller.shortcutSequence(QStringLiteral("timeline-razor"), {}) != QStringLiteral("Ctrl+Alt+9"))
    return fail("A unique mapping should persist through the production dialog");
  if (!dialog->property("activeShortcutPreset").toString().isEmpty())
    return fail("A custom keymap must not claim a preset is active");
  photo_editor::desktop::AppShellController reopened;
  if (reopened.shortcutSequence(QStringLiteral("timeline-razor"), {}) != QStringLiteral("Ctrl+Alt+9"))
    return fail("Dialog keymap should survive controller recreation");

  // Preserve the actual conflict UI in an optional review artifact.
  set_sequence(dialog, QStringLiteral("Ctrl+S"));
  QTest::qWait(50);
  auto* status = dialog->findChild<QQuickItem*>(QStringLiteral("keyboardShortcutStatus"));
  if (!status || !status->isVisible() || status->height() <= 0 ||
      status->mapToScene(QPointF()).y() < 0 ||
      status->mapToScene(QPointF(0, status->height())).y() > 900 ||
      !status->property("text").toString().contains(QStringLiteral("Save")))
    return fail("Conflict feedback must remain visible above the scrolling shortcut list");
  const auto evidence = qEnvironmentVariable("LOOKSY_TIMELINE_UI_EVIDENCE_DIR");
  auto* window = qobject_cast<QQuickWindow*>(host.get());
  if (!evidence.isEmpty() && window) {
    QDir().mkpath(evidence);
    QTest::qWait(150);
    if (!window->grabWindow().save(QDir(evidence).filePath(QStringLiteral("shortcut-fixed-conflict.png"))))
      return fail("Conflict screenshot should be captured");
  }
  QMetaObject::invokeMethod(dialog, "close");
  delivery->setProperty("visible", true);
  QTest::qWait(150);
  auto* coverage = delivery->findChild<QObject*>(QStringLiteral("timelineQcSignalScanCoverage"));
  if (!coverage || !coverage->property("visible").toBool() ||
      !coverage->property("text").toString().contains(QStringLiteral("1 video · 2 audio streams · incomplete")))
    return fail("QC UI must expose stream coverage and an incomplete scan");
  QVariant spans;
  QVariant stream_label;
  const QVariantMap stream_input{{QStringLiteral("index"), 2},
                                 {QStringLiteral("type"), QStringLiteral("audio")}};
  if (!QMetaObject::invokeMethod(delivery, "streamText", Q_RETURN_ARG(QVariant, stream_label),
      Q_ARG(QVariant, stream_input)) ||
      !stream_label.toString().startsWith(QStringLiteral("Audio stream 3 ·")))
    return fail("QC stream details must use the same stream numbering as their findings");
  const QVariantList input{QVariantMap{{QStringLiteral("streamLabel"), QStringLiteral("Audio stream 3")},
      {QStringLiteral("startSeconds"), 0.0}, {QStringLiteral("endSeconds"), 2.0}}};
  if (!QMetaObject::invokeMethod(delivery, "spansText", Q_RETURN_ARG(QVariant, spans),
      Q_ARG(QVariant, QStringLiteral("Audio silence")), Q_ARG(QVariant, input)) ||
      !spans.toString().contains(QStringLiteral("Audio stream 3 00:00.000–00:02.000")))
    return fail("QC findings must identify which stream contains silence");
  if (!evidence.isEmpty() && window &&
      !window->grabWindow().save(QDir(evidence).filePath(QStringLiteral("qc-multistream-coverage.png"))))
    return fail("QC screenshot should be captured");
  std::cout << "Production shortcut conflict feedback, persistence, and QC stream labels passed\n";
  return 0;
}
