#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
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
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ToneSpinBox.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* control = qobject_cast<QQuickItem*>(object.get());
  if (control == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("ToneSpinBox component should instantiate");
  }

  QQuickWindow window;
  window.resize(220, 90);
  control->setObjectName(QStringLiteral("sceneVideoFrameCount"));
  control->setSize(QSizeF(108, 44));
  control->setProperty("from", 16);
  control->setProperty("to", 1920);
  control->setProperty("stepSize", 16);
  control->setProperty("value", 480);
  control->setProperty("editable", true);
  control->setProperty("valueUnit", QStringLiteral("frames"));
  control->setProperty("accessibleLabel", QStringLiteral("Sampled video frames"));
  control->setParentItem(window.contentItem());
  window.show();
  settle();

  auto* accessible = QAccessible::queryAccessibleInterface(control);
  if (accessible == nullptr || accessible->role() != QAccessible::SpinBox ||
      accessible_name(control) != QStringLiteral("Sampled video frames") ||
      accessible->text(QAccessible::Description) != QStringLiteral("480 frames")) {
    return fail("ToneSpinBox should expose its numeric role, explicit name, and current value with units");
  }

  control->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Up, Qt::NoModifier);
  QCoreApplication::sendEvent(control, &press);
  QCoreApplication::sendEvent(control, &release);
  settle();
  if (control->property("value").toInt() != 496)
    return fail("ToneSpinBox should preserve keyboard stepping and the configured increment");

  if (accessible->text(QAccessible::Description) != QStringLiteral("496 frames"))
    return fail("ToneSpinBox should update its accessible value description after keyboard input");

  auto* actions = accessible->actionInterface();
  if (actions == nullptr || !actions->actionNames().contains(QAccessibleActionInterface::increaseAction()) ||
      !actions->actionNames().contains(QAccessibleActionInterface::decreaseAction()))
    return fail("ToneSpinBox should expose accessible increase and decrease actions");
  actions->doAction(QAccessibleActionInterface::increaseAction());
  if (control->property("value").toInt() != 512)
    return fail("ToneSpinBox accessible increase action should use the configured step");
  actions->doAction(QAccessibleActionInterface::decreaseAction());
  if (control->property("value").toInt() != 496)
    return fail("ToneSpinBox accessible decrease action should use the configured step");

  auto* editor = control->findChild<QQuickItem*>(QStringLiteral("sceneVideoFrameCountEditor"));
  if (editor == nullptr)
    return fail("ToneSpinBox should expose a named editable numeric field");
  QSignalSpy modified(control, SIGNAL(valueModified()));
  if (!modified.isValid())
    return fail("ToneSpinBox should expose its user-modified signal");
  const auto modified_before_text = modified.count();
  editor->forceActiveFocus(Qt::OtherFocusReason);
  settle();
  QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
  QKeyEvent selectAllRelease(QEvent::KeyRelease, Qt::Key_A, Qt::ControlModifier);
  QCoreApplication::sendEvent(&window, &selectAll);
  QCoreApplication::sendEvent(&window, &selectAllRelease);
  QKeyEvent enterText(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("703"));
  QCoreApplication::sendEvent(&window, &enterText);
  QKeyEvent acceptEdit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &acceptEdit);
  settle();
  if (control->property("value").toInt() != 703 || modified.count() != modified_before_text + 1)
    return fail("ToneSpinBox should commit a final typed value on Enter");

  editor->forceActiveFocus(Qt::OtherFocusReason);
  QCoreApplication::sendEvent(&window, &selectAll);
  QCoreApplication::sendEvent(&window, &selectAllRelease);
  QKeyEvent finalText(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("721"));
  QCoreApplication::sendEvent(&window, &finalText);
  QQuickItem focus_sink;
  focus_sink.setFlag(QQuickItem::ItemIsFocusScope, true);
  focus_sink.setParentItem(window.contentItem());
  focus_sink.setFocus(true);
  focus_sink.forceActiveFocus(Qt::OtherFocusReason);
  settle();
  if (editor->hasActiveFocus() || control->property("value").toInt() != 721 ||
      modified.count() != modified_before_text + 2) {
    std::cerr << "focus-loss state: value=" << control->property("value").toInt()
              << " valueModified=" << modified.count()
              << " editorFocus=" << editor->hasActiveFocus()
              << " controlFocus=" << control->hasActiveFocus()
              << " editorText=" << editor->property("text").toString().toStdString()
              << " draftText=" << editor->property("draftText").toString().toStdString()
              << " editingText=" << editor->property("editingText").toBool()
              << " focusSink=" << focus_sink.hasActiveFocus() << '\n';
    return fail("ToneSpinBox should commit a final typed value when focus leaves the editor");
  }

  control->setProperty("accessibleLabel", QString());
  settle();
  if (accessible_name(control) != QStringLiteral("Scene Video Frame Count value"))
    return fail("ToneSpinBox should derive a readable accessible name from its object name");

  std::cout << "ToneSpinBox exposes named numeric semantics and keyboard stepping\n";
  return 0;
}
