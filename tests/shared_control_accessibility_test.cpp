#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
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

bool reachable(QAccessibleInterface* interface, QObject* target, QSet<QObject*>& visited) {
  if (interface == nullptr || visited.contains(interface->object())) return false;
  if (interface->object() == target) return true;
  visited.insert(interface->object());
  for (int index = 0; index < interface->childCount(); ++index) {
    if (reachable(interface->child(index), target, visited)) return true;
  }
  return false;
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
    height: 520
    property int toggleActivations: 0
    property int swatchActivations: 0
    property int actionActivations: 0
    property int miniActivations: 0
    property int recentActivations: 0

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
    InspectorSection {
        objectName: "accessibleSection"
        x: 8; y: 110; width: 344
        title: "Delivery settings"
        InspectorSlider {
            objectName: "accessibleExposure"
            width: 320
            label: "Exposure"
            from: -5; to: 5; value: 0.5; valueText: "0.50 EV"
        }
        ToneComboBox {
            objectName: "accessibleFormat"
            accessibleLabel: "Output format"
            model: ["PNG", "JPEG"]
        }
    }
    RecentDocumentRow {
        objectName: "accessibleRecent"
        x: 8; y: 350; width: 344
        title: "Saved project"
        onActivated: harness.recentActivations++
    }
    StudioTinyInput {
        objectName: "accessibleSearch"
        x: 8; y: 440; width: 344; height: 34
        placeholder: "Search images"
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/shared-control-accessibility-harness.qml")));

  std::unique_ptr<QObject> harness(component.create());
  if (!harness) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Shared control accessibility harness should instantiate");
  }

  QQuickWindow window;
  window.resize(360, 520);
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

  auto* section = find("accessibleSection");
  auto* exposure = find("accessibleExposure");
  auto* format = find("accessibleFormat");
  auto* recent = find("accessibleRecent");
  auto* search = find("accessibleSearch");
  if (!section || !exposure || !format || !recent || !search)
    return fail("Inspector and search accessibility fixtures should instantiate");
  if (!expect_role_and_name(section, QAccessible::Grouping, QStringLiteral("Delivery settings")))
    return fail("An inspector containing controls must expose a group, not a static text leaf");
  auto* exposure_field = exposure->findChild<QObject*>(QStringLiteral("accessibleExposureValueField"));
  for (auto* child : {exposure, exposure_field, format}) {
    QSet<QObject*> visited;
    if (!child || !reachable(accessible(section), child, visited))
      return fail("Inspector sliders, typed values and format choices must be reachable from their accessible parent");
  }
  if (!do_action(recent, QAccessibleActionInterface::pressAction()) ||
      harness->property("recentActivations").toInt() != 1)
    return fail("A Recent document must open through accessibility activation");
  recent->setProperty("actionable", false);
  static_cast<void>(do_action(recent, QAccessibleActionInterface::pressAction()));
  if (harness->property("recentActivations").toInt() != 1)
    return fail("An unavailable Recent document must ignore accessibility activation");
  auto* search_input = search->findChild<QObject*>(QStringLiteral("accessibleSearchInput"));
  if (!expect_role_and_name(search_input, QAccessible::EditableText, QStringLiteral("Search images")))
    return fail("Library search must expose a named editable text field");

  std::cout << "Shared controls expose working accessible press and toggle actions\n";
  return 0;
}
