#include <QGuiApplication>
#include <QQmlError>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStringList>
#include <QThread>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto fail = [](int line) {
    std::cerr << "Account panel assertion failed at line " << line << '\n';
    return 1;
  };
  QQmlEngine engine;
  QStringList qmlWarnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                   [&qmlWarnings](const QList<QQmlError>& warnings) {
                     for (const auto& warning : warnings) qmlWarnings.append(warning.toString());
                   });
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
    property bool signedIn: false
    property bool canSignOut: false
    property bool emailCodePending: false
    property string accountEmail: ""
    property var accountLicenses: []
    property string emailRequested: ""
    property string emailVerified: ""
    property string vipRedeemed: ""
    property string checkoutOffer: ""
    property bool portalOpened: false
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
    function requestEmailSignIn(email) { emailRequested=email; emailCodePending=true }
    function verifyEmailSignIn(code) { emailVerified=code; canSignOut=true; signedIn=true; emailCodePending=false; accountEmail="owner@example.test" }
    function cancelEmailSignIn() { emailCodePending=false }
    function redeemVip(code) { vipRedeemed=code }
    function signOut() { canSignOut=false; signedIn=false; accountEmail=""; accountLicenses=[] }
    function selectAccountLicense(id) {}
    function startCheckout(offer) { checkoutOffer=offer }
    function openBillingPortal() { portalOpened=true }
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
  if (!window) { std::cerr << component.errorString().toStdString(); return fail(__LINE__); }
  const auto settle = [] { for (int i = 0; i < 12; ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5); } };
  settle();
  auto* access = window->findChild<QObject*>("access");
  if (!access) return fail(__LINE__);
  const auto capture = [&](const char* variable) {
    const auto output = qEnvironmentVariable(variable);
    return output.isEmpty() || window->grabWindow().save(output);
  };
  if (!capture("LOOKSY_TEST_ACCOUNT_GUEST_IMAGE")) return fail(__LINE__);
  auto* email = window->findChild<QObject*>("customerAccountEmail");
  auto* code = window->findChild<QObject*>("customerEmailCode");
  auto* vip = window->findChild<QObject*>("customerVipCode");
  auto* send = window->findChild<QObject*>("customerSendEmailCode");
  auto* verify = window->findChild<QObject*>("customerVerifyEmailCode");
  auto* redeem = window->findChild<QObject*>("customerRedeemVipCode");
  auto* signOut = window->findChild<QObject*>("customerSignOut");
  if (!email || !code || !vip || !send || !verify || !redeem || !signOut) return fail(__LINE__);
  email->setProperty("text", "owner@example.test");
  if (!QMetaObject::invokeMethod(send, "clicked")) return fail(__LINE__);
  settle();
  if (!code->property("visible").toBool() || !access->property("emailCodePending").toBool()) return fail(__LINE__);
  code->setProperty("text", "12345678");
  if (!QMetaObject::invokeMethod(verify, "clicked")) return fail(__LINE__);
  settle();
  if (!access->property("signedIn").toBool() || !code->property("text").toString().isEmpty() ||
      !vip->property("visible").toBool()) return fail(__LINE__);
  vip->setProperty("text", "synthetic-vip");
  if (!QMetaObject::invokeMethod(redeem, "clicked")) return fail(__LINE__);
  settle();
  if (access->property("vipRedeemed").toString() != "synthetic-vip" || !vip->property("text").toString().isEmpty()) return fail(__LINE__);
  auto* purchase = window->findChild<QObject*>("customerPurchaseLink");
  auto* subscription = window->findChild<QObject*>("customerSubscriptionLink");
  auto* portal = window->findChild<QObject*>("customerBillingPortalLink");
  if (!purchase || !subscription || !portal || !purchase->property("visible").toBool() ||
      !subscription->property("visible").toBool() || !portal->property("visible").toBool()) return fail(__LINE__);
  if (!QMetaObject::invokeMethod(purchase, "clicked") || access->property("checkoutOffer").toString() != "one-time" ||
      !QMetaObject::invokeMethod(subscription, "clicked") || access->property("checkoutOffer").toString() != "subscription" ||
      !QMetaObject::invokeMethod(portal, "clicked") || !access->property("portalOpened").toBool()) return fail(__LINE__);
  if (!capture("LOOKSY_TEST_ACCOUNT_EMAIL_IMAGE")) return fail(__LINE__);
  if (!QMetaObject::invokeMethod(signOut, "clicked")) return fail(__LINE__);
  settle();
  access->setProperty("canSignOut", true);
  settle();
  auto* localSignOut = window->findChild<QObject*>("customerLocalSignOut");
  if (!localSignOut || !localSignOut->property("visible").toBool() ||
      !QMetaObject::invokeMethod(localSignOut, "clicked") || access->property("canSignOut").toBool()) return fail(__LINE__);
  auto* keyToggle = window->findChild<QObject*>("customerShowPurchaseKey");
  if (!keyToggle || !QMetaObject::invokeMethod(keyToggle, "clicked")) return fail(__LINE__);
  settle();
  auto* field = window->findChild<QObject*>("customerLicenseKey");
  if (!field || !field->property("visible").toBool() || field->property("echoMode").toInt() != 2) return fail(__LINE__);
  field->setProperty("text", "synthetic-test-key");
  if (!QMetaObject::invokeMethod(field, "accepted")) return fail(__LINE__);
  settle();
  if (!field->property("text").toString().isEmpty()) return fail(__LINE__);
  if (!capture("LOOKSY_TEST_ACCOUNT_IMAGE")) return fail(__LINE__);
  auto* recover = window->findChild<QObject*>("recoverCustomerAccess");
  if (!recover || purchase->property("visible").toBool() || subscription->property("visible").toBool() ||
      portal->property("visible").toBool()) return fail(__LINE__);
  if (access->property("devices").toList().size() != 2 ||
      access->property("currentInstallationId").toString() != "device-current") return fail(__LINE__);
  auto* deactivate = window->findChild<QObject*>("deactivateCurrentDeviceAction");
  if (!deactivate || !QMetaObject::invokeMethod(deactivate, "clicked")) return fail(__LINE__);
  settle();
  auto* deactivateDialog = window->findChild<QObject*>("deactivateCurrentDeviceDialog");
  const auto confirmCapture = qEnvironmentVariable("LOOKSY_TEST_ACCOUNT_DEACTIVATE_CONFIRM_IMAGE");
  if (!deactivateDialog || !deactivateDialog->property("visible").toBool() ||
      (!confirmCapture.isEmpty() && !window->grabWindow().save(confirmCapture))) return fail(__LINE__);
  if (!QMetaObject::invokeMethod(deactivateDialog, "reject")) return fail(__LINE__);
  field->setProperty("text", "synthetic-recovery-key");
  if (!QMetaObject::invokeMethod(access, "revokeForTest")) return fail(__LINE__);
  settle();
  if (access->property("devices").toList().size() != 0 ||
      !access->property("currentInstallationId").toString().isEmpty() || !capture("LOOKSY_TEST_ACCOUNT_REVOKED_IMAGE")) return fail(__LINE__);
  if (!QMetaObject::invokeMethod(recover, "clicked")) return fail(__LINE__);
  settle();
  if (access->property("recovered").toString() != "synthetic-recovery-key" ||
      !field->property("text").toString().isEmpty()) return fail(__LINE__);
  QObject* panel = window->findChild<QObject*>("customerAccountPanel");
  if (!panel) return fail(__LINE__);
  panel->setProperty("controller", QVariant::fromValue(static_cast<QObject*>(nullptr)));
  settle();
  if (deactivate->property("visible").toBool() || deactivate->property("enabled").toBool()) return fail(__LINE__);
  for (const auto& warning : qmlWarnings) {
    if (warning.contains("null", Qt::CaseInsensitive) || warning.contains("TypeError") ||
        warning.contains("ReferenceError")) {
      std::cerr << warning.toStdString() << '\n';
      return fail(__LINE__);
    }
  }
  std::cout << "Account panel exercises email signup and recovery, secret-field clearing, VIP redemption, guest, paid-device, revoked-recovery, and unavailable-service states without a null-controller warning.\n";
}
