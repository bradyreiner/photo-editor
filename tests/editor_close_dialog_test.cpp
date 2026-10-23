#include "qml_popup_wait.h"
#include "editor_close_coordinator.h"
#include <QAccessible>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  const auto fail = [](const char* message) {
    std::cerr << message << '\n';
    return 1;
  };
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  EditorCloseCoordinator close(nullptr, [](QString* error) {
    *error = "The recovery checkpoint could not be written. Your latest edits are still in memory. Check available disk space and retry.";
    return false;
  }, [](bool) {});
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
  id: fixture
  width: 640; height: 380; visible: true; color: "#161b20"
  property var coordinator
  onClosing: function(event) { event.accepted = coordinator.handleWindowClose() }
  Button {
    objectName: "editorCloseOpener"
    text: "Focused control"
    anchors.centerIn: parent
  }
  EditorCloseDialog { parent: Overlay.overlay; coordinator: fixture.coordinator }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/close-dialog-fixture.qml")));
  std::unique_ptr<QObject> object(component.createWithInitialProperties({{"coordinator", QVariant::fromValue(&close)}}));
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return fail("Close dialog fixture should load"); }
  const auto settle = [&] { for (int i = 0; i < 10; ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5); } wait_for_popup_transition(window->findChild<QObject*>("editorCloseDialog")); };
  auto* opener = qobject_cast<QQuickItem*>(window->findChild<QObject*>("editorCloseOpener"));
  if (!opener) return fail("Close dialog fixture should contain its opener");
  window->show();
  opener->forceActiveFocus(Qt::TabFocusReason);
  settle();
  if (window->activeFocusItem() != opener) return fail("Close dialog opener should receive focus before the request");
  if (window->close()) return fail("A failed checkpoint should keep the window open");
  settle();
  auto* dialog = window->findChild<QObject*>("editorCloseDialog");
  auto* detail = window->findChild<QObject*>("editorCloseDetail");
  auto* keep = window->findChild<QObject*>("editorCloseKeepEditing");
  if (!window->isVisible() || !dialog || !dialog->property("opened").toBool() ||
      !detail || detail->property("text") != close.error() || !keep ||
      window->activeFocusItem() != keep || !keep->property("visualFocus").toBool())
    return fail("Close dialog should expose the recovery error and focus Keep editing on entry");
  auto* accessible_background = qvariant_cast<QQuickItem*>(dialog->property("background"));
  auto* dialog_accessible = QAccessible::queryAccessibleInterface(accessible_background);
  auto* keep_accessible = QAccessible::queryAccessibleInterface(keep);
  if (dialog_accessible == nullptr || dialog_accessible->role() != QAccessible::Dialog ||
      dialog_accessible->text(QAccessible::Name) != QStringLiteral("Keep this window open") ||
      dialog_accessible->text(QAccessible::Description) != close.error() ||
      keep_accessible == nullptr || keep_accessible->text(QAccessible::Name) != QStringLiteral("Keep editing"))
    return fail("Close dialog should expose its accessible role, error, and named Keep editing action");
  QKeyEvent escape_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &escape_press);
  QKeyEvent escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &escape_release);
  settle();
  if (!dialog->property("opened").toBool() || window->activeFocusItem() != keep)
    return fail("Escape should remain contained while the checkpoint error is unresolved");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_CLOSE_IMAGE");
  if (!output.isEmpty() && !window->grabWindow().save(output))
    return fail("Close dialog visual proof should save its capture");
  if (!keep || !QMetaObject::invokeMethod(keep, "clicked"))
    return fail("Keep editing should remain invokable");
  settle();
  QElapsedTimer focus_return;
  focus_return.start();
  while ((dialog->property("opened").toBool() || window->activeFocusItem() != opener ||
          !opener->property("visualFocus").toBool()) && focus_return.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (!window->isVisible() || dialog->property("opened").toBool() || !close.error().isEmpty() ||
      window->activeFocusItem() != opener || !opener->property("visualFocus").toBool()) {
    auto* active_focus = window->activeFocusItem();
    std::cerr << "Close dialog focus return state: dialog-open="
              << dialog->property("opened").toBool()
              << " active=" << (active_focus ? active_focus->objectName().toStdString() : "<none>")
              << " expected=" << opener->objectName().toStdString()
              << " visual=" << opener->property("visualFocus").toBool()
              << " error=" << close.error().toStdString() << '\n';
    return fail("Keep editing should close the dialog and restore visible focus to its opener");
  }
  std::cout << "Window close blocked; recovery error semantics, safe entry focus, Escape containment, Keep editing, and focus return passed.\n";
}
