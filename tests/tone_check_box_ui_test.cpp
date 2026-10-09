#include <QAccessible>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (auto i = 0; i < 8; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QString accessible_name(QObject* object) {
  auto* interface = object == nullptr ? nullptr : QAccessible::queryAccessibleInterface(object);
  return interface == nullptr ? QString() : interface->text(QAccessible::Name).trimmed();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);

  QQmlEngine engine;
  QQmlComponent component(
      &engine,
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ToneCheckBox.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* control = qobject_cast<QQuickItem*>(object.get());
  if (control == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("ToneCheckBox component should instantiate");
  }

  QQuickWindow window;
  window.resize(300, 80);
  control->setObjectName(QStringLiteral("copyPhotoEditsColorGroup"));
  control->setProperty("text", QStringLiteral("Color"));
  control->setProperty("accessibleLabel", QStringLiteral("Copy Color edits"));
  control->setProperty("accessibleDescription", QStringLiteral("Copy white balance and color adjustments."));
  control->setParentItem(window.contentItem());
  window.show();
  settle();

  auto* accessible = QAccessible::queryAccessibleInterface(control);
  if (accessible == nullptr || accessible->role() != QAccessible::CheckBox ||
      accessible_name(control) != QStringLiteral("Copy Color edits") ||
      accessible->text(QAccessible::Description) != QStringLiteral("Copy white balance and color adjustments.") ||
      accessible->state().checked) {
    return fail("ToneCheckBox should expose a named checkbox role, description, and unchecked state");
  }

  control->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(control, &press);
  QCoreApplication::sendEvent(control, &release);
  settle();
  if (!control->property("checked").toBool() || !accessible->state().checked)
    return fail("ToneCheckBox should toggle from the keyboard and report its checked state");

  control->setProperty("accessibleLabel", QString());
  control->setProperty("accessibleDescription", QString());
  control->setProperty("text", QString());
  settle();
  if (accessible_name(control) != QStringLiteral("Copy Photo Edits Color Group option"))
    return fail("ToneCheckBox should derive a readable name from its object name");

  std::cout << "ToneCheckBox exposes readable names, descriptions, and keyboard state\n";
  return 0;
}
