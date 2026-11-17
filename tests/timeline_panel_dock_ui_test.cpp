#include <QAccessible>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPoint>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QVariant>
#include <QtTest/QTest>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
void settle(int milliseconds = 120) {
  const int steps = std::max(1, milliseconds / 4);
  for (int iteration = 0; iteration < steps; ++iteration) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(4);
  }
}

QObject* find_object_recursive(QObject* root, const QString& name, QSet<QObject*>& visited) {
  if (root == nullptr || visited.contains(root))
    return nullptr;
  visited.insert(root);
  if (root->objectName() == name)
    return root;

  for (auto* child : root->children()) {
    if (auto* found = find_object_recursive(child, name, visited))
      return found;
  }
  if (auto* item = qobject_cast<QQuickItem*>(root)) {
    for (auto* child : item->childItems()) {
      if (auto* found = find_object_recursive(child, name, visited))
        return found;
    }
  }
  return nullptr;
}

QObject* find_object(QObject* root, const QString& name) {
  QSet<QObject*> visited;
  return find_object_recursive(root, name, visited);
}

bool click_object(QQuickWindow* window, QObject* object) {
  auto* item = qobject_cast<QQuickItem*>(object);
  if (window == nullptr || item == nullptr || !item->isVisible() || item->width() <= 0.0 || item->height() <= 0.0)
    return false;
  const QPoint point = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
  return true;
}

bool visible_panel(QObject* state, const QString& panelId) {
  QVariant result;
  return QMetaObject::invokeMethod(state, "visible", Q_RETURN_ARG(QVariant, result),
                                   Q_ARG(QVariant, panelId)) && result.toBool();
}

bool move_panel(QObject* state, const QString& panelId, const QString& dockId, int index) {
  QVariant result;
  return QMetaObject::invokeMethod(state, "movePanel", Q_RETURN_ARG(QVariant, result),
                                   Q_ARG(QVariant, panelId), Q_ARG(QVariant, dockId),
                                   Q_ARG(QVariant, index)) && result.toBool();
}

bool item_inside(QQuickItem* item, QQuickItem* parent, qreal epsilon = 1.0) {
  if (item == nullptr || parent == nullptr)
    return false;
  const QPointF position = item->mapToItem(parent, QPointF(0.0, 0.0));
  return position.x() >= -epsilon && position.y() >= -epsilon &&
         position.x() + item->width() <= parent->width() + epsilon &&
         position.y() + item->height() <= parent->height() + epsilon;
}

bool item_inside_window(QQuickItem* item, QQuickWindow* window, qreal epsilon = 1.0) {
  if (item == nullptr || window == nullptr)
    return false;
  const QPointF position = item->mapToScene(QPointF(0.0, 0.0));
  return position.x() >= -epsilon && position.y() >= -epsilon &&
         position.x() + item->width() <= window->width() + epsilon &&
         position.y() + item->height() <= window->height() + epsilon;
}

bool wait_for_boolean_property(QObject* object, const char* propertyName, bool expected,
                               int timeoutMilliseconds = 1200) {
  if (object == nullptr)
    return false;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMilliseconds) {
    if (object->property(propertyName).toBool() == expected)
      return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(4);
  }
  return object->property(propertyName).toBool() == expected;
}

bool wait_for_pointer_reachable(QQuickItem* item, QQuickWindow* window,
                                int timeoutMilliseconds = 1200) {
  if (item == nullptr || window == nullptr)
    return false;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMilliseconds) {
    if (item->isVisible() && item->width() > 0.0 && item->height() > 0.0 &&
        item_inside_window(item, window))
      return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(4);
  }
  return item->isVisible() && item->width() > 0.0 && item->height() > 0.0 &&
         item_inside_window(item, window);
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  const QString qml = QStringLiteral(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml") + QStringLiteral(PHOTO_EDITOR_SOURCE_DIR) + QStringLiteral(R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    objectName: "timelinePanelDockFixture"
    width: 980
    height: 700
    visible: true
    color: "transparent"

    Theme { id: sharedTheme }

    TimelinePanelDockState {
        id: dockState
        objectName: "timelinePanelDockState"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: sharedTheme.space8
        spacing: sharedTheme.gapDense

        TimelinePanelStrip {
            objectName: "timelinePanelStrip"
            Layout.alignment: Qt.AlignLeft
            dockState: dockState
            theme: sharedTheme
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: sharedTheme.gapDense

            TimelinePanelDock {
                objectName: "timelineDock_right"
                id: rightDock
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: sharedTheme
                dockState: dockState
                dockId: "right"
                title: "Right dock"
            }

            TimelinePanelDock {
                objectName: "timelineDock_auxiliary"
                id: auxiliaryDock
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: sharedTheme
                dockState: dockState
                dockId: "auxiliary"
                title: "Auxiliary dock"
            }

            TimelinePanelDock {
                objectName: "timelineDock_floating"
                id: floatingDock
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: sharedTheme
                dockState: dockState
                dockId: "floating"
                floatingPanelId: "effects"
                title: "Floating Effects"
            }
        }
    }
}
)qml" );
  component.setData(qml.toUtf8(), QUrl::fromLocalFile(QStringLiteral("timeline-panel-dock-ui-test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture)
    return fail(qPrintable(component.errorString()));

  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  QObject* state = find_object(fixture.get(), QStringLiteral("timelinePanelDockState"));
  if (window == nullptr || state == nullptr)
    return fail("The dock strip and panel state should instantiate in a desktop window");
  window->show();
  settle(220);

  const QStringList panelIds = {QStringLiteral("media"), QStringLiteral("source"),
                                QStringLiteral("effects"), QStringLiteral("inspector"),
                                QStringLiteral("color"), QStringLiteral("mixer"),
                                QStringLiteral("keyframes"), QStringLiteral("titles"),
                                QStringLiteral("captions"), QStringLiteral("transcript"),
                                QStringLiteral("scopes"), QStringLiteral("review")};
  const QVariantMap panelLabels = {
      {QStringLiteral("media"), QStringLiteral("Library")},
      {QStringLiteral("source"), QStringLiteral("Source")},
      {QStringLiteral("effects"), QStringLiteral("Effects")},
      {QStringLiteral("inspector"), QStringLiteral("Controls")},
      {QStringLiteral("color"), QStringLiteral("Color")},
      {QStringLiteral("mixer"), QStringLiteral("Mixer")},
      {QStringLiteral("keyframes"), QStringLiteral("Keyframes")},
      {QStringLiteral("titles"), QStringLiteral("Titles")},
      {QStringLiteral("captions"), QStringLiteral("Captions")},
      {QStringLiteral("transcript"), QStringLiteral("Transcript")},
      {QStringLiteral("scopes"), QStringLiteral("Scopes")},
      {QStringLiteral("review"), QStringLiteral("Notes")},
  };
  const auto open_panels = [&] {
    if (!click_object(window, find_object(fixture.get(), QStringLiteral("timelinePanelsMenuButtonPrimaryAction")))) return false;
    auto* menu = find_object(fixture.get(), QStringLiteral("timelinePanelsMenuButtonPopup"));
    return wait_for_boolean_property(menu, "opened", true);
  };
  const auto select_panel = [&](const QString& id) {
    const int index = static_cast<int>(panelIds.indexOf(id));
    if (index < 0 || !open_panels()) return false;
    QTest::keyClick(window, Qt::Key_Home);
    for (int position = 0; position < index; ++position)
      QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Return);
    if (!wait_for_boolean_property(find_object(fixture.get(), QStringLiteral("timelinePanelsMenuButtonPopup")), "visible", false)) return false;
    settle();
    QVariant dock;
    QVariant active;
    QMetaObject::invokeMethod(state, "dock", Q_RETURN_ARG(QVariant, dock), Q_ARG(QVariant, id));
    QMetaObject::invokeMethod(state, "activePanel", Q_RETURN_ARG(QVariant, active), Q_ARG(QVariant, dock));
    if (!visible_panel(state, id) || active.toString() != id)
      std::cerr << "Panel menu selection: requested=" << id.toStdString() << " active=" << active.toString().toStdString() << '\n';
    return visible_panel(state, id) && active.toString() == id;
  };
  const auto dock_action = [&](const QString& dock, const QString& action) {
    if (!click_object(window, find_object(fixture.get(), QStringLiteral("timelineDockMoveButton_") + dock)))
      return false;
    auto* menu = find_object(fixture.get(), QStringLiteral("timelineDockMoveMenu_") + dock);
    if (!wait_for_boolean_property(menu, "opened", true)) return false;
    if (!click_object(window, find_object(menu, QStringLiteral("timelineDockMenu") + action + "_" + dock))) return false;
    return wait_for_boolean_property(menu, "visible", false);
  };
  if (!open_panels()) return fail("The labeled Panels menu must be reachable through pointer input");
  for (int index = 0; index < panelIds.size(); ++index) {
    auto* row = qobject_cast<QQuickItem*>(find_object(fixture.get(),
        QStringLiteral("timelinePanelsMenuButtonPopupItem%1").arg(index)));
    if (!row || !item_inside_window(row, window) ||
        row->property("itemLabel").toString() != panelLabels.value(panelIds[index]).toString())
      return fail("Every named panel must be readable inside the Panels menu");
  }
  QTest::keyClick(window, Qt::Key_Escape);
  settle();

  if (!select_panel(QStringLiteral("mixer")) ||
      !visible_panel(state, QStringLiteral("mixer")))
    return fail("Choosing a panel menu entry should reveal the panel");
  if (!select_panel(QStringLiteral("effects")) ||
      !visible_panel(state, QStringLiteral("mixer")) || !visible_panel(state, QStringLiteral("effects")))
    return fail("Focusing another panel should leave Mixer open");

  const auto drag_panel = [&](const QString& id, const QString& destination) {
    auto* tab = qobject_cast<QQuickItem*>(find_object(fixture.get(), QStringLiteral("timelineDockTabDrag_") + id));
    auto* host = qobject_cast<QQuickItem*>(find_object(fixture.get(), QStringLiteral("timelineDock_") + destination));
    if (!tab || !host) return false;
    const auto from = tab->mapToScene(QPointF(tab->width()/2, tab->height()/2)).toPoint();
    const auto to = host->mapToScene(QPointF(host->width()/2, host->height()/2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 8; ++step) QTest::mouseMove(window, from + (to-from)*step/8, 10);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    settle();
    QVariant dock;
    QMetaObject::invokeMethod(state, "dock", Q_RETURN_ARG(QVariant, dock), Q_ARG(QVariant, id));
    return dock.toString() == destination && state->property("draggedPanelId").toString().isEmpty();
  };
  if (!drag_panel(QStringLiteral("effects"), QStringLiteral("auxiliary")) ||
      !drag_panel(QStringLiteral("effects"), QStringLiteral("right")))
    return fail("Dragging a tab between real dock surfaces must move its panel and end the drag");

  if (!click_object(window, find_object(fixture.get(), QStringLiteral("timelineDockMoveButton_right"))) ||
      !click_object(window, find_object(find_object(fixture.get(), QStringLiteral("timelineDockMoveMenu_right")), QStringLiteral("timelineDockMove_auxiliary"))))
    return fail("The dock move menu should open and accept a destination dock");
  QVariant effects_dock;
  QMetaObject::invokeMethod(state, "dock", Q_RETURN_ARG(QVariant, effects_dock),
                            Q_ARG(QVariant, QStringLiteral("effects")));
  if (effects_dock.toString() != QStringLiteral("auxiliary"))
    return fail("Moving a tab from the dock header should update its dock placement");

  if (!dock_action(QStringLiteral("auxiliary"), QStringLiteral("Float")) ||
      !dock_action(QStringLiteral("floating"), QStringLiteral("Float")))
    return fail("Float and Dock controls should operate from the current dock and floating window");
  effects_dock.clear();
  QMetaObject::invokeMethod(state, "dock", Q_RETURN_ARG(QVariant, effects_dock),
                            Q_ARG(QVariant, QStringLiteral("effects")));
  if (effects_dock.toString() != QStringLiteral("auxiliary"))
    return fail("Dock should restore a floated tab to its previous dock");

  if (!dock_action(QStringLiteral("auxiliary"), QStringLiteral("Close")) ||
      visible_panel(state, QStringLiteral("effects")))
    return fail("The explicit tab close control should hide a panel while its menu entry remains available");
  if (!select_panel(QStringLiteral("effects")) ||
      !visible_panel(state, QStringLiteral("effects")))
    return fail("A closed panel should be reopenable from the Panels menu");

  for (int index = 0; index < panelIds.size(); ++index) {
    if (!move_panel(state, panelIds[index], QStringLiteral("auxiliary"), index))
      return fail("The narrow dock fixture should place every panel in the same tab group");
  }
  auto* right_dock = qobject_cast<QQuickItem*>(find_object(fixture.get(), QStringLiteral("timelineDock_right")));
  auto* auxiliary_dock = qobject_cast<QQuickItem*>(find_object(fixture.get(), QStringLiteral("timelineDock_auxiliary")));
  auto* floating_dock = qobject_cast<QQuickItem*>(find_object(fixture.get(), QStringLiteral("timelineDock_floating")));
  if (right_dock == nullptr || auxiliary_dock == nullptr || floating_dock == nullptr)
    return fail("The narrow dock fixture should expose each retained dock host");
  right_dock->setVisible(false);
  floating_dock->setVisible(false);
  QVariant focus_scopes;
  if (!QMetaObject::invokeMethod(state, "focusPanel", Q_RETURN_ARG(QVariant, focus_scopes),
          Q_ARG(QVariant, QStringLiteral("scopes"))) || !focus_scopes.toBool())
    return fail("The narrow dock test should focus Scopes before resizing");
  window->resize(320, 360);
  settle(220);

  if (auxiliary_dock->property("headerHeight").toInt() > 40)
    return fail("A dock with many tabs should keep its header to one compact row at narrow width");
  if (auxiliary_dock->property("activePanelId").toString() != QStringLiteral("scopes"))
    return fail("The most recently focused tab should remain the active tab after the dock narrows");

  auto* tab_viewport = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockTabViewport_auxiliary")));
  auto* active_tab_item = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockTabItem_scopes")));
  auto* active_tab = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockTab_scopes")));
  if (tab_viewport == nullptr || active_tab_item == nullptr || active_tab == nullptr ||
      !active_tab_item->isVisible() || !active_tab->isVisible() ||
      !item_inside(active_tab_item, tab_viewport)) {
    std::cerr << "Active-tab reveal diagnostic: viewport=";
    if (tab_viewport == nullptr) {
      std::cerr << "<missing>";
    } else {
      std::cerr << tab_viewport->width() << 'x' << tab_viewport->height()
                << " content=" << tab_viewport->property("contentWidth").toReal()
                << "x" << tab_viewport->property("contentHeight").toReal()
                << " offset=" << tab_viewport->property("contentX").toReal();
    }
    std::cerr << " activeTab=";
    if (active_tab_item == nullptr) {
      std::cerr << "<missing>";
    } else if (tab_viewport != nullptr) {
      const QPointF position = active_tab_item->mapToItem(tab_viewport, QPointF(0.0, 0.0));
      std::cerr << position.x() << ',' << position.y() << ' '
                << active_tab_item->width() << 'x' << active_tab_item->height()
                << " visible=" << active_tab_item->isVisible();
    }
    std::cerr << '\n';
    return fail("The active tab should auto-scroll fully inside the horizontally scrolling tab strip");
  }

  for (const QString& panelId : panelIds) {
    auto* tab = qobject_cast<QQuickItem*>(find_object(
        fixture.get(), QStringLiteral("timelineDockTab_") + panelId));
    if (tab == nullptr || !tab->property("showText").toBool() ||
        tab->property("text").toString() != panelLabels.value(panelId).toString())
      return fail("Every dock tab should retain its complete readable label while scrolling");
  }
  auto* active_close = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockClose_scopes")));
  if (active_close == nullptr || active_close->isVisible())
    return fail("Tabs must not reserve or display repeated close buttons");

  auto* first_tab_item = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockTabItem_media")));
  if (first_tab_item == nullptr ||
      std::abs(first_tab_item->mapToItem(tab_viewport, QPointF(0.0, 0.0)).y() -
               active_tab_item->mapToItem(tab_viewport, QPointF(0.0, 0.0)).y()) > 1.0)
    return fail("All dock tabs should stay on one horizontal row instead of wrapping");

  auto* move_button = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockMoveButton_auxiliary")));
  auto* float_button = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockFloatButton_auxiliary")));
  const auto action_is_accessible = [window](QQuickItem* action, const QString& actionName) {
    if (action == nullptr || !action->isVisible() || !action->property("enabled").toBool() ||
        action->property("showText").toBool() || !item_inside_window(action, window))
      return false;
    const QString accessibleName = action->property("accessibleName").toString();
    const QString tooltip = action->property("tooltipText").toString();
    return accessibleName.contains(actionName, Qt::CaseInsensitive) &&
           tooltip.contains(actionName, Qt::CaseInsensitive);
  };
  if (!action_is_accessible(move_button, QStringLiteral("Options")) ||
      (float_button && float_button->isVisible()))
    return fail("One accessible Options button should replace repeated Close, Move and Float buttons");

  if (tab_viewport->property("contentX").toReal() <= 0.0)
    return fail("Selecting the last tab should scroll the tab strip to reveal it");

  auto* keyboard_tab = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockTab_keyframes")));
  if (keyboard_tab == nullptr || !keyboard_tab->property("activeFocusOnTab").toBool())
    return fail("Dock tabs should remain reachable through keyboard focus");
  keyboard_tab->forceActiveFocus(Qt::TabFocusReason);
  settle();
  auto* keyboard_tab_item = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockTabItem_keyframes")));
  if (!keyboard_tab->property("activeFocus").toBool() ||
      !item_inside(keyboard_tab_item, tab_viewport))
    return fail("Keyboard-focused dock tabs should be auto-revealed within the horizontal viewport");

  tab_viewport->setProperty("contentX", 0.0);
  QTest::mouseMove(window, QPoint(window->width() - 4, window->height() - 4));
  settle();
  auto* media_tab = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockTab_media")));
  auto* media_close = qobject_cast<QQuickItem*>(find_object(
      fixture.get(), QStringLiteral("timelineDockClose_media")));
  if (media_tab == nullptr || media_close == nullptr || media_close->isVisible())
    return fail("Inactive dock tabs should keep their close action quiet until hovered or focused");
  const QPoint mediaCenter = media_tab->mapToScene(
      QPointF(media_tab->width() / 2.0, media_tab->height() / 2.0)).toPoint();
  QTest::mouseMove(window, mediaCenter);
  settle();
  if (media_close->isVisible())
    return fail("Hovering must not introduce another close button or resize a tab");
  QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, mediaCenter);
  settle();
  auto* media_menu = find_object(fixture.get(), QStringLiteral("timelineDockMoveMenu_auxiliary"));
  if (!click_object(window, find_object(media_menu, QStringLiteral("timelineDockMenuClose_auxiliary"))) ||
      visible_panel(state, QStringLiteral("media")))
    return fail("Right-clicking a tab must offer a working contextual Close action");
  if (!wait_for_boolean_property(media_menu, "visible", false)) return fail("The tab context menu must finish closing before opening Panels");

  if (!select_panel(QStringLiteral("scopes")) ||
      auxiliary_dock->property("activePanelId").toString() != QStringLiteral("scopes"))
    return fail("The Panels menu should restore Scopes as the active dock panel before Move");

  if (!click_object(window, move_button)) {
    const QPointF point = move_button != nullptr
        ? move_button->mapToScene(QPointF(move_button->width() / 2.0, move_button->height() / 2.0))
        : QPointF();
    std::cerr << "Move-button pointer diagnostic: visible="
              << (move_button != nullptr && move_button->isVisible())
              << " enabled=" << (move_button != nullptr && move_button->property("enabled").toBool())
              << " center=" << point.x() << ',' << point.y() << '\n';
    return fail("The compact dock header Move control should receive pointer input");
  }
  QObject* move_menu = find_object(fixture.get(), QStringLiteral("timelineDockMoveMenu_auxiliary"));
  if (!wait_for_boolean_property(move_menu, "opened", true))
    return fail("The compact dock header Move control should open its destination menu");
  if (!click_object(window, move_button))
    return fail("The dock Move control should accept a second click while its menu is open");
  settle();
  if (move_menu == nullptr || move_menu->property("visible").toBool())
    return fail("Clicking the same dock Move control again should close its menu");
  if (!click_object(window, move_button) || !wait_for_boolean_property(move_menu, "opened", true))
    return fail("The dock Move control should reopen after its menu is toggled closed");
  QObject* bottom_target = find_object(move_menu, QStringLiteral("timelineDockMove_bottom"));
  auto* bottom_target_item = qobject_cast<QQuickItem*>(bottom_target);
  if (!wait_for_pointer_reachable(bottom_target_item, window) ||
      !click_object(window, bottom_target)) {
    const QPointF point = bottom_target_item != nullptr
        ? bottom_target_item->mapToScene(QPointF(bottom_target_item->width() / 2.0,
                                                 bottom_target_item->height() / 2.0))
        : QPointF();
    std::cerr << "Move-destination pointer diagnostic: found=" << (bottom_target_item != nullptr)
              << " visible=" << (bottom_target_item != nullptr && bottom_target_item->isVisible())
              << " bounds=";
    if (bottom_target_item != nullptr) {
      std::cerr << point.x() << ',' << point.y() << ' '
                << bottom_target_item->width() << 'x' << bottom_target_item->height()
                << " insideWindow=" << item_inside_window(bottom_target_item, window);
    } else {
      std::cerr << "<missing>";
    }
    std::cerr << '\n';
    return fail("The compact dock header should expose a pointer-reachable Move destination");
  }
  QVariant scopes_dock;
  QMetaObject::invokeMethod(state, "dock", Q_RETURN_ARG(QVariant, scopes_dock),
                            Q_ARG(QVariant, QStringLiteral("scopes")));
  if (scopes_dock.toString() != QStringLiteral("bottom"))
    return fail("The narrow header Move action should update the selected tab placement");
  if (!move_panel(state, QStringLiteral("scopes"), QStringLiteral("auxiliary"), 999))
    return fail("The compact dock fixture should restore the panel after checking Move");
  settle();
  if (!dock_action(QStringLiteral("auxiliary"), QStringLiteral("Float")))
    return fail("The compact dock header Float action should remain operable by pointer");
  scopes_dock.clear();
  QMetaObject::invokeMethod(state, "dock", Q_RETURN_ARG(QVariant, scopes_dock),
                            Q_ARG(QVariant, QStringLiteral("scopes")));
  if (scopes_dock.toString() != QStringLiteral("floating"))
    return fail("The narrow header Float action should update the selected tab placement");
  return EXIT_SUCCESS;
}
