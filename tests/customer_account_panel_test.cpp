#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QThread>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
  width: 640; height: 440; visible: true; color: "#171718"
  QtObject {
    id: access
    property bool configured: true
    property bool active: false
    property bool busy: false
    property string status: "Start a guest trial or activate your purchase."
    property string plan: ""
    property string received: ""
    function activate(key) { received=key; active=true; plan="Studio"; status="Your purchase is active." }
    function startTrial() { active=true; plan="Guest trial"; status="Your trial is active." }
    function refresh() {}
  }
  CustomerAccountPanel { anchors.fill: parent; anchors.margins: 28; controller: access }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/account-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return 1; }
  const auto settle = [] { for (int i = 0; i < 12; ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5); } };
  settle();
  auto* field = window->findChild<QObject*>("customerLicenseKey");
  if (!field || !field->property("visible").toBool() || field->property("echoMode").toInt() != 2) return 1;
  field->setProperty("text", "synthetic-test-key");
  if (!QMetaObject::invokeMethod(field, "accepted")) return 1;
  settle();
  if (!field->property("text").toString().isEmpty()) return 1;
  const auto output = qEnvironmentVariable("LOOKSY_TEST_ACCOUNT_IMAGE");
  if (!output.isEmpty() && !window->grabWindow().save(output)) return 1;
  std::cout << "Account panel renders, masks license input, activates and clears the entered key.\n";
}
