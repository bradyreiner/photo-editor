#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QFile source(QStringLiteral(RETOUCH_TEST_ROOT "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  if (!source.open(QIODevice::ReadOnly)) return 1;
  const auto workspace = QString::fromUtf8(source.readAll());
  QString functions;
  for (const auto* name : {"retouchHudPrimaryText", "retouchHudPrimaryTooltip", "triggerRetouchPrimaryAction"}) {
    const auto start = workspace.indexOf(QStringLiteral("    function %1(").arg(name));
    const auto end = workspace.indexOf(QStringLiteral("\n    function "), start + 1);
    if (start < 0 || end < 0) return 1;
    functions += workspace.mid(start, end - start);
  }
  QQmlEngine engine;
  QQmlComponent component(&engine);
  const auto qml = QStringLiteral(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
Item {
    id: root
    width: 780; height: 160
    property alias editorPreview: controller
    property int exits: 0
    function completeRetouchTool() { exits++ }
    function activateEditTool(tool) { exits++ }
    QtObject {
        id: controller
        objectName: "controller"
        property bool hasActiveMask: true
        property string activeMaskToolRole: "retouch"
        property bool activeMaskCommitted: true
        property bool inpaintProgressActive: true
        property int cancellations: 0
        property int applications: 0
        function cancelActiveRetouch() { cancellations++; inpaintProgressActive = false }
        function applyActiveRetouch() { applications++ }
    }
%1
    ToolCommandBar {
        x: 20; y: 50; width: 740
        toolLabel: "Retouch"; modeText: "Remove"
        statusText: controller.inpaintProgressActive ? "Generating replacement" : "Previous patch preserved"
        primaryText: root.retouchHudPrimaryText()
        primaryIconName: controller.inpaintProgressActive ? "close" : "check"
        primaryTooltip: root.retouchHudPrimaryTooltip()
        primaryButtonObjectName: "primary"
        onPrimaryTriggered: root.triggerRetouchPrimaryAction()
    }
}
)QML").arg(functions);
  component.setData(qml.toUtf8(), QUrl::fromLocalFile(QStringLiteral(RETOUCH_TEST_ROOT "/tests/retouch-harness.qml")));
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return 1; }
  QQuickWindow window;
  window.resize(780, 160);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show(); window.requestActivate();
  const auto settle = [] {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 200) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  };
  settle();
  auto* controller = root->findChild<QObject*>("controller");
  auto* primary = root->findChild<QQuickItem*>("primary");
  if (!primary || primary->property("text").toString() != "Cancel") return 1;
  const auto output = qEnvironmentVariable("PHOTO_RETOUCH_UI_OUTPUT");
  if (!output.isEmpty() && !window.grabWindow().save(output)) return 1;
  const auto click = [&] {
    const auto point = primary->mapToScene(QPointF(primary->width()/2, primary->height()/2));
    QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &release);
    settle();
  };
  click();
  if (controller->property("cancellations").toInt()!=1 || primary->property("text").toString()!="Done") return 1;
  controller->setProperty("activeMaskCommitted", false);
  controller->setProperty("inpaintProgressActive", true);
  settle(); click();
  if (controller->property("cancellations").toInt()!=2 || primary->property("text").toString()!="Apply") return 1;
  click();
  if (controller->property("applications").toInt()!=1) return 1;
  std::cout << "Native retouch primary action cancels draft and regeneration, then restores Apply/Done\n";
  return 0;
}
