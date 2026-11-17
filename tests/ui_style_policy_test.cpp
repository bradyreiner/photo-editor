#include <QColor>
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
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include "qml_style_policy.h"

namespace {
void settle() {
  for (int i = 0; i < 6; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}
QVariant read(QObject* object, const char* property) {
  return QQmlProperty(object, QString::fromLatin1(property)).read();
}
QObject* part(QObject* object, const char* property) {
  return qvariant_cast<QObject*>(read(object, property));
}
bool near(double a, double b) { return std::abs(a - b) < 0.1; }
QString surface_error(QObject* surface, int radius, int border) {
  if (!surface) return QStringLiteral("missing shared background");
  if (!near(read(surface, "radius").toDouble(), radius)) return QStringLiteral("corner radius exceeds the shallow surface policy");
  if (!near(read(surface, "border.width").toDouble(), border)) return QStringLiteral("resting outline violates the contrast policy");
  if (read(surface, "color").value<QColor>() != QColor("#242424")) return QStringLiteral("popup fill is outside the neutral palette");
  return {};
}
QString inset_error(QObject* popup, int inset) {
  for (const auto* edge : {"leftPadding", "rightPadding", "topPadding", "bottomPadding"})
    if (!near(read(popup, edge).toDouble(), inset)) return QStringLiteral("popup edge inset violates the density policy");
  return {};
}
int fail(const QString& reason) { std::cerr << reason.toStdString() << '\n'; return 1; }
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QStringList errors;
  QObject warning_context;
  QObject::connect(&engine, &QQmlEngine::warnings, &warning_context,
                    [&errors](const QList<QQmlError>& warnings) {
    for (const auto& warning : warnings) errors.append(warning.toString());
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
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    width: 900; height: 700; visible: true
    color: "#101010"
    Theme { id: theme; objectName: "policyTheme" }
    PopupSurface { objectName: "surfaceSpecimen"; x: 20; y: 20; width: 100; height: 64; shadowColor: "transparent" }
    ActionButton { objectName: "actionSpecimen"; x: 20; y: 110; text: "Apply" }
    MiniButton { objectName: "miniSpecimen"; x: 160; y: 110; text: "Reset" }
    ToolButton { objectName: "toolSpecimen"; x: 280; y: 110; label: "Tool"; iconName: "settings" }
    AppPopup { objectName: "popoverSpecimen"; parent: Overlay.overlay; x: 180; y: 200; width: 280; height: 100 }
    AppDialog {
        objectName: "dialogSpecimen"; parent: Overlay.overlay
        modal: false
        title: "Review settings"; x: 180; y: 340; width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Item { implicitHeight: 40 }
    }
    AppMenu { objectName: "menuSpecimen"; MenuItem { text: "Copy" } }
    AppToolTip { objectName: "tooltipSpecimen"; text: "Copy settings" }
    AppDrawer { objectName: "drawerSpecimen"; width: 180; height: parent ? parent.height : 700 }
    InspectorSlider {
        objectName: "standardSliderSpecimen"; x: 520; y: 20
        width: Math.min(theme.scaled(320), Math.max(0, parent.width - x - 20))
        label: "Exposure"; from: -5; to: 5; value: 1.25; valueText: "1.25 EV"
    }
    InspectorSlider {
        objectName: "compactHudSliderSpecimen"; x: 520; y: 130
        width: Math.min(theme.scaled(188), Math.max(0, parent.width - x - 20))
        label: "Brush size"; from: 0; to: 100; value: 100; valueText: "100%"
        styleVariant: "compact-hud"; forceCompact: true
        showResetButton: false; valueInputEnabled: false
    }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/ui-style-policy-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) return fail(component.errorString());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  const auto find = [&](const char* name) { return fixture->findChild<QObject*>(QString::fromLatin1(name)); };
  auto* specimen = find("surfaceSpecimen");
  auto* dialog = find("dialogSpecimen");
  auto* popover = find("popoverSpecimen");
  auto* standard_slider = qobject_cast<QQuickItem*>(find("standardSliderSpecimen"));
  auto* compact_hud_slider = qobject_cast<QQuickItem*>(find("compactHudSliderSpecimen"));
  if (!standard_slider || !compact_hud_slider) return fail("Both standard and compact HUD slider variants must instantiate");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_STYLE_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  int cases = 0;
  for (const auto* density : {"compact", "balanced", "comfortable"}) {
    for (const auto scale : {0.9, 1.0, 2.0}) {
      for (const bool contrast : {false, true}) {
        settings->setProperty("interfaceDensity", density);
        settings->setProperty("interfaceScale", scale);
        settings->setProperty("highContrast", contrast);
        settle();
        const auto label = QStringLiteral("%1 / %2x / contrast=%3: ").arg(QString::fromLatin1(density)).arg(scale).arg(contrast);
        if (!near(standard_slider->property("valueColumnWidth").toDouble(), std::round(80.0 * scale)) ||
            !near(compact_hud_slider->property("valueColumnWidth").toDouble(), std::round(48.0 * scale)) ||
            !near(standard_slider->property("resetColumnWidth").toDouble(), std::round(28.0 * scale)) ||
            !near(compact_hud_slider->property("resetColumnWidth").toDouble(), std::round(28.0 * scale)))
          return fail(label + "standard and compact HUD slider columns must scale to their declared variants");
        const auto integrated_errors = qml_popup_style_violations(fixture.get());
        if (!integrated_errors.isEmpty()) return fail(label + integrated_errors.join('\n'));
        for (const auto* token : {"ink950", "ink900", "ink850", "ink800", "ink750", "ink700"}) {
          const auto color = find("policyTheme")->property(token).value<QColor>();
          if (!color.isValid() || color.red() != color.green() || color.green() != color.blue() || color.alpha() != 255)
            return fail(label + QString::fromLatin1(token) + " must remain an opaque neutral");
        }
        const auto radius = static_cast<int>(std::round(4 * scale));
        const auto command_radius = static_cast<int>(std::round(3 * scale));
        const auto inset = static_cast<int>(std::round((QString::fromLatin1(density) == "compact" ? 8 : QString::fromLatin1(density) == "balanced" ? 12 : 16) * scale));
        for (const auto* name : {"popoverSpecimen", "dialogSpecimen", "menuSpecimen", "tooltipSpecimen", "drawerSpecimen"}) {
          auto* popup = find(name);
          const auto error = surface_error(part(popup, "background"), QString::fromLatin1(name) == "drawerSpecimen" ? 0 : radius, contrast ? 1 : 0);
          if (!error.isEmpty()) return fail(label + QString::fromLatin1(name) + ": " + error);
        }
        if (!inset_error(popover, inset).isEmpty() || !inset_error(dialog, inset).isEmpty()) return fail(label + "dialog/popover insets drifted");
        auto* header = part(dialog, "header");
        auto* footer = part(dialog, "footer");
        for (const auto* edge : {"leftPadding", "rightPadding", "topPadding"})
          if (!near(read(header, edge).toDouble(), inset)) return fail(label + "header and body must share one edge inset");
        for (const auto* edge : {"leftPadding", "rightPadding", "bottomPadding"})
          if (!near(read(footer, edge).toDouble(), inset)) return fail(label + "actions and body must share one edge inset");
        if (!near(read(header, "bottomPadding").toDouble(), 0) || !near(read(footer, "topPadding").toDouble(), 0))
          return fail(label + "header/footer must not double the content gap");
        for (const auto* name : {"actionSpecimen", "miniSpecimen", "toolSpecimen"}) {
          auto* control = qobject_cast<QQuickItem*>(find(name));
          auto* background = part(control, "background");
          if (!background || !near(read(background, "cornerRadius").toDouble(), command_radius) ||
              !near(read(background, "borderWidth").toDouble(), contrast ? 1 : 0)) return fail(label + QString::fromLatin1(name) + " command geometry drifted");
          const auto fill = read(background, "fillColor").value<QColor>();
          if (fill.alpha() > 0 && (fill.red() != fill.green() || fill.green() != fill.blue()))
            return fail(label + QString::fromLatin1(name) + " resting command fill must remain neutral");
          const auto before = control->size();
          control->forceActiveFocus(Qt::TabFocusReason);
          settle();
          const auto focus_width = std::max(2.0, std::round(2.0 * scale));
          if (!near(read(background, "borderWidth").toDouble(), focus_width) || control->size() != before)
            return fail(label + "keyboard focus must retain its visible outline without changing geometry");
          window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
          settle();
        }
        QMetaObject::invokeMethod(popover, "open");
        QMetaObject::invokeMethod(dialog, "open");
        settle();
        const auto frame = window->grabWindow();
        if (frame.isNull()) return fail(label + "style check requires a rendered frame");
        // Sample the actual painted center and straight edge away from text and
        // shadows. A new border/fill can no longer pass on metadata alone.
        auto* surface_item = qobject_cast<QQuickItem*>(specimen);
        const auto origin = surface_item->mapToItem(window->contentItem(), QPointF(0, 0));
        const auto dpr = frame.devicePixelRatio();
        const auto pixel = [&](int x, int y) { return frame.pixelColor(static_cast<int>((origin.x() + x) * dpr), static_cast<int>((origin.y() + y) * dpr)); };
        if (pixel(50, 32) != QColor("#242424")) return fail(label + "rendered popup fill drifted");
        if (!contrast && pixel(0, 32) != pixel(50, 32)) return fail(label + "rendered resting popup has an outline");
        if (!output.isEmpty() && scale == 1.0 && !contrast && !frame.save(QDir(output).filePath(QString::fromLatin1(density) + ".png")))
          return fail("Could not save style proof frame");
        QMetaObject::invokeMethod(dialog, "close");
        QMetaObject::invokeMethod(popover, "close");
        settle();
        ++cases;
      }
    }
  }
  // Prove these checks reject the regressions they are intended to prevent.
  settings->setProperty("interfaceScale", 1.0);
  settings->setProperty("interfaceDensity", "balanced");
  settings->setProperty("highContrast", false);
  settle();
  compact_hud_slider->setProperty("styleVariant", QStringLiteral("unknown-compact-mode"));
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains(QStringLiteral("unknown inspector slider style variant")))
    return fail("The inspector style gate must reject an unknown slider variant");
  compact_hud_slider->setProperty("styleVariant", QStringLiteral("compact-hud"));
  compact_hud_slider->setProperty("forceCompact", false);
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains(QStringLiteral("compact HUD variant requires")))
    return fail("The compact HUD variant must require forced compact layout");
  compact_hud_slider->setProperty("forceCompact", true);
  compact_hud_slider->setProperty("showResetButton", true);
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains(QStringLiteral("compact HUD variant requires")))
    return fail("The compact HUD variant must not expose a reset column");
  compact_hud_slider->setProperty("showResetButton", false);
  compact_hud_slider->setProperty("valueInputEnabled", true);
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains(QStringLiteral("compact HUD variant requires")))
    return fail("The compact HUD variant must not expose typed value entry");
  compact_hud_slider->setProperty("valueInputEnabled", false);
  if (!qml_popup_style_violations(fixture.get()).isEmpty())
    return fail("Restoring the declared compact HUD variant must satisfy the style gate");
  specimen->setProperty("radius", 20);
  if (surface_error(specimen, 4, 0).isEmpty()) return fail("Rounded replacement escaped the style gate");
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Real-window audit missed a runtime radius override");
  specimen->setProperty("radius", 4);
  QQmlProperty(specimen, QStringLiteral("border.width")).write(2);
  if (surface_error(specimen, 4, 0).isEmpty()) return fail("Bordered replacement escaped the style gate");
  QQmlProperty(specimen, QStringLiteral("border.width")).write(0);
  specimen->setProperty("color", QColor("#242440"));
  if (surface_error(specimen, 4, 0).isEmpty()) return fail("Blue-tinted chrome escaped the style gate");
  specimen->setProperty("color", QColor("#242424"));
  popover->setProperty("padding", 32);
  if (inset_error(popover, 12).isEmpty()) return fail("Over-padded replacement escaped the style gate");
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Real-window audit missed a runtime padding override");
  if (!errors.isEmpty()) return fail(errors.join('\n'));
  std::cout << cases << " style configurations passed; rounded, bordered, tinted, and over-padded mutations rejected.\n";
  return 0;
}
