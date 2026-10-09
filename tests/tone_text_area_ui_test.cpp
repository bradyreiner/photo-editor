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
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ToneTextArea.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* field = qobject_cast<QQuickItem*>(object.get());
  if (field == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("ToneTextArea component should instantiate");
  }

  QQuickWindow window;
  window.resize(360, 160);
  field->setObjectName(QStringLiteral("timelineMediaNotesField"));
  field->setSize(QSizeF(320, 96));
  field->setProperty("placeholderText", QStringLiteral("Editorial notes and approvals"));
  field->setParentItem(window.contentItem());
  window.show();
  settle();

  auto* accessible = QAccessible::queryAccessibleInterface(field);
  if (accessible == nullptr || accessible->role() != QAccessible::EditableText ||
      accessible_name(field) != QStringLiteral("Editorial notes and approvals") ||
      accessible->text(QAccessible::Description) != QStringLiteral("Editorial notes and approvals")) {
    return fail("ToneTextArea should expose its placeholder as a named multiline editable field");
  }

  field->setProperty("accessibleLabel", QStringLiteral("Media notes"));
  settle();
  if (accessible_name(field) != QStringLiteral("Media notes"))
    return fail("ToneTextArea should prefer an explicit accessible label");

  field->setProperty("accessibleLabel", QString());
  field->setProperty("placeholderText", QString());
  settle();
  if (accessible_name(field) != QStringLiteral("Timeline Media Notes text area"))
    return fail("ToneTextArea should derive a readable name from its object name");

  field->setProperty("readOnly", true);
  settle();
  accessible = QAccessible::queryAccessibleInterface(field);
  if (accessible == nullptr || accessible->role() != QAccessible::StaticText)
    return fail("read-only ToneTextArea should expose static text semantics");

  std::cout << "ToneTextArea exposes named multiline and read-only semantics\n";
  return 0;
}
