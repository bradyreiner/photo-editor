#include <QAccessible>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QVariantList>
#include <iostream>
#include <memory>

namespace {

bool wait_for_component(QQmlComponent& component) {
  for (auto i = 0; i < 200 && component.isLoading(); ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  return component.status() == QQmlComponent::Ready;
}

QQuickItem* find_visual_object_name(QQuickItem* root, const QString& name) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == name)
    return root;
  for (auto* child : root->childItems()) {
    if (auto* found = find_visual_object_name(child, name))
      return found;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/MenuButton.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* button = qobject_cast<QQuickItem*>(object.get());
  if (!button) { std::cerr << component.errorString().toStdString(); return 1; }
  QQuickWindow window;
  button->setParentItem(window.contentItem());
  button->setObjectName("boundsMenu");
  button->setProperty("text", "Layer actions");
  if (button->activeFocusOnTab()) {
    std::cerr << "The MenuButton wrapper should not add a duplicate focus stop\n";
    return 1;
  }
  QVariantList items;
  for (auto index = 0; index < 30; ++index) {
    items.append(QVariantMap{{"key", QString::number(index)},
        {"label", index == 0 ? QString("Relink Image…") : QString("Layer action %1").arg(index)},
        {"description", index == 0 ? QString("Choose a replacement image") : QString()},
        {"enabled", index != 5 && index != 9}, {"disabledReason", "Test disabled action"}});
  }
  button->setProperty("menuItems", items);
  auto settle = [] {
    // Allow the shared popup's 150 ms opening transition to finish before
    // checking settled geometry, focus, and reopening after dismissal.
    for (auto i = 0; i < 40; ++i) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
  };
  auto* primary_action = qobject_cast<QQuickItem*>(
      object->findChild<QObject*>("boundsMenuPrimaryAction"));
  auto* menu_action = qobject_cast<QQuickItem*>(
      object->findChild<QObject*>("boundsMenuMenuAction"));
  auto* popup = object->findChild<QObject*>("boundsMenuPopup");
  auto* popup_content = popup != nullptr
                            ? qvariant_cast<QQuickItem*>(popup->property("contentItem"))
                            : nullptr;
  if (popup == nullptr || popup_content == nullptr ||
      find_visual_object_name(popup_content, QStringLiteral("boundsMenuPopupItem0")) != nullptr) {
    std::cerr << "Closed MenuPopover should not materialize menu rows\n";
    return 1;
  }
  QQmlComponent signal_capture_component(&engine);
  signal_capture_component.setData(R"qml(
import QtQuick
Item {
    id: root
    property var target
    property string activatedKey: ""
    Connections {
        target: root.target
        function onMenuItemTriggered(key) { root.activatedKey = key }
    }
}
)qml", QUrl(QStringLiteral("inmemory:menu-popover-signal-capture.qml")));
  if (!wait_for_component(signal_capture_component)) {
    std::cerr << signal_capture_component.errorString().toStdString() << '\n';
    return 1;
  }
  std::unique_ptr<QObject> signal_capture(signal_capture_component.create());
  if (!signal_capture) {
    std::cerr << signal_capture_component.errorString().toStdString() << '\n';
    return 1;
  }
  signal_capture->setProperty("target", QVariant::fromValue<QObject*>(button));
  auto* primary_accessible = QAccessible::queryAccessibleInterface(primary_action);
  auto* menu_accessible = QAccessible::queryAccessibleInterface(menu_action);
  if (primary_action == nullptr || menu_action == nullptr ||
      !primary_action->activeFocusOnTab() || !menu_action->activeFocusOnTab() ||
      primary_accessible == nullptr || menu_accessible == nullptr ||
      primary_accessible->role() != QAccessible::Button ||
      menu_accessible->role() != QAccessible::Button ||
      primary_accessible->text(QAccessible::Name) != QStringLiteral("Layer actions") ||
      menu_accessible->text(QAccessible::Name) != QStringLiteral("More options for Layer actions")) {
    std::cerr << "MenuButton focus/accessibility: rootTab=" << button->activeFocusOnTab()
              << " primary=" << (primary_action != nullptr)
              << " primaryTab=" << (primary_action != nullptr && primary_action->activeFocusOnTab())
              << " primaryRole=" << (primary_accessible != nullptr ? primary_accessible->role() : QAccessible::NoRole)
              << " primaryName=" << (primary_accessible != nullptr ? primary_accessible->text(QAccessible::Name).toStdString() : "")
              << " menu=" << (menu_action != nullptr)
              << " menuTab=" << (menu_action != nullptr && menu_action->activeFocusOnTab())
              << " menuRole=" << (menu_accessible != nullptr ? menu_accessible->role() : QAccessible::NoRole)
              << " menuName=" << (menu_accessible != nullptr ? menu_accessible->text(QAccessible::Name).toStdString() : "")
              << '\n';
    return 1;
  }
  for (const auto& size : {QSize(990, 620), QSize(360, 260)}) {
    window.resize(size); window.show();
    for (const auto& corner : {QPoint(0, 0), QPoint(size.width() - 40, size.height() - 40)}) {
      button->setPosition(corner);
      if (!QMetaObject::invokeMethod(button, "openMenu")) return 1;
      settle();
      auto* popup = object->findChild<QObject*>("boundsMenuPopup");
      auto* content = popup ? qvariant_cast<QQuickItem*>(popup->property("contentItem")) : nullptr;
      if (!content) return 1;
      const auto find_menu_item = [](const auto& self, QQuickItem* parent, const QString& name)
          -> QAccessibleInterface* {
        if (parent == nullptr)
          return nullptr;
        for (auto* child : parent->childItems()) {
          auto* accessible = QAccessible::queryAccessibleInterface(child);
          if (accessible != nullptr && accessible->role() == QAccessible::MenuItem &&
              accessible->text(QAccessible::Name) == name)
            return accessible;
          if (auto* nested = self(self, child, name))
            return nested;
        }
        return nullptr;
      };
      auto* first_menu_row_accessible =
          find_menu_item(find_menu_item, content, QStringLiteral("Relink Image…"));
      auto* disabled_menu_row_accessible =
          find_menu_item(find_menu_item, content, QStringLiteral("Layer action 5"));
      if (first_menu_row_accessible == nullptr ||
          first_menu_row_accessible->text(QAccessible::Description) !=
              QStringLiteral("Choose a replacement image") ||
          disabled_menu_row_accessible == nullptr ||
          !disabled_menu_row_accessible->state().disabled ||
          disabled_menu_row_accessible->text(QAccessible::Description) !=
              QStringLiteral("Test disabled action")) {
        std::cerr << "Menu row accessibility should separate enabled descriptions from disabled reasons\n";
        return 1;
      }
      const auto bounds = content->mapRectToScene(QRectF(0, 0, content->width(), content->height()));
      if (!QRectF(QPointF(0, 0), QSizeF(size)).contains(bounds)) {
        std::cerr << "Menu escaped " << size.width() << 'x' << size.height() << '\n'; return 1;
      }
      QVariant focused;
      if (!QMetaObject::invokeMethod(popup, "focusMenuItem", Q_RETURN_ARG(QVariant, focused), Q_ARG(QVariant, 29)) ||
          !focused.toBool()) return 1;
      settle();
      if (content->property("contentY").toDouble() <= 0 || popup->property("currentFocusIndex").toInt() != 29) return 1;
      if (!QMetaObject::invokeMethod(popup, "focusMenuItem", Q_RETURN_ARG(QVariant, focused), Q_ARG(QVariant, 4)) ||
          !QMetaObject::invokeMethod(popup, "focusNextMenuItem", Q_ARG(QVariant, 1))) return 1;
      if (popup->property("currentFocusIndex").toInt() != 6) return 1;
      if (!QMetaObject::invokeMethod(popup, "focusMenuItem", Q_RETURN_ARG(QVariant, focused), Q_ARG(QVariant, 0))) return 1;
      settle();
      if (content->property("contentY").toDouble() != 0.0) return 1;
      QMetaObject::invokeMethod(button, "closeMenu");
      settle();
    }
  }
  for (auto* opener : {primary_action, menu_action}) {
    opener->forceActiveFocus(Qt::TabFocusReason);
    if (!QMetaObject::invokeMethod(button, "openMenu")) return 1;
    settle();
    if (!popup || !popup->property("visible").toBool()) {
      std::cerr << "Menu did not open for the focused action\n";
      return 1;
    }
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &escape);
    settle();
    if (popup->property("visible").toBool() || window.activeFocusItem() != opener) {
      std::cerr << "Escape should close the menu and return focus to its opener\n";
      return 1;
    }
  }
  button->setProperty("primaryOpensMenu", true);
  settle();
  if (menu_action->isVisible() || !primary_action->activeFocusOnTab())
    return 1;

  // The production MenuPopover must materialize from the current model only
  // while open, and reopen with the replacement payload rather than a stale
  // delegate snapshot.
  QMetaObject::invokeMethod(button, "closeMenu");
  settle();
  if (find_visual_object_name(popup_content, QStringLiteral("boundsMenuPopupItem0")) != nullptr)
    return 1;
  QVariantList refreshed_items{
      QVariantMap{{"key", "fresh-payload-key"},
                  {"label", "Fresh payload action"},
                  {"enabled", true}}};
  button->setProperty("menuItems", refreshed_items);
  if (!QMetaObject::invokeMethod(button, "openMenu"))
    return 1;
  settle();
  auto* refreshed_row = find_visual_object_name(
      popup_content, QStringLiteral("boundsMenuPopupItem0"));
  if (!popup->property("visible").toBool() || refreshed_row == nullptr ||
      refreshed_row->property("itemKey").toString() !=
      QStringLiteral("fresh-payload-key") ||
      refreshed_row->property("itemLabel").toString() !=
          QStringLiteral("Fresh payload action")) {
    std::cerr << "Reopened MenuPopover should materialize the current payload\n";
    return 1;
  }
  auto* refreshed_row_item = qobject_cast<QQuickItem*>(refreshed_row);
  if (refreshed_row_item == nullptr)
    return 1;
  refreshed_row_item->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent activate_fresh(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QCoreApplication::sendEvent(refreshed_row_item, &activate_fresh);
  settle();
  if (signal_capture->property("activatedKey").toString() !=
          QStringLiteral("fresh-payload-key") ||
      popup->property("visible").toBool() ||
      find_visual_object_name(popup_content, QStringLiteral("boundsMenuPopupItem0")) != nullptr) {
    std::cerr << "Return should activate the fresh menu payload and close its popup\n";
    return 1;
  }
  button->setParentItem(nullptr);

  QQmlComponent split_component(
      &engine,
      QUrl::fromLocalFile(
          QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/SplitButton.qml")));
  std::unique_ptr<QObject> split_object(split_component.create());
  auto* split_button = qobject_cast<QQuickItem*>(split_object.get());
  if (split_button == nullptr) {
    std::cerr << split_component.errorString().toStdString();
    return 1;
  }
  split_button->setParentItem(window.contentItem());
  split_button->setObjectName("boundsSplit");
  split_button->setProperty("text", "Start Export");
  auto* split_primary_action = qobject_cast<QQuickItem*>(
      split_object->findChild<QObject*>("boundsSplitPrimaryAction"));
  auto* split_menu_action = qobject_cast<QQuickItem*>(
      split_object->findChild<QObject*>("boundsSplitMenuAction"));
  auto* split_primary_accessible = QAccessible::queryAccessibleInterface(split_primary_action);
  auto* split_menu_accessible = QAccessible::queryAccessibleInterface(split_menu_action);
  if (split_button->activeFocusOnTab() || split_primary_action == nullptr ||
      split_menu_action == nullptr || !split_primary_action->activeFocusOnTab() ||
      !split_menu_action->activeFocusOnTab() || split_primary_accessible == nullptr ||
      split_menu_accessible == nullptr || split_primary_accessible->role() != QAccessible::Button ||
      split_menu_accessible->role() != QAccessible::Button ||
      split_primary_accessible->text(QAccessible::Name) != QStringLiteral("Start Export") ||
      split_menu_accessible->text(QAccessible::Name) !=
          QStringLiteral("More options for Start Export")) {
    std::cerr << "SplitButton should expose its primary and menu actions as the only named button focus stops\n";
    return 1;
  }
  split_button->setParentItem(nullptr);
  std::cout << "Menu and split-button action focus stops, menu-item accessibility, viewport bounds, overflow scrolling, and disabled-item navigation passed\n";
  return 0;
}
