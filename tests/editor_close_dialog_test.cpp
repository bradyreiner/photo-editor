#include "editor_close_coordinator.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
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
  EditorCloseDialog { parent: Overlay.overlay; coordinator: fixture.coordinator }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/close-dialog-fixture.qml")));
  std::unique_ptr<QObject> object(component.createWithInitialProperties({{"coordinator", QVariant::fromValue(&close)}}));
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return 1; }
  const auto settle = [] { for (int i = 0; i < 10; ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5); } };
  settle();
  if (window->close()) return 1;
  settle();
  auto* dialog = window->findChild<QObject*>("editorCloseDialog");
  auto* detail = window->findChild<QObject*>("editorCloseDetail");
  if (!window->isVisible() || !dialog || !dialog->property("opened").toBool() ||
      !detail || detail->property("text") != close.error()) return 1;
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_CLOSE_IMAGE");
  if (!output.isEmpty() && !window->grabWindow().save(output)) return 1;
  auto* keep = window->findChild<QObject*>("editorCloseKeepEditing");
  if (!keep || !QMetaObject::invokeMethod(keep, "clicked")) return 1;
  settle();
  if (!window->isVisible() || dialog->property("opened").toBool() || !close.error().isEmpty()) return 1;
  std::cout << "Window close blocked; modal failure detail visible; Keep editing leaves the window open.\n";
}
