#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
#include <memory>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace {
void settle(int milliseconds = 250) {
  QElapsedTimer clock;
  clock.start();
  while (clock.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(10);
  }
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QQuickItem* find_item(QQuickItem* root, const QString& name) {
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems())
    if (auto* found = find_item(child, name)) return found;
  return nullptr;
}
}

namespace {
int run_stall_proof(QQmlEngine& engine, QQmlPropertyMap& settings) {
  const std::vector<std::pair<QString, QString>> cases = {
      {"character", "LooksyAgentCharacter { anchors.centerIn: parent; width: 118; height: 137; mode: \"scan\" }"},
      {"character-sort", "LooksyAgentCharacter { anchors.centerIn: parent; width: 118; height: 137; mode: \"sort\" }"},
      {"character-assemble", "LooksyAgentCharacter { anchors.centerIn: parent; width: 118; height: 137; mode: \"assemble\" }"},
      {"character-export", "LooksyAgentCharacter { anchors.centerIn: parent; width: 118; height: 137; mode: \"export\" }"},
      {"character-progress", "LooksyAgentCharacter { anchors.centerIn: parent; width: 118; height: 137; mode: \"scan\"; progress: 0.35 }"},
      {"character-walk", "LooksyAgentCharacter { anchors.centerIn: parent; width: 118; height: 137; mode: \"walk\" }"},
      {"glyph", "OpticalActivityIndicator { anchors.centerIn: parent; width: 24; height: 24 }"},
      {"progress", "ProgressStrip { anchors.centerIn: parent; width: 280; height: 4 }"},
      {"light-waves", "AdditiveLightWaves { anchors.centerIn: parent; width: 280; height: 64 }"},
      {"optical-light", "LooksyLight { anchors.centerIn: parent; width: 64; height: 64; mode: \"thinking\" }"},
      {"skeleton", "SkeletonBlock { anchors.centerIn: parent; width: 280; height: 64 }"},
      {"skeleton-delayed", "SkeletonBlock { anchors.centerIn: parent; width: 280; height: 64; phaseDelay: 420 }"},
      {"startup", "LooksyLoadingScene { anchors.centerIn: parent; width: 350; compact: true; scene: \"startup\" }"},
      {"library", "LooksyLoadingScene { anchors.centerIn: parent; width: 350; compact: true; scene: \"library\" }"},
      {"timeline", "LooksyLoadingScene { anchors.centerIn: parent; width: 350; compact: true; scene: \"timeline\" }"},
      {"import", "LooksyLoadingScene { anchors.centerIn: parent; width: 350; compact: true; scene: \"import\" }"},
      {"export", "LooksyLoadingScene { anchors.centerIn: parent; width: 350; compact: true; scene: \"export\" }"}};
  QJsonArray rows;
  bool passed = true;
  for (const auto& [name, body] : cases) {
    settings.insert(QStringLiteral("reducedMotion"), false);
    QQmlComponent component(&engine);
    const auto qml = QStringLiteral("import QtQuick\nimport QtQuick.Controls.Basic\nimport \"file:" PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components\"\nApplicationWindow { width: 420; height: 320; visible: true; color: \"#111719\"; %1 }").arg(body);
    component.setData(qml.toUtf8(), QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/animation-stall-fixture.qml")));
    std::unique_ptr<QObject> fixture(component.create());
    auto* window = qobject_cast<QQuickWindow*>(fixture.get());
    if (!window) {
      for (const auto& error : component.errors()) std::cerr << error.toString().toStdString() << '\n';
      return fail("Stall fixture must load the real production component");
    }
    settle(500);
    const auto first = window->grabWindow();
    std::mutex mutex;
    std::vector<double> frames;
    std::atomic<bool> observing{false};
    std::atomic<bool> render_thread{false};
    const auto origin = std::chrono::steady_clock::now();
    const auto connection = QObject::connect(window, &QQuickWindow::afterRendering, window, [&] {
      if (!observing.load()) return;
      render_thread.store(QThread::currentThread() != QCoreApplication::instance()->thread());
      const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - origin).count();
      std::lock_guard guard(mutex);
      frames.push_back(ms);
    }, Qt::DirectConnection);
    // Deliberately do not process GUI events during this interval. Production
    // scene graph presentation must carry on without a QML timer/JS repaint.
    observing.store(true);
    QThread::msleep(900);
    observing.store(false);
    std::vector<double> observed;
    { std::lock_guard guard(mutex); observed = frames; }
    settle(60);
    const bool pixels_changed = first != window->grabWindow();
    double largest_gap = observed.empty() ? 900.0 : observed.front();
    for (std::size_t i = 1; i < observed.size(); ++i)
      largest_gap = std::max(largest_gap, observed[i] - observed[i - 1]);
    if (!observed.empty()) largest_gap = std::max(largest_gap, 900.0 - observed.back());
    const bool smooth = render_thread.load() && observed.size() >= 20 && largest_gap < 100 && pixels_changed;
    settings.insert(QStringLiteral("reducedMotion"), true);
    settle(400);
    { std::lock_guard guard(mutex); frames.clear(); }
    observing.store(true);
    QThread::msleep(250);
    observing.store(false);
    std::size_t quiet_frames;
    { std::lock_guard guard(mutex); quiet_frames = frames.size(); }
    QObject::disconnect(connection);
    const bool quiet = quiet_frames <= 2;
    rows.append(QJsonObject{{"surface", name}, {"blockedGuiMs", 900},
                           {"renderedFrames", static_cast<int>(observed.size())},
                           {"largestFrameGapMs", largest_gap}, {"dedicatedRenderThread", render_thread.load()},
                           {"pixelsChanged", pixels_changed}, {"reducedMotionFrames", static_cast<int>(quiet_frames)},
                           {"passed", smooth && quiet}});
    passed = passed && smooth && quiet;
    std::cout << name.toStdString() << ": " << observed.size() << " frames during GUI stall, max gap "
              << largest_gap << " ms, reduced-motion frames " << quiet_frames << '\n';
  }
  const QJsonObject report{{"renderLoop", qEnvironmentVariable("QSG_RENDER_LOOP")},
                           {"surfaces", rows}, {"passed", passed}};
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_STALL_REPORT");
  if (!output.isEmpty()) {
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("Cannot write stall proof report");
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
  }
  if (!passed && !qEnvironmentVariableIsSet("PHOTO_EDITOR_ALLOW_STALL_BASELINE"))
    return fail("Every busy surface must keep rendering during a GUI stall and respect reduced motion");
  return 0;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QQmlEngine engine;
  QQmlPropertyMap settings;
  settings.insert(QStringLiteral("reducedMotion"), false);
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &settings);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_STALL_REPORT")) return run_stall_proof(engine, settings);
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    flags: Qt.Window | Qt.FramelessWindowHint
    width: 1000; height: 740; visible: true; color: "#14171c"
    property bool gallery: false
    property bool workspaceProof: false
    property bool recoveryProof: false
    property bool repairPending: false
    property int retryCount: 0
    property int backupCount: 0
    property int repairCount: 0
    property bool startupBusy: true
    property bool busy: true
    property bool broken: false
    property bool quiet: false
    property real percent: -1
    property string sceneKind: "library"
    property string sceneTitle: "Opening your library"
    property string sceneDetail: "Loading photos and saved edits."
    Rectangle {
        anchors.fill: parent
        color: "#111719"
        visible: fixture.gallery
        Text {
            x: 44; y: 38; color: "#e8ece9"; font.pixelSize: 25
            text: "Light, in motion."
        }
        Text {
            x: 44; y: 78; color: "#84979c"; font.pixelSize: 13
            text: "Caustic wavefronts · additive color · optical instruments"
        }
        Row {
            x: 42; y: 130; spacing: 24
            Repeater {
                model: ["idle", "scan", "sort", "assemble", "export", "success", "warning"]
                delegate: Column {
                    required property string modelData
                    width: 146; spacing: 14
                    LooksyAgentCharacter {
                        width: 142; height: 165
                        mode: parent.modelData
                        tool: "none"
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: parent.modelData
                        color: "#a5b5b9"; font.pixelSize: 12
                        font.family: "monospace"
                    }
                }
            }
        }
        Row {
            x: 56; y: 375; spacing: 90
            Repeater {
                model: ["startup", "library", "timeline"]
                delegate: LooksyLoadingScene {
                    required property string modelData
                    width: 290; compact: true
                    scene: modelData
                    title: modelData === "startup" ? "Opening your library" : modelData === "library" ? "Finding your photos" : "Preparing Timeline"
                    detail: "Preparing your workspace."
                }
            }
        }
        Row {
            x: 56; y: 645; spacing: 26
            OpticalActivityIndicator { width: 18; height: 18 }
            OpticalActivityIndicator { width: 24; height: 24 }
            OpticalActivityIndicator { width: 32; height: 32 }
            Text { text: "CIRCULAR INTERFERENCE / MEASURED PROGRESS"; color: "#81949a"; font.pixelSize: 11; font.family: "monospace" }
            ProgressStrip { width: 210; height: 3; progress: 0.35; anchors.verticalCenter: parent.verticalCenter }
        }
    }
    Text {
        visible: !fixture.gallery
        x: 24; y: 24; color: "#858f99"; font.family: "monospace"; font.pixelSize: 12
        text: "LOOKSY / " + (fixture.startupBusy ? "STARTUP" : fixture.sceneKind.toUpperCase())
    }
    LooksyLoadingScene {
        id: scene
        objectName: "sceneFixture"
        anchors.centerIn: parent
        width: Math.max(100, Math.min(600, parent.width - 48, (parent.height - 120) * 1.5))
        visible: !fixture.gallery && !fixture.workspaceProof && !fixture.recoveryProof
        scene: fixture.startupBusy ? "startup" : fixture.sceneKind
        title: fixture.sceneTitle
        detail: fixture.sceneDetail
        active: fixture.busy
        failed: fixture.broken
        reducedMotion: fixture.quiet
        progress: fixture.percent
    }
    AgentWorkspaceOverlay {
        objectName: "opticalEditorStatusFixture"
        anchors.fill: parent
        visible: fixture.workspaceProof
        applyInProgress: fixture.workspaceProof
        reducedMotion: fixture.quiet
    }
    LooksyAssistantDock {
        objectName: "opticalAgentEntryFixture"
        x: 24; y: 24
        visible: fixture.workspaceProof
    }

    RecoveryStatusPanel {
        objectName: "recoverySummarySurface"
        width: parent.width
        y: 64
        height: implicitHeight
        visible: fixture.recoveryProof
        catalogFailure: true
        repairPending: fixture.repairPending
        summary: "Your originals are unchanged. Retry to reconnect to your library."
        details: ["The catalog service is unavailable. No photos or saved edits have been discarded."]
        onRetryRequested: fixture.retryCount += 1
        onBackupRequested: fixture.backupCount += 1
        onRepairRequested: fixture.repairPending = true
        onConfirmRepairRequested: { fixture.repairCount += 1; fixture.repairPending = false }
        onCancelRepairRequested: fixture.repairPending = false
    }

})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/loading-scenes-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    for (const auto& error : component.errors()) std::cerr << error.toString().toStdString() << '\n';
    return fail("The production scene components must load");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* scene = find_item(window->contentItem(), QStringLiteral("sceneFixture"));
  auto* field = find_item(scene, QStringLiteral("loadingSceneLightField"));
  auto* robot = find_item(scene, QStringLiteral("loadingSceneRobot"));
  auto* strip = find_item(scene, QStringLiteral("loadingSceneProgress"));
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_LOADING_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  const auto capture = [&](const QString& name) {
    return output.isEmpty() || window->grabWindow().save(QDir(output).filePath(name + QStringLiteral(".png")));
  };
  settle(400);
  if (!scene->isVisible() || !field->property("motionEnabled").toBool() ||
      find_item(window->contentItem(), QStringLiteral("startupLoadingPopup")) || !capture(QStringLiteral("startup-scene")))
    return fail("Startup must present one in-workspace light scene without a second modal loader");
  fixture->setProperty("startupBusy", false);
  settle(350);

  if (!scene || robot || !field || !strip)
    return fail("Loading must render the optical field and progress without a character");
  const auto first_frame = window->grabWindow();
  settle(500);
  if (!field->property("motionEnabled").toBool() ||
      first_frame == window->grabWindow() || !capture(QStringLiteral("library-scene")))
    return fail("Real loading must move the caustic wavefronts in the native render");
  for (const auto& kind : {QStringLiteral("timeline"), QStringLiteral("startup"), QStringLiteral("import"), QStringLiteral("export")}) {
    fixture->setProperty("sceneKind", kind);
    fixture->setProperty("sceneTitle", kind == QStringLiteral("timeline") ? "Preparing Timeline" : "Opening editor");
    fixture->setProperty("sceneDetail", kind == QStringLiteral("timeline") ? "Opening tracks, preview, and editing tools." : "Preparing the workspace.");
    settle(350);
    if (!field->isVisible() || !field->property("motionEnabled").toBool() || !capture(kind + QStringLiteral("-workspace-scene")))
      return fail("Every workspace must render its optical loading field");
  }
  fixture->setProperty("sceneKind", "library");
  fixture->setProperty("sceneTitle", "Your library starts here");
  fixture->setProperty("sceneDetail", "Import your photos to begin.");
  fixture->setProperty("busy", false);
  settle(300);
  const auto idle_phase = scene->property("phase").toDouble();
  settle(180);
  if (scene->property("motionEnabled").toBool() || strip->isVisible() ||
      scene->property("phase").toDouble() != idle_phase || !capture(QStringLiteral("library-empty-scene")))
    return fail("An empty library must show the resting scene without pretending to load");

  fixture->setProperty("busy", true);
  fixture->setProperty("percent", 0.35);
  fixture->setProperty("sceneTitle", "Importing photos");
  fixture->setProperty("sceneDetail", "Copying selected files.");
  settle(250);
  if (qAbs(strip->property("boundedProgress").toDouble() - 0.35) > 0.001 ||
      strip->property("indeterminate").toBool())
    return fail("Measured task progress must remain exactly 35 percent");
  if (!capture(QStringLiteral("import-measured-progress"))) return fail("Progress capture failed");
  fixture->setProperty("quiet", true);
  settle(200);
  const auto quiet_phase = scene->property("phase").toDouble();
  settle(220);
  if (scene->property("motionEnabled").toBool() || field->property("motionEnabled").toBool() ||
      strip->property("animate").toBool() || scene->property("phase").toDouble() != quiet_phase)
    return fail("Reduced motion must stop caustic fields and progress motion");
  const auto static_frame = window->grabWindow();
  settle(250);
  if (static_frame != window->grabWindow()) return fail("Reduced-motion loading pixels must remain static");
  fixture->setProperty("quiet", false);
  fixture->setProperty("broken", true);
  settle(200);
  if (field->property("motionEnabled").toBool() || !capture(QStringLiteral("failed-scene")))
    return fail("Failure must stop optical activity");
  fixture->setProperty("broken", false);
  scene->setVisible(false);
  settle(100);
  const auto hidden_phase = scene->property("phase").toDouble();
  settle(200);
  if (scene->property("phase").toDouble() != hidden_phase)
    return fail("Hidden scenes must stop all scene activity");
  scene->setVisible(true);
  fixture->setProperty("percent", -1);
  for (const auto size : {QSize(360, 480), QSize(1000, 740)}) {
    window->resize(size);
    settle(250);
    const auto bounds = scene->mapRectToItem(window->contentItem(), scene->boundingRect());
    if (!window->contentItem()->boundingRect().contains(bounds) || !capture(QStringLiteral("scene-%1x%2").arg(size.width()).arg(size.height()))) {
      std::cerr << "scene bounds " << bounds.x() << ',' << bounds.y() << ' ' << bounds.width() << 'x' << bounds.height()
                << " window " << window->width() << 'x' << window->height()
                << " capture-null=" << window->grabWindow().isNull() << '\n';
      for (auto* column : scene->childItems()) {
        std::cerr << column->metaObject()->className() << " width=" << column->width()
                  << " height=" << column->height() << " implicit-height=" << column->implicitHeight() << '\n';
        for (auto* item : column->childItems())
          std::cerr << "  " << item->metaObject()->className() << " width=" << item->width()
                    << " height=" << item->height() << " implicit-height=" << item->implicitHeight() << '\n';
      }
      return fail("Scenes and readable labels must fit both narrow and full-size windows");
    }
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_CHARACTER_GALLERY")) {
    window->resize(QSize(1240, 740));
    fixture->setProperty("gallery", true);
    settle(500);
    if (!capture(QStringLiteral("optical-loading-and-agent-system")))
      return fail("Character system gallery capture failed");
    const auto recording = qEnvironmentVariable("PHOTO_EDITOR_CHARACTER_ANIMATION_DIR");
    if (!recording.isEmpty()) {
      QDir().mkpath(recording);
      for (int frame = 0; frame < 48; ++frame) {
        settle(65);
        if (!window->grabWindow().save(QDir(recording).filePath(
                QStringLiteral("frame-%1.png").arg(frame, 3, 10, QLatin1Char('0')))))
          return fail("Native character motion frame capture failed");
      }
    }
  }
  // The editor exposes useful task status while the character stays in Agent Studio.
  fixture->setProperty("gallery", false);
  fixture->setProperty("workspaceProof", true);
  settle(350);
  auto* editor_status = find_item(window->contentItem(), QStringLiteral("opticalEditorStatusFixture"));
  auto* editor_rig = find_item(editor_status, QStringLiteral("agentWorkspaceRoamingRobot"));
  if (!editor_status->isVisible() || !editor_rig || editor_rig->isVisible() ||
      find_item(editor_status, QStringLiteral("agentWorkspaceRoamingCharacter")) ||
      !find_item(editor_status, QStringLiteral("agentWorkspaceActivitySweep")) ||
      !capture(QStringLiteral("editor-optical-status")))
    return fail("The editor must expose optical task status without a roaming character");
  fixture->setProperty("workspaceProof", false);
  settle(150);
  fixture->setProperty("recoveryProof", true);
  settle(200);
  auto* recovery = find_item(window->contentItem(), QStringLiteral("recoverySummarySurface"));
  auto* retry = find_item(recovery, QStringLiteral("catalogRetryButton"));
  auto* backup = find_item(recovery, QStringLiteral("catalogBackupButton"));
  auto* repair = find_item(recovery, QStringLiteral("catalogRepairButton"));
  auto* details = find_item(recovery, QStringLiteral("recoveryDetailsButton"));
  if (!retry->isVisible() || backup->isVisible() || repair->isVisible() || recovery->height() > 60)
    return fail("Recovery must be a compact strip with maintenance actions initially collapsed");
  QMetaObject::invokeMethod(retry, "click");
  if (fixture->property("retryCount").toInt() != 1) return fail("Retry must reach its catalog action");
  for (const auto size : {QSize(360, 480), QSize(640, 480), QSize(1000, 740)}) {
    window->resize(size);
    settle(200);
    for (auto* button : {retry, details})
      if (!recovery->boundingRect().contains(button->mapRectToItem(recovery, button->boundingRect())))
        return fail("Retry and Details must remain inside the strip at narrow widths");
    if (!capture(QStringLiteral("recovery-compact-%1").arg(size.width()))) return fail("Recovery capture failed");
    QMetaObject::invokeMethod(details, "click");
    settle(200);
    if (!backup->isVisible() || !repair->isVisible() || recovery->height() <= 60)
      return fail("Details must reveal complete diagnostics and maintenance actions");
    for (auto* button : {backup, repair})
      if (!recovery->boundingRect().contains(button->mapRectToItem(recovery, button->boundingRect())))
        return fail("Maintenance controls must wrap inside the expanded recovery strip");
    if (!capture(QStringLiteral("recovery-details-%1").arg(size.width()))) return fail("Details capture failed");
    QMetaObject::invokeMethod(details, "click");
    settle(100);
  }
  QMetaObject::invokeMethod(details, "click");
  QMetaObject::invokeMethod(backup, "click");
  QMetaObject::invokeMethod(repair, "click");
  settle(200);
  auto* confirm = find_item(recovery, QStringLiteral("catalogRepairConfirmButton"));
  auto* cancel = find_item(recovery, QStringLiteral("catalogRepairCancelButton"));
  if (fixture->property("backupCount").toInt() != 1 || fixture->property("repairCount").toInt() != 0 ||
      !confirm->isVisible() || !cancel->isVisible())
    return fail("Requesting a repair must expose confirmation without performing it");
  QMetaObject::invokeMethod(cancel, "click");
  if (fixture->property("repairPending").toBool() || fixture->property("repairCount").toInt() != 0)
    return fail("Cancelling repair must preserve the catalog");
  QMetaObject::invokeMethod(repair, "click");
  settle(100);
  if (!capture(QStringLiteral("recovery-confirmation"))) return fail("Repair confirmation capture failed");
  QMetaObject::invokeMethod(confirm, "click");
  if (fixture->property("repairCount").toInt() != 1) return fail("Only explicit confirmation may execute repair");
  fixture->setProperty("recoveryProof", false);
  settle(100);
  const auto animation_dir = qEnvironmentVariable("PHOTO_EDITOR_LOADING_ANIMATION_DIR");
  if (!animation_dir.isEmpty()) {
    QDir().mkpath(animation_dir);
    QFile trace(QDir(animation_dir).filePath(QStringLiteral("frames.jsonl")));
    if (!trace.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("Could not write the native frame trace");
    QElapsedTimer clock;
    clock.start();
    for (int frame = 0; frame < 360; ++frame) {
      if (frame % 120 == 0) {
        const auto kind = frame < 120 ? QStringLiteral("startup") : frame < 240 ? QStringLiteral("library") : QStringLiteral("timeline");
        fixture->setProperty("sceneKind", kind);
        fixture->setProperty("sceneTitle", kind == "startup" ? "Opening your library" : kind == "library" ? "Organizing photos" : "Preparing Timeline");
        fixture->setProperty("sceneDetail", kind == "timeline" ? "Opening tracks, preview, and editing tools." : "Loading photos and saved edits.");
        scene->setProperty("phase", 0);
      }
      settle(60);
      window->grabWindow().save(QDir(animation_dir).filePath(QStringLiteral("frame-%1.jpg").arg(frame, 3, 10, QLatin1Char('0'))), "JPG", 85);
      trace.write(QJsonDocument(QJsonObject::fromVariantMap({
          {QStringLiteral("frame"), frame}, {QStringLiteral("elapsedMs"), clock.elapsed()},
          {QStringLiteral("scene"), fixture->property("sceneKind")},
          {QStringLiteral("walking"), scene->property("traveling")},
          {QStringLiteral("opticalMotion"), field->property("motionEnabled")}})).toJson(QJsonDocument::Compact) + '\n');
    }
  }
  std::cout << "Character-free optical loading, startup lifecycle, visible activity, empty state, exact progress, hidden/reduced motion and responsive layout passed.\n";
  return 0;
}
