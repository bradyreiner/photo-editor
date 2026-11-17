#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickWindow>
#include <QTest>
#include <QThread>
#include <QUrl>

#include <cstdlib>
#include <iostream>

namespace {

class ManualIncubationController final : public QQmlIncubationController {};

int fail(const char* message) {
  std::cerr << "Timeline workspace Loader regression failed: " << message << '\n';
  return EXIT_FAILURE;
}

QString extract_async_expression(const QString& source, const QString& loader_marker) {
  const qsizetype loader_start = source.indexOf(loader_marker);
  if (loader_start < 0) return {};
  const qsizetype asynchronous_start = source.indexOf(QStringLiteral("asynchronous:"), loader_start);
  const qsizetype active_start = source.indexOf(QStringLiteral("active:"), asynchronous_start);
  if (asynchronous_start < 0 || active_start < 0) return {};
  return source.mid(asynchronous_start + QStringLiteral("asynchronous:").size(),
                    active_start - asynchronous_start - QStringLiteral("asynchronous:").size())
      .simplified();
}

bool drive_until(ManualIncubationController& controller, QObject* root,
                 const char* ready_property, int max_attempts) {
  for (int attempt = 0; attempt < max_attempts &&
       !root->property(ready_property).toBool(); ++attempt) {
    controller.incubateFor(1);
    QCoreApplication::processEvents();
  }
  return root->property(ready_property).toBool();
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication application(argc, argv);

  QFile main_qml(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/Main.qml"));
  QFile timeline_qml(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/TimelineView.qml"));
  if (!main_qml.open(QIODevice::ReadOnly | QIODevice::Text) ||
      !timeline_qml.open(QIODevice::ReadOnly | QIODevice::Text))
    return fail("Main.qml and TimelineView.qml should be readable for extracting production Loader bindings");
  const QString main_source = QString::fromUtf8(main_qml.readAll());
  const QString timeline_source = QString::fromUtf8(timeline_qml.readAll());
  const QString outer_async = extract_async_expression(
      main_source, QStringLiteral("objectName: \"timelineWorkspaceLoader\""));
  const QString inner_async = extract_async_expression(
      timeline_source, QStringLiteral("objectName: \"timelineEditorSurfaceLoader\""));
  if (outer_async != QStringLiteral(
          "timelineWorkspaceLoader.status === Loader.Loading || window.currentWorkspace !== window.timelineWorkspaceIndex"))
    return fail("The outer production binding should hold asynchronous mode while its Loader is Loading");
  if (inner_async != QStringLiteral(
          "timelineEditorPageLoader.status === Loader.Loading || !root.visible"))
    return fail("The inner production binding should hold asynchronous mode while its Loader is Loading");

  const QString fixture = QStringLiteral(R"QML(
import QtQuick
Window {
    id: window
    width: 320
    height: 180
    visible: true
    property int currentWorkspace: 0
    readonly property int timelineWorkspaceIndex: 1
    readonly property bool outerLoading: timelineWorkspaceLoader.status === Loader.Loading
    readonly property bool outerReady: timelineWorkspaceLoader.status === Loader.Ready
    readonly property bool outerAsync: timelineWorkspaceLoader.asynchronous
    readonly property bool innerLoading: timelineWorkspaceLoader.item
                                      ? timelineWorkspaceLoader.item.innerLoading : false
    readonly property bool innerReady: timelineWorkspaceLoader.item
                                    ? timelineWorkspaceLoader.item.innerReady : false
    readonly property bool innerAsync: timelineWorkspaceLoader.item
                                    ? timelineWorkspaceLoader.item.innerAsync : false

    Loader {
        id: timelineWorkspaceLoader
        objectName: "timelineWorkspaceLoader"
        anchors.fill: parent
        asynchronous: %1
        active: true
        visible: window.currentWorkspace === window.timelineWorkspaceIndex
        sourceComponent: Component {
            Item {
                id: root
                objectName: "incubatedTimelineWorkspace"
                readonly property bool innerLoading: timelineEditorPageLoader.status === Loader.Loading
                readonly property bool innerReady: timelineEditorPageLoader.status === Loader.Ready
                readonly property bool innerAsync: timelineEditorPageLoader.asynchronous

                Loader {
                    id: timelineEditorPageLoader
                    objectName: "timelineEditorSurfaceLoader"
                    anchors.fill: parent
                    asynchronous: %2
                    active: true
                    sourceComponent: Component {
                        Item {
                            objectName: "incubatedTimelineEditor"
                            Repeater {
                                model: 2048
                                delegate: Rectangle { width: 1; height: 1 }
                            }
                        }
                    }
                }
            }
        }
    }
}
)QML").arg(outer_async, inner_async);

  ManualIncubationController incubation_controller;
  QQmlEngine engine;
  engine.setIncubationController(&incubation_controller);
  QQmlComponent component(&engine);
  component.setData(fixture.toUtf8(), QUrl(QStringLiteral("inmemory:/timeline-workspace-loader-test.qml")));
  QElapsedTimer component_timer;
  component_timer.start();
  while (component.isLoading() && component_timer.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  if (!component.isReady()) {
    std::cerr << "component status: " << static_cast<int>(component.status()) << '\n';
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("The inline Loader test component should finish loading before creation");
  }
  QObject* root = component.create();
  if (root == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("The isolated Qt Quick Loader fixture should instantiate");
  }
  auto* window = qobject_cast<QQuickWindow*>(root);
  auto* outer_loader = root->findChild<QObject*>(QStringLiteral("timelineWorkspaceLoader"));
  if (window == nullptr || outer_loader == nullptr) {
    delete root;
    return fail("The fixture should expose its real window and outer Loader");
  }
  window->show();
  QTest::qWait(20);
  if (!root->property("outerLoading").toBool() || !root->property("outerAsync").toBool()) {
    delete root;
    return fail("Background Timeline prewarm should begin asynchronously");
  }

  bool outer_became_ready_with_inner_pending = false;
  for (int attempt = 0; attempt < 5000 && !root->property("outerReady").toBool(); ++attempt) {
    incubation_controller.incubateFor(1);
    QCoreApplication::processEvents();
    if (root->property("outerReady").toBool() && root->property("innerLoading").toBool())
      outer_became_ready_with_inner_pending = true;
  }
  if (!outer_became_ready_with_inner_pending) {
    delete root;
    return fail("The outer workspace should become Ready while its nested editor Loader is still incubating");
  }

  root->setProperty("currentWorkspace", 1);
  QCoreApplication::processEvents();
  if (!root->property("outerReady").toBool() || root->property("outerAsync").toBool() ||
      !root->property("innerLoading").toBool() || !root->property("innerAsync").toBool()) {
    delete root;
    return fail("Activating a prewarmed workspace must keep the nested Loading editor Loader asynchronous");
  }

  if (!drive_until(incubation_controller, root, "innerReady", 5000)) {
    delete root;
    return fail("The nested editor Loader should finish incubation within the bounded test window");
  }
  const bool completed = !root->property("innerAsync").toBool();
  delete root;
  if (!completed)
    return fail("The nested editor Loader should become synchronous only after reaching Ready");

  std::cout << "Outer Timeline and nested editor Loaders stay asynchronous until their own Ready states\n";
  return EXIT_SUCCESS;
}
