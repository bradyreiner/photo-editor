#include <QAccessible>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QQmlEngine engine;
  QQmlPropertyMap shell_settings;
  shell_settings.insert("interfaceScale", 1.0);
  shell_settings.insert("reducedMotion", true);
  engine.rootContext()->setContextProperty("appShell", &shell_settings);
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
  width: 660; height: 560; visible: true; color: "#161b20"
  QtObject {
    id: fixtureController
    objectName: "fixtureController"
    property var availableModels: [{id: "managed-vision", name: "Qwen3-VL 32B", model: "qwen3-vl:32b",
      runtime: "managed", runtimeState: "ready", available: false, statusReason: "Download the selected vision model to continue."}]
    property string selectedModelId: "managed-vision"
    property bool modelInstallRunning: false
    property bool modelProbeRunning: false
    property string modelInstallStatus: "Install failed"
    property string modelInstallOutput: "The registry manifest does not match the approved model pin. No model blobs were downloaded."
    property var modelInstallProgress: ({status: "failed", ollamaModel: "qwen3-vl:32b"})
    property double cachedBytes: 20000000000
    signal modelInstallChanged()
    function prepareSelectedModelInstall() { return {modelId: selectedModelId, runtime: "managed", installable: true,
      artifactBytes: 20910287340, estimatedCachedBytes: cachedBytes, requiredFreeBytes: 21984029164 - cachedBytes, license: "Apache-2.0"} }
    function installSelectedModel(confirmed) {
      if (!confirmed) return {errors: ["Confirmation required"]}
      modelInstallRunning = true
      modelInstallStatus = "Checking pinned model manifest"
      modelInstallProgress = {status: "starting", ollamaModel: "qwen3-vl:32b"}
      modelInstallChanged()
      return {ok: true}
    }
    function cancelModelInstall() {}
    function retryLocalRuntime() {}
  }
  Button {
    objectName: "setupDialogOpener"
    text: "Open local AI setup"
    anchors.centerIn: parent
  }
  AgentModelSetupDialog { id: setup; parent: Overlay.overlay; controller: fixtureController }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/model-setup-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return EXIT_FAILURE; }
  auto* dialog = window->findChild<QObject*>("agentModelSetupDialog");
  auto* opener = qobject_cast<QQuickItem*>(window->findChild<QObject*>("setupDialogOpener"));
  auto* close_button = window->findChild<QObject*>("modelSetupCloseButton");
  auto* detail = window->findChild<QObject*>("modelSetupStatusDetail");
  auto* controller = window->findChild<QObject*>("fixtureController");
  auto* storage = window->findChild<QObject*>("modelSetupStorageDetail");
  if (!dialog || !opener || !close_button || !detail || !controller || !storage ||
      dialog->property("opened").toBool()) return fail("Setup dialog fixture objects should exist and start closed");
  window->show();
  opener->forceActiveFocus(Qt::TabFocusReason);
  QElapsedTimer focus_settle;
  focus_settle.start();
  while (window->activeFocusItem() != opener && focus_settle.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (window->activeFocusItem() != opener)
    return fail("Setup dialog opener should receive focus before presentation");
  if (!QMetaObject::invokeMethod(dialog, "present"))
    return fail("Setup dialog should expose its present operation");
  QElapsedTimer settle;
  settle.start();
  while (dialog && !dialog->property("opened").toBool() && settle.elapsed() < 2000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (!dialog->property("opened").toBool() || window->activeFocusItem() != close_button ||
      !close_button->property("visualFocus").toBool() ||
      detail->property("text") != controller->property("modelInstallOutput"))
    return fail("Setup dialog should open with visible focus on Close and display the current install error");
  auto* accessible_background = qvariant_cast<QQuickItem*>(dialog->property("background"));
  auto* dialog_accessible = QAccessible::queryAccessibleInterface(accessible_background);
  auto* close_accessible = QAccessible::queryAccessibleInterface(close_button);
  if (dialog_accessible == nullptr || dialog_accessible->role() != QAccessible::Dialog ||
      dialog_accessible->text(QAccessible::Name) != QStringLiteral("Local AI setup") ||
      close_accessible == nullptr || close_accessible->text(QAccessible::Name) != QStringLiteral("Close"))
    return fail("Setup dialog and Close action should expose accessible roles and names");
  if (!storage->property("text").toString().contains("20.0 GB cached; checked before reuse") ||
      !storage->property("text").toString().contains("1.98 GB free"))
    return fail("Setup dialog should report cached and remaining model storage");
  auto* details_scroll = dialog->findChild<QQuickItem*>("modelSetupDetailsScroll");
  auto* download = dialog->findChild<QQuickItem*>("confirmModelDownloadButton");
  auto* close_item = qobject_cast<QQuickItem*>(close_button);
  if (!details_scroll || !download || !close_item)
    return fail("Setup should provide separate scrollable details and persistent actions");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_MODEL_SETUP_IMAGE");
  if (!output.isEmpty() && !window->grabWindow().save(output))
    return fail("Setup dialog capture should save when requested");
  dialog->setProperty("actionError", QString("Download could not finish. Check your local model server and try again.\n").repeated(18));
  for (const auto& size : {QSize(540, 420), QSize(1024, 760)}) {
    shell_settings.insert("interfaceScale", size.width() == 1024 ? 2.0 : 1.0);
    window->resize(size);
    settle.restart();
    while (settle.elapsed() < 200) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
    const QRectF window_bounds(0, 0, window->width(), window->height());
    const auto bounds = [](QQuickItem* item) { return item->mapRectToScene(item->boundingRect()); };
    if (!window_bounds.contains(bounds(close_item)) || !window_bounds.contains(bounds(download)) ||
        !window_bounds.contains(bounds(details_scroll)) || !close_item->isVisible() || !download->isVisible() ||
        bounds(details_scroll).intersects(bounds(close_item)) ||
        bounds(details_scroll).intersects(bounds(download)))
      return fail("Long setup errors should scroll without covering Close or Download at compact and enlarged UI sizes");
    auto* flickable = qvariant_cast<QQuickItem*>(details_scroll->property("contentItem"));
    if (!flickable || flickable->property("contentHeight").toDouble() <= flickable->height())
      return fail("Setup should retain overflow details inside a scrollable viewport");
    details_scroll->forceActiveFocus(Qt::TabFocusReason);
    const auto before_scroll = flickable->property("contentY").toDouble();
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &down);
    QCoreApplication::processEvents();
    if (flickable->property("contentY").toDouble() <= before_scroll)
      return fail("Overflow setup details should be reachable with the keyboard");
    if (!output.isEmpty()) {
      const QFileInfo target(output);
      const auto variant = target.dir().filePath(target.completeBaseName() + QString("-%1.png").arg(size.width()));
      if (!window->grabWindow().save(variant)) return fail("Compact setup review capture should save");
    }
  }
  dialog->setProperty("actionError", "");
  shell_settings.insert("interfaceScale", 1.0);
  window->resize({660, 560});
  QCoreApplication::processEvents();
  controller->setProperty("cachedBytes", 0.0);
  QMetaObject::invokeMethod(controller, "modelInstallChanged");
  QCoreApplication::processEvents();
  if (storage->property("text").toString().contains("GB cached") ||
      !storage->property("text").toString().contains("21.98 GB free"))
    return fail("Setup dialog should refresh its free-storage estimate");
  auto* retry = window->findChild<QObject*>("confirmModelDownloadButton");
  if (!retry || !retry->property("enabled").toBool() || !QMetaObject::invokeMethod(retry, "clicked"))
    return fail("Setup dialog should allow confirmed retry when the install plan is valid");
  QCoreApplication::processEvents();
  if (!controller->property("modelInstallRunning").toBool() || detail->property("text") != "Checking pinned model manifest")
    return fail("Setup retry should show the checking state and start the install controller");
  controller->setProperty("modelInstallRunning", false);
  controller->setProperty("modelInstallProgress", QVariantMap{{"status", "failed"}, {"ollamaModel", "another-model"}});
  QMetaObject::invokeMethod(controller, "modelInstallChanged");
  QCoreApplication::processEvents();
  if (detail->property("text") == controller->property("modelInstallOutput"))
    return fail("A failure for another model should not replace the selected model status");
  QKeyEvent escape_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &escape_press);
  QKeyEvent escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &escape_release);
  settle.restart();
  while (dialog->property("visible").toBool() && settle.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  settle.restart();
  while ((window->activeFocusItem() != opener || !opener->property("visualFocus").toBool()) &&
         settle.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (dialog->property("visible").toBool() || window->activeFocusItem() != opener ||
      !opener->property("visualFocus").toBool()) {
    auto* active_focus = window->activeFocusItem();
    auto* saved_focus = qvariant_cast<QQuickItem*>(dialog->property("focusReturnItem"));
    std::cerr << "Escape focus return state: visible=" << dialog->property("visible").toBool()
              << " active=" << (active_focus ? active_focus->objectName().toStdString() : "<none>")
              << " expected=" << opener->objectName().toStdString()
              << " saved=" << (saved_focus ? saved_focus->objectName().toStdString() : "<none>")
              << " visual=" << opener->property("visualFocus").toBool() << '\n';
    return fail("Escape should dismiss setup and return visible focus to its opener");
  }
  if (!QMetaObject::invokeMethod(dialog, "present"))
    return fail("Setup dialog should reopen after dismissal");
  settle.restart();
  while (!dialog->property("opened").toBool() && settle.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (!dialog->property("opened").toBool() || window->activeFocusItem() != close_button ||
      !close_button->property("visualFocus").toBool()) {
    auto* active_focus = window->activeFocusItem();
    auto* saved_focus = qvariant_cast<QQuickItem*>(dialog->property("focusReturnItem"));
    std::cerr << "Reopened setup focus state: opened=" << dialog->property("opened").toBool()
              << " active=" << (active_focus ? active_focus->objectName().toStdString() : "<none>")
              << " expected=" << close_button->objectName().toStdString()
              << " saved=" << (saved_focus ? saved_focus->objectName().toStdString() : "<none>")
              << " visual=" << close_button->property("visualFocus").toBool() << '\n';
    return fail("Setup dialog should show focus on Close after reopening");
  }
  opener->deleteLater();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QKeyEvent fallback_escape_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &fallback_escape_press);
  QKeyEvent fallback_escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &fallback_escape_release);
  settle.restart();
  while (dialog->property("visible").toBool() && settle.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  auto* focus_fallback = qvariant_cast<QQuickItem*>(dialog->property("focusFallbackItem"));
  if (dialog->property("visible").toBool() || window->activeFocusItem() != focus_fallback) {
    return fail("Escape should fall back to window focus after the opener is removed");
  }
  std::cout << "Setup card: dialog semantics, entry focus, Escape focus return and fallback, download error, confirmed retry, and other-model failure exclusion passed.\n";
  return EXIT_SUCCESS;
}
