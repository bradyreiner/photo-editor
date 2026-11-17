#include "depth_motion_controller.h"
#include "depth_point_cloud_viewport.h"
#include "scene_library_controller.h"
#include "scene_video_recorder.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTest>
#include <QSet>
#include <functional>
#include <cmath>
#include <iostream>
#include <memory>

using photo_editor::desktop::DepthMotionController;
using photo_editor::desktop::DepthPointCloudViewport;
using photo_editor::desktop::SceneLibraryController;
namespace {
void settle(int milliseconds = 250) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}
QQuickItem* find(QQuickItem* root, const QString& name) {
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems()) if (auto* result = find(child, name)) return result;
  return nullptr;
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool await(const std::function<bool()>& predicate, int timeout = 30000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) settle(20);
  return predicate();
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir home;
  const auto live_review = qEnvironmentVariable("DEPTH_MOTION_LIVE_REVIEW_PROJECT");
  if (live_review.isEmpty()) {
    qputenv("PHOTO_EDITOR_DEPTH_MOTION_HOME", home.path().toUtf8());
    qputenv("PHOTO_EDITOR_SCENE_DOCUMENTS_PATH", home.filePath("scene-library").toUtf8());
  }
  const auto python = qEnvironmentVariable("DEPTH_MOTION_TEST_PYTHON");
  if (!python.isEmpty()) qputenv("PHOTO_EDITOR_DEPTH_MOTION_PYTHON", python.toUtf8());
  qmlRegisterType<DepthMotionController>("PhotoEditor", 1, 0, "DepthMotionController");
  qmlRegisterType<DepthPointCloudViewport>("PhotoEditor", 1, 0, "DepthPointCloudViewport");
  qmlRegisterType<photo_editor::desktop::SceneVideoRecorder>("PhotoEditor", 1, 0, "SceneVideoRecorder");
  qmlRegisterType<SceneLibraryController>("PhotoEditor", 1, 0, "SceneLibraryController");
  DepthMotionController controller;
  if (controller.generate("/missing/source.png", {}, {}) || controller.snapshot().value("error").toString().isEmpty())
    return fail("Missing sources must fail before starting a process");
  if (QFileInfo::exists(home.filePath("projects"))) return fail("Invalid input must not create project output");
  if (controller.openProject("/missing/project.json").value("ok").toBool()) return fail("Missing project must fail");
  controller.cancel();
  const auto unusual_source = home.filePath(QString::fromUtf8("source Népal #100% ? clip.mp4"));
  if (!controller.localMediaUrl(unusual_source).isLocalFile() ||
      controller.localMediaUrl(unusual_source).toLocalFile() != unusual_source ||
      controller.localMediaUrl(QUrl::fromLocalFile(unusual_source).toString()).toLocalFile() != unusual_source ||
      !controller.localMediaUrl({}).isEmpty())
    return fail("Source paths and encoded file URLs must preserve local filenames at the media boundary");
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import "file:)QML" PHOTO_EDITOR_SOURCE_DIR R"QML(/apps/desktop/qml/components"
ApplicationWindow {
    width: 1180; height: 900; visible: true; color: "#121212"
    QtObject {
        id: library; objectName: "recordingCatalogFixture"
        property bool busy: false
        property string projectFilter: "Nepal 2026"
        property string errorMessage: ""
        property int calls: 0
        property string importedProject: ""
        property var files: []
        property bool complete: false
        function parentFolderUrl(path) { return "file:///recordings" }
        function localPathForUrl(path) { return "/recordings" }
        function assetIdForFilePath(path) { return complete && files.indexOf(path) >= 0 ? "asset-" + files.indexOf(path) : "" }
        function importSelectionWithOptions(folder, paths, project, mode) {
            calls++; files = paths; importedProject = project; busy = true
        }
    }
    DepthMotionWorkspace {
        id: dialog; anchors.fill: parent; catalog: library
        onLibraryScenesRequested: { dialog.visible = false; sceneLibrary.visible = true }
    }
    SavedScenesLibrary {
        id: sceneLibrary; anchors.fill: parent; anchors.margins: 16; visible: false
        onOpenRequested: scene => {
            const result = dialog.openProject(scene.projectPath)
            if (result.ok) { sceneLibrary.visible = false; dialog.visible = true }
        }
        onCreateRequested: { sceneLibrary.visible = false; dialog.visible = true }
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/depth-motion-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (!window) {
    for (const auto& error : component.errors()) std::cerr << error.toString().toStdString() << '\n';
    return fail("Production DepthMotionDialog must load");
  }
  window->requestActivate();
  settle(600);
  if (!live_review.isEmpty()) {
    auto* native = fixture->findChild<DepthMotionController*>("depthMotionController");
    auto* cloud = fixture->findChild<DepthPointCloudViewport*>("depthPointCloudViewport");
    window->setTitle(QStringLiteral("Looksy · 3D Motion"));
    if (!native || !native->openProject(live_review).value("ok").toBool()) return fail("Open the interactive review project");
    QObject::connect(&app, &QCoreApplication::aboutToQuit, native, [native, cloud] {
      if (cloud && cloud->status().value("ready").toBool()) native->savePointCloudView(cloud->viewState());
    });
    return app.exec();
  }
  std::cout << "Platform: " << QGuiApplication::platformName().toStdString()
            << "; active window: " << window->isActive() << '\n';
  if (qEnvironmentVariableIsSet("DEPTH_MOTION_REQUIRE_ACTIVE") && !window->isActive())
    return fail("Native qualification requires an active foreground window");
  const auto evidence = qEnvironmentVariable("DEPTH_MOTION_EVIDENCE_DIR");
  if (!evidence.isEmpty()) QDir().mkpath(evidence);
  for (const auto& size : {QSize(1180, 900), QSize(680, 820), QSize(420, 740)}) {
    window->resize(size);
    settle(300);
    auto* choose = find(window->contentItem(), "depthMotionChooseSource");
    auto* play = find(window->contentItem(), "depthMotionPlay");
    auto* settings = find(window->contentItem(), "depthMotionInspectorToggle");
    auto* stage = find(window->contentItem(), "depthMotionSceneStage");
    if (!settings || !stage) return fail("The Scene viewport and inspector toggle must remain discoverable");
    if (size.width() < 760) {
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, settings->mapToScene(settings->boundingRect().center()).toPoint()); settle();
    }
    auto* render = find(window->contentItem(), "depthMotionRender");
    auto* output = find(window->contentItem(), "depthMotionExportMovie");
    if (!render || !choose || !play || !output || !settings || !stage) {
      std::cerr << "Width " << size.width() << "; render " << bool(render) << "; choose " << bool(choose)
                << "; play " << bool(play) << "; export " << bool(output) << "; settings " << bool(settings) << "; stage " << bool(stage) << '\n';
      return fail("Primary actions must exist at every width");
    }
    if (!choose->isEnabled() || render->isEnabled() || play->isEnabled() || output->isEnabled())
      return fail("Initial action admission must reflect missing source/runtime/output");
    if (stage->height() < window->height() * .8 || stage->width() < window->width() * .7)
      return fail("The full-screen Scene viewport must own most of the workspace");
    for (auto* item : {render, choose, play, settings}) {
      const auto bounds = item->mapRectToScene(item->boundingRect());
      if (!item->isVisible() || bounds.left() < 0 || bounds.top() < 0
          || bounds.right() > window->width() || bounds.bottom() > window->height())
        return fail("Primary controls must remain visible inside the window");
    }
    if (!evidence.isEmpty()) window->grabWindow().save(QDir(evidence).filePath(QStringLiteral("depth-motion-%1.png").arg(size.width())));
    if (size.width() < 760) {
      auto* drawer = fixture->findChild<QObject*>("depthMotionSettingsDrawer");
      QMetaObject::invokeMethod(drawer, "close"); settle();
    }
  }
  if (!python.isEmpty()) {
    QImage source(256, 192, QImage::Format_RGB32), map(256, 192, QImage::Format_Grayscale16);
    for (int y = 0; y < source.height(); ++y) {
      auto* depths = reinterpret_cast<quint16*>(map.scanLine(y));
      for (int x = 0; x < source.width(); ++x) {
        source.setPixel(x, y, qRgb(x, y, x / 2));
        depths[x] = static_cast<quint16>(x * 257);
      }
    }
    source.save(home.filePath("source.png"));
    map.save(home.filePath("map.png"));
    controller.setProperty("editingAllowed", false);
    if (controller.generate(home.filePath("source.png"), home.filePath("map.png"), {}))
      return fail("Read-only access must reject depth generation before any work starts");
    window->resize(1180, 900);
    auto* dialog = fixture->findChild<QObject*>("depthMotionWorkspace");
    auto* native = fixture->findChild<DepthMotionController*>("depthMotionController");
    if (!dialog || !native) return fail("Production dialog and controller must remain discoverable");
    QProcess source_encoder;
    source_encoder.start("/opt/homebrew/bin/ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=256x192:rate=24",
        "-t", "3", "-c:v", "libx264", home.filePath("trim source #100%.mp4")});
    if (!source_encoder.waitForFinished(5000) || source_encoder.exitCode() != 0) return fail("Create the real pre-depth source preview fixture");
    dialog->setProperty("sourcePath", QUrl::fromLocalFile(home.filePath("trim source #100%.mp4")).toString());
    if (!await([&] { return dialog->property("sourceSeconds").toDouble() > 2.9; }, 10000)) {
      auto* source_player = fixture->findChild<QObject*>("depthMotionSourcePlayer");
      std::cerr << QJsonDocument::fromVariant(QVariantMap{{"isVideo", dialog->property("sourceIsVideo")},
          {"source", source_player->property("source")}, {"duration", source_player->property("duration")},
          {"error", source_player->property("errorString")}, {"status", source_player->property("mediaStatus")}}).toJson().toStdString();
      return fail("The source duration must be available before any depth processing");
    }
    if (!QMetaObject::invokeMethod(dialog, "setTrim", Q_ARG(double, 1.25), Q_ARG(double, 1.75)) ||
        std::abs(dialog->property("trimIn").toDouble() - 1.25) > .001 || std::abs(dialog->property("trimOut").toDouble() - 1.75) > .001 ||
        !dialog->property("sourcePreviewActive").toBool() || native->snapshot().value("busy").toBool())
      return fail("Fractional In/Out trimming must preview the original source without starting inference");
    auto* source_player = fixture->findChild<QObject*>("depthMotionSourcePlayer");
    if (QGuiApplication::platformName() == "cocoa" &&
        !await([&] { return source_player->property("seekable").toBool() && source_player->property("position").toInt() >= 1200; }, 5000))
    { std::cerr << QJsonDocument::fromVariant(QVariantMap{{"position", source_player->property("position")}, {"seekable", source_player->property("seekable")}, {"duration", source_player->property("duration")}, {"status", source_player->property("mediaStatus")}, {"pendingSeek", dialog->property("sourceNeedsInitialSeek")}}).toJson().toStdString();
      return fail("The native source preview must seek to the chosen In point before depth processing"); }
    QVariant trim_settings;
    QMetaObject::invokeMethod(dialog, "renderSettings", Q_RETURN_ARG(QVariant, trim_settings));
    if (trim_settings.toMap().value("start").toDouble() != 1.25 || trim_settings.toMap().value("duration").toDouble() != .5)
      return fail("The processing request must use exactly the selected trim range");
    for (const auto& size : {QSize(1180, 900), QSize(680, 820), QSize(420, 740)}) {
      window->resize(size); settle(200);
      auto* range = find(window->contentItem(), "depthMotionTrimRange");
      if (!range || !range->isVisible() || !window->contentItem()->boundingRect().contains(range->mapRectToScene(range->boundingRect())))
        return fail("Pre-depth source trim must remain visible at all window widths");
      if (!evidence.isEmpty()) window->grabWindow().save(QDir(evidence).filePath(QStringLiteral("source-trim-%1.png").arg(size.width())));
    }
    window->resize(1180, 900);
    // Instantiate the actual workspace under a qrc base URL, as in the shipped
    // app. The usual filesystem fixture would hide bare-path URL regressions.
    QFile workspace_source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/DepthMotionWorkspace.qml"));
    if (!workspace_source.open(QIODevice::ReadOnly)) return fail("Read the production workspace for the resource-context regression");
    auto resource_qml = workspace_source.readAll();
    const auto components_url = QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components")).toString().toUtf8();
    resource_qml.replace("import PhotoEditor\n", "import PhotoEditor\nimport \"" + components_url + "\"\n");
    resource_qml.replace("\"SceneCameraNavigation.js\"", "\"" + components_url + "/SceneCameraNavigation.js\"");
    QQmlComponent resource_component(&engine);
    resource_component.setData(resource_qml, QUrl("qrc:/depth-source-regression/DepthMotionWorkspace.qml"));
    std::unique_ptr<QObject> resource_fixture(resource_component.create());
    auto* resource_item = qobject_cast<QQuickItem*>(resource_fixture.get());
    if (!resource_item) {
      for (const auto& error : resource_component.errors()) std::cerr << error.toString().toStdString() << '\n';
      return fail("The production workspace must instantiate with its packaged qrc URL context");
    }
    dialog->setProperty("visible", false);
    resource_item->setParentItem(window->contentItem()); resource_item->setSize(window->size());
    auto* resource_player = resource_fixture->findChild<QObject*>("depthMotionSourcePlayer");
    QVariantMap restored_recipe{{"source", home.filePath("trim source #100%.mp4")},
        {"settings", QVariantMap{{"start", 1.25}, {"duration", .5}}}};
    QMetaObject::invokeMethod(resource_fixture.get(), "applyRecipe", Q_ARG(QVariant, QVariant(restored_recipe)));
    resource_fixture->findChild<QObject*>("depthMotionPreviewMode")->setProperty("currentIndex", 2);
    if (!resource_player || !await([&] {
          const auto url = resource_player->property("source").toUrl();
          return url.isLocalFile() && url.toLocalFile() == restored_recipe.value("source").toString() &&
              (QGuiApplication::platformName() != "cocoa" || resource_player->property("duration").toInt() == 3000);
        }, 10000) || !resource_player->property("errorString").toString().isEmpty())
      return fail("Reopening a saved raw source path in the packaged resource context must play a local file, including spaces and percent signs");
    auto* restored_out = resource_fixture->findChild<QObject*>("depthMotionTrimOut");
    auto* restored_range = resource_fixture->findChild<QObject*>("depthMotionTrimRange");
    auto* restored_first = restored_range ? restored_range->property("first").value<QObject*>() : nullptr;
    auto* restored_second = restored_range ? restored_range->property("second").value<QObject*>() : nullptr;
    if (!restored_out || !restored_first || !restored_second || !await([&] {
          return restored_out->property("value").toInt() == 1750 &&
              std::abs(restored_first->property("value").toDouble() - 1.25) < .001 &&
              std::abs(restored_second->property("value").toDouble() - 1.75) < .001;
        }, 2000)) return fail("Restored In/Out controls must retain the saved range after asynchronous source loading");
    if (!evidence.isEmpty()) { settle(300); window->grabWindow().save(QDir(evidence).filePath("restored-source-resource-context.png")); }
    resource_fixture.reset(); dialog->setProperty("visible", true);
    dialog->setProperty("sourcePath", home.filePath("source.png"));
    dialog->setProperty("depthPath", home.filePath("map.png"));
    auto* duration = fixture->findChild<QObject*>("depthMotionDuration");
    if (!duration) return fail("Duration control must exist");
    duration->setProperty("value", 1000);
    settle(300);
    auto* render = find(window->contentItem(), "depthMotionRender");
    if (!render->isEnabled()) return fail("A source, depth map and runtime must admit rendering");
    const auto render_point = render->mapToScene(render->boundingRect().center()).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, render_point);
    if (!await([&] { return native->snapshot().value("busy").toBool(); }, 2000)) return fail("Pointer render action must start the local worker");
    auto* build_status = fixture->findChild<QObject*>("depthMotionStatusMessage");
    if (!build_status || build_status->property("text").toString() != native->snapshot().value("status").toString())
      return fail("The worker's active processing status must be shown while depth is building");
    if (!await([&] { return !native->snapshot().value("busy").toBool(); })) return fail("Native worker must finish within the fixture budget");
    if (!native->snapshot().value("error").toString().isEmpty()) {
      std::cerr << native->snapshot().value("error").toString().toStdString() << '\n';
      return fail("Native worker must produce a completed project");
    }
    const auto project = native->snapshot().value("project").toString();
    if (project.isEmpty() || dialog->property("settingsChanged").toBool()) return fail("Completed preview must correspond to current controls");
    auto* cloud = fixture->findChild<DepthPointCloudViewport*>("depthPointCloudViewport");
    auto* gestures = find(window->contentItem(), "depthPointCloudGestures");
    if (!cloud || !gestures || !await([&] { return cloud->status().value("frameSerial").toInt() > 0; }))
      return fail("A generated project must publish a real interactive XYZRGB frame");
    const auto first_view = cloud->viewState();
    cloud->setPlaying(true);
    const auto point = gestures->mapToScene(gestures->boundingRect().center()).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseMove(window, point + QPoint(80, 25));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point + QPoint(80, 25));
    if (!await([&] { return cloud->position() > .1; }) || !cloud->playing() ||
        cloud->viewState().value("rotation") == first_view.value("rotation"))
      return fail("A real pointer orbit changes 3D camera pose while playback continues");
    const auto serial = cloud->status().value("frameSerial").toULongLong();
    cloud->setZExaggeration(3);
    cloud->setPointSize(3);
    cloud->setAppearance({{"shape", 2}, {"density", .5}, {"brightness", 1.5}, {"colorMode", 1}});
    if (!await([&] { return cloud->status().value("frameSerial").toULongLong() > serial; }) || !cloud->playing())
      return fail("Live Z exaggeration must publish geometry without stopping playback");
    const auto press_camera_control = [&](const QString& name) {
      auto* control = find(window->contentItem(), name);
      if (!control || !control->isVisible()) return false;
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, control->mapToScene(control->boundingRect().center()).toPoint());
      settle(60); return true;
    };
    if (!press_camera_control("sceneCameraLookModeButton")) return fail("The shared Scene Look control must be visible");
    const auto before_look = cloud->viewState();
    QTest::keyClick(window, Qt::Key_Right);
    if (cloud->viewState().value("position") != before_look.value("position") ||
        cloud->viewState().value("rotation") == before_look.value("rotation") || !cloud->playing())
      return fail("Scene Look turns in place while point-cloud playback continues");
    const auto before_flight = cloud->viewState().value("position");
    QTest::keyClick(window, Qt::Key_W);
    if (cloud->viewState().value("position") == before_flight) return fail("Scene WASD navigation must move the point-cloud camera");
    const auto before_roll = cloud->viewState();
    QTest::keyClick(window, Qt::Key_Right, Qt::AltModifier);
    if (cloud->viewState().value("position") != before_roll.value("position") ||
        cloud->viewState().value("rotation") == before_roll.value("rotation"))
      return fail("Alt-arrow roll must preserve camera position and permit an unlocked horizon");
    const auto point_look = cloud->appearance();
    if (!press_camera_control("sceneCameraResetViewButton") || cloud->appearance() != point_look || cloud->zExaggeration() != 3)
      return fail("The shared Reset camera control must preserve point appearance and Z exaggeration");
    cloud->setPlaying(false);
    if (!await([&] { return !cloud->status().value("busy").toBool(); })) return fail("Point-cloud navigation must settle");
    const auto before_target = cloud->viewState();
    if (!press_camera_control("sceneCameraOrbitTargetButton")) return fail("The shared target picker must be visible");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    if (dialog->property("orbitPickArmed").toBool() || !dialog->property("actionError").toString().isEmpty() ||
        cloud->viewState().value("position") != before_target.value("position") ||
        cloud->viewState().value("rotation") != before_target.value("rotation"))
      return fail("Picking a visible orbit target must keep the current view unchanged");
    const auto saved_view = cloud->viewState();
    if (!native->savePointCloudView(saved_view)) return fail("Interactive camera and Z settings must save");
    auto* save_scene = find(window->contentItem(), "depthMotionSaveSceneButton");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, save_scene->mapToScene(save_scene->boundingRect().center()).toPoint()); settle();
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    for (char character : QByteArray("Cloud Camera")) QTest::keyClick(window, character);
    QTest::keyClick(window, Qt::Key_Return); settle();
    SceneLibraryController saved_library;
    if (saved_library.scenes().size() != 1 || saved_library.scenes()[0].toMap().value("name") != "Cloud Camera")
      return fail("The native Save scene action must create the named scene in Library");
    if (!await([&] { saved_library.refresh(); return !saved_library.scenes()[0].toMap().value("previewSource").toString().isEmpty(); }, 5000))
      return fail("Saving a scene must publish its viewport thumbnail for Library");
    auto* library_button = find(window->contentItem(), "depthMotionLibraryButton");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, library_button->mapToScene(library_button->boundingRect().center()).toPoint()); settle();
    const auto library_id = saved_library.scenes()[0].toMap().value("id").toString();
    auto* open_scene = find(window->contentItem(), "libraryOpenSceneButton" + library_id);
    auto* scene_grid = find(window->contentItem(), "savedScenesGrid");
    if (!open_scene || !open_scene->isVisible() || !scene_grid ||
        !scene_grid->mapRectToScene(scene_grid->boundingRect()).contains(open_scene->mapRectToScene(open_scene->boundingRect())))
      return fail("The saved scene Open action must fit visibly inside the real Library grid");
    if (!evidence.isEmpty()) window->grabWindow().save(QDir(evidence).filePath("scene-library.png"));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, open_scene->mapToScene(open_scene->boundingRect().center()).toPoint()); settle();
    if (!cloud->isVisible() || cloud->viewState() != saved_view)
      return fail("Opening a saved scene from Library must restore camera, appearance and playhead");
    cloud->resetView(); cloud->restoreView(saved_view);
    if (cloud->viewState() != saved_view) return fail("Interactive camera, point size and Z scale must restore exactly");
    cloud->setAppearance({{"colorMode", 2}, {"gradientNear", "#173b73"}, {"gradientMid", "#2cc7ce"},
        {"gradientFar", "#e7ffff"}, {"gradientPreset", "Glacier"}});
    const auto gradient_view = cloud->viewState(); cloud->resetView(); cloud->restoreView(gradient_view);
    if (cloud->viewState() != gradient_view) return fail("Custom gradient colors must persist with a scene view");
    // Simulate an older generated project that predates source-color layer frames.
    if (!QDir(QDir(project).filePath("source-video")).removeRecursively() ||
        !native->openProject(QDir(project).filePath("project.depthmotion.json")).value("ok").toBool() ||
        native->snapshot().value("recordingLayersAvailable").toBool())
      return fail("Prepare the old-project layer backfill fixture");
    auto* recorder = fixture->findChild<photo_editor::desktop::SceneVideoRecorder*>("sceneVideoRecorder");
    auto* record_button = find(window->contentItem(), "sceneCameraRecordButton");
    if (!recorder || !record_button || !await([&] { return record_button->isEnabled(); }, 5000)) {
      std::cerr << QJsonDocument::fromVariant(QVariantMap{{"settingsChanged", dialog->property("settingsChanged")},
          {"backend", native->snapshot()}, {"cloud", cloud->status()}, {"useCloudClock", dialog->property("useCloudClock")}}).toJson().toStdString();
      return fail("Camera recording must be visible and usable in the workstation");
    }
    // A gradient must be recorded even when the user is previewing source colors.
    cloud->setAppearance({{"colorMode", 0}, {"gradientNear", "#ff0080"}, {"gradientMid", "#ff0080"},
        {"gradientFar", "#ff0080"}, {"gradientPreset", "Custom"}, {"brightness", 1}});
    auto* recording_palette = find(window->contentItem(), "depthPointCloudGradientPreset");
    if (!recording_palette || !recording_palette->isVisible())
      return fail("The recording gradient palette must be discoverable while Source color is previewed");
    cloud->setAppearance({{"gradientPreset", "Spectrum"}});
    recording_palette->setProperty("currentIndex", 6);
    if (!QMetaObject::invokeMethod(recording_palette, "activated", Q_ARG(int, 6)) ||
        cloud->appearance().value("gradientPreset") != "Custom")
      return fail("Custom recording gradient editing must work independently of the Source color preview");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, record_button->mapToScene(record_button->boundingRect().center()).toPoint());
    if (!await([&] { return recorder->recording(); }, 5000) || !native->snapshot().value("recordingLayersAvailable").toBool())
      return fail("Record must backfill older cached source layers and begin capturing automatically");
    settle(550);
    cloud->orbit(28, 8); cloud->setPlaying(true); settle(650);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, record_button->mapToScene(record_button->boundingRect().center()).toPoint());
    if (!await([&] { return !recorder->busy(); }, 15000) || recorder->outputUrl().isEmpty()) {
      std::cerr << QJsonDocument::fromVariant(QVariantMap{{"recording", recorder->recording()}, {"busy", recorder->busy()},
          {"seconds", recorder->seconds()}, {"output", recorder->outputUrl()}, {"error", recorder->error()},
          {"recorderStatus", recorder->status()}, {"buttonEnabled", record_button->isEnabled()}, {"buttonText", record_button->property("text")}}).toJson().toStdString();
      if (!evidence.isEmpty()) window->grabWindow().save(QDir(evidence).filePath("recording-failure.png"));
      return fail("Stop must finalize the camera recording as a project MP4");
    }
    const auto recording = recorder->outputUrl().toLocalFile();
    if (!recording.startsWith(QFileInfo(project).canonicalFilePath() + "/recordings/") || QFileInfo(recording).size() < 1000)
      return fail("Camera video must save inside its originating project, with actual encoded frames");
    QProcess probe;
    probe.start("/opt/homebrew/bin/ffprobe", {"-v", "error", "-show_streams", "-show_format", "-of", "json", recording});
    if (!probe.waitForFinished(5000) || probe.exitCode() != 0) return fail("A recorded camera MP4 must be decodable");
    const auto encoded = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    const auto video = encoded.value("streams").toArray().first().toObject();
    if (video.value("codec_name") != "h264" || video.value("nb_frames").toString().toInt() < 20 ||
        std::abs(encoded.value("format").toObject().value("duration").toString().toDouble() - recorder->seconds()) > 1.0 / 24 + .005)
      return fail("Camera recording must preserve wall-clock duration at 24 fps");
    const auto layer_files = recorder->outputFiles();
    if (layer_files.size() != 4) return fail("One camera recording must publish exactly four separate layer MP4s");
    QSet<QString> expected_layer_files{"point-cloud.mp4", "source-color-points.mp4", "depth.mp4", "source-color.mp4"};
    QSet<QString> actual_layer_files;
    for (const auto& file : layer_files) actual_layer_files.insert(QFileInfo(file).fileName());
    if (actual_layer_files != expected_layer_files) return fail("The four videos must include the selected look, source-color points, flat depth map, and source reference");
    QFile recording_metadata(QFileInfo(recording).dir().filePath("recording.json"));
    if (!recording_metadata.open(QIODevice::ReadOnly)) return fail("Layer recordings must preserve camera samples");
    const auto metadata = QJsonDocument::fromJson(recording_metadata.readAll()).object();
    const auto samples = metadata.value("samples").toArray();
    if (samples.size() < 2 || samples.first().toObject().value("camera") == samples.last().toObject().value("camera"))
      return fail("Layer metadata must contain the actual shared moving camera");
    QSet<QByteArray> layer_hashes;
    for (const auto& file : layer_files) {
      QFile layer(file);
      if (!layer.open(QIODevice::ReadOnly)) return fail("Read the recorded layer for independent content verification");
      layer_hashes.insert(QCryptographicHash::hash(layer.readAll(), QCryptographicHash::Sha256));
      if (!evidence.isEmpty()) {
        const auto exports = QDir(evidence).filePath("recording-exports"); QDir().mkpath(exports);
        QFile::copy(file, QDir(exports).filePath(QFileInfo(file).fileName()));
      }
      probe.start("/opt/homebrew/bin/ffprobe", {"-v", "error", "-show_streams", "-show_format", "-of", "json", file});
      if (!probe.waitForFinished(5000) || probe.exitCode() != 0) return fail("Every recorded layer must decode");
      const auto other = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
      const auto stream = other.value("streams").toArray().first().toObject();
      if (stream.value("nb_frames") != video.value("nb_frames") || stream.value("width") != video.value("width") ||
          stream.value("height") != video.value("height") || stream.value("r_frame_rate") != video.value("r_frame_rate") ||
          other.value("format").toObject().value("duration") != encoded.value("format").toObject().value("duration"))
        return fail("All layer MP4s must have identical frame count, dimensions, frame rate and duration");
    }
    if (layer_hashes.size() != 4) return fail("The four layer MP4s must contain distinct passes rather than duplicate viewport videos");
    if (!evidence.isEmpty()) QFile::copy(recording_metadata.fileName(), QDir(evidence).filePath("recording-exports/recording.json"));
    auto* recording_catalog = fixture->findChild<QObject*>("recordingCatalogFixture");
    auto* recording_controls = fixture->findChild<QObject*>("sceneRecordingControls");
    if (!recording_catalog || !recording_controls || recording_catalog->property("calls").toInt() != 1 ||
        recording_catalog->property("importedProject") != "Nepal 2026" || !recording_controls->property("importing").toBool())
      return fail("All four completed MP4 layers must enter the originating catalog project through one import");
    recording_catalog->setProperty("projectFilter", "Different project");
    recording_catalog->setProperty("complete", true); recording_catalog->setProperty("busy", false);
    if (!await([&] { return !recording_controls->property("importing").toBool(); }, 2000) ||
        recording_catalog->property("importedProject") != "Nepal 2026")
      return fail("Project changes must not redirect the completed recording's import");
    QProcess gradient_decoder;
    const auto gradient_frame_path = home.filePath("gradient-recording.png");
    gradient_decoder.start("/opt/homebrew/bin/ffmpeg", {"-v", "error", "-i", recording, "-frames:v", "1", gradient_frame_path});
    if (!gradient_decoder.waitForFinished(5000) || gradient_decoder.exitCode() != 0)
      return fail("Decode the gradient recording while Source color was selected");
    const QImage gradient_pixels(gradient_frame_path);
    double gradient_red = 0, gradient_green = 0, gradient_blue = 0, gradient_count = 0;
    for (int y = 0; y < gradient_pixels.height(); ++y) for (int x = 0; x < gradient_pixels.width(); ++x) {
      const auto pixel = gradient_pixels.pixelColor(x, y);
      if (pixel.red() > 80) { gradient_red += pixel.red(); gradient_green += pixel.green(); gradient_blue += pixel.blue(); ++gradient_count; }
    }
    if (gradient_count < 100 || gradient_red < gradient_green + gradient_count * 30 || gradient_blue < gradient_green + gradient_count * 10)
      return fail("The gradient MP4 must use the saved palette even when preview colorMode is Source color");
    if (!evidence.isEmpty()) QFile::copy(gradient_frame_path, QDir(evidence).filePath("gradient-recording.png"));
    QProcess depth_decoder;
    const auto depth_movie = QFileInfo(recording).dir().filePath("depth.mp4");
    const auto flat_depth_frame = home.filePath("flat-depth-recording.png");
    depth_decoder.start("/opt/homebrew/bin/ffmpeg", {"-v", "error", "-i", depth_movie, "-frames:v", "1", flat_depth_frame});
    if (!depth_decoder.waitForFinished(5000) || depth_decoder.exitCode() != 0)
      return fail("The depth-map MP4 must decode independently");
    const QImage flat_depth(flat_depth_frame);
    const auto left_depth = flat_depth.pixelColor(flat_depth.width() / 4, flat_depth.height() / 2);
    const auto right_depth = flat_depth.pixelColor(flat_depth.width() * 3 / 4, flat_depth.height() / 2);
    if (flat_depth.isNull() || std::abs(left_depth.red() - left_depth.green()) > 3 ||
        std::abs(right_depth.green() - right_depth.blue()) > 3 || right_depth.red() - left_depth.red() < 50)
      return fail("The third MP4 must contain the continuous source-aligned grayscale depth map, not projected points");
    if (!evidence.isEmpty()) QFile::copy(flat_depth_frame, QDir(evidence).filePath("flat-depth-recording.png"));
    QProcess decode;
    const auto decoded = home.filePath("recorded-%02d.png");
    decode.start("/opt/homebrew/bin/ffmpeg", {"-v", "error", "-i", recording, "-vf", "fps=2", "-frames:v", "2", decoded});
    if (!decode.waitForFinished(5000) || decode.exitCode() != 0 || QImage(home.filePath("recorded-01.png")) == QImage(home.filePath("recorded-02.png")))
      return fail("Recorded video pixels must change with the actual scene camera");
    if (!recorder->start(cloud, project)) return fail("A second camera recording must be admitted");
    settle(100); recorder->cancel();
    if (recorder->busy() || !QFileInfo(recording).isFile()) return fail("Cancel must preserve completed camera videos");
    auto* depth_preview_mode = fixture->findChild<QObject*>("depthMotionPreviewMode");
    depth_preview_mode->setProperty("currentIndex", 3); settle(150);
    if (!cloud->depthPreview() || !native->snapshot().value("depthVideoAvailable").toBool())
      return fail("Every completed motion scene must expose its cached depth video");
    cloud->setPosition(.2); cloud->setPlaying(true); settle(250);
    if (cloud->position() < .35 || !cloud->playing()) return fail("The Depth video view must play using the source clock");
    cloud->setPlaying(false);
    depth_preview_mode->setProperty("currentIndex", 4);
    if (!await([&] { return cloud->colorPass() == "source" && !cloud->status().value("busy").toBool(); }, 2000))
      return fail("Source-color points must be a discoverable live 3D view");
    cloud->setCaptureLayers(true);
    if (!await([&] { return cloud->recordingLayers().value("images").toMap().size() == 5; }, 2000))
      return fail("Recordable layers must remain available while source-color points are previewed");
    const auto source_preview_layers = cloud->recordingLayers().value("images").toMap();
    if (qvariant_cast<QImage>(source_preview_layers.value("point-cloud")) == qvariant_cast<QImage>(source_preview_layers.value("source-color-points")))
      return fail("Source-color preview must not replace the selected point-cloud look in the recording");
    cloud->setCaptureLayers(false);
    depth_preview_mode->setProperty("currentIndex", 5);
    if (!await([&] { return cloud->colorPass() == "depth" && cloud->recordingLayers().value("images").toMap().size() == 5; }, 2000))
      return fail("Depth-colored points must use the same live geometry and camera");
    if (!evidence.isEmpty()) window->grabWindow().save(QDir(evidence).filePath("depth-points-view.png"));
    depth_preview_mode->setProperty("currentIndex", 0); cloud->setPlaying(false); cloud->restoreView(saved_view); settle(200);
    if (!evidence.isEmpty()) { settle(); window->grabWindow().save(QDir(evidence).filePath("depth-pointcloud-orbit.png")); }
    if (!native->exportFile("cloud", home.filePath("export.ply"))
        || !native->exportFile("depth", home.filePath("export-depth.png"))
        || !native->exportFile("movie", home.filePath("export.mp4"))) return fail("All three native export paths must work");
    if (native->exportFile("movie", home.filePath("export.mp4"))) return fail("Export must preserve an existing destination");
    native->openProject(QDir(project).filePath("project.depthmotion.json"));
    DepthMotionController reopened;
    if (!reopened.openProject(QDir(project).filePath("project.depthmotion.json")).value("ok").toBool())
      return fail("A saved project must reopen through the native controller");
    const auto recipe = reopened.snapshot().value("recipe").toMap();
    auto settings = recipe.value("settings").toMap();
    settings.insert("motion", 0.04);
    if (!reopened.generate(recipe.value("source").toString(), recipe.value("depthMap").toString(), settings)
        || !await([&] { return !reopened.snapshot().value("busy").toBool(); })) return fail("Reopened projects must support camera rerendering");
    if (reopened.snapshot().value("recipe").toMap().value("cachePath") != recipe.value("cachePath"))
      return fail("Camera changes must reuse saved depth");
    const auto last_project = reopened.snapshot().value("project");
    settings.insert("duration", 12);
    if (!reopened.generate(recipe.value("source").toString(), recipe.value("depthMap").toString(), settings)
        || !await([&] { return reopened.snapshot().value("progress").toDouble() >= 0.6; }))
      return fail("A cancellation fixture must reach the rendering stage");
    reopened.cancel();
    if (!await([&] { return !reopened.snapshot().value("busy").toBool(); })
        || reopened.snapshot().value("project") != last_project
        || !reopened.snapshot().value("error").toString().isEmpty())
      return fail("Cancel must stop the worker and preserve the last completed project");
    auto* play = find(window->contentItem(), "depthMotionPlay");
    auto* preview_mode = find(window->contentItem(), "depthMotionPreviewMode");
    preview_mode->setProperty("currentIndex", 1);
    settle(500);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, play->mapToScene(play->boundingRect().center()).toPoint());
    auto* player = fixture->findChild<QObject*>("depthMotionPlayer");
    if (!player || !await([&] { return player->property("position").toInt() > 0; }, 5000)) {
      if (player) std::cerr << QJsonDocument::fromVariant(QVariantMap{
          {"source", player->property("source")}, {"state", player->property("playbackState")},
          {"status", player->property("mediaStatus")}, {"duration", player->property("duration")},
          {"error", player->property("errorString")}, {"playEnabled", play->isEnabled()}}).toJson().toStdString();
      return fail("Rendered MP4 must actually advance in the native preview");
    }
    settle(200);
    if (!evidence.isEmpty()) window->grabWindow().save(QDir(evidence).filePath("depth-motion-rendered.png"));
    // Changing the live palette must invalidate the old movie, then render the
    // chosen stops through the production button without estimating depth again.
    preview_mode->setProperty("currentIndex", 0); cloud->setPlaying(false);
    cloud->setAppearance({{"colorMode", 2}, {"gradientNear", "#ff0080"}, {"gradientMid", "#ff0080"},
        {"gradientFar", "#ff0080"}, {"gradientPreset", "Custom"}, {"brightness", 1}});
    auto* palette_export = find(window->contentItem(), "depthMotionExportMovie");
    if (!dialog->property("renderColorsChanged").toBool() || !palette_export || palette_export->isEnabled())
      return fail("A changed point-cloud palette must not silently export the previous source-colored movie");
    const auto palette_before = native->snapshot().value("recipe").toMap();
    const auto palette_cache = palette_before.value("cachePath");
    QFile palette_cache_file(QDir(palette_cache.toString()).filePath("manifest.json"));
    QVariantMap palette_cache_manifest;
    if (palette_cache_file.open(QIODevice::ReadOnly)) palette_cache_manifest = QJsonDocument::fromJson(palette_cache_file.readAll()).object().toVariantMap();
    auto* palette_render = find(window->contentItem(), "depthMotionRender");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, palette_render->mapToScene(palette_render->boundingRect().center()).toPoint());
    if (!await([&] { return native->snapshot().value("busy").toBool(); }, 2000) ||
        !await([&] { return !native->snapshot().value("busy").toBool(); }, 30000) ||
        !native->snapshot().value("error").toString().isEmpty()) return fail("Render the selected point-cloud color stops through the actual action");
    const auto palette_recipe = native->snapshot().value("recipe").toMap();
    if (palette_recipe.value("cachePath") != palette_cache || !await([&] {
          return !dialog->property("renderColorsChanged").toBool() && palette_export->isEnabled() &&
              cloud->appearance().value("gradientMid") == "#ff0080";
        }, 2000) || palette_recipe.value("settings").toMap().value("pointAppearance").toMap().value("gradientPreset") != "Custom") {
      std::cerr << QJsonDocument::fromVariant(QVariantMap{{"oldCache", palette_cache}, {"newCache", palette_recipe.value("cachePath")},
          {"oldIdentity", palette_before.value("identity")}, {"newIdentity", palette_recipe.value("identity")},
          {"cacheManifest", palette_cache_manifest}, {"cachedFrameExists", QFileInfo(QDir(palette_cache.toString()).filePath("000000.npz")).isFile()},
          {"colorsChanged", dialog->property("renderColorsChanged")}, {"settingsChanged", dialog->property("settingsChanged")},
          {"exportEnabled", palette_export->isEnabled()}, {"liveAppearance", cloud->appearance()},
          {"renderedSettings", palette_recipe.value("settings")}}).toJson().toStdString();
      return fail("A palette render must retain the chosen live look, become exportable, and reuse saved depth");
    }
    const auto palette_movie = home.filePath("palette-export.mp4"), palette_frame = home.filePath("palette-frame.png");
    if (!native->exportFile("movie", palette_movie)) return fail("Export the selected palette MP4");
    decode.start("/opt/homebrew/bin/ffmpeg", {"-v", "error", "-i", palette_movie, "-frames:v", "1", palette_frame});
    if (!decode.waitForFinished(5000) || decode.exitCode() != 0) return fail("Decode the palette MP4 for independent pixel checks");
    const QImage palette_pixels(palette_frame);
    double red_sum = 0, green_sum = 0, blue_sum = 0, colored_pixels = 0;
    for (int y = 0; y < palette_pixels.height(); ++y) for (int x = 0; x < palette_pixels.width(); ++x) {
      const auto color = palette_pixels.pixelColor(x, y);
      if (color.red() > 80) { red_sum += color.red(); green_sum += color.green(); blue_sum += color.blue(); ++colored_pixels; }
    }
    if (colored_pixels < 100 || red_sum / colored_pixels < green_sum / colored_pixels + 30 || blue_sum / colored_pixels < green_sum / colored_pixels + 10)
      return fail("The encoded MP4 must contain the chosen pink point colors, rather than the original image colors");
    DepthMotionController palette_reopened;
    if (!palette_reopened.openProject(QDir(native->snapshot().value("project").toString()).filePath("project.depthmotion.json")).value("ok").toBool() ||
        palette_reopened.snapshot().value("recipe").toMap().value("settings").toMap().value("pointAppearance").toMap().value("gradientMid") != "#ff0080")
      return fail("The rendered palette must survive saving and reopening");
    if (!evidence.isEmpty()) {
      QFile::copy(palette_movie, QDir(evidence).filePath("palette-export.mp4"));
      QFile::copy(palette_frame, QDir(evidence).filePath("palette-frame.png"));
    }
    const auto review = qEnvironmentVariable("DEPTH_MOTION_REVIEW_PROJECT");
    if (!review.isEmpty()) {
      if (!native->openProject(review).value("ok").toBool()) return fail("The real-photo project must reopen in the production dialog");
      if (!await([&] { return !native->snapshot().value("busy").toBool() && cloud->status().value("ready").toBool(); }, 120000))
        return fail("The real-video cached depth must become an interactive cloud");
      if (dialog->property("sourcePath").toString() != native->snapshot().value("recipe").toMap().value("source").toString())
        return fail("Opening a project directly must restore the source/build controls from its recipe");
      preview_mode->setProperty("currentIndex", 0); settle(500);
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, play->mapToScene(play->boundingRect().center()).toPoint());
      if (!await([&] { return cloud->status().value("frameIndex").toInt() > 3; }, 5000)) return fail("Real-video point geometry must advance during playback");
      cloud->orbit(50, 15); cloud->setZExaggeration(2.5); settle(500);
      if (!cloud->playing() || cloud->status().value("error").toString().size()) return fail("Real-video playback must survive live camera and Z changes");
      const auto first_serial = cloud->status().value("frameSerial").toULongLong();
      QSet<int> source_frames;
      const auto sampled_frames = QObject::connect(cloud, &DepthPointCloudViewport::statusChanged, cloud,
          [&source_frames, cloud] { source_frames.insert(cloud->status().value("frameIndex").toInt()); });
      QTimer continuous_orbit;
      continuous_orbit.setInterval(33);
      QObject::connect(&continuous_orbit, &QTimer::timeout, cloud, [cloud] { cloud->orbit(.5, .08); });
      continuous_orbit.start();
      QElapsedTimer playback_sample; playback_sample.start(); settle(2000); continuous_orbit.stop();
      const auto published = cloud->status().value("frameSerial").toULongLong() - first_serial;
      const double rate = double(published) * 1000 / double(playback_sample.elapsed());
      QObject::disconnect(sampled_frames);
      std::cout << "Live point-cloud publications per second: " << rate << "; points: "
                << cloud->status().value("pointCount").toInt() << "; render ms: " << cloud->status().value("renderMilliseconds").toInt() << '\n';
      std::cout << "Distinct source frames published during the 2-second orbit: " << source_frames.size() << '\n';
      if (rate < 12) return fail("The real-video point cloud must sustain interactive playback while orbiting");
      cloud->setPlaying(false); cloud->resetCamera(); cloud->setPointSize(2); cloud->setZExaggeration(2);
      cloud->setAppearance({{"shape", 1}, {"density", .65}, {"colorMode", 2}, {"gradientNear", "#173b73"},
          {"gradientMid", "#2cc7ce"}, {"gradientFar", "#e7ffff"}, {"gradientPreset", "Glacier"}});
      cloud->setPosition(0); cloud->setPlaying(true);
      if (!recorder->start(cloud, native->snapshot().value("project").toString())) return fail("The Nepal scene must admit a camera recording");
      QElapsedTimer take; take.start();
      while (take.elapsed() < 2000) { cloud->orbit(.15, .025); settle(40); }
      recorder->stop();
      if (!await([&] { return !recorder->busy(); }, 20000) || recorder->outputUrl().isEmpty())
        return fail("The actual Nepal video and live camera must finish as a project-local MP4");
      probe.start("/opt/homebrew/bin/ffprobe", {"-v", "error", "-show_format", "-of", "json", recorder->outputUrl().toLocalFile()});
      if (!probe.waitForFinished(5000) || probe.exitCode() != 0 ||
          std::abs(QJsonDocument::fromJson(probe.readAllStandardOutput()).object().value("format").toObject().value("duration").toString().toDouble() - recorder->seconds()) > 1.0 / 24 + .005)
        return fail("The real camera recording must preserve its elapsed duration within one video frame");
      if (!evidence.isEmpty()) {
        QFile result(QDir(evidence).filePath("nepal-recording.json"));
        if (!result.open(QIODevice::WriteOnly)) return fail("Write the Nepal camera recording receipt");
        result.write(QJsonDocument::fromVariant(QVariantMap{{"output", recorder->outputUrl()}, {"seconds", recorder->seconds()},
            {"status", recorder->status()}, {"project", native->snapshot().value("project")}}).toJson());
      }
      if (!native->snapshot().value("depthVideoAvailable").toBool()) {
        if (!native->prepareDepthVideo() || !await([&] { return !native->snapshot().value("busy").toBool(); }))
          return fail("An older saved Nepal scene must prepare its depth video without inference");
      }
      depth_preview_mode->setProperty("currentIndex", 3); cloud->setPosition(0); cloud->setPlaying(true);
      QSet<int> depth_frames;
      const auto sample_depth = QObject::connect(cloud, &DepthPointCloudViewport::statusChanged, [&] {
        depth_frames.insert(cloud->status().value("frameIndex").toInt());
      });
      settle(700); QObject::disconnect(sample_depth);
      if (!cloud->depthPreview() || depth_frames.size() < 8 || !cloud->playing())
        return fail("The Nepal depth video must publish distinct frames while playing");
      if (!evidence.isEmpty()) window->grabWindow().save(QDir(evidence).filePath("nepal-depth-video.png"));
      depth_preview_mode->setProperty("currentIndex", 0); cloud->setPlaying(false);
      if (!evidence.isEmpty()) {
        window->grabWindow().save(QDir(evidence).filePath("depth-pointcloud-real-video.png"));
        QFile metrics(QDir(evidence).filePath("playback.json"));
        if (!metrics.open(QIODevice::WriteOnly)) return fail("Write the native playback receipt");
        metrics.write(QJsonDocument::fromVariant(QVariantMap{{"publicationsPerSecond", rate}, {"status", cloud->status()},
            {"view", cloud->viewState()}, {"project", review}, {"distinctSourceFramesIn2Seconds", source_frames.size()}}).toJson());
      }
    }
    std::cout << "Native pointer render, playback, export, save/reopen, cached rerender and cancellation passed.\n";
  }
  std::cout << "Depth Motion controller admission and three responsive production UI layouts passed.\n";
  return 0;
}
