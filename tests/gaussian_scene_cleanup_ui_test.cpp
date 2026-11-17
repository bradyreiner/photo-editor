#include <QQmlProperty>
#include "gaussian_scene_controller.h"
#include "gaussian_scene_viewport.h"
#include "scene_library_controller.h"
#include "scene_video_recorder.h"
#include "gaussian_saved_views.h"
#include "native_window_test_input.h"
#include <QGuiApplication>
#include <QSettings>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QDataStream>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
int wait_limit_ms = 20000;
bool wait_for(const std::function<bool()>& predicate) {
  QElapsedTimer elapsed; elapsed.start();
  while (!predicate() && elapsed.elapsed() < wait_limit_ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); }
  return predicate();
}
bool write(const QString& path, const QByteArray& bytes) {
  QFile file(path); return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
QQuickItem* visual_item(QQuickItem* parent, const QString& name) {
  if (!parent) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems()) if (auto* found = visual_item(child,name)) return found;
  return nullptr;
}
QVector<QQuickItem*> visual_items(QQuickItem* parent, const QString& name) {
  QVector<QQuickItem*> matches;
  if (!parent) return matches;
  if (parent->objectName() == name) matches.push_back(parent);
  for (auto* child : parent->childItems()) {
    const auto child_matches = visual_items(child,name);
    matches += child_matches;
  }
  return matches;
}
QByteArray synthetic_ply(bool depth_layers = false) {
  const QStringList properties{"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"};
  QByteArray bytes("ply\nformat binary_little_endian 1.0\nelement vertex 4\n");
  for (const auto& property : properties) bytes += "property float " + property.toUtf8() + '\n';
  bytes += "end_header\n";
  QByteArray payload; QDataStream stream(&payload, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian); stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  for (int row = 0; row < 4; ++row) for (const auto& property : properties)
    stream << (property == "x" ? (static_cast<float>(row) - 1.5F) * 0.7F : property == "z" ? (depth_layers ? 2.0F+static_cast<float>(row)*0.6F : 3.0F) :
        property == "rot_0" ? 1.0F : property.startsWith("scale_") ? -2.0F : property == "opacity" ? (row == 0 ? -5.0F : 3.0F) :
        property == QStringLiteral("f_dc_%1").arg(row % 3) ? 1.5F : 0.0F);
  return bytes + payload;
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName("LooksyCleanupTest"); QCoreApplication::setApplicationName("SceneCleanup");
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
  qmlRegisterType<GaussianSceneViewport>("PhotoEditor", 1, 0, "GaussianSceneViewport");
  qmlRegisterType<photo_editor::desktop::SceneVideoRecorder>("PhotoEditor", 1, 0, "SceneVideoRecorder");
  qmlRegisterType<SceneLibraryController>("PhotoEditor", 1, 0, "SceneLibraryController");
  QTemporaryDir temporary;
  qputenv("PHOTO_EDITOR_SCENE_DOCUMENTS_PATH", temporary.filePath("scene-library").toUtf8());
  const auto args = app.arguments();
  const bool background_allowed = args.contains("--allow-background");
  const auto option = [&](const QString& key) { const auto i = args.indexOf(key); return i < 0 ? QString() : args.value(i + 1); };
  if (args.contains("--wait-ms")) {
    bool valid = false; const auto value = option("--wait-ms").toInt(&valid);
    if (!valid || value < 20000 || value > 120000) return 2;
    wait_limit_ms = value;
  }
  const auto evidence = option("--proof-dir");
  if (!evidence.isEmpty() && (!QFileInfo(evidence).isAbsolute() || QFileInfo::exists(evidence) || !QDir().mkpath(evidence))) return 2;
  const auto root = evidence.isEmpty() ? temporary.path() : evidence;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QDir(root).filePath("preferences"));
  if (args.contains("--studio-camera") || args.contains("--navigation-controls-proof")) {
    QSettings legacy_layout(QSettings::IniFormat,QSettings::UserScope,"LooksyCleanupTest","SceneCleanup");
    legacy_layout.beginGroup(QStringLiteral("SceneWorkspace"));
    legacy_layout.setValue(QStringLiteral("studioLayoutVersion"),1);
    legacy_layout.setValue(QStringLiteral("outlinerVisible"),true);
    legacy_layout.setValue(QStringLiteral("shotsVisible"),true);
    legacy_layout.endGroup();
    legacy_layout.sync();
    if (legacy_layout.status()!=QSettings::NoError) return 2;
  }
  QString source = option("--ply");
  const auto project = QDir(root).filePath("project");
  GaussianSceneController controller;
  bool accepted = false;
  const auto source_project = option("--project");
  if (!source_project.isEmpty()) {
    QFile source_manifest(QDir(source_project).filePath("scene.json"));
    if (!source_manifest.open(QIODevice::ReadOnly)) return 2;
    const bool imported=QJsonDocument::fromJson(source_manifest.readAll()).object().value("importedArtifact").toBool();
    // Only copy the renderer inputs; never edit the user's existing project.
    for (const auto& relative : {"scene.json", "trained/scene.ply", "dataset/sparse/cameras.bin", "dataset/sparse/images.bin"}) {
      if (imported && QString::fromLatin1(relative).startsWith("dataset/")) continue;
      const auto target = QDir(project).filePath(QString::fromLatin1(relative));
      if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(QDir(source_project).filePath(QString::fromLatin1(relative)), target)) return 2;
    }
    if (args.contains("--photo-navigation")) for (const auto& relative:{"camera-views.json","scene-objects.json"}) {
      const auto from=QDir(source_project).filePath(QString::fromLatin1(relative));
      if (QFileInfo::exists(from) && !QFile::copy(from,QDir(project).filePath(QString::fromLatin1(relative)))) return 2;
    }
    accepted = controller.openProject(project).value("ok").toBool();
  } else {
    if (source.isEmpty()) { source = QDir(root).filePath("fixture.ply"); if (!write(source, synthetic_ply(args.contains("--depth-cleanup")))) return 2; }
    accepted = controller.importPly(source, project).value("ok").toBool();
  }
  if (!accepted || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state") != "trained") { std::cerr << "Import failed\n"; return 1; }
  QQmlEngine engine; engine.rootContext()->setContextProperty("cleanupController", &controller);
  QStringList qml_warnings;
  QObject::connect(&engine,&QQmlEngine::warnings,&app,[&](const QList<QQmlError>& errors) {
    for (const auto& error : errors) qml_warnings.append(error.toString());
  });
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/workspaces"
ApplicationWindow {
    width: 1440; height: 900; visible: true; title: "Looksy — Gaussian scene cleanup"
    SceneWorkspace { anchors.fill: parent; controller: cleanupController }
})qml", QUrl("file:///looksy-cleanup-proof.qml"));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) { std::cerr << component.errorString().toStdString(); return 1; }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* viewport = fixture->findChild<GaussianSceneViewport*>("gaussianSceneViewport");
  auto* workspace = fixture->findChild<QQuickItem*>("sceneWorkspace");
  const auto studio_geometry_valid = [&] {
    auto* stage=fixture->findChild<QQuickItem*>("sceneStudioStage");
    if (!workspace || !stage || !viewport || stage->width()<200 || stage->height()<150) return false;
    const auto stage_rect=stage->mapRectToScene(stage->boundingRect());
    const auto viewport_rect=viewport->mapRectToScene(viewport->boundingRect());
    if (stage_rect!=viewport_rect || !workspace->mapRectToScene(workspace->boundingRect()).contains(stage_rect)) return false;
    for (const char* name:{"sceneStudioOutliner","sceneStudioInspector","sceneStudioShots"}) {
      auto* panel=fixture->findChild<QQuickItem*>(name);
      if (!panel || (panel->isVisible() && stage_rect.intersects(panel->mapRectToScene(panel->boundingRect())))) return false;
    }
    return true;
  };
  auto* popup = fixture->findChild<QObject*>("sceneCleanupPopup");
  const auto fail = [&](const char* reason) {
    std::cerr << reason << '\n';
    for (const auto& warning : qml_warnings) std::cerr << warning.toStdString() << '\n';
    if (workspace) std::cerr << "Action error: " << workspace->property("actionError").toString().toStdString() << '\n';
    if (viewport) std::cerr << QJsonDocument::fromVariant(viewport->status()).toJson().constData();
    if (window && !evidence.isEmpty()) window->grabWindow().save(QDir(root).filePath("failure.png"));
    return 1;
  };
  if (!window || !viewport || !workspace || !popup || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; })) return fail("Scene did not render");
  const auto unavailable_foreground = [&] {
    std::cerr << "Foreground lighting latency qualification unavailable: macOS did not activate the exposed proof window.\n";
    if (!evidence.isEmpty()) write(QDir(root).filePath("foreground-unavailable.json"),QJsonDocument::fromVariant(QVariantMap{
        {"qualified",false},{"windowExposed",window->isExposed()},{"windowActive",window->isActive()},
        {"reason","macOS foreground activation unavailable; input latency gate not run"}}).toJson());
    return 77;
  };
  if ((args.contains("--progressive-lighting") || args.contains("--photo-navigation")) && !background_allowed) {
    activate_native_test_window(window);
    if (!wait_for([&] { return window->isActive() && window->isExposed(); })) return unavailable_foreground();
  };

  int swapped = 0;
  QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] { ++swapped; });
  const auto capture = [&](const QString& name) {
    const int before = swapped;
    if (!wait_for([&] { window->update(); return swapped >= before + 3 && popup->property("visible").toBool() == popup->property("opened").toBool(); })) {
      std::cerr << "Capture " << name.toStdString() << " did not settle: swaps=" << swapped-before << " popup visible=" << popup->property("visible").toBool() << " opened=" << popup->property("opened").toBool() << '\n';
      return false;
    }
    if (evidence.isEmpty()) return true;
    const auto image = window->grabWindow();
    const auto expected = QSize(qRound(window->width() * window->devicePixelRatio()), qRound(window->height() * window->devicePixelRatio()));
    if (image.size() != expected) std::cerr << "Capture " << name.toStdString() << " size " << image.width() << 'x' << image.height() << " expected " << expected.width() << 'x' << expected.height() << '\n';
    return image.size() == expected && image.save(QDir(root).filePath(name + ".png"));
  };
  const auto native_scene_tab_click = [&](const char* name) {
    QCoreApplication::processEvents();
    QTest::qWait(30); // Let the new tab's layout settle before mapping a native click.
    auto* item = visual_item(window->contentItem(), QString::fromLatin1(name));
    if (!item || !item->isVisible() || !item->isEnabled() || item->width() <= 0 || item->height() <= 0)
      return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(item->boundingRect().center()).toPoint());
    return true;
  };
  const auto click = [&](const char* name) {
    auto* button = fixture->findChild<QObject*>(name);
    if (button && button->property("visible").toBool() && button->property("enabled").toBool())
      return QMetaObject::invokeMethod(button, "clicked");
    if (QString::fromLatin1(name) == QStringLiteral("sceneObjectsButton"))
      return native_scene_tab_click("sceneStudioObjectsTab");
    if (QString::fromLatin1(name) == QStringLiteral("scenePhotoSettingsButton"))
      return native_scene_tab_click("sceneStudioCameraTab");
    return false;
  };
  if (args.contains("--studio-camera") || args.contains("--navigation-controls-proof")) {
    auto* studio_nav_settings = fixture->findChild<QObject*>("sceneWorkspaceSettings");
    auto* studio_nav_camera_panel = fixture->findChild<QObject*>("scenePhotoControlsPopup");
    auto* studio_nav_composition_panel = fixture->findChild<QObject*>("sceneCompositionPopup");
    auto* studio_nav_contents_panel = visual_item(window->contentItem(), QStringLiteral("sceneStudioOutliner"));
    auto* studio_nav_shots_panel = visual_item(window->contentItem(), QStringLiteral("sceneStudioShots"));
    auto* studio_nav_contents_toggle = visual_item(window->contentItem(), QStringLiteral("sceneContentsToggleButton"));
    auto* studio_nav_shots_toggle = visual_item(window->contentItem(), QStringLiteral("sceneShotsToggleButton"));
    auto* studio_nav_lighting_details = fixture->findChild<QObject*>("sceneLightingDetails");
    auto* studio_nav_lighting_details_text = fixture->findChild<QQuickItem*>("sceneLightingDetailsText");
    auto* studio_nav_output_details = fixture->findChild<QObject*>("sceneCompositionOutputDetails");
    auto* studio_nav_output_details_text = fixture->findChild<QQuickItem*>("sceneCompositionOutputDetailsText");
    auto* studio_nav_composition_scroll = fixture->findChild<QObject*>("sceneCompositionScroll");
    const auto click_studio_nav_tab = [&](const char* name, const QString& panel_name,
                                          int camera_page, int composition_section) {
      auto* tab = visual_item(window->contentItem(), QString::fromLatin1(name));
      if (!studio_nav_settings || !studio_nav_camera_panel || !studio_nav_composition_panel ||
          !tab || !tab->isVisible() ||
          !tab->isEnabled() || tab->width() <= 0 || tab->height() <= 0)
        return false;
      const auto point = tab->mapToScene(tab->boundingRect().center()).toPoint();
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
      return wait_for([&] {
        if (!tab->property("checked").toBool() ||
            studio_nav_settings->property("inspectorPanel").toString() != panel_name)
          return false;
        if (panel_name == QStringLiteral("camera"))
          return studio_nav_camera_panel->property("opened").toBool() &&
                 studio_nav_camera_panel->property("page").toInt() == camera_page;
        return studio_nav_composition_panel->property("opened").toBool() &&
               studio_nav_composition_panel->property("section").toInt() == composition_section;
      });
    };
    if (!studio_nav_settings || !studio_nav_contents_panel || !studio_nav_shots_panel ||
        !studio_nav_contents_toggle || !studio_nav_shots_toggle ||
        studio_nav_settings->property("outlinerVisible").toBool() ||
        studio_nav_settings->property("shotsVisible").toBool() ||
        studio_nav_settings->property("studioLayoutVersion").toInt()<2 ||
        studio_nav_contents_panel->isVisible() || studio_nav_shots_panel->isVisible() ||
        !studio_nav_contents_toggle->isVisible() || !studio_nav_contents_toggle->isEnabled() ||
        studio_nav_contents_toggle->property("active").toBool() ||
        !studio_nav_shots_toggle->isVisible() || !studio_nav_shots_toggle->isEnabled() ||
        studio_nav_shots_toggle->property("active").toBool())
      return fail("The legacy expanded layout was not migrated to collapsed Contents and Shots with visible reopen controls");
    if (!native_scene_tab_click("sceneContentsToggleButton") ||
        !wait_for([&] { return studio_nav_settings->property("outlinerVisible").toBool() &&
                             studio_nav_contents_panel->isVisible() && studio_nav_contents_toggle->property("active").toBool(); }) ||
        !studio_geometry_valid() ||
        !native_scene_tab_click("sceneContentsToggleButton") ||
        !wait_for([&] { return !studio_nav_settings->property("outlinerVisible").toBool() &&
                             !studio_nav_contents_panel->isVisible() && !studio_nav_contents_toggle->property("active").toBool(); }))
      return fail("The visible Contents control did not open and close the collapsed contents panel");
    if (!native_scene_tab_click("sceneShotsToggleButton") ||
        !wait_for([&] { return studio_nav_settings->property("shotsVisible").toBool() &&
                             studio_nav_shots_panel->isVisible() && studio_nav_shots_toggle->property("active").toBool(); }) ||
        !studio_geometry_valid() ||
        !native_scene_tab_click("sceneShotsToggleButton") ||
        !wait_for([&] { return !studio_nav_settings->property("shotsVisible").toBool() &&
                             !studio_nav_shots_panel->isVisible() && !studio_nav_shots_toggle->property("active").toBool(); }))
      return fail("The visible Shots control did not open and close the collapsed shots panel");
    auto* studio_nav_camera_focus_field = visual_item(window->contentItem(), QStringLiteral("sceneLensFocusField"));
    auto* studio_nav_camera_aperture = visual_item(window->contentItem(), QStringLiteral("sceneSceneApertureValue"));
    auto* studio_nav_composition_section = visual_item(window->contentItem(), QStringLiteral("sceneCompositionSection"));
    auto* studio_nav_render_control = visual_item(window->contentItem(), QStringLiteral("scenePhotoContrastSlider"));
    if (!studio_nav_settings || !studio_nav_camera_panel || !studio_nav_composition_panel ||
        !studio_nav_composition_section ||
        !click_studio_nav_tab("sceneStudioCameraTab", QStringLiteral("camera"), 4, -1) ||
        !studio_nav_camera_focus_field || !studio_nav_camera_focus_field->isVisible() ||
        !studio_nav_camera_aperture || !studio_nav_camera_aperture->isVisible() ||
        !capture("studio-nav-camera"))
      return fail("The Camera route must open its camera controls through the visible inspector tab");
    if (!click_studio_nav_tab("sceneStudioObjectsTab", QStringLiteral("composition"), -1, 0) ||
        !studio_nav_composition_section->isVisible() || !studio_nav_composition_section->isEnabled() ||
        !capture("studio-nav-objects"))
      return fail("The Objects route must open the Objects section through its visible inspector tab");
    if (!click_studio_nav_tab("sceneStudioLightingTab", QStringLiteral("composition"), -1, 1) ||
        !studio_nav_composition_section->isVisible() || !studio_nav_composition_section->isEnabled() ||
        !studio_nav_lighting_details || studio_nav_lighting_details->property("expanded").toBool() ||
        !studio_nav_lighting_details_text || studio_nav_lighting_details_text->isVisible() ||
        !studio_nav_output_details || studio_nav_output_details->property("expanded").toBool() ||
        !studio_nav_output_details_text || studio_nav_output_details_text->isVisible() ||
        !capture("studio-nav-lighting"))
      return fail("The Lighting route should keep long help collapsed while leaving it available in details");
    if (!native_scene_tab_click("sceneLightingDetailsHeader") ||
        !wait_for([&] { return studio_nav_lighting_details->property("expanded").toBool() && studio_nav_lighting_details_text->isVisible(); }) ||
        !studio_nav_lighting_details_text->property("text").toString().contains(QStringLiteral("Area radiance")) ||
        !studio_nav_lighting_details_text->property("text").toString().contains(QStringLiteral("Navigation uses 8 samples")) ||
        !capture("studio-nav-lighting-details") ||
        !native_scene_tab_click("sceneLightingDetailsHeader") ||
        !wait_for([&] { return !studio_nav_lighting_details->property("expanded").toBool(); }) ||
        !studio_nav_composition_scroll ||
        !studio_nav_composition_scroll->setProperty("contentY", qMax(0.0,
            studio_nav_composition_scroll->property("contentHeight").toReal() -
            studio_nav_composition_scroll->property("height").toReal())) ||
        !native_scene_tab_click("sceneCompositionOutputDetailsHeader") ||
        !wait_for([&] { return studio_nav_output_details->property("expanded").toBool() && studio_nav_output_details_text->isVisible(); }) ||
        !studio_nav_output_details_text->property("text").toString().contains(QStringLiteral("Photos and films")) ||
        !studio_nav_output_details_text->property("text").toString().contains(QStringLiteral("PLY export contains splats only")) ||
        !native_scene_tab_click("sceneCompositionOutputDetailsHeader") ||
        !wait_for([&] { return !studio_nav_output_details->property("expanded").toBool(); }))
      return fail("Lighting and photo/PLY explanations should expand from accessible collapsed detail groups");
    if (!click_studio_nav_tab("sceneStudioRenderTab", QStringLiteral("camera"), 0, -1) ||
        !studio_nav_render_control || !studio_nav_render_control->isVisible() || !capture("studio-nav-render"))
      return fail("The Render route must open render controls through the visible inspector tab");
    if (!click_studio_nav_tab("sceneStudioCameraTab", QStringLiteral("camera"), 4, -1))
      return fail("The Camera route should remain reachable after switching among all four inspector tabs");
    auto* studio_nav_scene_header = visual_item(window->contentItem(), QStringLiteral("sceneObjectsButton"));
    auto* studio_nav_photo_header = visual_item(window->contentItem(), QStringLiteral("scenePhotoSettingsButton"));
    auto* studio_nav_take_photo = visual_item(window->contentItem(), QStringLiteral("sceneTakePhotoButton"));
    auto* studio_nav_layout_button = visual_item(window->contentItem(), QStringLiteral("sceneWorkspaceLayoutButton"));
    const auto studio_nav_header_fits = [&](QQuickItem* item) {
      return item && item->isVisible() && item->isEnabled() && item->width() > 0 && item->height() > 0 &&
          window->contentItem()->boundingRect().contains(
              item->mapRectToItem(window->contentItem(), item->boundingRect()));
    };
    const auto studio_nav_footer_fits = [&](const char* name) {
      auto* item = visual_item(window->contentItem(), QString::fromLatin1(name));
      return studio_nav_shots_panel && item && item->isVisible() && item->width() > 0 && item->height() > 0 &&
          studio_nav_shots_panel->mapRectToItem(window->contentItem(), studio_nav_shots_panel->boundingRect())
              .contains(item->mapRectToItem(window->contentItem(), item->boundingRect()));
    };
    if (!studio_nav_scene_header || !studio_nav_photo_header || !studio_nav_take_photo ||
        !studio_nav_layout_button || studio_nav_scene_header->isVisible() ||
        studio_nav_photo_header->isVisible() || !capture("studio-header-inspector-open"))
      return fail("Scene composition and Photo settings header shortcuts should disappear while their inspector tabs are available");
    if (!studio_nav_settings->setProperty("inspectorVisible", false) ||
        !wait_for([&] {
          return !studio_nav_settings->property("inspectorVisible").toBool() &&
                 studio_nav_scene_header->isVisible() && studio_nav_photo_header->isVisible();
        }) || !studio_nav_header_fits(studio_nav_scene_header) ||
        !studio_nav_header_fits(studio_nav_photo_header) ||
        !capture("studio-header-fallback"))
      return fail("Hiding the inspector should restore the header shortcuts as bounded fallback actions");
    if (!native_scene_tab_click("scenePhotoSettingsButton") ||
        !wait_for([&] {
          return studio_nav_settings->property("inspectorVisible").toBool() &&
                 studio_nav_camera_panel->property("opened").toBool();
        }))
      return fail("The Photo settings header fallback should reopen the inspector through native input");
    window->resize(900, 760);
    if (!wait_for([&] { return studio_geometry_valid() && viewport->status().value("previewPhase") == "photo"; }) ||
        !native_scene_tab_click("sceneShotsToggleButton") ||
        !wait_for([&] { return studio_nav_settings->property("shotsVisible").toBool() &&
                             studio_nav_shots_panel->isVisible() && studio_nav_shots_toggle->property("active").toBool(); }) ||
        !studio_geometry_valid())
      return fail("The Shots controls could not be reopened for the footer layout proof");
    if (!wait_for([&] {
          return studio_geometry_valid() && viewport->status().value("previewPhase") == "photo";
        }) || studio_nav_scene_header->isVisible() || studio_nav_photo_header->isVisible() ||
        !studio_nav_header_fits(studio_nav_take_photo) ||
        !studio_nav_header_fits(studio_nav_layout_button) ||
        !studio_nav_footer_fits("sceneExportPhotoButton") ||
        !studio_nav_footer_fits("sceneEditPhotoButton") ||
        !studio_nav_footer_fits("sceneSavedViews") ||
        !studio_nav_footer_fits("sceneSaveViewButton") ||
        !studio_nav_footer_fits("sceneCameraPathButton") ||
        !studio_nav_footer_fits("sceneCameraPathPlayButton") ||
        !studio_nav_footer_fits("sceneCameraPathSlider") ||
        !studio_nav_footer_fits("sceneFilmExportButton") ||
        !capture("studio-header-900"))
      return fail("At 900 px, the open inspector should leave bounded capture and layout actions in the header");
    if (!studio_nav_settings->setProperty("inspectorVisible", false) ||
        !wait_for([&] {
          return !studio_nav_settings->property("inspectorVisible").toBool() &&
                 studio_nav_photo_header->isVisible() &&
                 studio_nav_header_fits(studio_nav_photo_header);
        }) || !capture("studio-header-900-fallback"))
      return fail("At 900 px, Photo settings should remain a bounded header fallback when the inspector is hidden");
    if (!native_scene_tab_click("scenePhotoSettingsButton") ||
        !wait_for([&] {
          return studio_nav_settings->property("inspectorVisible").toBool() &&
                 studio_nav_camera_panel->property("opened").toBool();
        }))
      return fail("Photo settings should reopen the inspector at 900 px through native input");
    if (!native_scene_tab_click("sceneShotsToggleButton") ||
        !wait_for([&] { return !studio_nav_settings->property("shotsVisible").toBool() &&
                             !studio_nav_shots_panel->isVisible() && !studio_nav_shots_toggle->property("active").toBool(); }))
      return fail("The Shots footer should return to its collapsed default after the compact layout proof");
    window->resize(1440, 900);
    if (!wait_for([&] {
          return studio_geometry_valid() && viewport->status().value("previewPhase") == "photo";
        }) || !click_studio_nav_tab("sceneStudioCameraTab", QStringLiteral("camera"), 4, -1))
      return fail("The Camera inspector route should remain ready for physical camera controls after the compact header proof");
#include "gaussian_camera_studio_ui_proof.inc"
  }
  if (args.contains("--photo-navigation")) {
#include "gaussian_photo_navigation_ui_proof.inc"
  }
  const auto initial_count = viewport->status().value("pointCount").toULongLong();
  if (args.contains("--photo-capture-options")) {
    const auto source = QDir(root).filePath("photo-inputs");
    const auto target = QDir(root).filePath("photo-options-project");
    if (!QDir().mkpath(source)) return fail("Photo input folder unavailable");
    for (int i = 0; i < 8; ++i)
      if (!write(QDir(source).filePath(QStringLiteral("photo%1.png").arg(i)),"preparer-handoff-fixture")) return fail("Photo handoff fixture unavailable");
    auto* mode = fixture->findChild<QObject*>("sceneCaptureMode");
    auto* input = fixture->findChild<QObject*>("sceneCapturePath");
    auto* destination = fixture->findChild<QObject*>("sceneProjectPath");
    auto* controls = fixture->findChild<QObject*>("sceneControlsPopup");
    if (!mode || !input || !destination || !controls || !mode->setProperty("currentIndex",0) ||
        !input->setProperty("text",source) || !destination->setProperty("text",target) ||
        !workspace->setProperty("showCaptureSetup",true) || !workspace->setProperty("advancedCapture",true) ||
        !QMetaObject::invokeMethod(controls,"open")) return fail("Photo options setup unavailable");
    auto* shared = visual_item(window->contentItem(),"scenePhotoSameCamera");
    auto* strict = visual_item(window->contentItem(),"scenePhotoStrictMatching");
    auto* scroll = visual_item(window->contentItem(),"sceneCaptureScroll");
    auto* create = visual_item(window->contentItem(),"sceneStartCapture");
    const auto reveal = [&](QQuickItem* item) {
      if (!item || !scroll || !capture("photo-layout")) return false;
      const auto local = scroll->mapFromItem(item,QPointF(0,item->height()*0.5));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+local.y()-scroll->height()*0.5,0.0,maximum));
      if (!capture("photo-revealed")) return false;
      const auto bounds = item->mapRectToScene(item->boundingRect());
      const auto viewport_bounds = scroll->mapRectToScene(scroll->boundingRect());
      return item->isVisible() && viewport_bounds.contains(bounds);
    };
    const auto toggle = [&](QQuickItem* item) {
      if (!reveal(item)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(18,item->height()*0.5)).toPoint());
      return wait_for([&] { return item->property("checked").toBool(); });
    };
    if (!shared || !strict || shared->property("checked").toBool() || strict->property("checked").toBool() ||
        !toggle(shared) || !toggle(strict) || !capture("photo-options-desktop")) return fail("Native photo options/defaults failed");
    window->resize(1024,760);
    if (!reveal(shared) || !capture("photo-options-compact") || !mode->setProperty("currentIndex",1) || shared->isVisible() || strict->isVisible() ||
        !mode->setProperty("currentIndex",2) || shared->isVisible() || strict->isVisible() ||
        !mode->setProperty("currentIndex",0) || !shared->property("checked").toBool() || !strict->property("checked").toBool() ||
        !reveal(create) || !capture("photo-options-compact-create")) return fail("Photo-only choices or compact scrolling failed");
    bool received = false;
    controller.setCapturePreparer([&](const QStringList& files,const QString&,int,const auto&) {
      received = files.size() == 8;
      return QVariantMap{{"ok",false},{"error",received ? "Photo options handoff verified" : "Wrong photo inputs"}};
    });
    for (const auto* variable : {"PHOTO_EDITOR_COLMAP_BINARY","PHOTO_EDITOR_BRUSH_BINARY"})
      qputenv(variable,QCoreApplication::applicationFilePath().toUtf8());
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,create->mapRectToScene(create->boundingRect()).center().toPoint());
    if (!wait_for([&] { return !controller.snapshot().value("busy").toBool() && received; }) ||
        controller.snapshot().value("error") != "Photo options handoff verified" ||
        !controller.snapshot().value("photoSharedIntrinsics").toBool() || !controller.snapshot().value("photoStrictMatching").toBool() || !qml_warnings.isEmpty())
      return fail("Native Create did not freeze both photo choices");
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{{"passed",true},
        {"defaultOptionsOff",true},{"nativeToggleBoth",true},{"photoOnly",true},{"compactScrollAndCreate",true},{"nativeCreateHandoff",received},
        {"scope","Native SceneWorkspace options and Create input; injected preparer intentionally stops before alignment. Real alignment is separately measured."}}).toJson())) return 2;
    return 0;
  }
  if (args.contains("--video-focus-selection")) {
    const auto source = QDir(root).filePath("selection-input.mov");
    const auto target = QDir(root).filePath("selection-project");
    if (!write(source,"source-provenance-fixture")) return fail("Video selection input unavailable");
    if (!QMetaObject::invokeMethod(workspace,"prepareVideoInput",Q_ARG(QString,source))) return fail("Video capture setup did not open");
    auto* choice = visual_item(window->contentItem(),"sceneVideoPreferSharpFrames");
    auto* destination = fixture->findChild<QObject*>("sceneProjectPath");
    auto* mode = fixture->findChild<QObject*>("sceneCaptureMode");
    if (!choice || !destination || !mode || choice->property("checked").toBool() || !destination->setProperty("text",target) || !capture("video-focus-default")) return fail("Video focus choice/default unavailable");
    const auto native_choice = [&] {
      const auto point = choice->mapToScene(QPointF(20,choice->height()*0.5)).toPoint();
      if (!choice->isVisible() || point.x() < 0 || point.y() < 0 || point.x() >= window->width() || point.y() >= window->height()) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point); QTest::qWait(80);
      return choice->property("checked").toBool();
    };
    if (!native_choice() || !capture("video-focus-selected")) return fail("Native focus choice did not toggle");
    window->resize(1024,760);
    if (!capture("video-focus-compact") || !mode->setProperty("currentIndex",0) || choice->isVisible() ||
        !mode->setProperty("currentIndex",2) || !choice->isVisible() || !choice->property("checked").toBool()) return fail("Compact/video-only focus choice failed");
    auto* create = visual_item(window->contentItem(),"sceneStartCapture");
    auto* controls = fixture->findChild<QObject*>("sceneControlsPopup");
    if (!create || !controls) return fail("Compact capture action unavailable");
    const auto choice_bounds = choice->mapRectToScene(choice->boundingRect()), create_bounds = create->mapRectToScene(create->boundingRect());
    const auto popup_right = controls->property("x").toDouble()+controls->property("width").toDouble();
    if (choice_bounds.right() > popup_right || create_bounds.right() > popup_right || create_bounds.bottom() >= window->height()) return fail("Compact capture action is clipped");
    bool received = false;
    controller.setCapturePreparer([](const auto&,const auto&,int,const auto&) { return QVariantMap{{"ok",false}}; });
    controller.setVideoPreparer([&](const QString& input,const QString&,int,int,const QString&,const QString&,const auto&,const auto&,bool sharp) {
      received = input == QFileInfo(source).canonicalFilePath() && sharp;
      return QVariantMap{{"ok",false},{"error",received ? "Selection handoff verified" : "Wrong selection handoff"}};
    });
    for (const auto* variable : {"PHOTO_EDITOR_COLMAP_BINARY","PHOTO_EDITOR_BRUSH_BINARY","PHOTO_EDITOR_FFMPEG_BINARY","PHOTO_EDITOR_FFPROBE_BINARY"})
      qputenv(variable,QCoreApplication::applicationFilePath().toUtf8());
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,create_bounds.center().toPoint());
    if (!wait_for([&] { return !controller.snapshot().value("busy").toBool() && received; }) ||
        !received || controller.snapshot().value("error") != "Selection handoff verified" || !controller.snapshot().value("preferSharpFrames").toBool() || !qml_warnings.isEmpty())
      return fail("Native Create scene did not forward selected frame preparation mode");
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{{"passed",true},
        {"defaultUniform",true},{"nativeToggle",true},{"compactLayout",true},{"unclippedCreateAction",true},{"nativeCreateAction",true},{"videoOnly",true},{"createActionModeForwarded",received},
        {"scope","Native SceneWorkspace capture setup and real Create action to an injected video preparer; decoding/selection and reconstruction require separate evidence"}}).toJson())) return 2;
    return 0;
  }
#include "gaussian_capture_emission_ui_proof.inc"
#include "gaussian_haze_ui_proof.inc"
#include "gaussian_shadow_receiver_ui_proof.inc"
  const bool sequence_workflow = args.contains("--frame-sequence");
  if (args.contains("--emitters") || sequence_workflow) {
    if (!click("sceneObjectsButton")) return fail("Emitter inspector unavailable");
    auto* panel = fixture->findChild<QObject*>("sceneCompositionPopup");
    auto* section = fixture->findChild<QObject*>("sceneCompositionSection");
    const auto ready = [&] { return viewport->status().value("previewPhase") == "photo"; };
    const auto choose = [&](int page) { return section && section->setProperty("currentIndex",page) && QMetaObject::invokeMethod(section,"activated",Q_ARG(int,page)); };
    const auto edit = [&](const char* name,const char* value) {
      auto* field = visual_item(window->contentItem(),QString::fromLatin1(name));
      return field && field->setProperty("text",QString::fromLatin1(value)) && field->setProperty("userEdited",true) && QMetaObject::invokeMethod(field,"editingFinished");
    };
    const auto count = [&] { return viewport->status().value("metrics").toMap().value("cameraVisibleEmitterCount").toInt(); };
    const auto native_toggle = [&](int expected) {
      auto* button = visual_item(window->contentItem(),"sceneLightCameraVisibleButton");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!button || !scroll || !button->isVisible() || !button->isEnabled()) return false;
      if (!capture("source-pointer-layout")) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = button->mapToScene(QPointF(button->width()/2,button->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum));
      if (!capture("source-pointer-controls")) return false;
      const auto hit = button->mapToScene(QPointF(button->width()/2,button->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      return wait_for([&] { return ready() && count() == expected; });
    };
    if (!panel || !wait_for([&] { return panel->property("opened").toBool(); }) || !choose(1) || !click("sceneAddRectangleLightButton")) return fail("Cannot add rectangle source");
    const auto view = gaussian_saved_camera(viewport->status().value("metrics").toMap().value("camera").toMap());
    auto composition = gaussian_scene_composition(viewport->status().value("sceneComposition").toMap());
    if (!view || !composition || composition->lights.size() != 1 || !composition->objects.empty()) return fail("Emitter fixture must contain lights only");
    const float depth = std::clamp(viewport->status().value("sceneFocusDistance").toFloat()*0.25F,0.05F,1e6F);
    auto& rectangle = composition->lights[0]; const auto rectangle_id = rectangle.id;
    rectangle.vector = view->position+view->forward*depth; rectangle.size = {depth*0.12F,depth*0.07F}; rectangle.color = {1,0.7F,0.4F}; rectangle.intensity = 2;
    QMatrix3x3 axes; for (int row = 0; row < 3; ++row) { axes(row,0) = view->right[row]; axes(row,1) = -view->down[row]; axes(row,2) = -view->forward[row]; }
    rectangle.rotation = QQuaternion::fromRotationMatrix(axes).toEulerAngles();
    if (!viewport->setSceneComposition(composition->description()).value("ok").toBool() || !wait_for(ready) || !native_toggle(1) || !capture("rectangle-visible")) return fail("Rectangle native visibility failed");
    if (!click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && count() == 0; }) ||
        !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && count() == 1; })) return fail("Emitter visibility undo/redo failed");
    if (!choose(2) || !edit("sceneEffect_power","0.8") || !choose(3) || !edit("sceneEffect_distance","20")) return fail("Emitter effects unavailable");
    auto* alpha = fixture->findChild<QObject*>("sceneAlphaModelCombo");
    if (!choose(2) || !alpha || !alpha->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(alpha,"activated",Q_ARG(int,1)) || !choose(1) ||
        !click("sceneAddSphereLightButton")) return fail("Effect/alpha edits lost the source schema");
    composition = gaussian_scene_composition(viewport->status().value("sceneComposition").toMap());
    if (!composition || composition->lights.size() != 2 || !composition->lights[0].camera_visible) return fail("Rectangle visibility was lost during other edits");
    auto& sphere = composition->lights[1]; sphere.vector = view->position+view->forward*(depth*1.3F)+view->right*(depth*0.23F);
    sphere.radius = depth*0.06F; sphere.intensity = 1.5F; sphere.color = {0.4F,0.65F,1};
    if (!viewport->setSceneComposition(composition->description()).value("ok").toBool() || !wait_for(ready) || !native_toggle(2) || !capture("both-sources")) return fail("Sphere native visibility failed");
    if (!click("sceneCompositionVisibilityButton") || !wait_for([&] { return ready() && count() == 1; }) ||
        !click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && count() == 2; })) return fail("Whole-light visibility disagrees with source visibility");
    auto photo_settings = view->photo; photo_settings.post.bloom_amount = 0.6F; photo_settings.post.bloom_radius = 32; photo_settings.post.threshold = 0.8F;
    if (!viewport->setPhotoSettings(photo_settings.description()).value("ok").toBool() || !viewport->setFocusMode("auto-centre").value("ok").toBool() ||
        !wait_for([&] { return ready() && count() == 2 && std::abs(viewport->status().value("autofocus").toMap().value("resolvedFocusDistance").toDouble()-depth) < depth*0.001; })) return fail("Autofocus did not resolve the rectangle source");
    const auto accepted = viewport->status().value("sceneComposition").toMap();
    QMetaObject::invokeMethod(panel,"close"); const auto photo = QDir(root).filePath("visible-sources-photo.png");
    if (!viewport->exportPhoto(photo,800).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == photo; })) return fail("Lights-only photograph failed");
    QFile photo_file(photo+".camera.json");
    if (!photo_file.open(QIODevice::ReadOnly)) return fail("Source photo receipt unavailable");
    const auto photo_receipt = QJsonDocument::fromJson(photo_file.readAll()).toVariant().toMap();
    if (photo_receipt.value("sceneObjects").toMap() != accepted || photo_receipt.value("alphaModel") != "inria-alpha-099-v1") return fail("Lights-only photo omitted frozen sources");
    const auto shot_a = viewport->saveView("Visible sources A"); viewport->orbit(2,0); const auto shot_b = viewport->saveView("Visible sources B");
    if (!shot_a.value("ok").toBool() || !shot_b.value("ok").toBool() ||
        !viewport->saveCameraPath({{"viewIds",QVariantList{shot_a.value("id"),shot_b.value("id")}}, {"segmentSeconds",0.5}, {"fps",24}}).value("ok").toBool()) return fail("Source path unavailable");
    const auto movie = QDir(root).filePath(sequence_workflow ? QString::fromUtf8("Rendered frames β") : "visible-sources-film.mp4");
    if (sequence_workflow) {
      auto* controls = fixture->findChild<QObject*>("sceneControlsPopup");
      auto* options = fixture->findChild<QObject*>("sceneFilmExportOptions");
      auto* edge = fixture->findChild<QObject*>("sceneExportEdge");
      if (!controls || !options || !edge || !QMetaObject::invokeMethod(controls,"open") ||
          !wait_for([&] { return controls->property("opened").toBool(); }) || !click("sceneFilmExportButton") ||
          !wait_for([&] { return options->property("opened").toBool(); })) return fail("Export format popup unavailable");
      auto* format = visual_item(window->contentItem(),"sceneFilmFormatCombo");
      auto* name = visual_item(window->contentItem(),"sceneSequenceFolderName");
      auto* render_button = visual_item(window->contentItem(),"sceneFilmDestinationButton");
      if (!format || !name || !render_button) return fail("Export format controls unavailable");
      if (!edge->setProperty("currentIndex",4) || render_button->isEnabled() || !capture("sequence-format-admission")) return fail("MP4 admitted an 8192 edge");
      const auto hit = format->mapToScene(QPointF(format->width()/2,format->height()/2));
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      QQuickItem* choice = nullptr;
      if (!wait_for([&] { choice = visual_item(window->contentItem(),"sceneFilmFormatComboOption1"); return choice && choice->isVisible(); }) || !capture("sequence-format-menu")) return fail("PNG format option unavailable");
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,choice->mapToScene(QPointF(choice->width()/2,choice->height()/2)).toPoint());
      auto* menu = format->property("popup").value<QObject*>();
      if (!menu || !wait_for([&] { return !menu->property("visible").toBool() && format->property("currentIndex").toInt() == 1; }) ||
          render_button->isEnabled() || !workspace->setProperty("sequenceParentFolder",QUrl::fromLocalFile(root)) || !name->setProperty("text","../escape") ||
          render_button->isEnabled() || !name->setProperty("text",QString::fromUtf8("Rendered frames β")) || !render_button->isEnabled()) return fail("New folder validation/8K PNG admission failed");
      edge->setProperty("currentIndex",0);  // 800px native UI export; core tests cover an odd 257px grid.
      if (!capture("sequence-ready")) return fail("Sequence controls cannot be captured");
      const auto render_hit = render_button->mapToScene(QPointF(render_button->width()/2,render_button->height()/2));
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,render_hit.toPoint());
      if (!wait_for([&] { return viewport->status().value("filmExporting").toBool(); })) return fail("Native PNG render action failed");
      QMetaObject::invokeMethod(controls,"close");
    } else if (!viewport->exportFilm(movie,256).value("ok").toBool()) return fail("Source film failed");
    auto hidden = accepted; auto lamps = hidden.value("lights").toList();
    for (auto& value : lamps) { auto lamp = value.toMap(); lamp.remove("cameraVisible"); value = lamp; } hidden["lights"] = lamps;
    if (!viewport->setSceneComposition(hidden).value("ok").toBool() || !wait_for([&] { return !viewport->status().value("filmExporting").toBool(); }) ||
        !viewport->status().value("filmResult").toMap().value("ok").toBool()) return fail("Frozen source film/concurrent edit failed");
    const auto export_result = viewport->status().value("filmResult").toMap();
    if (sequence_workflow && (export_result.value("receipt").toMap().contains("frames") ||
        export_result.value("receipt").toMap().value("frameIntegrityCount").toInt() != 12 ||
        export_result.value("outputFormat") != "png-sequence")) return fail("Sequence status did not retain a bounded receipt summary");
    QFile film_file(sequence_workflow ? QDir(movie).filePath("camera-path.json") : movie+".camera-path.json");
    if (!film_file.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(film_file.readAll()).toVariant().toMap().value("sceneObjects").toMap() != accepted) return fail("Concurrent edit changed frozen film sources");
    viewport->setSource({});
    if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); })) return fail("Source project reopen failed");
    viewport->setSource(controller.snapshot());
    if (!wait_for([&] { return ready() && count() == 0; }) || !viewport->restoreView(shot_a.value("id").toString()).value("ok").toBool() ||
        !wait_for([&] { return ready() && count() == 0; })) return fail("Shot recall reverted global source visibility");
    window->resize(1024,760);
    if (!click("sceneObjectsButton") || !wait_for([&] { return panel->property("opened").toBool() && ready(); }) || !choose(1) ||
        !panel->setProperty("selectedId",rectangle_id) || !native_toggle(1) || !capture("sources-compact")) return fail("Compact source control failed");
    if (sequence_workflow) {
      QMetaObject::invokeMethod(panel,"close"); auto* options = fixture->findChild<QObject*>("sceneFilmExportOptions");
      if (!options || !QMetaObject::invokeMethod(options,"open") || !wait_for([&] { return options->property("opened").toBool(); })) return fail("Compact export popup unavailable");
      auto* format = visual_item(window->contentItem(),"sceneFilmFormatCombo");
      auto* button = visual_item(window->contentItem(),"sceneFilmDestinationButton");
      if (!format || !button || !capture("sequence-compact")) return fail("Compact export controls unavailable");
      for (int index : {0,1}) {
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,format->mapToScene(QPointF(format->width()/2,format->height()/2)).toPoint());
        QQuickItem* choice = nullptr;
        if (!wait_for([&] { choice = visual_item(window->contentItem(),QStringLiteral("sceneFilmFormatComboOption%1").arg(index)); return choice && choice->isVisible(); }) || !capture("sequence-compact-menu")) return fail("Compact format menu failed");
        const auto hit = choice->mapToScene(QPointF(choice->width()/2,choice->height()/2));
        if (!QRectF(0,0,window->width(),window->height()).contains(hit)) return fail("Compact format option escaped the window");
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
        auto* menu = format->property("popup").value<QObject*>();
        if (!menu || !wait_for([&] { return !menu->property("visible").toBool() && format->property("currentIndex").toInt() == index; })) return fail("Compact format selection failed");
      }
      if (!capture("sequence-compact-final") || !QRectF(0,0,window->width(),window->height()).contains(button->mapToScene(QPointF(button->width()/2,button->height()/2))))
        return fail("Compact render action escaped the window");
    }
    if (!qml_warnings.isEmpty()) return fail("Source controls emitted QML warnings");
    const QVariantMap receipt{{"passed",true},{"initialCount",initial_count},{"acceptedComposition",accepted},{"metrics",viewport->status().value("metrics")},{"sequenceWorkflow",sequence_workflow},{"exportPath",movie},
        {"checks",QStringList{"native-source-toggles","whole-light-hide","undo-redo","v9-effect-alpha-retention","source-autofocus","lights-only-photo","frozen-film","global-reopen-recall","compact-native-toggle"}},
        {"scope",sequence_workflow ? "Native PNG-sequence format/folder controls and frozen primary-emitter export; native OS folder chooser is preconfigured by the fixture" :
            "Native primary-emitter workflow; not emissive-material GI, splat-cast shadows or full photographic parity"}};
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(receipt).toJson())) return 2;
    return 0;
  }
  const bool alpha_workflow = args.contains("--alpha-model");
  const bool scan_workflow = args.contains("--scan-shadows");
  const bool spatial_workflow = args.contains("--spatial-reflections");
  const bool progressive_workflow = args.contains("--progressive-lighting");
  const bool bounces_workflow = progressive_workflow || args.contains("--surface-bounces");
  const bool diffuse_workflow = bounces_workflow || args.contains("--spatial-diffuse");
  if (args.contains("--environment") || alpha_workflow || scan_workflow || spatial_workflow || diffuse_workflow) {
    if (!click("sceneObjectsButton")) return fail("Environment inspector unavailable");
    auto* objects = fixture->findChild<QObject*>("sceneCompositionPopup");
    if (!objects || !wait_for([&] { return objects->property("opened").toBool(); }) || !click("sceneAddSphereButton")) return fail("Environment receiver unavailable");
    auto* section = fixture->findChild<QObject*>("sceneCompositionSection");
    const auto choose = [&](int page) { return section && section->setProperty("currentIndex",page) && QMetaObject::invokeMethod(section,"activated",Q_ARG(int,page)); };
    const auto edit = [&](const char* name,const char* value) {
      auto* field = visual_item(window->contentItem(),QString::fromLatin1(name));
      return field && field->setProperty("text",QString::fromLatin1(value)) && field->setProperty("userEdited",true) && QMetaObject::invokeMethod(field,"editingFinished");
    };
    const auto ready = [&] { return viewport->status().value("previewPhase") == "photo"; };
    const auto native_alpha = [&](int index) {
      if (!choose(2)) return false;
      auto* combo = visual_item(window->contentItem(),"sceneAlphaModelCombo");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!combo || !scroll || !combo->isVisible() || !combo->isEnabled()) return false;
      auto* menu = combo->property("popup").value<QObject*>();
      if (!menu || !wait_for([&] { return !menu->property("visible").toBool(); })) return false;
      scroll->setProperty("contentY",0);
      if (!capture("alpha-pointer-controls")) return false;
      const auto hit = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      QQuickItem* option_item = nullptr;
      if (!wait_for([&] { option_item = visual_item(window->contentItem(),QStringLiteral("sceneAlphaModelComboOption%1").arg(index));
          return menu->property("opened").toBool() && option_item && option_item->isVisible(); })) return false;
      if (!capture("alpha-pointer-menu")) return false;
      const auto option_hit = option_item->mapToScene(QPointF(option_item->width()/2,option_item->height()/2));
      if (!QRectF(0,0,window->width(),window->height()).contains(option_hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,option_hit.toPoint());
      return wait_for([&] { return !menu->property("visible").toBool() && ready() && viewport->status().value("metrics").toMap().value("alphaModel") ==
          (index ? "inria-alpha-099-v1" : "brush-alpha-0999-v1"); });
    };
    if (alpha_workflow && (!native_alpha(1) || !click("sceneCompositionUndoButton") ||
        !wait_for([&] { return ready() && viewport->status().value("metrics").toMap().value("alphaModel") == "brush-alpha-0999-v1"; }) ||
        !click("sceneCompositionRedoButton") ||
        !wait_for([&] { return ready() && viewport->status().value("metrics").toMap().value("alphaModel") == "inria-alpha-099-v1"; })))
      return fail("Native opacity selection/undo/redo failed");
    if (!objects || !wait_for([&] { return objects->property("opened").toBool() && ready(); }) || !choose(1) ||
        !edit("sceneCompositionAmbient","0")) return fail("Environment light page unavailable");
    auto* quality = fixture->findChild<QObject*>("sceneLightSamples");
    const int environment_quality = args.contains("--environment-final-quality") ? 2 : 0;
    if (!quality || !quality->setProperty("currentIndex",environment_quality) || !QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,environment_quality)) ||
        !wait_for(ready) || !capture("environment-off")) return fail("Environment sample controls failed");
    const auto native_toggle = [&] {
      auto* button = visual_item(window->contentItem(),"sceneCompositionEnvironmentButton");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!button || !scroll || !button->isVisible() || !button->isEnabled()) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = button->mapToScene(QPointF(button->width()/2,button->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      if (!scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum)) ||
          !capture("environment-pointer-controls")) return false;
      const auto hit = button->mapToScene(QPointF(button->width()/2,button->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint()); return wait_for(ready);
    };
    if (!native_toggle() || !viewport->status().value("metrics").toMap().contains("environmentLightingModel") ||
        !edit("sceneEnvironmentStrength","0.7") || !wait_for(ready) || !capture("environment-on")) return fail("Native environment toggle/strength failed");
    const auto env = viewport->status().value("sceneComposition").toMap().value("environment").toMap();
    if (!env.value("diffuse").toBool() || std::abs(env.value("strength").toDouble()-0.7) > 1e-6 ||
        !click("sceneCompositionUndoButton") || !wait_for(ready) ||
        std::abs(viewport->status().value("sceneComposition").toMap().value("environment").toMap().value("strength").toDouble()-1) > 1e-6 ||
        !click("sceneCompositionRedoButton") || !wait_for(ready)) return fail("Environment undo/redo failed");
    if (!choose(2) || !edit("sceneEffect_power","0.8") || !choose(3) || !edit("sceneEffect_distance","20") ||
        !choose(1) || !wait_for(ready) || viewport->status().value("sceneComposition").toMap().value("environment").toMap() != env)
      return fail("Other effect edits discarded the environment schema");
    const auto native_scan_toggle = [&](bool expected) {
      auto* button = visual_item(window->contentItem(),"sceneScanShadowsButton");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!button || !scroll || !button->isVisible() || !button->isEnabled()) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = button->mapToScene(QPointF(button->width()/2,button->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      if (!scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum)) || !capture("scan-shadow-pointer-controls")) return false;
      const auto hit = button->mapToScene(QPointF(button->width()/2,button->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      return wait_for([&] { return ready() && viewport->status().value("sceneComposition").toMap().value("scanShadows").toBool() == expected &&
          viewport->status().value("metrics").toMap().contains("scanShadowModel") == expected; });
    };
    if (scan_workflow) {
      if (!native_scan_toggle(true) || !click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && !viewport->status().value("sceneComposition").toMap().value("scanShadows").toBool(); }) ||
          !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && viewport->status().value("sceneComposition").toMap().value("scanShadows").toBool(); }) ||
          !choose(2) || !edit("sceneEffect_power","0.75") || !choose(1) || !wait_for(ready) || !viewport->status().value("sceneComposition").toMap().value("scanShadows").toBool()) return fail("Scan shadow pointer/undo/effect retention failed");
    }
    const auto native_reflection = [&](int index) {
      if (!choose(0)) return false;
      auto* combo = visual_item(window->contentItem(),"sceneReflectionModeCombo");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!combo || !scroll || !combo->isVisible() || !combo->isEnabled()) return false;
      auto* menu = combo->property("popup").value<QObject*>();
      if (!menu || !wait_for([&] { return !menu->property("visible").toBool(); })) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      if (!scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum)) || !capture("spatial-pointer-controls")) return false;
      const auto hit = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      QQuickItem* option_item = nullptr;
      if (!wait_for([&] { option_item = visual_item(window->contentItem(),QStringLiteral("sceneReflectionModeComboOption%1").arg(index));
          return menu->property("opened").toBool() && option_item && option_item->isVisible(); }) || !capture("spatial-pointer-menu")) return false;
      const auto option_hit = option_item->mapToScene(QPointF(option_item->width()/2,option_item->height()/2));
      if (!QRectF(0,0,window->width(),window->height()).contains(option_hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,option_hit.toPoint());
      return wait_for([&] { return !menu->property("visible").toBool() && ready() &&
          viewport->status().value("sceneComposition").toMap().value("reflectionMode") == (index ? QVariant("spatial") :
              viewport->status().value("sceneComposition").toMap().value("illuminationMode") == "spatial" ? QVariant("probe") : QVariant()) &&
          viewport->status().value("metrics").toMap().value("reflectionModel") == (index ? "spatial-gaussian-peak-ordered-ggx-v1" : "object-center-linear-half-capture-probe-v1"); });
    };
    const auto native_illumination = [&](int index) {
      if (!choose(1)) return false;
      auto* combo = visual_item(window->contentItem(),"sceneIlluminationModeCombo");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!combo || !scroll || !combo->isVisible() || !combo->isEnabled()) return false;
      auto* menu = combo->property("popup").value<QObject*>();
      if (!menu || !wait_for([&] { return !menu->property("visible").toBool(); })) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      if (!scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum)) || !capture("diffuse-pointer-controls")) return false;
      const auto hit = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      QQuickItem* option_item = nullptr;
      if (!wait_for([&] { option_item = visual_item(window->contentItem(),QStringLiteral("sceneIlluminationModeComboOption%1").arg(index));
          return menu->property("opened").toBool() && option_item && option_item->isVisible(); }) || !capture("diffuse-pointer-menu")) return false;
      const auto option_hit = option_item->mapToScene(QPointF(option_item->width()/2,option_item->height()/2));
      if (!QRectF(0,0,window->width(),window->height()).contains(option_hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,option_hit.toPoint());
      return wait_for([&] { return !menu->property("visible").toBool() && ready() &&
          viewport->status().value("sceneComposition").toMap().value("illuminationMode") == (index ? QVariant("spatial") : bounces_workflow ? QVariant("probe") : QVariant()) &&
          viewport->status().value("metrics").toMap().value("environmentLightingModel") == (index ? "surface-cosine-spatial-gaussian-v1" : "object-center-cosine-probe-diffuse-visibility-v1"); });
    };
    const auto native_depth = [&](int depth) {
      if (!choose(1)) return false;
      auto* combo = visual_item(window->contentItem(),"sceneSurfaceBouncesCombo");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!combo || !scroll || !combo->isVisible() || !combo->isEnabled()) return false;
      auto* menu = combo->property("popup").value<QObject*>();
      if (!menu || !wait_for([&] { return !menu->property("visible").toBool(); })) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      if (!scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum)) || !capture("surface-bounces-controls")) return false;
      const auto hit = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      QQuickItem* option_item = nullptr;
      if (!wait_for([&] { option_item = visual_item(window->contentItem(),QStringLiteral("sceneSurfaceBouncesComboOption%1").arg(depth-1));
          return menu->property("opened").toBool() && option_item && option_item->isVisible(); }) || !capture("surface-bounces-menu")) return false;
      const auto option_hit = option_item->mapToScene(QPointF(option_item->width()/2,option_item->height()/2));
      if (!QRectF(0,0,window->width(),window->height()).contains(option_hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,option_hit.toPoint());
      return wait_for([&] { return !menu->property("visible").toBool() && ready() &&
          viewport->status().value("sceneComposition").toMap().value("surfaceBounces",1).toInt() == depth &&
          viewport->status().value("metrics").toMap().contains("surfaceTransportModel") == (depth > 1); });
    };
    if (diffuse_workflow) {
      if (!native_illumination(1) || !click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && !viewport->status().value("sceneComposition").toMap().contains("illuminationMode"); }) ||
          !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && viewport->status().value("sceneComposition").toMap().value("illuminationMode") == "spatial"; }) ||
          !choose(2) || !edit("sceneEffect_power","0.75") || !choose(1) || !wait_for(ready) || viewport->status().value("sceneComposition").toMap().value("illuminationMode") != "spatial") return fail("Spatial diffuse pointer/undo/effect retention failed");
      // Combined methods are exercised on the ordinary fixture; dense combined
      // transport performance is a separate qualification from diffuse alone.
      if (source_project.isEmpty() && (!choose(0) || !click("sceneCompositionReflectionsButton") || !wait_for(ready) ||
          !native_reflection(1) || !native_reflection(0) || !native_reflection(1) || !choose(2) || !edit("sceneEffect_power","0.75") || !choose(1) || !wait_for(ready) ||
          viewport->status().value("sceneComposition").toMap().value("illuminationMode") != "spatial")) return fail("Combined spatial methods lost their independent settings");
    }
    if (spatial_workflow) {
      if (!choose(0) || !click("sceneCompositionReflectionsButton") || !wait_for(ready)) return fail("Reflection control unavailable");
      const auto items = viewport->status().value("sceneComposition").toMap().value("objects").toList();
      if (items.isEmpty() || !objects->setProperty("selectedId",items.front().toMap().value("id")) ||
          !edit("sceneComposition_metallic","1") || !edit("sceneComposition_roughness","0.15") ||
          !edit("sceneComposition_color_0","0.95") || !edit("sceneComposition_color_1","0.95") || !edit("sceneComposition_color_2","0.95") || !wait_for(ready)) return fail("Reflection material controls failed");
      if (!native_reflection(1) || !click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && !viewport->status().value("sceneComposition").toMap().contains("reflectionMode"); }) ||
          !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && viewport->status().value("sceneComposition").toMap().value("reflectionMode") == "spatial"; }) ||
          !choose(2) || !edit("sceneEffect_power","0.75") || !choose(1) || !wait_for(ready) || viewport->status().value("sceneComposition").toMap().value("reflectionMode") != "spatial") return fail("Spatial mode pointer/undo/effect retention failed");
    }
    if (bounces_workflow && (!native_depth(4) || !click("sceneCompositionUndoButton") ||
        !wait_for([&] { return ready() && !viewport->status().value("sceneComposition").toMap().contains("surfaceBounces"); }) ||
        !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && viewport->status().value("sceneComposition").toMap().value("surfaceBounces").toInt() == 4; }) ||
        !choose(2) || !edit("sceneEffect_power","0.75") || !choose(1) || !wait_for(ready) ||
        viewport->status().value("sceneComposition").toMap().value("schema") != "looksy-scene-composition-v13")) return fail("Surface depth pointer/undo/effect retention failed");
    const auto accepted = viewport->status().value("sceneComposition").toMap();
    if (alpha_workflow && accepted.value("alphaModel") != "inria-alpha-099-v1") return fail("Effects discarded the opacity convention");
    QMetaObject::invokeMethod(objects,"close");
    const auto photo = QDir(root).filePath("environment-photo.png");
    if (!viewport->exportPhoto(photo,800).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == photo; })) return fail("Environment photo failed");
    QFile photo_file(photo+".camera.json");
    if (!photo_file.open(QIODevice::ReadOnly)) return fail("Photo receipt unavailable");
    const auto photo_receipt = QJsonDocument::fromJson(photo_file.readAll()).toVariant().toMap();
    if (photo_receipt.value("sceneObjects").toMap() != accepted || (alpha_workflow &&
        (photo_receipt.value("schema") != "framehouse-gaussian-photo-v5" || photo_receipt.value("alphaModel") != "inria-alpha-099-v1")))
      return fail("Environment photo recipe lost lighting");
    if (alpha_workflow) {
      const auto splat = QDir(root).filePath("alpha-export.ply");
      if (!viewport->exportSplat(splat).value("ok").toBool() || !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool() && QFileInfo::exists(splat+".splat.json"); }))
        return fail("Opacity splat export failed");
      QFile receipt(splat+".splat.json"), original(controller.snapshot().value("artifactPath").toString()), output(splat);
      if (!receipt.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(receipt.readAll()).toVariant().toMap().value("alphaModel") != "inria-alpha-099-v1" ||
          !original.open(QIODevice::ReadOnly) || !output.open(QIODevice::ReadOnly) || original.readAll() != output.readAll()) return fail("Splat export lost profile or changed rows");
    }
    const auto shot_a = viewport->saveView("Environment A"); viewport->orbit(2,0);
    const auto shot_b = viewport->saveView("Environment B");
    if (!shot_a.value("ok").toBool() || !shot_b.value("ok").toBool() ||
        !viewport->saveCameraPath({{"viewIds",QVariantList{shot_a.value("id"),shot_b.value("id")}}, {"segmentSeconds",0.5}, {"fps",24}}).value("ok").toBool()) return fail("Environment shots/path failed");
    const auto movie = QDir(root).filePath("environment-film.mp4");
    if (!viewport->exportFilm(movie,256).value("ok").toBool()) return fail("Environment film failed");
    auto changed = accepted; auto changed_env = env; changed_env["strength"] = 0.2; changed["environment"] = changed_env;
    if (alpha_workflow) changed["alphaModel"] = "brush-alpha-0999-v1";
    if (spatial_workflow) changed["reflectionMode"] = "probe";
    if (diffuse_workflow) changed["illuminationMode"] = "probe";
    if (bounces_workflow) changed["surfaceBounces"] = 2;
    if (!viewport->setSceneComposition(changed).value("ok").toBool() || !wait_for([&] { return !viewport->status().value("filmExporting").toBool(); }) ||
        !viewport->status().value("filmResult").toMap().value("ok").toBool()) return fail("Concurrent environment edit/film failed");
    changed_env = viewport->status().value("sceneComposition").toMap().value("environment").toMap();
    QFile film_file(movie+".camera-path.json");
    if (!film_file.open(QIODevice::ReadOnly)) return fail("Film receipt unavailable");
    const auto film_receipt = QJsonDocument::fromJson(film_file.readAll()).toVariant().toMap();
    if (film_receipt.value("sceneObjects").toMap() != accepted || (alpha_workflow &&
        (film_receipt.value("schema") != "framehouse-gaussian-film-v4" || film_receipt.value("alphaModel") != "inria-alpha-099-v1")))
      return fail("Environment edit changed frozen film lighting");
    viewport->setSource({});
    if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); })) return fail("Environment project reopen failed");
    viewport->setSource(controller.snapshot());
    if (!wait_for(ready) || viewport->status().value("sceneComposition").toMap().value("environment").toMap() != changed_env ||
        !viewport->restoreView(shot_a.value("id").toString()).value("ok").toBool() || !wait_for(ready)) return fail("Environment was lost on reopen/recall");
    if (spatial_workflow && viewport->status().value("sceneComposition").toMap().contains("reflectionMode")) return fail("Shot recall reverted the global reflection mode");
    if (diffuse_workflow && (bounces_workflow ? viewport->status().value("sceneComposition").toMap().value("illuminationMode") != "probe" : viewport->status().value("sceneComposition").toMap().contains("illuminationMode"))) return fail("Shot recall reverted the global illumination mode");
    if (bounces_workflow && viewport->status().value("sceneComposition").toMap().value("surfaceBounces").toInt() != 2) return fail("Shot recall reverted global depth");
    if (alpha_workflow && viewport->status().value("metrics").toMap().value("alphaModel") != "brush-alpha-0999-v1") return fail("Reopen/shot recall reverted the global opacity convention");
    window->resize(1024,760);
    if (!click("sceneObjectsButton") || !wait_for([&] { return objects->property("opened").toBool() && ready(); }) || !choose(1) ||
        !capture("environment-compact") || !native_toggle() || viewport->status().value("metrics").toMap().contains("environmentLightingModel") ||
        !native_toggle() || !viewport->status().value("metrics").toMap().contains("environmentLightingModel") || !capture("environment-compact-pointer"))
      return fail("Compact native environment controls failed");
    if (alpha_workflow && (!native_alpha(1) || !native_alpha(0) || !native_alpha(1) || !capture("alpha-compact"))) return fail("Compact native opacity controls failed");
    if (scan_workflow && (!viewport->status().value("sceneComposition").toMap().value("scanShadows").toBool() || !native_scan_toggle(false) || !native_scan_toggle(true) || !capture("scan-shadows-compact"))) return fail("Reopened/compact scan shadow toggle failed");
    if (spatial_workflow && (!native_reflection(1) || !native_reflection(0) || !native_reflection(1) || !capture("spatial-compact"))) return fail("Compact native spatial controls failed");
    if (diffuse_workflow && (!native_illumination(1) || !native_illumination(0) || !native_illumination(1) || !capture("diffuse-compact"))) return fail("Compact native diffuse controls failed");
    if (bounces_workflow && (!native_depth(8) || !native_depth(1) || !native_depth(4) || !capture("surface-bounces-compact"))) return fail("Compact native surface depth controls failed");
    QVariantMap progressive_proof;
    if (progressive_workflow) {
      QMetaObject::invokeMethod(objects,"close");
      const bool initially_active = window->isActive();
      if (!background_allowed && !window->isActive()) activate_native_test_window(window);
      if (!background_allowed && !window->isActive()) return unavailable_foreground();
      auto refine = viewport->status().value("sceneComposition").toMap();
      refine["lightSamples"] = source_project.isEmpty() ? 128 : 16; refine["ambient"] = 0.001;
      QElapsedTimer first_timer; first_timer.start();
      if (!viewport->setSceneComposition(refine).value("ok").toBool() || !wait_for([&] {
          const auto status = viewport->status(), metrics = status.value("metrics").toMap();
          return status.value("previewPhase") == "refining" && status.value("publishedPhase") == "refining" &&
              metrics.value("progressiveLighting").toBool() && status.value("lightingPreviewSamples").toInt() > 0 &&
              status.value("lightingPreviewSamples").toInt() < status.value("lightingPreviewTargetSamples").toInt();
        })) return fail("No native progressive lighting prefix");
      const auto first_ms = first_timer.elapsed(); const auto first_metrics = viewport->status().value("metrics").toMap();
      if (first_ms > 5000 || first_metrics.value("previewReadback").toBool() || !capture("lighting-first-prefix")) return fail("Native lighting prefix gate failed");
      const auto early_composition = viewport->status().value("sceneComposition").toMap();
      const auto early_photo = QDir(root).filePath("lighting-early-photo.png");
      if (!viewport->exportPhoto(early_photo,800).value("ok").toBool() || !viewport->status().value("exporting").toBool()) return fail("Early lighting photo did not queue");
      refine["ambient"] = 0.003;
      if (!viewport->setSceneComposition(refine).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == early_photo; })) return fail("Early lighting export/concurrent edit failed");
      QFile early_receipt_file(early_photo+".camera.json");
      if (!early_receipt_file.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(early_receipt_file.readAll()).toVariant().toMap().value("sceneObjects").toMap() != early_composition ||
          viewport->status().value("lastExportRenderMetrics").toMap().value("softLightSamples").toInt() != early_composition.value("lightSamples").toInt()) return fail("Early lighting photo used a prefix or unfrozen settings");
      if (!wait_for(ready) || !capture("lighting-complete")) return fail("Native lighting completion failed");
      const auto final_metrics = viewport->status().value("metrics").toMap();
      const auto start = viewport->mapToScene(QPointF(viewport->width()*0.35,viewport->height()*0.5)).toPoint();
      if (!background_allowed && !window->isActive()) activate_native_test_window(window);
      if (!background_allowed && (!window->isActive() || !window->isExposed())) return unavailable_foreground();
      const bool foreground_at_input = window->isActive() && window->isExposed();
      QElapsedTimer drag_timer; drag_timer.start(); QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
      const auto press_ms = drag_timer.elapsed(); QVariantList dispatch_trace;
      QSet<QByteArray> poses; QVariantList live_trace; int live_partials = 0; qint64 first_live_ms = -1, last_publication = -1;
      for (int step = 0; step < 30; ++step) {
        const auto before_move = drag_timer.elapsed();
        QTest::mouseMove(window,start+QPoint(step*2,0)); const auto after_move = drag_timer.elapsed(); QTest::qWait(60);
        dispatch_trace.append(QVariantMap{{"step",step},{"beforeMoveMs",before_move},{"afterMoveMs",after_move},{"afterWaitMs",drag_timer.elapsed()},{"windowActive",window->isActive()}});
        const auto status = viewport->status(), metrics = status.value("metrics").toMap();
        const auto publication = status.value("publishedFrames").toLongLong();
        if (status.value("navigating").toBool() && status.value("publishedPhase") == "live" && publication != last_publication) {
          if (first_live_ms < 0) first_live_ms = drag_timer.elapsed(); last_publication = publication;
          if (metrics.value("progressiveLighting").toBool()) ++live_partials;
          const auto pose = metrics.value("camera").toMap();
          poses.insert(QJsonDocument::fromVariant(QVariantMap{{"position",pose.value("position")},{"forward",pose.value("forward")}}).toJson(QJsonDocument::Compact));
          live_trace.append(QVariantMap{{"elapsedMs",drag_timer.elapsed()},{"camera",pose},{"samples",metrics.value("softLightSamples")},{"partial",metrics.value("progressiveLighting")}});
          if (metrics.value("previewReadback").toBool() || metrics.value("width").toInt() > 720 || metrics.value("softLightSamples").toInt() > 8) return fail("Live lighting preview contract failed");
        }
      }
      if (!capture("lighting-held-navigation")) return fail("Live lighting capture failed");
      QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(58,0));
      const auto latest = viewport->saveView("Latest progressive input"); QVariantMap authored_pose;
      for (const auto& value : viewport->status().value("savedViews").toList()) if (value.toMap().value("id") == latest.value("id")) authored_pose = value.toMap().value("camera").toMap();
      const bool held_foreground = foreground_at_input && std::all_of(dispatch_trace.cbegin(),dispatch_trace.cend(),[](const auto& value) { return value.toMap().value("windowActive").toBool(); });
      if (!held_foreground && !background_allowed) return unavailable_foreground();
      if (poses.size() < 2 || first_live_ms < 0 || (held_foreground && first_live_ms > 500) || (!source_project.isEmpty() && live_partials < 1) || !latest.value("ok").toBool()) {
        std::cerr << "Held navigation diagnostics firstMs=" << first_live_ms << " poses=" << poses.size() << " partials=" << live_partials << " saved=" << latest.value("ok").toBool() << '\n';
        std::cerr << "Press dispatch ms=" << press_ms << " active=" << window->isActive() << '\n';
        std::cerr << QJsonDocument::fromVariant(dispatch_trace).toJson(QJsonDocument::Compact).constData() << '\n';
        std::cerr << QJsonDocument::fromVariant(live_trace).toJson(QJsonDocument::Compact).constData() << '\n';
        return fail("Held navigation did not publish changing live poses");
      }
      if (!wait_for(ready)) return fail("Held navigation did not settle to full photo quality");
      const auto settled = viewport->status().value("metrics").toMap(); const auto settled_pose = settled.value("camera").toMap();
      for (const auto* key : {"position","forward","right","down"}) if (settled_pose.value(QLatin1String(key)) != authored_pose.value(QLatin1String(key))) return fail("Final lighting frame does not match latest input pose");
      if (settled.value("softLightSamples").toInt() != refine.value("lightSamples").toInt() || !capture("lighting-latest-settled")) return fail("Latest input did not refine to its full quality");
      // Invalidate a worker that has already published a prefix. Its later
      // snapshots must never repopulate a cleared/new source epoch.
      refine["ambient"] = 0.002;
      if (!viewport->setSceneComposition(refine).value("ok").toBool() || !wait_for([&] { return viewport->status().value("publishedPhase") == "refining" && viewport->status().value("lightingPreviewSamples").toInt() > 0; })) return fail("Epoch fixture did not publish a prefix");
      viewport->setSource({}); QTest::qWait(500);
      if (viewport->status().value("frameAvailable").toBool()) return fail("Stale lighting prefix survived source clear");
      progressive_proof = {{"firstVisiblePrefixMs",first_ms},{"firstMetrics",first_metrics},{"completedMetrics",final_metrics},
          {"firstHeldLiveMs",first_live_ms},{"pressDispatchMs",press_ms},{"initiallyActive",initially_active},{"measuredWhileActive",held_foreground},{"foregroundTimingQualified",held_foreground && first_live_ms <= 500},{"backgroundAllowed",background_allowed},{"dispatchTrace",dispatch_trace},{"changingHeldPoses",poses.size()},{"heldPartialFrames",live_partials},{"heldTrace",live_trace},
          {"earlyExportFrozenAtFullSamples",true},{"latestPoseMatches",true},{"settledMetrics",settled},{"sourceClearRejectsStalePrefixes",true},{"scope","native visible prefix, held pointer navigation and latest-pose settlement; not sustained input-to-photon qualification"}};
    }
    if (!qml_warnings.isEmpty()) return fail("Environment workflow emitted QML warnings");
    const QVariantMap receipt{{"passed",true},{"initialCount",initial_count},{"acceptedComposition",accepted},
        {"reopenedEnvironment",changed_env},{"metrics",viewport->status().value("metrics")},{"alphaWorkflow",alpha_workflow},{"scanShadowWorkflow",scan_workflow},{"spatialWorkflow",spatial_workflow},{"spatialDiffuseWorkflow",diffuse_workflow},{"surfaceBouncesWorkflow",bounces_workflow},{"progressiveLightingWorkflow",progressive_workflow},{"progressiveLightingProof",progressive_proof},{"combinedMethodsFixture",diffuse_workflow && source_project.isEmpty()},{"waitLimitMs",wait_limit_ms},
        {"checks",QStringList{"native-toggle","strength","undo-redo","effect-schema-retention","photo-recipe","frozen-film-recipe","reopen-recall","compact-native-toggle"} +
            (alpha_workflow ? QStringList{"native-alpha-selection","alpha-undo-redo","alpha-effect-retention","profiled-photo-film","unchanged-ply-rows","frozen-film-profile","global-profile-recall","compact-native-alpha-selection"} : QStringList{}) +
            (spatial_workflow ? QStringList{"native-reflection-mode","reflection-undo-redo","reflection-effect-retention","frozen-spatial-photo-film","global-mode-recall","compact-native-mode-selection"} : QStringList{}) +
            (bounces_workflow ? QStringList{"native-surface-depth","depth-undo-redo","v13-effect-retention","frozen-depth-photo-film","global-depth-recall","compact-native-depth-selection"} : QStringList{}) +
            (diffuse_workflow ? QStringList{"native-illumination-mode","illumination-undo-redo","illumination-effect-retention","frozen-spatial-diffuse-photo-film","global-illumination-recall","compact-native-illumination-selection"} : QStringList{})},
        {"scope",alpha_workflow ? "SceneWorkspace opacity-convention workflow and export provenance; not complete original-3DGS or photographic parity" :
            spatial_workflow ? "Native SceneWorkspace spatial reflection controls, persistence and frozen exports; independent radiance and photographic quality are separate gates" :
            diffuse_workflow ? "Native SceneWorkspace spatial diffuse controls, persistence and frozen exports; independent radiance and photographic quality are separate gates" :
            "SceneWorkspace environment-light workflow; independent radiance/visibility and photographic quality are separate gates"}};
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(receipt).toJson())) return 2;
    return 0;
  }
  if (!click("sceneCleanupButton") || !wait_for([&] { return popup->property("opened").toBool(); }) || !capture("cleanup-before")) return fail("Cleanup controls unavailable");
  if (args.contains("--depth-cleanup")) {
    if (!source_project.isEmpty() || initial_count != 4) return fail("Depth layer proof requires its independent four-point fixture");
    const auto camera = gaussian_saved_camera(viewport->status().value("metrics").toMap().value("camera").toMap());
    if (!camera) return fail("Depth proof camera unavailable");
    const double first_depth = QVector3D::dotProduct(QVector3D(-1.05F,0,2)-camera->position,camera->forward);
    const double last_depth = QVector3D::dotProduct(QVector3D(1.05F,0,3.8F)-camera->position,camera->forward);
    const auto cutoff = QString::number((first_depth+last_depth)/2,'g',12);
    const auto choose_last = [&](const char* name) {
      auto* item = visual_item(window->contentItem(),name);
      if (!item) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapRectToScene(item->boundingRect()).center().toPoint());
      auto* menu = item->property("popup").value<QObject*>();
      if (!menu || !wait_for([&] { return menu->property("opened").toBool(); })) return false;
      QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return);
      return wait_for([&] { return item->property("currentIndex").toInt() == 1 && !menu->property("visible").toBool(); });
    };
    const auto enter = [&](const char* name, const QString& text) {
      auto* item = visual_item(window->contentItem(),name);
      if (!item) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapRectToScene(item->boundingRect()).center().toPoint());
      QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
      for (const auto character : text) QTest::keyClick(window,character.toLatin1());
      return item->property("text").toString() == text;
    };
    if (!choose_last("sceneCleanupDepthMode") || !enter("sceneCleanupFarDepth","0") || workspace->property("cleanupDepthValid").toBool() ||
        fixture->findChild<QObject*>("sceneCleanupRemoveButton")->property("enabled").toBool() ||
        !enter("sceneCleanupFarDepth",cutoff) || !workspace->property("cleanupDepthValid").toBool()) return fail("Native depth input validation failed");
    if (viewport->cleanupLasso(QVariantList{QVariantMap{{"x",.1},{"y",.1}},QVariantMap{{"x",.9},{"y",.1}},QVariantMap{{"x",.5},{"y",.9}}},false,4,2).value("ok").toBool() ||
        viewport->cleanupSelection(.1,.1,.8,.8,false,-1,2).value("ok").toBool()) return fail("Invalid depth API began an edit");
    const auto stroke = [&](bool lasso, bool keep) {
      if (!click(keep ? "sceneCleanupKeepButton" : "sceneCleanupRemoveButton") ||
          !wait_for([&] { return workspace->property("cleanupSelectionArmed").toBool() && popup->property("opened").toBool() && studio_geometry_valid(); })) return false;
      auto* area = visual_item(window->contentItem(),"sceneCleanupMarquee");
      if (!area) return false;
      const auto point = [&](double x, double y) { return area->mapToScene(QPointF(
          area->property("imageX").toDouble()+x*area->property("imageWidth").toDouble(),
          area->property("imageY").toDouble()+y*area->property("imageHeight").toDouble())).toPoint(); };
      QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point(.02,.02));
      if (lasso) for (const auto& p : {QPointF(.98,.02),QPointF(.98,.98),QPointF(.02,.98)}) {
        QTest::mouseMove(window,point(p.x(),p.y()),10); QTest::qWait(25);
      }
      else { QTest::mouseMove(window,point(.98,.98),10); QTest::qWait(25); }
      if (!capture(lasso ? "depth-lasso-outline" : "depth-rectangle-outline")) return false;
      QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,lasso ? point(.02,.02) : point(.98,.98));
      return wait_for([&] { return !viewport->status().value("cleanupBusy").toBool() && viewport->status().value("previewPhase") == "photo" && popup->property("opened").toBool(); });
    };
    const auto visibility = [&] {
      QFile file(QDir(project).filePath("cleanup.json")); if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
      return QByteArray::fromBase64(QJsonDocument::fromJson(file.readAll()).object().value("visibility").toString().toLatin1());
    };
    if (!capture("depth-controls") || !stroke(false,false) || visibility() != QByteArray("\0\0\1\1",4) ||
        !click("sceneCleanupUndoButton") || !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) ||
        viewport->status().value("pointCount").toULongLong() != 4 || !click("sceneCleanupRedoButton") ||
        !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) || visibility() != QByteArray("\0\0\1\1",4)) return fail("Depth rectangle or undo/redo selected wrong layers");
    if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool() && viewport->status().value("pointCount").toULongLong() == 2; }) ||
        visibility() != QByteArray("\0\0\1\1",4) || !click("sceneCleanupResetButton") ||
        !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); })) return fail("Depth edit did not survive reopening");
    if (!QMetaObject::invokeMethod(popup,"open") || !wait_for([&] { return popup->property("opened").toBool(); })) return fail("Depth popup could not reopen after restoration");
    window->resize(1024,760);
    if (!wait_for([&] { return window->width() == 1024 && window->height() == 760 && studio_geometry_valid() && viewport->status().value("previewPhase") == "photo"; })) return fail("Depth compact studio resize failed");
    if (!capture("depth-resized")) return fail("Depth compact popup capture failed");
    if (!choose_last("sceneCleanupShape")) return fail("Depth compact native shape selection failed");
    if (!enter("sceneCleanupFarDepth",cutoff) || !capture("depth-controls-compact")) return fail("Depth compact native field input failed");
    if (!stroke(true,true) || visibility() != QByteArray("\1\1\0\0",4) || !capture("depth-lasso-kept") ||
        !viewport->exportSplat(QDir(root).filePath("near-layer.ply")).value("ok").toBool() ||
        !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool() && QFileInfo::exists(QDir(root).filePath("near-layer.ply.splat.json")); }) ||
        !QFileInfo::exists(QDir(root).filePath("near-layer.ply"))) return fail("Compact depth lasso or export selected wrong layers");
    const QVariantMap receipt{{"passed",true},{"depthRangeWorkflow",true},{"cutoffSceneUnits",cutoff.toDouble()},
        {"checks",QStringList{"native-depth-mode","native-depth-fields","invalid-depth-rejection","rectangle-far-layer-preserved","undo-redo","reopen","compact-lasso-near-layer-kept","PLY-export"}},
        {"scope","SceneWorkspace camera-forward depth-range cleanup; not automatic visible-surface segmentation"}};
    if (!qml_warnings.isEmpty()) return fail("Depth workflow emitted QML warnings");
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(receipt).toJson())) return 2;
    std::cout << QJsonDocument::fromVariant(receipt).toJson().constData(); return 0;
  }
  const bool depth_lasso_workflow = args.contains("--depth-lasso");
  const bool lasso_workflow = args.contains("--lasso-cleanup") || depth_lasso_workflow;
  std::vector<float> cleanup_depths;
  double cleanup_cutoff = 0;
  const auto depth_mask_valid = [&](bool keep_inside) {
    if (!depth_lasso_workflow) return true;
    QFile file(QDir(project).filePath("cleanup.json"));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto mask = QByteArray::fromBase64(QJsonDocument::fromJson(file.readAll()).object().value("visibility").toString().toLatin1());
    if (mask.size() != static_cast<qsizetype>(cleanup_depths.size())) return false;
    qsizetype in_range_hidden = 0, out_of_range = 0;
    for (qsizetype i = 0; i < mask.size(); ++i) {
      const double depth = cleanup_depths[static_cast<size_t>(i)];
      if (depth <= 0 || depth > cleanup_cutoff+1e-5) {
        ++out_of_range;
        if (bool(mask[i]) == keep_inside) return false;
      } else if (!mask[i]) ++in_range_hidden;
    }
    return out_of_range > 0 && (keep_inside || in_range_hidden > 0);
  };
  if (lasso_workflow) {
    auto* shape = visual_item(window->contentItem(),"sceneCleanupShape");
    if (!shape || shape->property("currentIndex").toInt() != 0) return fail("Default rectangle selection changed");
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,shape->mapRectToScene(shape->boundingRect()).center().toPoint());
    QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return);
    if (!wait_for([&] { return shape->property("currentIndex").toInt() == 1; }) || !capture("lasso-controls")) return fail("Native lasso shape selection failed");
    const auto malformed = QVariantList{QVariantMap{{"x",.2},{"y",.2}},QVariantMap{{"x",.8},{"y",.2}},QVariantMap{{"x",".5"},{"y",.8}}};
    if (viewport->cleanupLasso(malformed,false).value("ok").toBool() || viewport->status().value("cleanupBusy").toBool()) return fail("Malformed lasso began an edit");
    if (!click("sceneCleanupRemoveButton") || !wait_for([&] { return workspace->property("cleanupSelectionArmed").toBool() && popup->property("opened").toBool() && studio_geometry_valid(); })) return fail("Lasso did not arm");
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,QPoint(550,220));
    QTest::mouseMove(window,QPoint(720,680),10);
    QTest::keyClick(window,Qt::Key_Escape);
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,QPoint(720,680));
    if (!wait_for([&] { return !workspace->property("cleanupSelectionArmed").toBool(); }) ||
        viewport->status().value("pointCount").toULongLong() != initial_count || viewport->status().value("cleanupBusy").toBool() ||
        !click("sceneCleanupButton")) return fail("Escape changed the scene or failed to cancel the lasso");
  }
  if (depth_lasso_workflow) {
    if (source_project.isEmpty()) return fail("Dense depth proof needs a source project");
    const auto camera = gaussian_saved_camera(viewport->status().value("metrics").toMap().value("camera").toMap());
    const auto artifact_hash = controller.snapshot().value("artifactValidation").toMap().value("artifactSha256").toString();
    const auto original = load_gaussian_scene(QDir(project).filePath("trained/scene.ply"),QDir(project).filePath("dataset/sparse"),artifact_hash);
    if (!camera || !original.scene || original.scene->points.size() != initial_count) return fail("Dense depth source unavailable");
    std::vector<float> positive;
    for (const auto& point : original.scene->points) {
      const auto& p = point.center_opacity;
      const float depth = QVector3D::dotProduct(QVector3D(p[0],p[1],p[2])-camera->position,camera->forward);
      cleanup_depths.push_back(depth); if (depth > 0) positive.push_back(depth);
    }
    if (positive.size() < 3) return fail("Dense depth fixture has no separated positive depths");
    const auto middle = positive.begin()+static_cast<std::ptrdiff_t>(positive.size()/2);
    std::nth_element(positive.begin(),middle,positive.end()); cleanup_cutoff = *middle;
    auto* mode = visual_item(window->contentItem(),"sceneCleanupDepthMode");
    auto* far = visual_item(window->contentItem(),"sceneCleanupFarDepth");
    if (!mode || !far) return fail("Dense depth controls unavailable");
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,mode->mapRectToScene(mode->boundingRect()).center().toPoint());
    auto* menu = mode->property("popup").value<QObject*>();
    if (!menu || !wait_for([&] { return menu->property("opened").toBool(); })) return fail("Dense depth menu did not open");
    QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return);
    if (!wait_for([&] { return mode->property("currentIndex").toInt() == 1 && !menu->property("visible").toBool(); })) return fail("Dense depth mode did not commit");
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,far->mapRectToScene(far->boundingRect()).center().toPoint());
    QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
    for (const auto character : QString::number(cleanup_cutoff,'g',12)) QTest::keyClick(window,character.toLatin1());
    if (!workspace->property("cleanupDepthValid").toBool() || !capture("dense-depth-controls")) return fail("Dense depth field did not commit");
  }
  if (!click("sceneCleanupRemoveButton") || !wait_for([&] { return workspace->property("cleanupSelectionArmed").toBool() && popup->property("opened").toBool() && studio_geometry_valid(); })) return fail("Rectangle tool did not arm");
  // Exercise the real MouseArea and letterbox mapping with a reversed drag.
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, lasso_workflow ? QPoint(550,220) : QPoint(720,680));
  if (lasso_workflow) {
    for (const auto& p : {QPoint(720,220),QPoint(720,680),QPoint(550,680),QPoint(610,450)}) { QTest::mouseMove(window,p,10); QTest::qWait(25); }
  } else QTest::mouseMove(window, QPoint(550, 220), 10);
  if (!capture("cleanup-marquee")) return fail("Cannot capture selection rectangle");
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(550, 220));
  if (!wait_for([&] { return !viewport->status().value("cleanupBusy").toBool() && viewport->status().value("previewPhase") == "photo"; }) ||
      viewport->status().value("pointCount").toULongLong() >= initial_count || !viewport->status().value("cleanupCanUndo").toBool() ||
      !viewport->status().value("cleanupResult").toMap().value("ok").toBool() || !capture("cleanup-after")) return fail("Rectangle cleanup did not remove and render splats");
  const auto cleaned_count = viewport->status().value("pointCount").toULongLong();
  if (!depth_mask_valid(false)) return fail("Depth lasso removed a far or behind-camera original splat");
  if (!click("sceneCleanupUndoButton") || !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) ||
      viewport->status().value("pointCount").toULongLong() != initial_count || !click("sceneCleanupRedoButton") ||
      !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) || viewport->status().value("pointCount").toULongLong() != cleaned_count) return fail("Undo/redo failed");
  const auto output = QDir(root).filePath("cleaned.ply");
  if (!viewport->exportSplat(output).value("ok").toBool() || !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) ||
      !viewport->status().value("cleanupResult").toMap().value("ok").toBool()) return fail("Splat export failed");
  const auto photo = QDir(root).filePath("cleaned-photo.png");
  if (!viewport->exportPhoto(photo, 800).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == photo; })) return fail("Cleaned scene photo failed");
  QFile photo_receipt(photo + ".camera.json");
  if (!photo_receipt.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(photo_receipt.readAll()).toVariant().toMap().value("sceneCleanup").toMap().value("visibleCount").toULongLong() != cleaned_count)
    return fail("Photo did not record scene cleanup");
  viewport->setSource({});
  if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); })) return fail("Project reopen failed");
  viewport->setSource(controller.snapshot());
  if (!wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || viewport->status().value("pointCount").toULongLong() != cleaned_count || !capture("cleanup-reopened")) return fail("Cleanup was lost after reopening");
  if (!QMetaObject::invokeMethod(popup, "open")) return fail("Cannot reopen cleanup controls");
  auto* opacity = fixture->findChild<QObject*>("sceneCleanupOpacitySlider");
  if (!opacity || !opacity->setProperty("value", 0.1) || !click("sceneCleanupFilterButton") ||
      !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) || !click("sceneCleanupResetButton") ||
      !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) || viewport->status().value("pointCount").toULongLong() != initial_count) return fail("Filter or original restoration failed");
  window->resize(1024, 760);
  if (!wait_for([&] { return window->width() == 1024 && studio_geometry_valid() && viewport->status().value("previewPhase") == "photo" && viewport->status().value("frameAvailable").toBool(); }) || !capture("cleanup-compact")) return fail("Compact cleanup studio geometry/capture failed");
  if (lasso_workflow) {
    if (!click("sceneCleanupKeepButton") || !wait_for([&] { return workspace->property("cleanupSelectionArmed").toBool() && popup->property("opened").toBool() && studio_geometry_valid(); })) return fail("Compact keep lasso did not arm");
    auto* area = visual_item(window->contentItem(),"sceneCleanupMarquee");
    if (!area) return fail("Lasso hit area unavailable");
    const auto position = [&](double x, double y) {
      return area->mapToScene(QPointF(area->property("imageX").toDouble()+x*area->property("imageWidth").toDouble(),
          area->property("imageY").toDouble()+y*area->property("imageHeight").toDouble())).toPoint();
    };
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,position(.25,.25));
    for (const auto& p : {QPointF(.5,.25),QPointF(.5,.75),QPointF(.25,.75)}) { QTest::mouseMove(window,position(p.x(),p.y()),10); QTest::qWait(25); }
    if (!capture("lasso-compact-keep-outline")) return fail("Compact lasso outline unavailable");
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position(.25,.25));
    if (!wait_for([&] { return !viewport->status().value("cleanupBusy").toBool() && viewport->status().value("previewPhase") == "photo"; }) ||
        viewport->status().value("pointCount").toULongLong() >= initial_count || !viewport->status().value("cleanupResult").toMap().value("ok").toBool() ||
        !depth_mask_valid(true) || !capture("lasso-compact-kept") || !click("sceneCleanupResetButton") ||
        !wait_for([&] { return !viewport->status().value("cleanupBusy").toBool(); }) || viewport->status().value("pointCount").toULongLong() != initial_count) return fail("Compact keep lasso or restore failed");
  }
  bool object_proof = false;
  QVariantMap pupil_settings;
  QVariantMap tone_settings;
  if (args.contains("--objects")) {
    QMetaObject::invokeMethod(popup,"close"); window->resize(1440,900);
    if (!wait_for([&] { return window->width() == 1440 && studio_geometry_valid() && viewport->status().value("previewPhase") == "photo"; }) || !click("sceneObjectsButton")) return fail("Object controls unavailable");
    auto* objects = fixture->findChild<QObject*>("sceneCompositionPopup");
    if (!objects || !wait_for([&] { return objects->property("opened").toBool(); }) || !click("sceneAddSphereButton") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("solidObjectCount").toInt() == 1; }) || !capture("objects-sphere")) return fail("Added sphere did not render");
    for (const auto& setting : {std::pair<const char*,const char*>{"sceneComposition_metallic","0.95"}, {"sceneComposition_roughness","0.08"}}) {
      auto* field = visual_item(window->contentItem(),QString::fromLatin1(setting.first));
      if (!field || !field->setProperty("text",QString::fromLatin1(setting.second)) || !field->setProperty("userEdited",true) ||
          !QMetaObject::invokeMethod(field,"editingFinished")) return fail("Material numeric field failed");
    }
    if (!click("sceneCompositionReflectionsButton") || !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().contains("reflectionModel"); }) || !capture("objects-reflections")) return fail("Scan reflections failed");
    auto* section = fixture->findChild<QObject*>("sceneCompositionSection");
    const auto choose_section = [&](int index) {
      return section && section->setProperty("currentIndex",index) && QMetaObject::invokeMethod(section,"activated",Q_ARG(int,index));
    };
    const auto edit_effect = [&](const char* key,const char* value) {
      auto* field = visual_item(window->contentItem(),QString::fromLatin1(key));
      return field && field->setProperty("text",QString::fromLatin1(value)) && field->setProperty("userEdited",true) && QMetaObject::invokeMethod(field,"editingFinished");
    };
    const auto sphere_id = objects->property("selectedId");
    if (!choose_section(2)) return fail("Captured lighting page unavailable");
    auto* lighting_mode = fixture->findChild<QObject*>("sceneCaptureLightingMode");
    if (!lighting_mode || !lighting_mode->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(lighting_mode,"activated",Q_ARG(int,1)) ||
        !edit_effect("sceneEffect_power","0.8") || !edit_effect("sceneEffect_shadowStrength","0.7") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().contains("captureLightingModel"); }) ||
        !capture("captured-lighting")) return fail("Captured lighting controls failed");
    if (!choose_section(3) || !edit_effect("sceneEffect_density","0.006") || !edit_effect("sceneEffect_distance","30") ||
        !edit_effect("sceneEffect_anisotropy","0.35") || !edit_effect("sceneHazeColor_0","0.65") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().contains("hazeModel"); }) ||
        !capture("scene-haze")) return fail("Haze controls failed");
    if (!choose_section(0) || !objects->setProperty("selectedId",sphere_id)) return fail("Could not return to object controls");
    if (!click("sceneCompositionVisibilityButton") || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
        viewport->status().value("metrics").toMap().contains("solidObjectCount") || !click("sceneCompositionUndoButton") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; })) return fail("Object hide/undo failed");
    const auto camera_state = viewport->status().value("metrics").toMap().value("camera").toMap();
    const auto vector3 = [](const QVariant& value) { const auto a = value.toList(); return QVector3D(a[0].toFloat(),a[1].toFloat(),a[2].toFloat()); };
    const auto camera_position = vector3(camera_state.value("position")), right = vector3(camera_state.value("right"));
    const auto down = vector3(camera_state.value("down"));
    const auto current_objects = viewport->status().value("sceneComposition").toMap().value("objects").toList();
    const auto sphere_position = vector3(current_objects[0].toMap().value("position"));
    const float mesh_depth = (sphere_position-camera_position).length();
    const auto mesh_center = sphere_position+right*(mesh_depth*0.30F);
    QByteArray mesh_bytes("# independent imported surface\nmtllib imported materials.mtl\n");
    for (const auto& corner : {QVector2D(-1,-1),QVector2D(1,-1),QVector2D(1,1),QVector2D(-1,1)}) {
      const auto p = mesh_center+(right*corner.x()+down*corner.y())*(mesh_depth*0.10F);
      mesh_bytes += "v "+QByteArray::number(p.x(),'g',9)+" "+QByteArray::number(p.y(),'g',9)+" "+QByteArray::number(p.z(),'g',9)+"\n";
    }
    mesh_bytes += "vt 0 1\nvt 1 1\nvt 1 0\nvt 0 0\nusemtl Checkered\nf 1/1 2/2 3/3\nusemtl Blue\nf 1/1 3/3 4/4\n";
    const auto mesh_mtl = QDir(root).filePath("imported materials.mtl"), mesh_image = QDir(root).filePath("imported color.png");
    QImage texture(256,256,QImage::Format_RGBA8888);
    for (int y = 0; y < texture.height(); ++y) for (int x = 0; x < texture.width(); ++x) texture.setPixelColor(x,y,(x/32+y/32)%2 ? QColor(245,145,40) : QColor(30,80,160));
    if (!write(mesh_mtl,"newmtl Checkered\nKd 1 1 1\nPr 0.55\nPm 0.15\nmap_Kd imported color.png\nnewmtl Blue\nKd 0.03 0.1 1\nPr 0.3\nPm 0.2\n") || !texture.save(mesh_image)) return fail("Material fixture failed");
    const auto mesh_file = QDir(root).filePath("imported-surface.obj");
    if (!write(mesh_file,mesh_bytes) || !click("sceneMeshCoordinatesButton")) return fail("Mesh fixture or placement controls failed");
    auto* mesh_picker = fixture->findChild<QObject*>("sceneMeshPicker");
    // Inject the selected URL into the real dialog's accepted signal; native
    // file-picker pointer navigation is outside this component test's scope.
    if (!mesh_picker || !mesh_picker->setProperty("selectedFile",QUrl::fromLocalFile(mesh_file)) || !QMetaObject::invokeMethod(mesh_picker,"accepted") ||
        !wait_for([&] { return !viewport->status().value("importingMesh").toBool() && viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("meshTriangleCount").toInt() == 2; }) ||
        !viewport->status().value("meshImportResult").toMap().value("ok").toBool() || !capture("objects-imported-mesh")) return fail("Imported mesh controls/render failed");
    if (!QFile::remove(mesh_file) || !QFile::remove(mesh_mtl) || !QFile::remove(mesh_image)) return fail("Cannot remove original geometry/material/image fixtures");
    const auto imported_mesh_id = objects->property("selectedId");
    if (!choose_section(4) || !objects->setProperty("selectedId",imported_mesh_id)) return fail("Imported materials page failed");
    auto* surface_slot = fixture->findChild<QObject*>("sceneSurfaceSlot");
    if (!surface_slot || !surface_slot->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(surface_slot,"activated",Q_ARG(int,1)) ||
        !edit_effect("sceneSurface_roughness","0.6") || !edit_effect("sceneSurface_metallic","0.25") || !edit_effect("sceneSurface_color_0","0.9") ||
        !edit_effect("sceneSurface_uvOffset_0","0.125") || !click("sceneSurfaceClampButton") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("colorImageCount").toInt() == 1 && surface_slot->property("currentIndex").toInt() == 1 && surface_slot->property("displayText").toString() == "Checkered"; }) || !capture("materials-edited")) return fail("Imported surface/image controls failed");
    if (!click("sceneSurfaceImageButton") || !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("colorImageCount").toInt() == 0; }) ||
        !click("sceneSurfaceImageButton") || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; })) return fail("Color image toggle failed");
    if (!surface_slot->setProperty("currentIndex",2) || !QMetaObject::invokeMethod(surface_slot,"activated",Q_ARG(int,2)) || !edit_effect("sceneSurface_emission_2","0.02") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && surface_slot->property("currentIndex").toInt() == 2 && surface_slot->property("displayText").toString() == "Blue"; }) || !capture("materials-second-slot")) return fail("Second imported face material failed");
    if (!choose_section(0) || !objects->setProperty("selectedId",imported_mesh_id) || !click("sceneImportedMaterialMode")) return fail("Single material override unavailable");
    // Editable transforms and materials retain the resolved immutable geometry.
    if (!edit_effect("sceneComposition_roughness","0.65") || !edit_effect("sceneComposition_metallic","0.2") || !click("sceneImportedMaterialMode") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; })) return fail("Imported material edits lost geometry");
    const auto native_composition_click = [&](const char* name) {
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      auto* button = visual_item(window->contentItem(),QString::fromLatin1(name));
      if (!scroll || !button) return false;
      const auto center = [&] { return button->mapToScene(QPointF(button->width()/2,button->height()/2)); };
      const auto scroll_origin = scroll->mapToScene(QPointF());
      const double maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      const double next_y = std::clamp(scroll->property("contentY").toDouble()+center().y()-scroll_origin.y()-scroll->height()/2,0.0,maximum);
      if (!scroll->setProperty("contentY",next_y) || !capture("objects-scroll-controls")) return false;
      const QRectF bounds(scroll->mapToScene(QPointF()),QSizeF(scroll->width(),scroll->height()));
      if (!bounds.contains(center()) || !button->isVisible() || !button->isEnabled()) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center().toPoint()); return true;
    };
    const auto before_pointer_undo = viewport->status().value("sceneComposition").toMap();
    if (!native_composition_click("sceneCompositionUndoButton") || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
        viewport->status().value("sceneComposition").toMap() == before_pointer_undo || !native_composition_click("sceneCompositionRedoButton") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || viewport->status().value("sceneComposition").toMap() != before_pointer_undo)
      return fail("Scrolled native pointer hit testing or mesh undo/redo failed");
    const auto mesh_selection = objects->property("selectedId");
    if (!choose_section(1)) return fail("Light inspector unavailable");
    auto* quality = fixture->findChild<QObject*>("sceneLightSamples");
    const auto lamps = viewport->status().value("sceneComposition").toMap().value("lights").toList();
    if (!quality || !quality->setProperty("currentIndex",0) || !QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,0)) ||
        !objects->setProperty("selectedId",lamps[0].toMap().value("id")) || !edit_effect("sceneLight_angularRadius","0.5") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("softLightSamples").toInt() == 16; }) || !capture("lights-soft-sun")) return fail("Finite sun controls failed");
    if (!click("sceneAddSphereLightButton") || !edit_effect("sceneLight_radius","0.2") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || !capture("lights-spherical")) return fail("Spherical emitter controls failed");
    if (!click("sceneAddRectangleLightButton") || !edit_effect("sceneLight_width","0.8") || !edit_effect("sceneLight_height","0.5") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("objectLightCount").toInt() == 3; }) || !capture("lights-rectangle")) return fail("Rectangle emitter controls failed");
    if (!choose_section(0) || !objects->setProperty("selectedId",mesh_selection)) return fail("Cannot return from light inspector");
    // Exercise post controls through the rendered workspace, then carry the
    // resulting shot through photo/film freezing and save/reopen/restore.
    QMetaObject::invokeMethod(objects,"close");
    if (!click("scenePhotoSettingsButton")) return fail("Photo controls unavailable");
    auto* photo_popup = fixture->findChild<QObject*>("scenePhotoControlsPopup");
    auto* photo_section = fixture->findChild<QObject*>("scenePhotoSettingsSectionCombo");
    auto* post_section = fixture->findChild<QObject*>("scenePostSectionCombo");
    if (!photo_popup || !photo_section || !post_section || !wait_for([&] { return photo_popup->property("opened").toBool(); }) ||
        !photo_section->setProperty("currentIndex",0) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,0)) ||
        !capture("camera-tone-controls")) return fail("Tone controls unavailable");
    const auto native_tone_edit = [&](const char* name, double fraction = 0.7) {
      auto* slider = visual_item(window->contentItem(),QString::fromLatin1(name));
      auto* scroll = visual_item(window->contentItem(),"scenePhotoControlsScroll");
      if (!slider || !scroll || !slider->isVisible() || !slider->isEnabled()) return false;
      const auto center = slider->mapToScene(QPointF(slider->width()*fraction,slider->height()/2));
      const auto origin = scroll->mapToScene(QPointF());
      const double maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      if (!scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum))) return false;
      const QPointF hit = slider->mapToScene(QPointF(slider->width()*fraction,slider->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      return wait_for([&] { return viewport->status().value("previewPhase") == "photo"; });
    };
    if (!native_tone_edit("scenePhotoContrastSlider") || !native_tone_edit("scenePhotoHighlightCompressionSlider") ||
        !capture("camera-tone")) return fail("Native tone slider hit testing failed");
    const auto accepted_tone = viewport->status().value("photoSettings").toMap().value("tone").toMap();
    tone_settings = accepted_tone;
    if (accepted_tone.value("contrast").toDouble() <= 1 || accepted_tone.value("highlightCompression").toDouble() <= 0.5)
      return fail("Tone controls did not change the renderer settings");
    if (!photo_popup || !photo_section || !post_section || !wait_for([&] { return photo_popup->property("opened").toBool(); }) ||
        !photo_section->setProperty("currentIndex",5) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,5)) ||
        !edit_effect("scenePost_bloomAmount","0.8") || !edit_effect("scenePost_bloomRadius","16") || !edit_effect("scenePost_threshold","0.6") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("cameraPostApplied").toBool(); }) ||
        !capture("camera-bloom")) return fail("Bloom controls failed");
    if (!post_section->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(post_section,"activated",Q_ARG(int,1)) ||
        !edit_effect("scenePost_glareAmount","0.6") || !edit_effect("scenePost_glareRadius","48") || !edit_effect("scenePost_glareRays","3") ||
        !edit_effect("scenePost_glareRotation","25") || !edit_effect("scenePost_spectralIntensity","0.7") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || !capture("camera-spectral-glare")) return fail("Glare controls failed");
    if (!post_section->setProperty("currentIndex",2) || !QMetaObject::invokeMethod(post_section,"activated",Q_ARG(int,2)) ||
        !edit_effect("scenePost_chromaticAberration","0.2") || !edit_effect("scenePost_saturation","1.1") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || !capture("camera-color")) return fail("Camera color controls failed");
    const auto accepted_post = viewport->status().value("photoSettings").toMap().value("post").toMap();
    // Editing another camera section must preserve the new post schema.
    if (!photo_section->setProperty("currentIndex",3) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,3))) return fail("Film page unavailable");
    auto* grain_slider = fixture->findChild<QObject*>("sceneFilmGrainSlider");
    if (!grain_slider || !grain_slider->setProperty("value",0.1) || !QMetaObject::invokeMethod(grain_slider,"moved") ||
        viewport->status().value("photoSettings").toMap().value("post").toMap() != accepted_post) return fail("Film edit discarded post settings");
    if (!photo_section->setProperty("currentIndex",2) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,2))) return fail("Aperture page unavailable");
    auto* lens_quality = fixture->findChild<QObject*>("sceneLensQualityCombo");
    auto* aperture_slider = fixture->findChild<QObject*>("sceneLensApertureSlider");
    if (!lens_quality || !lens_quality->setProperty("currentIndex",0) || !QMetaObject::invokeMethod(lens_quality,"activated",Q_ARG(int,0)) ||
        !aperture_slider || !aperture_slider->setProperty("value",0.03) || !QMetaObject::invokeMethod(aperture_slider,"moved") ||
        !edit_effect("sceneLens_apertureEdge","3") || !edit_effect("sceneLens_vignettingDistance","0.04") || !edit_effect("sceneLens_vignettingScale","1.1") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
        viewport->status().value("photoSettings").toMap().value("post").toMap() != accepted_post || !capture("camera-pupil")) return fail("Pupil controls failed or discarded post settings");
    auto* lens_character = fixture->findChild<QObject*>("sceneLensCharacterCombo");
    if (!lens_character || !lens_character->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(lens_character,"activated",Q_ARG(int,1)) ||
        !edit_effect("sceneLens_sphericalDistortion","0.08") || !edit_effect("sceneLens_astigmatism","-0.5") || !edit_effect("sceneLens_fieldCurvature","0.15") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || !capture("camera-aberrations")) return fail("Aberration controls failed");
    if (!photo_section->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,1))) return fail("Focus page unavailable");
    auto* focus_mode = fixture->findChild<QObject*>("sceneFocusModeCombo");
    if (!focus_mode || !focus_mode->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(focus_mode,"activated",Q_ARG(int,1)) ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && !viewport->status().value("autofocus").toMap().isEmpty(); }) ||
        !capture("camera-autofocus")) return fail("Continuous autofocus did not resolve");
    const double initial_focus = viewport->status().value("autofocus").toMap().value("resolvedFocusDistance").toDouble();
    viewport->dolly(0.25);
    if (!wait_for([&] { return viewport->status().value("previewPhase") == "photo" && !viewport->status().value("autofocus").toMap().isEmpty(); }) ||
        (source_project.isEmpty() && std::abs(viewport->status().value("autofocus").toMap().value("resolvedFocusDistance").toDouble()-initial_focus) < 1e-5) ||
        !capture("camera-autofocus-moved")) return fail("Autofocus did not follow camera movement");
    const double automatic_focus = viewport->status().value("autofocus").toMap().value("resolvedFocusDistance").toDouble();
    if (!focus_mode->setProperty("currentIndex",0) || !QMetaObject::invokeMethod(focus_mode,"activated",Q_ARG(int,0)) ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
        std::abs(viewport->status().value("lensSettings").toMap().value("focusDistance").toDouble()-automatic_focus) > 1e-5 ||
        viewport->status().value("lensSettings").toMap().contains("focusMode")) return fail("Manual mode did not freeze the displayed autofocus distance");
    if (!focus_mode->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(focus_mode,"activated",Q_ARG(int,1)) ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && !viewport->status().value("autofocus").toMap().isEmpty(); })) return fail("Cannot restore continuous focus");
    const auto focus_pick_viewport_size=viewport->size();
    if (!click("scenePickFocusButton") || !wait_for([&] { return workspace->property("focusPickArmed").toBool() &&
        photo_popup->property("opened").toBool() && studio_geometry_valid() && viewport->size()==focus_pick_viewport_size; }))
      return fail("Manual picker did not retain the docked camera inspector and framing");
    const auto focus_point = viewport->mapToScene(QPointF(viewport->width()/2,viewport->height()/2)).toPoint();
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,focus_point);
    if (!wait_for([&] { return !viewport->status().value("focusPicking").toBool() && viewport->status().value("previewPhase") == "photo" && !viewport->status().value("lensSettings").toMap().contains("focusMode"); }) ||
        !viewport->status().value("focusPickResult").toMap().value("ok").toBool() || !capture("camera-autofocus-picked-manual")) return fail("Native focus pick did not switch autofocus to manual");
    if (!photo_popup->property("opened").toBool() ||
        !focus_mode->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(focus_mode,"activated",Q_ARG(int,1)) ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && !viewport->status().value("autofocus").toMap().isEmpty(); })) return fail("Could not resume autofocus after manual picking");
    pupil_settings = viewport->status().value("lensSettings").toMap();
    if (pupil_settings.value("schema") != "framehouse-thin-lens-v5") return fail("Pupil settings schema missing");
    // An edit in the Film page must retain the new lens schema too.
    if (!photo_section->setProperty("currentIndex",3) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,3)) ||
        !grain_slider->setProperty("value",0.12) || !QMetaObject::invokeMethod(grain_slider,"moved") ||
        viewport->status().value("lensSettings").toMap() != pupil_settings) return fail("Film edit discarded pupil settings");
    QMetaObject::invokeMethod(photo_popup,"close");
    if (!QMetaObject::invokeMethod(objects,"open") || !wait_for([&] { return objects->property("opened").toBool(); })) return fail("Cannot return to composition");
    const auto sphere_composition = viewport->status().value("sceneComposition").toMap();
    const auto object_photo = QDir(root).filePath("objects-photo.png");
    if (!viewport->exportPhoto(object_photo,800).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == object_photo; })) return fail("Object photo export failed");
    QFile object_receipt(object_photo+".camera.json");
    if (!object_receipt.open(QIODevice::ReadOnly)) return fail("Photo receipt unavailable");
    const auto photo_receipt = QJsonDocument::fromJson(object_receipt.readAll()).toVariant().toMap();
    if (photo_receipt.value("sceneObjects").toMap() != sphere_composition ||
        photo_receipt.value("camera").toMap().value("photo").toMap().value("tone").toMap() != accepted_tone ||
        photo_receipt.value("camera").toMap().value("photo").toMap().value("post").toMap() != accepted_post ||
        photo_receipt.value("authoredCamera").toMap().value("photo").toMap().value("lens").toMap() != pupil_settings ||
        photo_receipt.value("autofocus").toMap().isEmpty()) return fail("Photo lost frozen camera/composition");
    const auto exported_camera = gaussian_saved_camera(photo_receipt.value("camera").toMap());
    if (!exported_camera || exported_camera->photo.lens.auto_focus ||
        std::abs(exported_camera->photo.lens.focus_distance-photo_receipt.value("autofocus").toMap().value("resolvedFocusDistance").toDouble()) > 1e-5)
      return fail("Autofocus photo did not freeze its actual manual camera");
    if (!viewport->saveView("Object shot A").value("ok").toBool()) return fail("Cannot save object camera A");
    viewport->orbit(2,0);
    const auto saved_b = viewport->saveView("Object shot B");
    const auto views = viewport->status().value("savedViews").toList();
    if (!saved_b.value("ok").toBool() || views.size() != 2 ||
        !viewport->saveCameraPath({{"viewIds",QVariantList{views[0].toMap().value("id"),views[1].toMap().value("id")}}, {"segmentSeconds",0.5}, {"fps",24}}).value("ok").toBool()) return fail("Object camera path failed");
    const auto film = QDir(root).filePath("objects-film.mp4");
    if (!viewport->exportFilm(film,256).value("ok").toBool() || !click("sceneCompositionDeleteButton") ||
        !wait_for([&] { return !viewport->status().value("filmExporting").toBool(); }) || !viewport->status().value("filmResult").toMap().value("ok").toBool()) return fail("Frozen object film export failed");
    QFile film_receipt(film+".camera-path.json");
    if (!film_receipt.open(QIODevice::ReadOnly)) return fail("Film receipt unavailable");
    const auto frozen_film = QJsonDocument::fromJson(film_receipt.readAll()).toVariant().toMap();
    if (frozen_film.value("sceneObjects").toMap() != sphere_composition) return fail("Concurrent edit changed the accepted film composition");
    for (const auto& keyframe : frozen_film.value("keyframes").toList())
      if (keyframe.toMap().value("photo").toMap().value("tone").toMap() != accepted_tone ||
          keyframe.toMap().value("photo").toMap().value("post").toMap() != accepted_post || keyframe.toMap().value("photo").toMap().value("lens").toMap() != pupil_settings) return fail("Film lost camera tone/post settings");
    const auto focus_summary = viewport->status().value("filmResult").toMap().value("receipt").toMap().value("autofocus").toMap();
    if (focus_summary.contains("samples") || focus_summary.value("sampleCount").toInt() != 12) return fail("UI status duplicated the full autofocus trace");
    const auto focus_samples = frozen_film.value("autofocus").toMap().value("samples").toList();
    if (focus_samples.size() != 12 || frozen_film.value("schema") != "framehouse-gaussian-film-v3" ||
        std::abs(focus_samples[0].toMap().value("focusDistance").toDouble()-exported_camera->photo.lens.focus_distance) > 1e-3) return fail("Film did not record per-frame autofocus consistent with the photo");
    for (int i = 0; i < focus_samples.size(); ++i) if (focus_samples[i].toMap().value("frame").toInt() != i || focus_samples[i].toMap().value("focusDistance").toDouble() <= 0) return fail("Film autofocus trace is incomplete");
    if (!click("sceneCompositionUndoButton")) return fail("Cannot undo object removal");
    viewport->setSource({});
    if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); })) return fail("Object project reopen failed");
    viewport->setSource(controller.snapshot());
    if (!wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || viewport->status().value("sceneComposition").toMap() != sphere_composition || !capture("objects-reopened")) return fail("Objects or lights were lost on reopen");
    if (!viewport->restoreView(views[0].toMap().value("id").toString()).value("ok").toBool() ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
        viewport->status().value("photoSettings").toMap().value("post").toMap() != accepted_post ||
        viewport->status().value("photoSettings").toMap().value("tone").toMap() != accepted_tone ||
        viewport->status().value("lensSettings").toMap() != pupil_settings) return fail("Reopened saved shot lost post or pupil effects");
    if (!QMetaObject::invokeMethod(objects,"open") || !wait_for([&] { return objects->property("opened").toBool(); }) ||
        !click("sceneAddFloorButton") || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) || !capture("objects-floor")) return fail("Floor object failed");
    if (!section || !section->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(section,"activated",Q_ARG(int,1)) || !click("sceneAddPointLightButton") ||
        !wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("metrics").toMap().value("objectLightCount").toInt() == 4; }) || !capture("objects-lights")) return fail("Point light failed");
    window->resize(1024,760);
    if (!wait_for([&] { return window->width() == 1024 && studio_geometry_valid() && viewport->status().value("previewPhase") == "photo" && objects->property("opened").toBool(); }) || !capture("objects-compact")) return fail("Compact object controls failed");
    QMetaObject::invokeMethod(objects,"close");
    if (!click("scenePhotoSettingsButton") || !wait_for([&] { return photo_popup->property("opened").toBool(); }) ||
        !photo_section->setProperty("currentIndex",0) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,0)) ||
        !capture("camera-tone-compact") || !native_tone_edit("scenePhotoHighlightCompressionSlider",0.4) ||
        viewport->status().value("photoSettings").toMap().value("tone").toMap() == accepted_tone ||
        !capture("camera-tone-compact-pointer") ||
        !photo_section->setProperty("currentIndex",5) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,5)) ||
        !post_section->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(post_section,"activated",Q_ARG(int,1)) ||
        !capture("camera-glare-compact")) return fail("Compact post controls failed");
    auto* post_scroll = visual_item(window->contentItem(),"scenePhotoControlsScroll");
    auto* post_slider = visual_item(window->contentItem(),"scenePostSlider_glareAmount");
    if (!post_scroll || !post_slider || !post_slider->isVisible() || !post_slider->isEnabled()) return fail("Post slider unavailable");
    const QPointF hit = post_slider->mapToScene(QPointF(post_slider->width()*0.65,post_slider->height()/2));
    const QRectF scroll_bounds(post_scroll->mapToScene(QPointF()),QSizeF(post_scroll->width(),post_scroll->height()));
    if (!scroll_bounds.contains(hit)) return fail("Post slider clipped outside popup");
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
    if (!wait_for([&] { return viewport->status().value("previewPhase") == "photo" &&
        viewport->status().value("photoSettings").toMap().value("post").toMap().value("glareAmount").toDouble() > 2; }) ||
        !capture("camera-post-pointer")) return fail("Native post slider hit testing failed");
    // Scroll the aperture controls in the compact popup and exercise an actual
    // pointer drag; delegates must survive value changes throughout that drag.
    if (!photo_section->setProperty("currentIndex",2) || !QMetaObject::invokeMethod(photo_section,"activated",Q_ARG(int,2))) return fail("Compact pupil page unavailable");
    if (!lens_character->setProperty("currentIndex",0) || !QMetaObject::invokeMethod(lens_character,"activated",Q_ARG(int,0))) return fail("Cannot return to pupil controls");
    auto* pupil_slider = visual_item(window->contentItem(),"sceneLensSlider_apertureEdge");
    if (!pupil_slider || !post_scroll) return fail("Pupil slider unavailable");
    const auto scroll_origin = post_scroll->mapToScene(QPointF());
    const auto pupil_center = [&] { return pupil_slider->mapToScene(QPointF(pupil_slider->width()/2,pupil_slider->height()/2)); };
    const double scroll_max = std::max(0.0,post_scroll->property("contentHeight").toDouble()-post_scroll->height());
    const double scroll_y = std::clamp(post_scroll->property("contentY").toDouble()+pupil_center().y()-scroll_origin.y()-post_scroll->height()/2,0.0,scroll_max);
    if (!post_scroll->setProperty("contentY",scroll_y) || !capture("camera-pupil-compact") || !QRectF(post_scroll->mapToScene(QPointF()),QSizeF(post_scroll->width(),post_scroll->height())).contains(pupil_center())) return fail("Pupil controls clipped outside compact popup");
    const auto drag_start = pupil_slider->mapToScene(QPointF(pupil_slider->width()*0.38,pupil_slider->height()/2)).toPoint();
    const auto drag_end = pupil_slider->mapToScene(QPointF(pupil_slider->width()*0.72,pupil_slider->height()/2)).toPoint();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,drag_start);
    QTest::mouseMove(window,drag_end,40);
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,drag_end);
    if (!wait_for([&] { return viewport->status().value("previewPhase") == "photo" && viewport->status().value("lensSettings").toMap().value("apertureEdge").toDouble() > 5; }) ||
        !capture("camera-pupil-pointer")) return fail("Native pupil drag did not retain its delegate");
    if (source_project.isEmpty()) {
      // The synthetic fixture alone uses an explicitly supplied test scale;
      // this does not invent physical scale for a real captured scene.
      const QVariantMap physical{{"schema","framehouse-physical-thin-lens-v1"},{"metresPerSceneUnit",0.5},
          {"sensorWidthMm",36},{"sensorHeightMm",24},{"focalLengthMm",50},{"fNumber",2.8},{"focusDistanceM",2}};
      if (!viewport->setPhysicalCamera(physical).value("ok").toBool() || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; })) return fail("Physical pupil fixture failed");
      auto* distance_field = visual_item(window->contentItem(),"sceneLens_vignettingDistance");
      if (!distance_field || std::abs(distance_field->property("text").toDouble()-20) > 0.01 ||
          !edit_effect("sceneLens_vignettingDistance","30") ||
          !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
          std::abs(viewport->status().value("lensSettings").toMap().value("vignettingDistance").toDouble()-0.06) > 1e-6 ||
          !capture("camera-pupil-physical-mm")) return fail("Physical barrel distance did not roundtrip millimetres");
      if (!viewport->pickFocusAt(viewport->width()/2,viewport->height()/2).value("ok").toBool() ||
          !wait_for([&] { return !viewport->status().value("focusPicking").toBool() && viewport->status().value("previewPhase") == "photo" && !viewport->status().value("lensSettings").toMap().contains("focusMode"); }) ||
          !viewport->status().value("focusPickResult").toMap().value("ok").toBool() ||
          std::abs(viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()-viewport->status().value("focusPickResult").toMap().value("focusDistance").toDouble()*0.5) > 1e-5)
        return fail("Physical autofocus picking did not commit a consistent manual lens");
      if (!viewport->setFocusMode("auto-centre").value("ok").toBool() || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; })) return fail("Physical autofocus could not resume");
      auto rescaled_physical = physical; rescaled_physical.insert("metresPerSceneUnit",1.0);
      if (!viewport->setPhysicalCamera(rescaled_physical).value("ok").toBool() ||
          !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
          std::abs(distance_field->property("text").toDouble()-30) > 0.01 ||
          std::abs(viewport->status().value("lensSettings").toMap().value("vignettingDistance").toDouble()-0.03) > 1e-6)
        return fail("Changing measured scale changed the physical barrel distance");
      if (!click("sceneResetPhotoButton") || !wait_for([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
          viewport->status().value("lensSettings").toMap().contains("apertureEdge") || viewport->status().value("lensSettings").toMap().contains("astigmatism") ||
          viewport->status().value("physicalCamera").toMap().isEmpty()) return fail("Reset did not clear pupil character while retaining physical optics");
    }
    object_proof = true;
  }
  QVariantMap receipt{{"passed", true}, {"initialCount", initial_count}, {"cleanedCount", cleaned_count},
      {"checks", QStringList{"real-QML-rectangle-drag", "Metal-preview", "undo-redo", "PLY-export", "photo-provenance", "reopen", "filter", "restore-all", "compact-layout"}},
      {"sourceProject", source_project}, {"objectsAndLights",object_proof}, {"capturedLightingAndHaze",object_proof}, {"importedGeometry",object_proof}, {"importedMaterials",object_proof}, {"softLights",object_proof}, {"cameraPost",object_proof}, {"cameraTone",object_proof}, {"acceptedToneSettings",tone_settings}, {"continuousAutofocus",object_proof}, {"pupilControls",object_proof}, {"acceptedPupilSettings",pupil_settings}, {"photoSettings",viewport->status().value("photoSettings")}, {"composition",viewport->status().value("sceneComposition")}, {"metrics",viewport->status().value("metrics")}, {"scope", "SceneWorkspace component, not integrated Main.qml or photographic quality certification"}};
  if (lasso_workflow) {
    receipt.insert("lassoWorkflow",true);
    receipt.insert("checks",QStringList{"native-shape-choice","native-concave-lasso","malformed-rejection","escape-cancel","undo-redo","PLY-export","photo-provenance","reopen","compact-native-keep-lasso","restore-all"});
  }
  if (depth_lasso_workflow) {
    receipt.insert("depthRangeWorkflow",true); receipt.insert("depthFarSceneUnits",cleanup_cutoff);
    receipt.insert("depthMaskInvariantsPassed",true);
    auto checks = receipt.value("checks").toStringList(); checks.append({"native-depth-mode-and-field","remove-preserves-every-out-of-range-original","keep-excludes-every-out-of-range-original"}); receipt.insert("checks",checks);
  }
  if (!qml_warnings.isEmpty()) { for (const auto& warning : qml_warnings) std::cerr << warning.toStdString() << '\n'; return fail("Scene workflow emitted QML warnings"); }
  if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"), QJsonDocument::fromVariant(receipt).toJson())) return 2;
  std::cout << QJsonDocument::fromVariant(receipt).toJson().constData();
  if (args.contains("--inspect")) return app.exec();
  return 0;
}
