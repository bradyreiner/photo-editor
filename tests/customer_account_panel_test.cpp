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
  width: 860; height: 780; visible: true; color: "#171718"
  QtObject {
    id: access
    objectName: "access"
    property bool configured: true
    property bool active: false
    property bool busy: false
    property string status: "Start a guest trial or activate your purchase."
    property string plan: ""
    property string received: ""
    property string recovered: ""
    property string currentInstallationId: ""
    property string purchaseUrl: "https://store.looksy.test/buy"
    property string subscriptionPurchaseUrl: "https://store.looksy.test/subscribe"
    property string billingPortalUrl: "https://store.looksy.test/billing"
    property var devices: []
    function activate(key) {
      received=key; active=true; plan="Studio"; status="Your purchase is active.";
      currentInstallationId="device-current";
      devices=[
        { "id": "device-current", "platform": "mac-arm", "app_version": "0.1.0", "revoked": false },
        { "id": "device-old", "platform": "windows", "app_version": "0.0.8", "revoked": false }
      ];
    }
    function recoverAccess(key) { recovered=key; status="Your purchase was recovered." }
    function startTrial() { active=true; plan="Guest trial"; status="Your trial is active." }
    function refresh() {}
    function loadDevices() {}
    function revokeDevice(id) { received=id }
    function deactivateCurrentDevice() {}
    function recoverPurchase() {}
    function revokeForTest() {
      active=false; currentInstallationId=""; devices=[];
      status="This installation was revoked. Enter your purchase key to recover access.";
    }
  }
  CustomerAccountPanel { anchors.fill: parent; anchors.margins: 28; controller: access }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/account-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return 1; }
  const auto settle = [] { for (int i = 0; i < 12; ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5); } };
  settle();
  auto* access = window->findChild<QObject*>("access");
  if (!access) return 1;
  const auto capture = [&](const char* variable) {
    const auto output = qEnvironmentVariable(variable);
    return output.isEmpty() || window->grabWindow().save(output);
  };
  if (!capture("LOOKSY_TEST_ACCOUNT_GUEST_IMAGE")) return 1;
  auto* field = window->findChild<QObject*>("customerLicenseKey");
  if (!field || !field->property("visible").toBool() || field->property("echoMode").toInt() != 2) return 1;
  field->setProperty("text", "synthetic-test-key");
  if (!QMetaObject::invokeMethod(field, "accepted")) return 1;
  settle();
  if (!field->property("text").toString().isEmpty()) return 1;
  if (!capture("LOOKSY_TEST_ACCOUNT_IMAGE")) return 1;
  auto* recover = window->findChild<QObject*>("recoverCustomerAccess");
  auto* purchase = window->findChild<QObject*>("customerPurchaseLink");
  auto* subscription = window->findChild<QObject*>("customerSubscriptionLink");
  auto* portal = window->findChild<QObject*>("customerBillingPortalLink");
  if (!recover || !purchase || !subscription || !portal || !purchase->property("visible").toBool() ||
      !subscription->property("visible").toBool() || !portal->property("visible").toBool()) return 1;
  if (access->property("devices").toList().size() != 2 ||
      access->property("currentInstallationId").toString() != "device-current") return 1;
  auto* deactivate = window->findChild<QObject*>("deactivateCurrentDeviceAction");
  if (!deactivate || !QMetaObject::invokeMethod(deactivate, "clicked")) return 1;
  settle();
  auto* deactivateDialog = window->findChild<QObject*>("deactivateCurrentDeviceDialog");
  const auto confirmCapture = qEnvironmentVariable("LOOKSY_TEST_ACCOUNT_DEACTIVATE_CONFIRM_IMAGE");
  if (!deactivateDialog || !deactivateDialog->property("visible").toBool() ||
      (!confirmCapture.isEmpty() && !window->grabWindow().save(confirmCapture))) return 1;
  if (!QMetaObject::invokeMethod(deactivateDialog, "reject")) return 1;
  field->setProperty("text", "synthetic-recovery-key");
  if (!QMetaObject::invokeMethod(access, "revokeForTest")) return 1;
  settle();
  if (access->property("devices").toList().size() != 0 ||
      !access->property("currentInstallationId").toString().isEmpty() || !capture("LOOKSY_TEST_ACCOUNT_REVOKED_IMAGE")) return 1;
  if (!QMetaObject::invokeMethod(recover, "clicked")) return 1;
  settle();
  if (access->property("recovered").toString() != "synthetic-recovery-key" ||
      !field->property("text").toString().isEmpty()) return 1;
  std::cout << "Account panel renders guest, paid-device and revoked-recovery states; masks and clears purchase keys.\n";
}
