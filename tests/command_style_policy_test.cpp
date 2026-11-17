#include "qml_style_policy.h"
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QtTest/QTest>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (int i = 0; i < 4; ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(3); }
}
QObject* part(QObject* obj, const char* name) { return qvariant_cast<QObject*>(QQmlProperty(obj, QString::fromLatin1(name)).read()); }
int fail(const QString& error) { std::cerr << error.toStdString() << '\n'; return 1; }
bool press_accessible(QObject* object) {
  auto* interface = QAccessible::queryAccessibleInterface(object);
  auto* actions = interface ? interface->actionInterface() : nullptr;
  if (!actions || !actions->actionNames().contains(QAccessibleActionInterface::pressAction())) return false;
  actions->doAction(QAccessibleActionInterface::pressAction()); settle(); return true;
}
void selected(QObject* object, bool value) {
  if (object->objectName() == QStringLiteral("commandMenuRow")) {
    auto data = object->property("modelData").toMap(); data.insert(QStringLiteral("checked"), value); object->setProperty("modelData", data); return;
  }
  if (object->property("activeTool").isValid()) { object->setProperty("activeTool", value ? "shape" : ""); return; }
  for (const auto* key : {"selected", "active", "highlighted"})
    if (object->property(key).isValid()) { object->setProperty(key, value); return; }
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QQmlEngine engine;
  QStringList errors;
  QObject::connect(&engine, &QQmlEngine::warnings, [&errors](const QList<QQmlError>& warnings) {
    for (const auto& w : warnings) errors.append(w.toString());
  });
  QQmlComponent settings_component(&engine);
  settings_component.setData(R"qml(import QtQml
QtObject {
    property string interfaceDensity: "balanced"
    property real interfaceScale: 1
    property bool highContrast: false
    property bool reducedMotion: true
    property string systemUiFontFamily: ""
})qml", QUrl());
  std::unique_ptr<QObject> settings(settings_component.create());
  if (!settings) return fail(settings_component.errorString());
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), settings.get());
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: grid.width + 40; height: grid.implicitHeight + 80; visible: true; color: "#181818"
    property int activations: 0
    Theme { id: theme }
    GridLayout {
        id: grid; x: 20; y: 20; width: theme.scaled(860); columns: 3
        columnSpacing: theme.space16; rowSpacing: theme.space16
        ActionButton { objectName: "commandAction"; text: "Apply adjustments"; iconName: "check"; Layout.fillWidth: true; onClicked: fixture.activations++ }
        MiniButton { objectName: "commandMini"; text: "Copy settings"; iconName: "copy"; Layout.fillWidth: true; onClicked: fixture.activations++ }
        ToolButton { objectName: "commandTool"; label: "Select subject"; iconName: "select"; showLabel: true; Layout.fillWidth: true; onClicked: fixture.activations++ }
        FilterChip { objectName: "commandChip"; text: "Selected photographs"; iconName: "photo"; Layout.fillWidth: true; onClicked: fixture.activations++ }
        SidebarButton { objectName: "commandSidebar"; title: "Collections for review and delivery"; iconName: "folder"; badgeText: "24"; Layout.fillWidth: true; onClicked: fixture.activations++ }
        PreferenceNavRow { objectName: "commandPreference"; title: "Display and color management"; subtitle: "Monitor profiles and calibration"; Layout.fillWidth: true; onActivated: fixture.activations++ }
        EditInspectorTabButton { objectName: "commandEditTab"; label: "Adjustments"; tabKey: "adjust"; Layout.fillWidth: true; onSelectedRequested: fixture.activations++ }
        NavigationTab { objectName: "commandUnderline"; text: "Photographs"; underlineSelection: true; Layout.fillWidth: true; onClicked: fixture.activations++ }
        EditRailButton { objectName: "commandRail"; label: "Crop"; iconName: "crop"; onTriggered: fixture.activations++ }
        CarouselToolRailButton { objectName: "commandCarousel"; toolData: ({value: "shape", label: "Shapes", key: "U"}); menuItems: [{key: "related", label: "Related action"}]; compactLabelMode: true; Layout.fillWidth: true; onToolSelected: fixture.activations++ }
        PopupMenuRow { objectName: "commandMenuRow"; index: 0; modelData: ({key: "copy", label: "Copy settings", shortcut: "Ctrl+Shift+Alt+S", icon: "copy"}); Layout.fillWidth: true; onActivated: fixture.activations++ }
        SplitButton { objectName: "commandSplit"; text: "Review selected files"; menuItems: [{key: "folder", label: "Review folder"}]; Layout.fillWidth: true; onClicked: fixture.activations++ }
        MenuButton { objectName: "commandMenu"; text: "More options"; menuItems: [{key: "copy", label: "Copy"}]; Layout.fillWidth: true; onClicked: fixture.activations++ }
        ActionButton { objectName: "commandWrapped"; text: "Review destination and backup"; allowLabelWrap: true; Layout.preferredWidth: theme.scaled(180); onClicked: fixture.activations++ }
        ActionButton { objectName: "commandDanger"; text: "Remove selected"; tone: "danger"; Layout.fillWidth: true; onClicked: fixture.activations++ }
    }
    MenuButton {
        objectName: "commandTightMenu"; x: 20; y: grid.y + grid.height + 16
        text: "More"; secondaryActionMenu: true; menuTooltipText: "Adjustment actions"
        menuItems: [{key: "copy", label: "Copy"}]
        onClicked: fixture.activations++
    }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/command-style-policy-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) return fail(component.errorString());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  const auto find = [&](const QString& name) { return fixture->findChild<QQuickItem*>(name); };
  auto* carousel_menu = fixture->findChild<QObject*>(QStringLiteral("carouselToolMenu_shape"));
  auto* carousel_hotspot = find(QStringLiteral("carouselToolMenuHotspot_shape"));
  if (!window || !carousel_menu || !carousel_hotspot)
    return fail("Carousel tool variants should expose their menu hotspot");
  const auto carousel_hotspot_center = carousel_hotspot->mapToItem(
      window->contentItem(), QPointF(carousel_hotspot->width() / 2, carousel_hotspot->height() / 2)).toPoint();
  int carousel_click_index = 0;
  for (const bool expected_visible : {true, false}) {
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, carousel_hotspot_center);
    settle();
    const bool actual_visible = carousel_menu->property("visible").toBool();
    if (actual_visible != expected_visible) {
      std::cerr << "Carousel hotspot click " << carousel_click_index
                << " expected visible=" << expected_visible
                << " actual visible=" << actual_visible
                << " point=" << carousel_hotspot_center.x() << ',' << carousel_hotspot_center.y()
                << " hotspot=" << carousel_hotspot->width() << 'x' << carousel_hotspot->height()
                << '\n';
      return fail("Clicking the Carousel menu hotspot repeatedly should alternate open and closed");
    }
    ++carousel_click_index;
  }
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_COMMAND_STYLE_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  const QStringList names{"commandAction", "commandMini", "commandTool", "commandChip", "commandSidebar", "commandPreference", "commandEditTab", "commandUnderline", "commandRail", "commandCarousel", "commandMenuRow", "commandSplit", "commandMenu", "commandWrapped", "commandDanger", "commandTightMenu"};
  int cases = 0;
  for (const auto* density : {"compact", "balanced", "comfortable"}) {
    for (const auto scale : {0.9, 1.0, 2.0}) {
      for (const bool contrast : {false, true}) {
        settings->setProperty("interfaceDensity", density); settings->setProperty("interfaceScale", scale); settings->setProperty("highContrast", contrast);
        window->contentItem()->forceActiveFocus(Qt::OtherFocusReason); settle();
        const auto context = QStringLiteral("%1 / %2x / contrast=%3: ").arg(QString::fromLatin1(density)).arg(scale).arg(contrast);
        for (const auto& name : names) {
          auto* owner = find(name);
          auto* primary = find(name + "PrimaryAction");
          auto* control = primary ? primary : owner;
          if (!owner || !control || control->width() < std::round(24 * scale) || control->height() < std::round(24 * scale)) return fail(context + name + " pointer target is too small");
          const auto size = owner->size();
          const auto position = owner->position();
          selected(owner, true); settle();
          if (owner->size() != size || owner->position() != position) return fail(context + name + " selection changes layout");
          auto issues = qml_popup_style_violations(fixture.get());
          if (!issues.isEmpty()) return fail(context + issues.join('\n'));
          const auto center = control->mapToItem(window->contentItem(), QPointF(control->width() / 2, control->height() / 2)).toPoint();
          QTest::mouseMove(window, center); settle();
          if (owner->size() != size || owner->position() != position) return fail(context + name + " hover changes layout");
          control->forceActiveFocus(Qt::TabFocusReason); settle();
          auto* bg = part(control, "background");
          if (!bg || bg->property("borderWidth").toDouble() < std::round(2 * scale)) return fail(context + name + " needs visible keyboard focus");
          const auto before = fixture->property("activations").toInt();
          QTest::keyClick(window, Qt::Key_Space); settle();
          const bool menu_opener = owner->property("primaryOpensMenu").toBool();
          auto* popup = menu_opener ? fixture->findChild<QObject*>(name + "Popup") : nullptr;
          if (menu_opener) {
            if (!popup || !popup->property("visible").toBool() ||
                fixture->property("activations").toInt() != before)
              return fail(context + name + " keyboard activation must open its menu once");
            if (!press_accessible(control) || popup->property("visible").toBool())
              return fail(context + name + " accessible activation must close its menu once");
          } else {
            if (fixture->property("activations").toInt() != before + 1) return fail(context + name + " keyboard activation must fire exactly once");
            if (!press_accessible(control) || fixture->property("activations").toInt() != before + 2) return fail(context + name + " accessible activation must fire exactly once");
          }
          owner->setProperty("enabled", false); settle();
          press_accessible(control);
          if (fixture->property("activations").toInt() != before + (menu_opener ? 0 : 2) ||
              (popup && popup->property("visible").toBool()))
            return fail(context + name + " disabled command activated");
          owner->setProperty("enabled", true); selected(owner, false);
          window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
          QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2)); settle();
        }
        auto* menu_row = find("commandMenuRow");
        selected(menu_row, true); settle();
        auto* menu_icon = menu_row->findChild<QQuickItem*>("sharedMenuIcon");
        auto* menu_check = menu_row->findChild<QQuickItem*>("sharedMenuCheck");
        if (!menu_icon || menu_icon->property("name").toString() != QStringLiteral("copy") ||
            !menu_check || !menu_check->isVisible() || menu_check->opacity() < 0.99)
          return fail(context + "checked menu rows must preserve their identifying icon and a separate check");
        selected(menu_row, false); settle();
        for (const auto* name : {"commandAction", "commandMini", "commandTool"}) {
          auto* icon = find(name)->findChild<QQuickItem*>("sharedCommandIcon");
          if (!icon || icon->isVisible())
            return fail(context + name + " labelled command must default to text only");
          find(name)->setProperty("showLeadingIcon", true); settle();
          if (!icon->isVisible()) return fail(context + name + " explicit leading icon must remain available");
          find(name)->setProperty("showLeadingIcon", false); settle();
        }
        // Ordinary repeated commands should not form a wall of filled rectangles.
        for (const auto* name : {"commandAction", "commandMini", "commandChip"}) {
          auto* background = part(find(name), "background");
          if (!background || background->property("fillColor").value<QColor>().alpha() != 0)
            return fail(context + QString::fromLatin1(name) + " must rest transparent");
        }
        const auto issues = qml_popup_style_violations(fixture.get());
        if (!issues.isEmpty()) return fail(context + issues.join('\n'));
        auto* changing = find("commandTightMenu");
        const auto short_width = changing->width();
        auto* overflow_action = find("commandTightMenuPrimaryAction");
        if (!overflow_action || changing->property("showText").toBool() ||
            changing->property("showMenuIndicator").toBool() ||
            !changing->property("primaryOpensMenu").toBool() ||
            overflow_action->property("iconName").toString() != QStringLiteral("more") ||
            overflow_action->property("resolvedAccessibleName").toString() != QStringLiteral("Adjustment actions"))
          return fail(context + "secondary actions need one compact, contextually named overflow opener");
        changing->setProperty("text", QStringLiteral("More export options"));
        settle();
        const auto changed_issues = qml_popup_style_violations(fixture.get());
        if (changing->width() <= short_width || !changed_issues.isEmpty())
          return fail(context + "changing action labels must resize without clipping: " + changed_issues.join('\n'));
        changing->setProperty("text", QStringLiteral("More"));
        settle();
        if (!output.isEmpty() && scale == 1 && !contrast && !window->grabWindow().save(QDir(output).filePath(QString::fromLatin1(density) + ".png"))) return fail(context + "command proof capture failed");
        ++cases;
      }
    }
  }
  settings->setProperty("interfaceScale", 1.0); settings->setProperty("highContrast", false); settle();
  auto* preference_background = qobject_cast<QQuickItem*>(part(find("commandPreference"), "background"));
  const auto preference_background_z = preference_background->z();
  preference_background->setZ(1);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Covered Preferences label escaped the style audit");
  preference_background->setZ(preference_background_z);
  auto* bg = part(find("commandAction"), "background");
  bg->setProperty("cornerRadius", 20);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Rounded command replacement escaped the style audit");
  bg->setProperty("cornerRadius", 3); bg->setProperty("borderWidth", 1);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Resting outline replacement escaped the style audit");
  bg->setProperty("borderWidth", 0); bg->setProperty("fillColor", QColor("#242440"));
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Tinted command replacement escaped the style audit");
  bg->setProperty("fillColor", QColor("#242424"));
  auto* label = find("commandAction")->findChild<QQuickItem*>("sharedCommandLabel");
  QQmlProperty(label, QStringLiteral("font.pixelSize")).write(8);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Tiny command text escaped the style audit");
  QQmlProperty(label, QStringLiteral("font.pixelSize")).write(13);
  find("commandAction")->setProperty("showLeadingIcon", true); settle();
  auto* icon = find("commandAction")->findChild<QQuickItem*>("sharedCommandIcon");
  icon->setHeight(icon->width() / 2);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Stretched command icon escaped the style audit");
  if (!errors.isEmpty()) return fail(errors.join('\n'));
  std::cout << cases << " command configurations passed across 16 controls, with stable geometry, readable labels, keyboard/accessible activation, disabled behavior, and style mutations rejected.\n";
  return 0;
}
