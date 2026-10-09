#include <QAccessible>
#include <QCoreApplication>
#include <QEventLoop>
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
  for (int index = 0; index < 10; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QObject* load_component(QQmlEngine& engine, const QString& component_path) {
  QQmlComponent component(&engine, QUrl::fromLocalFile(component_path));
  auto* object = component.create();
  if (object == nullptr)
    std::cerr << component.errorString().toStdString() << '\n';
  return object;
}

QString accessible_text(QObject* object, QAccessible::Text text_type) {
  auto* interface = object == nullptr ? nullptr : QAccessible::queryAccessibleInterface(object);
  return interface == nullptr ? QString() : interface->text(text_type).trimmed();
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
  std::unique_ptr<QObject> action_button(load_component(
      engine,
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ActionButton.qml")));
  std::unique_ptr<QObject> mini_button(load_component(
      engine,
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/MiniButton.qml")));
  auto* action_item = qobject_cast<QQuickItem*>(action_button.get());
  auto* mini_item = qobject_cast<QQuickItem*>(mini_button.get());
  if (action_item == nullptr || mini_item == nullptr)
    return fail("ActionButton and MiniButton should load as QQuickItems");

  QQuickWindow window;
  window.resize(360, 120);
  action_button->setObjectName(QStringLiteral("undoButton"));
  action_button->setProperty("text", QString());
  action_button->setProperty("iconText", QStringLiteral("↶"));
  action_button->setProperty("showText", false);
  action_button->setProperty("tooltipText", QStringLiteral("Undo last edit"));
  action_button->setProperty("disabledReason", QStringLiteral("Nothing to undo."));
  action_button->setProperty("enabled", false);
  action_item->setPosition(QPointF(24, 20));
  action_item->setSize(QSizeF(44, 44));
  action_item->setParentItem(window.contentItem());

  mini_button->setObjectName(QStringLiteral("removeLayerButton"));
  mini_button->setProperty("text", QString());
  mini_button->setProperty("iconText", QStringLiteral("−"));
  mini_button->setProperty("role", QStringLiteral("icon"));
  mini_button->setProperty("tooltip", QStringLiteral("Remove selected layer"));
  mini_button->setProperty("disabledReason", QStringLiteral("Select a layer first."));
  mini_button->setProperty("enabled", false);
  mini_item->setPosition(QPointF(92, 20));
  mini_item->setSize(QSizeF(36, 36));
  mini_item->setParentItem(window.contentItem());

  window.show();
  settle();

  auto* action_accessible = QAccessible::queryAccessibleInterface(action_button.get());
  if (action_accessible == nullptr || action_accessible->role() != QAccessible::Button ||
      accessible_text(action_button.get(), QAccessible::Name) !=
          QStringLiteral("Undo last edit") ||
      accessible_text(action_button.get(), QAccessible::Description) !=
          QStringLiteral("Nothing to undo.")) {
    return fail("Disabled ActionButton should keep its action name and expose its reason as the description");
  }

  auto* mini_accessible = QAccessible::queryAccessibleInterface(mini_button.get());
  if (mini_accessible == nullptr || mini_accessible->role() != QAccessible::Button ||
      accessible_text(mini_button.get(), QAccessible::Name) !=
          QStringLiteral("Remove selected layer") ||
      accessible_text(mini_button.get(), QAccessible::Description) !=
          QStringLiteral("Select a layer first.")) {
    return fail("Disabled MiniButton should keep its action name and expose its reason as the description");
  }

  action_button->setProperty("tooltipText", QString());
  mini_button->setProperty("tooltip", QString());
  settle();
  if (accessible_text(action_button.get(), QAccessible::Name) != QStringLiteral("Undo") ||
      accessible_text(mini_button.get(), QAccessible::Name) != QStringLiteral("Remove Layer")) {
    return fail("Icon-only buttons should fall back to their readable object-name identity");
  }

  action_button->setObjectName(QStringLiteral("button"));
  action_button->setProperty("iconName", QStringLiteral("undo"));
  mini_button->setObjectName(QStringLiteral("button"));
  mini_button->setProperty("iconName", QStringLiteral("delete"));
  settle();
  if (accessible_text(action_button.get(), QAccessible::Name) != QStringLiteral("Undo") ||
      accessible_text(mini_button.get(), QAccessible::Name) != QStringLiteral("Delete")) {
    return fail("Icon-only buttons should fall back to a readable icon name when their object name is generic");
  }

  action_button->setProperty("text", QStringLiteral("Save"));
  action_button->setProperty("tooltipText", QStringLiteral("Save project"));
  action_button->setProperty("disabledReason", QStringLiteral("Saving is unavailable."));
  action_button->setProperty("accessibleName", QStringLiteral("Save this edit"));
  mini_button->setProperty("text", QStringLiteral("Delete"));
  mini_button->setProperty("tooltip", QStringLiteral("Delete selected layer"));
  mini_button->setProperty("disabledReason", QStringLiteral("Select a layer first."));
  mini_button->setProperty("a11yName", QStringLiteral("Delete this layer"));
  settle();
  if (accessible_text(action_button.get(), QAccessible::Name) != QStringLiteral("Save this edit") ||
      accessible_text(mini_button.get(), QAccessible::Name) != QStringLiteral("Delete this layer") ||
      accessible_text(action_button.get(), QAccessible::Description) !=
          QStringLiteral("Saving is unavailable.") ||
      accessible_text(mini_button.get(), QAccessible::Description) !=
          QStringLiteral("Select a layer first.")) {
    return fail("Explicit accessible names should override visible text while disabled reasons remain descriptions");
  }

  action_button->setProperty("accessibleName", QString());
  mini_button->setProperty("a11yName", QString());
  settle();
  if (accessible_text(action_button.get(), QAccessible::Name) != QStringLiteral("Save") ||
      accessible_text(mini_button.get(), QAccessible::Name) != QStringLiteral("Delete")) {
    return fail("Visible button labels should remain the accessible name when no explicit name is supplied");
  }

  std::cout << "ActionButton and MiniButton preserve action names, disabled reasons, and explicit accessible labels.\n";
  return 0;
}
