#include <QAccessible>
#include <QCoreApplication>
#include <QGuiApplication>
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
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ToneTextField.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* field = qobject_cast<QQuickItem*>(object.get());
  if (field == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("ToneTextField component should instantiate");
  }

  QQuickWindow window;
  window.resize(320, 100);
  field->setObjectName(QStringLiteral("audioOutputChannelField"));
  field->setSize(QSizeF(240, 40));
  field->setProperty("placeholderText", QStringLiteral("Enter gain"));
  field->setProperty("text", QStringLiteral("0.8"));
  field->setParentItem(window.contentItem());
  window.show();
  settle();

  auto* accessible = QAccessible::queryAccessibleInterface(field);
  if (accessible == nullptr || accessible->role() != QAccessible::EditableText ||
      accessible_name(field) != QStringLiteral("Enter gain") ||
      accessible->text(QAccessible::Description) != QStringLiteral("Enter gain")) {
    return fail("ToneTextField should expose its placeholder as a stable editable-field label");
  }

  field->setProperty("accessibleLabel", QStringLiteral("Master output gain"));
  settle();
  if (accessible_name(field) != QStringLiteral("Master output gain"))
    return fail("ToneTextField should prefer an explicit accessible label");

  field->setProperty("accessibleLabel", QString());
  field->setProperty("placeholderText", QString());
  settle();
  if (accessible_name(field) != QStringLiteral("Audio Output Channel field"))
    return fail("ToneTextField should derive a stable label from its object name when no placeholder exists");

  std::cout << "ToneTextField exposes stable names for placeholder, explicit label, and object-name fallback\n";
  return 0;
}
