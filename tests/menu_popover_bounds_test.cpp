#include <QAccessible>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QVariantList>
#include <QPointer>
#include <QEventLoop>
#include <QTimer>
#include <cmath>
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

bool has_visible_text(QQuickItem* root, const QString& text) {
  if (root == nullptr)
    return false;
  const auto value = root->property("text");
  if (root->isVisible() && value.isValid() && value.toString() == text)
    return true;
  for (auto* child : root->childItems()) {
    if (has_visible_text(child, text))
      return true;
  }
  return false;
}

void click_item(QQuickWindow& window, QQuickItem* target) {
  const auto point = target->mapToScene(
      QPointF(target->width() / 2, target->height() / 2));
  QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &press);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &release);
}

void click_window(QQuickWindow& window, const QPointF& point) {
  QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &press);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &release);
}

void send_escape(QQuickWindow& window) {
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &press);
  QCoreApplication::sendEvent(&window, &release);
}

bool verify_menu_signal_lifetimes() {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: 520; height: 400; visible: true
    property int deliveryCount: 0
    property string deliveredKey: ""
    property bool removeOwnerOnAction: false
    property bool replaceModelOnAction: false
    property bool split: false
    property bool nestedLoopOnAction: false
    function resetOwner() { owner.active = false; owner.active = true }
    function removeOwner() { owner.active = false }
    Loader {
        id: owner
        objectName: "lifetimeOwner"
        sourceComponent: fixture.split ? splitMenu : ordinaryMenu
    }
    function acceptAction(key, button) {
        deliveryCount += 1
        deliveredKey = key
        if (nestedLoopOnAction) title = "nested-event-loop"
        if (replaceModelOnAction) button.menuItems = []
        if (removeOwnerOnAction) owner.active = false
    }
    Component {
        id: ordinaryMenu
        MenuButton {
            id: ordinaryButton
            objectName: "lifetimeMenu"
            width: 180; height: 40
            text: "Actions"
            menuItems: [{key: "chosen-action", label: "Choose action"}]
            onMenuItemTriggered: function(key) { fixture.acceptAction(key, ordinaryButton) }
        }
    }
    Component {
        id: splitMenu
        SplitButton {
            id: splitButton
            objectName: "lifetimeMenu"
            width: 180; height: 40
            text: "Actions"
            menuItems: [{key: "chosen-action", label: "Choose action"}]
            onMenuItemTriggered: function(key) { fixture.acceptAction(key, splitButton) }
        }
    }
})qml", QUrl(QStringLiteral("inmemory:menu-signal-lifetime.qml")));
  if (!wait_for_component(component)) return false;
  std::unique_ptr<QObject> fixture(component.create());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return false; }
  QObject::connect(window, &QWindow::windowTitleChanged, window, [](const QString& title) {
    if (title != QStringLiteral("nested-event-loop")) return;
    // Native file dialogs pump another event loop. In the old path this
    // processes the row's deferred deletion inside its activation handler.
    QEventLoop modal_loop;
    QTimer::singleShot(180, &modal_loop, &QEventLoop::quit);
    modal_loop.exec();
  });
  const auto settle = [] {
    for (auto index = 0; index < 30; ++index) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QThread::msleep(5);
    }
  };
  settle();
  for (const auto split : {false, true}) {
    fixture->setProperty("split", split);
    for (const auto& mode : {QStringLiteral("dialog"), QStringLiteral("replace-model"), QStringLiteral("pointer"), QStringLiteral("return"),
             QStringLiteral("enter"), QStringLiteral("escape"),
             QStringLiteral("cancel"), QStringLiteral("owner-gone"),
             QStringLiteral("remove-owner"),
             QStringLiteral("duplicate")}) {
      std::cout << "menu_lifetime split=" << split << " mode=" << mode.toStdString() << std::endl;
      fixture->setProperty("deliveryCount", 0);
      fixture->setProperty("deliveredKey", "");
      fixture->setProperty("removeOwnerOnAction", mode == "remove-owner");
      fixture->setProperty("replaceModelOnAction", mode == "replace-model");
      fixture->setProperty("nestedLoopOnAction", mode == "dialog");
      window->setTitle(QString());
      QMetaObject::invokeMethod(fixture.get(), "resetOwner");
      settle();
      auto* owner = fixture->findChild<QObject*>("lifetimeOwner");
      auto* button = owner ? qvariant_cast<QQuickItem*>(owner->property("item")) : nullptr;
      QPointer<QObject> popup = button ? button->findChild<QObject*>("lifetimeMenuPopup") : nullptr;
      if (!popup) return false;
      // The fatal is deterministic when hiding a menu destroys its rows in
      // the same stack. It must also be safe with reduced/disabled motion.
      popup->setProperty("enter", QVariant::fromValue<QObject*>(nullptr));
      popup->setProperty("exit", QVariant::fromValue<QObject*>(nullptr));
      QMetaObject::invokeMethod(popup, "open");
      settle();
      auto* content = qvariant_cast<QQuickItem*>(popup->property("contentItem"));
      QPointer<QQuickItem> row = find_visual_object_name(content, "lifetimeMenuPopupItem0");
      if (!row) return false;
      row->forceActiveFocus(Qt::TabFocusReason);
      if (mode == "pointer") {
        click_item(*window, row);
      } else if (mode == "return" || mode == "enter" || mode == "escape") {
        QKeyEvent key(QEvent::KeyPress, mode == "return" ? Qt::Key_Return :
            mode == "enter" ? Qt::Key_Enter : Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(row, &key);
      } else {
        QMetaObject::invokeMethod(row, "clicked");
        if (mode == "duplicate" && row) QMetaObject::invokeMethod(row, "clicked");
        if (mode == "cancel") QMetaObject::invokeMethod(popup, "close");
        if (mode == "owner-gone") QMetaObject::invokeMethod(fixture.get(), "removeOwner");
      }
      settle();
      const auto cancelled = mode == "escape" || mode == "cancel" || mode == "owner-gone";
      if (fixture->property("deliveryCount").toInt() != (cancelled ? 0 : 1) ||
          (!cancelled && fixture->property("deliveredKey").toString() != "chosen-action") || row) {
        std::cerr << "menu lifetime failed: split=" << split << " mode=" << mode.toStdString()
                  << " deliveries=" << fixture->property("deliveryCount").toInt()
                  << " rowAlive=" << !row.isNull()
                  << " popupVisible=" << (popup && popup->property("visible").toBool())
                  << " rowParent=" << (row ? row->parentItem() : nullptr) << '\n';
        return false;
      }
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  if (!verify_menu_signal_lifetimes()) return 1;
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MENU_LIFETIME_ONLY")) return 0;
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
        target: root.target ? root.target : null
        ignoreUnknownSignals: true
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
      auto* described_row = find_visual_object_name(content, QStringLiteral("boundsMenuPopupItem0"));
      auto* plain_row = find_visual_object_name(content, QStringLiteral("boundsMenuPopupItem1"));
      auto* disabled_row = find_visual_object_name(content, QStringLiteral("boundsMenuPopupItem5"));
      if (described_row == nullptr || plain_row == nullptr || disabled_row == nullptr ||
          std::abs(described_row->height() - plain_row->height()) > 0.5 ||
          std::abs(disabled_row->height() - plain_row->height()) > 0.5 ||
          has_visible_text(described_row, QStringLiteral("Choose a replacement image")) ||
          has_visible_text(disabled_row, QStringLiteral("Test disabled action"))) {
        std::cerr << "Menu descriptions and disabled reasons should stay out of ordinary row content\n";
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

  // Exercise the actual pointer path: the opener click must toggle a visible
  // menu closed without Qt's outside-click delivery reopening it on release.
  window.resize(520, 360);
  button->setPosition(QPointF(20, 20));
  button->setWidth(160);
  button->setHeight(36);
  settle();
  click_item(window, menu_action);
  settle();
  if (!popup->property("visible").toBool()) {
    std::cerr << "Clicking the menu action should open its popup\n";
    return 1;
  }
  click_item(window, menu_action);
  settle();
  if (popup->property("visible").toBool()) {
    std::cerr << "Clicking the same menu action again should close its popup\n";
    return 1;
  }
  click_item(window, menu_action);
  settle();
  click_window(window, QPointF(470, 24));
  settle();
  if (popup->property("visible").toBool()) {
    std::cerr << "A click outside the menu owner should dismiss the popup\n";
    return 1;
  }

  // Popovers and selector dropdowns share one transient-menu owner.
  QQmlComponent second_menu_component(
      &engine, QUrl::fromLocalFile(QStringLiteral(
          PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/MenuButton.qml")));
  std::unique_ptr<QObject> second_menu_object(second_menu_component.create());
  auto* second_menu = qobject_cast<QQuickItem*>(second_menu_object.get());
  if (!second_menu) {
    std::cerr << second_menu_component.errorString().toStdString() << '\n';
    return 1;
  }
  second_menu->setParentItem(window.contentItem());
  second_menu->setObjectName("registrySecondMenu");
  second_menu->setPosition(QPointF(200, 20));
  second_menu->setWidth(160);
  second_menu->setHeight(36);
  second_menu->setProperty("text", "Second menu");
  second_menu->setProperty("menuItems", QVariantList{
      QVariantMap{{"key", "second"}, {"label", "Second menu action"}, {"enabled", true}}});
  auto* second_popup = second_menu_object->findChild<QObject*>("registrySecondMenuPopup");
  if (!second_popup || !QMetaObject::invokeMethod(button, "openMenu"))
    return 1;
  settle();
  if (!popup->property("visible").toBool() ||
      !QMetaObject::invokeMethod(second_menu, "openMenu"))
    return 1;
  settle();
  if (popup->property("visible").toBool() || !second_popup->property("visible").toBool()) {
    std::cerr << "Opening a second transient menu should close the first one\n";
    return 1;
  }

  QQmlComponent combo_component(
      &engine, QUrl::fromLocalFile(QStringLiteral(
          PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ToneComboBox.qml")));
  std::unique_ptr<QObject> combo_object(combo_component.create());
  auto* combo = qobject_cast<QQuickItem*>(combo_object.get());
  if (!combo) {
    std::cerr << combo_component.errorString().toStdString() << '\n';
    return 1;
  }
  combo->setParentItem(window.contentItem());
  combo->setObjectName("registryToneCombo");
  combo->setPosition(QPointF(200, 100));
  combo->setWidth(180);
  combo->setHeight(36);
  combo->setProperty("model", QStringList{"First", "Second"});
  combo->setProperty("currentIndex", 0);
  auto* combo_popup = combo_object->findChild<QObject*>("registryToneComboPopup");
  if (!combo_popup) return 1;
  settle();
  click_item(window, combo);
  settle();
  if (!combo_popup->property("visible").toBool()) {
    std::cerr << "ToneComboBox should open its dropdown from a pointer click\n";
    return 1;
  }
  if (!QMetaObject::invokeMethod(second_menu, "openMenu")) return 1;
  settle();
  if (combo_popup->property("visible").toBool() ||
      !second_popup->property("visible").toBool()) {
    std::cerr << "Opening a menu should dismiss a ToneComboBox dropdown\n";
    return 1;
  }
  click_item(window, menu_action);
  settle();
  if (second_popup->property("visible").toBool()) {
    std::cerr << "A menu action should toggle its transient popup closed\n";
    return 1;
  }
  QMetaObject::invokeMethod(button, "closeMenu");
  settle();

  QQmlComponent root_popup_component(&engine);
  root_popup_component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
Item {
    id: fixture
    width: 520
    height: 360
    property bool rootMenuInteriorClickReceived: false
    Rectangle {
        objectName: "rootMenuInteriorClickTarget"
        x: 468
        y: 318
        width: 44
        height: 32
        color: "transparent"
        MouseArea {
            anchors.fill: parent
            onClicked: fixture.rootMenuInteriorClickReceived = true
        }
    }
    ActionButton {
        id: firstOpener
        objectName: "rootWideFirstOpener"
        x: 20
        y: 20
        text: "First opener"
        onClicked: rootWidePopup.toggleTransient(firstOpener)
    }
    ActionButton {
        id: nextOpener
        objectName: "rootWideNextOpener"
        x: 20
        y: 72
        text: "Next opener"
        onClicked: rootWidePopup.toggleTransient(nextOpener)
    }
    ActionButton {
        id: appMenuOpener
        objectName: "appMenuToggleOpener"
        x: 20
        y: 124
        text: "App menu"
        onClicked: appMenu.toggleFrom(appMenuOpener)
    }
    MenuButton {
        id: dialogMenuButton
        objectName: "dialogMenuButton"
        x: 220
        y: 20
        width: 180
        height: 36
        text: "Open workflow dialog"
        menuItems: [{"key": "dialog", "label": "Open workflow dialog"}]
    }
    Connections {
        target: dialogMenuButton
        function onMenuItemTriggered(key) { workflowDialog.open() }
    }
    AppMenu {
        id: appMenu
        objectName: "appMenuTogglePopup"
        transientMenu: true
        AppMenuItem { text: "First menu command" }
        AppMenu {
            id: nestedAppMenu
            objectName: "rootWideNestedAppMenu"
            title: "Nested actions"
            transientMenu: false
            AppMenuItem { text: "Nested command" }
        }
    }
    AppPopup {
        id: workflowDialog
        objectName: "menuActionWorkflowDialog"
        parent: fixture
        x: 110
        y: 150
        width: 300
        modal: true
        focus: true
        contentItem: ToneTextField {
            id: workflowDialogField
            objectName: "menuActionWorkflowDialogField"
            accessibleLabel: "Workflow dialog field"
            text: "Ready for editing"
        }
        onOpened: workflowDialogField.forceActiveFocus(Qt.TabFocusReason)
    }
    AppPopup {
        id: rootWidePopup
        objectName: "rootWideTransientPopup"
        parent: fixture
        x: 260
        y: 220
        width: 230
        focus: true
        transientMenu: true
        contentItem: Item {
            implicitWidth: 230
            implicitHeight: 100
        }
    }
}
)qml", QUrl(QStringLiteral("inmemory:root-wide-menu-toggle-fixture.qml")));
  std::unique_ptr<QObject> root_popup_fixture(root_popup_component.create());
  auto* root_popup_fixture_item = qobject_cast<QQuickItem*>(root_popup_fixture.get());
  if (!root_popup_fixture_item) {
    std::cerr << root_popup_component.errorString().toStdString() << '\n';
    return 1;
  }
  root_popup_fixture_item->setParentItem(window.contentItem());
  root_popup_fixture_item->setPosition(QPointF(0, 0));
  auto* root_popup = root_popup_fixture->findChild<QObject*>("rootWideTransientPopup");
  auto* first_opener = root_popup_fixture->findChild<QQuickItem*>("rootWideFirstOpener");
  auto* next_opener = root_popup_fixture->findChild<QQuickItem*>("rootWideNextOpener");
  auto* app_menu = root_popup_fixture->findChild<QObject*>("appMenuTogglePopup");
  auto* nested_app_menu = root_popup_fixture->findChild<QObject*>("rootWideNestedAppMenu");
  auto* app_menu_opener = root_popup_fixture->findChild<QQuickItem*>("appMenuToggleOpener");
  auto* dialog_menu_button = root_popup_fixture->findChild<QQuickItem*>("dialogMenuButton");
  auto* workflow_dialog = root_popup_fixture->findChild<QObject*>("menuActionWorkflowDialog");
  auto* workflow_dialog_field = root_popup_fixture->findChild<QQuickItem*>("menuActionWorkflowDialogField");
  if (!root_popup || !first_opener || !next_opener || !app_menu || !nested_app_menu || !app_menu_opener ||
      !dialog_menu_button || !workflow_dialog || !workflow_dialog_field)
    return 1;
  click_item(window, first_opener);
  settle();
  if (!root_popup->property("visible").toBool()) {
    std::cerr << "Root-wide transient popup should open from its tracked opener\n";
    return 1;
  }
  click_item(window, first_opener);
  settle();
  if (root_popup->property("visible").toBool()) {
    std::cerr << "The same root-wide popup opener should close it on the forwarded click\n";
    return 1;
  }
  click_item(window, first_opener);
  settle();
  click_window(window, QPointF(495, 24));
  settle();
  if (root_popup->property("visible").toBool()) {
    std::cerr << "A root-wide popup should dismiss on an outside click\n";
    return 1;
  }
  click_item(window, next_opener);
  settle();
  if (!root_popup->property("visible").toBool() ||
      root_popup->property("transientOpener").value<QQuickItem*>() != next_opener) {
    std::cerr << "An outside dismissal must not block the next opener\n";
    return 1;
  }
  send_escape(window);
  settle();
  if (root_popup->property("visible").toBool() ||
      window.activeFocusItem() != next_opener) {
    std::cerr << "Escape should dismiss a root-wide popup and return focus to its opener\n";
    return 1;
  }
  click_item(window, app_menu_opener);
  settle();
  if (!app_menu->property("visible").toBool() || root_popup->property("visible").toBool()) {
    std::cerr << "Opening an AppMenu should dismiss another transient popup\n";
    return 1;
  }
  if (!QMetaObject::invokeMethod(nested_app_menu, "open")) {
    std::cerr << "The nested AppMenu fixture should open\n";
    return 1;
  }
  settle();
  if (!app_menu->property("visible").toBool() || !nested_app_menu->property("visible").toBool()) {
    std::cerr << "Opening a nested nontransient AppMenu should leave its transient root open\n";
    return 1;
  }
  QMetaObject::invokeMethod(nested_app_menu, "close");
  settle();
  click_window(window, QPointF(490, 334));
  settle();
  if (app_menu->property("visible").toBool() ||
      !root_popup_fixture->property("rootMenuInteriorClickReceived").toBool()) {
    std::cerr << "A transient AppMenu should dismiss on a click elsewhere inside its full-window parent\n";
    return 1;
  }
  click_item(window, app_menu_opener);
  settle();
  if (!app_menu->property("visible").toBool()) {
    std::cerr << "The AppMenu should reopen after an in-parent outside dismissal\n";
    return 1;
  }
  click_item(window, app_menu_opener);
  settle();
  if (app_menu->property("visible").toBool()) {
    std::cerr << "Clicking the same AppMenu opener again should close it\n";
    return 1;
  }
  click_item(window, app_menu_opener);
  settle();
  send_escape(window);
  settle();
  if (app_menu->property("visible").toBool() ||
      window.activeFocusItem() != app_menu_opener) {
    auto* active_focus = window.activeFocusItem();
    std::cerr << "AppMenu Escape state: visible=" << app_menu->property("visible").toBool()
              << " activeFocus=" << (active_focus ? active_focus->objectName().toStdString() : "<none>")
              << " openerFocus=" << app_menu_opener->hasActiveFocus()
              << " nestedVisible=" << nested_app_menu->property("visible").toBool()
              << '\n';
    std::cerr << "Escape should close AppMenu and return focus to its opener\n";
    return 1;
  }
  if (!QMetaObject::invokeMethod(dialog_menu_button, "openMenu")) return 1;
  settle();
  auto* dialog_menu_popup = dialog_menu_button->findChild<QObject*>(
      "dialogMenuButtonPopup");
  auto* dialog_menu_content = dialog_menu_popup
      ? qvariant_cast<QQuickItem*>(dialog_menu_popup->property("contentItem"))
      : nullptr;
  auto* dialog_menu_row = find_visual_object_name(
      dialog_menu_content, QStringLiteral("dialogMenuButtonPopupItem0"));
  if (!dialog_menu_row) {
    std::cerr << "The menu action row should materialize for dialog focus routing; popup="
              << (dialog_menu_popup != nullptr)
              << " visible=" << (dialog_menu_popup &&
                                   dialog_menu_popup->property("visible").toBool())
              << " content=" << (dialog_menu_content != nullptr) << '\n';
    return 1;
  }
  dialog_menu_row->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent activate_dialog_action(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QCoreApplication::sendEvent(dialog_menu_row, &activate_dialog_action);
  settle();
  if (!workflow_dialog->property("visible").toBool() ||
      window.activeFocusItem() != workflow_dialog_field) {
    std::cerr << "Opening a dialog from a menu action must retain its initial field focus\n";
    return 1;
  }
  workflow_dialog->setProperty("modal", false);
  QMetaObject::invokeMethod(workflow_dialog, "close");
  settle();
  root_popup_fixture_item->setParentItem(nullptr);

  button->setProperty("primaryOpensMenu", true);
  settle();
  if (menu_action->isVisible() || !primary_action->activeFocusOnTab())
    return 1;

  click_item(window, primary_action);
  settle();
  if (!popup->property("visible").toBool() ||
      !primary_action->property("active").toBool()) {
    std::cerr << "Primary MenuButton state: popup=" << popup->property("visible").toBool()
              << " active=" << primary_action->property("active").toBool()
              << " primaryOpensMenu=" << button->property("primaryOpensMenu").toBool()
              << " menuEnabled=" << button->property("menuEnabled").toBool() << '\n';
    return 1;
  }
  click_item(window, primary_action);
  settle();
  if (popup->property("visible").toBool()) {
    std::cerr << "Primary MenuButton should toggle its popup closed on repeat click\n";
    return 1;
  }

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
  second_menu->setParentItem(nullptr);
  combo->setParentItem(nullptr);

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
