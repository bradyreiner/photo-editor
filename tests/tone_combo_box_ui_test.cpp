#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
void settle() {
  // Check settled popup state after the shared 150 ms opening transition.
  for (auto i = 0; i < 40; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

void send_key(QQuickWindow& window, Qt::Key key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &release);
}

QString accessible_name(QObject* object) {
  auto* interface = object == nullptr ? nullptr : QAccessible::queryAccessibleInterface(object);
  return interface == nullptr ? QString() : interface->text(QAccessible::Name).trimmed();
}

QObject* find_visual_child(QQuickItem* parent, const QString& object_name) {
  if (parent == nullptr)
    return nullptr;
  for (auto* child : parent->childItems()) {
    if (child->objectName() == object_name)
      return child;
    if (auto* nested = find_visual_child(child, object_name))
      return nested;
  }
  return nullptr;
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
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ToneComboBox.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* combo = qobject_cast<QQuickItem*>(object.get());
  if (combo == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("ToneComboBox component should instantiate");
  }

  QQuickWindow window;
  window.resize(360, 260);
  combo->setObjectName(QStringLiteral("timelineAudioChannelModeCombo"));
  combo->setSize(QSizeF(120, 40));
  combo->setProperty("accessibleLabel", QString());
  combo->setProperty("model", [&]() {
    QVariantList values;
    for (auto index = 0; index < 30; ++index)
      values.append(QStringLiteral("Option %1").arg(index));
    return values;
  }());
  combo->setProperty("currentIndex", 2);
  combo->setParentItem(window.contentItem());
  window.show();
  settle();

  auto* accessible = QAccessible::queryAccessibleInterface(combo);
  if (accessible == nullptr || accessible->role() != QAccessible::ComboBox ||
      accessible_name(combo) != QStringLiteral("Timeline Audio Channel Mode combo box") ||
      accessible->text(QAccessible::Description) != QStringLiteral("Option 2")) {
    return fail("ToneComboBox should expose its semantic name, combo-box role, and selected value");
  }

  combo->setProperty("accessibleLabel", QStringLiteral("Audio output channel mode"));
  settle();
  if (accessible_name(combo) != QStringLiteral("Audio output channel mode"))
    return fail("ToneComboBox should prefer the explicit accessible label over its object name");

  auto* popup = object->findChild<QObject*>(QStringLiteral("timelineAudioChannelModeComboPopup"));
  if (popup == nullptr)
    return fail("ToneComboBox should expose its popup for runtime geometry checks");

  for (const auto y : {8.0, 210.0}) {
    combo->setX(220.0);
    combo->setY(y);
    settle();
    if (!QMetaObject::invokeMethod(popup, "open"))
      return fail("ToneComboBox popup should open through the shared control API");
    settle();
    if (!popup->property("visible").toBool())
      return fail("ToneComboBox should show its option popup");

    auto* content = qvariant_cast<QQuickItem*>(popup->property("contentItem"));
    if (content == nullptr)
      return fail("ToneComboBox popup should provide a scrollable option viewport");
    const QRectF viewport = content->mapRectToScene(QRectF(0, 0, content->width(), content->height()));
    const QRectF windowBounds(QPointF(0, 0), QSizeF(window.size()));
    const auto maximum_height = combo->property("maximumPopupHeight").toDouble();
    auto* popup_parent = qvariant_cast<QQuickItem*>(popup->property("parent"));
    const auto parent_bounds = popup_parent == nullptr
                                   ? QRectF()
                                   : popup_parent->mapRectToScene(
                                         QRectF(0, 0, popup_parent->width(), popup_parent->height()));
    const auto combo_scene_origin = combo->mapToScene(QPointF(0, 0));
    const auto combo_popup_parent_origin = popup_parent == nullptr
                                               ? QPointF()
                                               : combo->mapToItem(popup_parent, QPointF(0, 0));
    const auto popup_margin = combo->property("popupHorizontalMargin").toDouble();
    const auto popup_width = popup->property("width").toDouble();
    const auto expected_popup_x = std::max(
        popup_margin,
        std::min(combo_popup_parent_origin.x(), parent_bounds.width() - popup_width - popup_margin));
    const auto actual_popup_x = popup->property("x").toDouble();
    if (std::abs(actual_popup_x - expected_popup_x) > 1.0) {
      std::cerr << "Popup placement: comboX=" << combo->x()
                << " comboSceneX=" << combo_scene_origin.x()
                << " comboParentX=" << combo_popup_parent_origin.x()
                << " popupX=" << actual_popup_x
                << " expectedX=" << expected_popup_x
                << " viewportX=" << viewport.x()
                << " viewportRight=" << viewport.right() << '\n';
      return fail("ToneComboBox popup should stay anchored near the control when clamped to the right edge");
    }
    const auto should_open_above = y > window.height() / 2;
    const auto opens_above = combo->property("popupOpensAbove").toBool();
    const auto combo_scene_top = combo->mapToScene(QPointF(0, 0)).y();
    if (opens_above != should_open_above ||
        (opens_above && viewport.bottom() >= combo_scene_top) ||
        (!opens_above && viewport.top() <= combo_scene_top + combo->height())) {
      std::cerr << "Vertical placement: comboY=" << combo->y()
                << " comboSceneTop=" << combo_scene_top
                << " anchorY=" << combo->property("popupAnchorY").toDouble()
                << " opensAbove=" << opens_above
                << " popupY=" << popup->property("y").toDouble()
                << " popupHeight=" << popup->property("height").toDouble()
                << " viewportY=" << viewport.y()
                << " viewportBottom=" << viewport.bottom() << '\n';
      return fail("ToneComboBox popup should flip above the control when the remaining space below is too small");
    }
    if (!windowBounds.contains(viewport) || popup->property("height").toDouble() > maximum_height + 1.0) {
      std::cerr << "Popup geometry: x=" << viewport.x() << " y=" << viewport.y()
                << " width=" << viewport.width() << " height=" << viewport.height()
                << " popupHeight=" << popup->property("height").toDouble()
                << " maxHeight=" << maximum_height
                << " opensAbove=" << combo->property("popupOpensAbove").toBool()
                << " availableAbove=" << combo->property("popupSpaceAbove").toDouble()
                << " availableBelow=" << combo->property("popupSpaceBelow").toDouble() << '\n';
      return fail("ToneComboBox option viewport should stay inside short windows and obey its height cap");
    }

    auto* selected_marker = qobject_cast<QQuickItem*>(
        find_visual_child(content, QStringLiteral("timelineAudioChannelModeComboSelectedMarker2")));
    if (selected_marker == nullptr || !selected_marker->isVisible()) {
      std::cerr << "Selected marker: found=" << (selected_marker != nullptr)
                << " visible=" << (selected_marker != nullptr && selected_marker->isVisible())
                << " currentIndex=" << combo->property("currentIndex").toInt()
                << " popupVisible=" << popup->property("visible").toBool() << '\n';
      return fail("ToneComboBox should mark the selected option inside the popup");
    }

    const auto capture_dir = qEnvironmentVariable("PHOTO_EDITOR_COMBO_CAPTURE_DIR");
    if (!capture_dir.isEmpty()) {
      QDir().mkpath(capture_dir);
      const auto name = y < 100 ? QStringLiteral("combo-opens-below.png")
                                : QStringLiteral("combo-opens-above.png");
      if (!window.grabWindow().save(QDir(capture_dir).filePath(name)))
        return fail("ToneComboBox visual proof screenshot should save");
    }

    if (!QMetaObject::invokeMethod(popup, "close"))
      return fail("ToneComboBox popup should close through the shared control API");
    settle();
  }

  combo->setY(8.0);
  combo->forceActiveFocus(Qt::TabFocusReason);
  send_key(window, Qt::Key_Space);
  settle();
  if (!popup->property("visible").toBool())
    return fail("ToneComboBox should open from the keyboard while focused");
  send_key(window, Qt::Key_Down);
  settle();
  if (combo->property("highlightedIndex").toInt() != 3)
    return fail("ToneComboBox should move its highlighted option with the keyboard");
  send_key(window, Qt::Key_Escape);
  settle();
  if (popup->property("visible").toBool() || combo->property("currentIndex").toInt() != 2 ||
      window.activeFocusItem() != combo)
    return fail("Escape should cancel the highlighted option and return focus to ToneComboBox");

  send_key(window, Qt::Key_Space);
  settle();
  send_key(window, Qt::Key_Down);
  settle();
  send_key(window, Qt::Key_Return);
  settle();
  if (popup->property("visible").toBool() || combo->property("currentIndex").toInt() != 3 ||
      window.activeFocusItem() != combo)
    return fail("Enter should commit the highlighted option and return focus to ToneComboBox");

  QVariantList role_model;
  for (auto index = 0; index < 30; ++index) {
    QVariantMap option;
    option.insert(QStringLiteral("id"), QStringLiteral("shot-%1").arg(index));
    option.insert(QStringLiteral("name"), QStringLiteral("Saved shot %1").arg(index));
    role_model.append(option);
  }
  combo->setProperty("model", role_model);
  combo->setProperty("textRole", QStringLiteral("name"));
  combo->setProperty("valueRole", QStringLiteral("id"));
  combo->setProperty("currentIndex", 3);
  combo->setY(8.0);
  settle();
  if (combo->property("displayText").toString() != QStringLiteral("Saved shot 3") ||
      combo->property("currentValue").toString() != QStringLiteral("shot-3") ||
      accessible->text(QAccessible::Description) != QStringLiteral("Saved shot 3")) {
    return fail("ToneComboBox should preserve display and selected-value semantics for role-based models");
  }
  if (!QMetaObject::invokeMethod(popup, "open"))
    return fail("ToneComboBox should open a popup for role-based models");
  settle();
  auto* content = qvariant_cast<QQuickItem*>(popup->property("contentItem"));
  auto* selected_option = qobject_cast<QQuickItem*>(
      find_visual_child(content, QStringLiteral("timelineAudioChannelModeComboOption3")));
  if (selected_option == nullptr ||
      selected_option->property("optionText").toString() != QStringLiteral("Saved shot 3")) {
    return fail("ToneComboBox option delegates should use the configured text role");
  }
  if (!QMetaObject::invokeMethod(popup, "close"))
    return fail("ToneComboBox should close a role-based model popup");
  settle();

  std::cout << "ToneComboBox accessibility naming, role-based values, selected marker, and popup bounds passed\n";
  return 0;
}
