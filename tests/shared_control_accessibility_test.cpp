#include <QAccessible>
#include <QAccessibleActionInterface>
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
  for (int i = 0; i < 8; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

QAccessibleInterface* accessible(QObject* object) {
  return object == nullptr ? nullptr : QAccessible::queryAccessibleInterface(object);
}

bool do_action(QObject* object, const QString& action_name) {
  auto* interface = accessible(object);
  auto* actions = interface == nullptr ? nullptr : interface->actionInterface();
  return actions != nullptr && actions->actionNames().contains(action_name) &&
      (actions->doAction(action_name), true);
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);

  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
Item {
    id: harness
    width: 360
    height: 220
    property int toggleActivations: 0
    property int swatchActivations: 0
    property int actionActivations: 0
    property int miniActivations: 0

    ToolOptionToggle {
        objectName: "accessibleOption"
        x: 8; y: 8
        label: "Show guides"
        onToggled: function(next) { harness.toggleActivations++; checked = next }
    }
    ColorSwatchButton {
        objectName: "accessibleSwatch"
        x: 8; y: 56
        label: "Choose blue"
        swatchColor: "#4287f5"
        onPicked: function(color) { harness.swatchActivations++ }
    }
    ActionButton {
        objectName: "accessibleAction"
        x: 56; y: 56
        text: "Apply"
        onClicked: harness.actionActivations++
    }
    MiniButton {
        objectName: "accessibleMini"
        x: 150; y: 56
        text: "Reset"
        onClicked: harness.miniActivations++
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/shared-control-accessibility-harness.qml")));

  std::unique_ptr<QObject> harness(component.create());
  if (!harness) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Shared control accessibility harness should instantiate");
  }

  QQuickWindow window;
  window.resize(360, 220);
  qobject_cast<QQuickItem*>(harness.get())->setParentItem(window.contentItem());
  window.show();
  settle();

  const auto find = [&](const char* name) { return harness->findChild<QObject*>(QString::fromLatin1(name)); };
  auto* toggle = find("accessibleOption");
  auto* swatch = find("accessibleSwatch");
  auto* action = find("accessibleAction");
  auto* mini = find("accessibleMini");
  if (!toggle || !swatch || !action || !mini)
    return fail("Shared controls should all instantiate");

  const auto expect_role_and_name = [](QObject* object, QAccessible::Role role, const QString& name) {
    auto* interface = accessible(object);
    return interface != nullptr && interface->role() == role &&
        interface->text(QAccessible::Name).trimmed() == name;
  };
  if (!expect_role_and_name(toggle, QAccessible::CheckBox, QStringLiteral("Show guides")) ||
      !expect_role_and_name(swatch, QAccessible::Button, QStringLiteral("Choose blue")) ||
      !expect_role_and_name(action, QAccessible::Button, QStringLiteral("Apply")) ||
      !expect_role_and_name(mini, QAccessible::Button, QStringLiteral("Reset")))
    return fail("Shared controls should expose their expected accessible names and roles");

  if (!do_action(toggle, QAccessibleActionInterface::toggleAction()) ||
      !toggle->property("checked").toBool() || harness->property("toggleActivations").toInt() != 1)
    return fail("The accessible toggle action should change ToolOptionToggle state once");
  if (!do_action(toggle, QAccessibleActionInterface::pressAction()) ||
      toggle->property("checked").toBool() || harness->property("toggleActivations").toInt() != 2)
    return fail("The accessible press action should activate ToolOptionToggle once");

  if (!do_action(swatch, QAccessibleActionInterface::pressAction()) ||
      harness->property("swatchActivations").toInt() != 1)
    return fail("The accessible press action should activate ColorSwatchButton once");
  if (!do_action(action, QAccessibleActionInterface::pressAction()) ||
      harness->property("actionActivations").toInt() != 1)
    return fail("ActionButton should expose a working accessible press action");
  if (!do_action(mini, QAccessibleActionInterface::pressAction()) ||
      harness->property("miniActivations").toInt() != 1)
    return fail("MiniButton should expose a working accessible press action");

  swatch->setProperty("enabled", false);
  static_cast<void>(do_action(swatch, QAccessibleActionInterface::pressAction()));
  if (harness->property("swatchActivations").toInt() != 1)
    return fail("A disabled color swatch must ignore accessible activation");

  std::cout << "Shared controls expose working accessible press and toggle actions\n";
  return 0;
}
