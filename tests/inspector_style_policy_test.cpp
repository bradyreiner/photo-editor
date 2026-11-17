#include "qml_style_policy.h"
#include <QDir>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (int i = 0; i < 6; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(3);
  }
}
QVariant read(QObject* obj, const char* name) { return QQmlProperty(obj, QString::fromLatin1(name)).read(); }
QObject* part(QObject* obj, const char* name) { return qvariant_cast<QObject*>(read(obj, name)); }
bool near(double a, double b) { return std::abs(a - b) <= 0.5; }
bool contains(QQuickItem* row, QQuickItem* child) {
  return row && child && row->boundingRect().adjusted(-1, -1, 1, 1).contains(child->mapRectToItem(row, child->boundingRect()));
}
bool overlaps(QQuickItem* row, QQuickItem* a, QQuickItem* b) {
  return a->mapRectToItem(row, a->boundingRect()).adjusted(1, 1, -1, -1).intersects(b->mapRectToItem(row, b->boundingRect()));
}
bool usesSharedNumericType(QObject* control, QObject* theme) {
  if (!control || !theme) return false;
  const auto font = qvariant_cast<QFont>(read(control, "font"));
  return read(control, "font.family").toString() == theme->property("fontNumeric").toString() &&
      theme->property("fontNumeric").toString() == theme->property("fontUi").toString() &&
      font.featureValue(QFont::Tag("tnum")) == 1 && font.featureValue(QFont::Tag("lnum")) == 1;
}
int fail(const QString& error) { std::cerr << error.toStdString() << '\n'; return 1; }
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
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
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), settings.get());
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: specimens.width + 40; height: 940; visible: true; color: "#181818"
    property real specimenWidth: 336
    property bool brushSpecimenActive: false
    property int edits: 0
    property int resets: 0
    property int groupToggles: 0
    Theme { id: theme; objectName: "inspectorPolicyTheme" }
    Item {
        id: workspaceStub
        property string adjustmentControlTargetSupportContract: "fixture"
        property color mint: theme.textMain
        function adjustmentControlTargetSupportSignature(value) { return value }
        function adjustmentControlCanWriteActiveTarget(value) { return true }
        function adjustmentControlDisabledReason(value) { return "" }
    }
    QtObject {
        id: previewStub
        function beginCoalescedEdit() {}
        function endCoalescedEdit() {}
    }
    Column {
        id: specimens; x: 20; y: 20; width: fixture.specimenWidth; spacing: theme.gapDense
        visible: !fixture.brushSpecimenActive
        StudioAdjustmentSlider {
            objectName: "policyExposure"; width: parent.width
            workspace: workspaceStub; previewController: previewStub
            label: "Exposure"; from: -5; to: 5; stepSize: 0.01; value: -2.75
            valueText: value.toFixed(2) + " EV"
            onValueDragged: function(next) { value = next; fixture.edits++ }
            onResetRequested: fixture.resets++
        }
        StudioAdjustmentSlider {
            objectName: "policyTemperature"; width: parent.width
            workspace: workspaceStub; previewController: previewStub
            label: "Temperature"; valueText: "12000 K"; value: 45
            onValueDragged: function(next) { value = next }
        }
        StudioAdjustmentSlider {
            objectName: "policyLongLabel"; width: parent.width
            workspace: workspaceStub; previewController: previewStub
            label: "Highlight reconstruction strength"; valueText: "-100.00%"; value: -100
            onValueDragged: function(next) { value = next }
        }
        ToneTextField { objectName: "policyNumeric"; width: parent.width; numericEntry: true; text: "-123.45"; accessibleLabel: "Numeric value" }
        ToneSpinBox { objectName: "policySpin"; width: parent.width; from: -100; to: 100; value: -45; editable: true; valueUnit: "%" }
        ToneComboBox { objectName: "policyCombo"; width: parent.width; model: ["Luminance", "Color"] }
        ToneTextArea { objectName: "policyArea"; width: parent.width; height: theme.scaled(64); text: "Adjustment notes" }
        InspectorSection {
            objectName: "policySection"; width: parent.width
            title: "Advanced photographic detail controls"; resetAvailable: true; resetObjectName: "policySectionReset"
            ToneTextField { Layout.fillWidth: true; text: "Section field"; accessibleLabel: "Section field" }
        }
        StudioCollapsibleGroup {
            objectName: "policyGroup"; width: parent.width
            title: "Additional optical adjustments"; onToggled: { expanded = !expanded; fixture.groupToggles++ }
            ToneTextField { Layout.fillWidth: true; text: "Nested field"; accessibleLabel: "Nested field" }
        }
        StudioPanel {
            objectName: "policyPanel"; width: parent.width; height: theme.scaled(100); title: "Inspector panel"
            Label { Layout.fillWidth: true; text: "Shared neutral surroundings"; color: theme.textMain }
        }
    }
    BrushToolHud {
        objectName: "policyBrushHud"; x: 20; y: 20
        width: fixture.specimenWidth; embedded: true
        visible: fixture.brushSpecimenActive
    }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/inspector-style-policy-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) return fail(component.errorString());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  const auto find = [&](const QString& name) { return fixture->findChild<QQuickItem*>(name); };
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_INSPECTOR_STYLE_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  int cases = 0;
  for (const auto* density : {"compact", "balanced", "comfortable"}) {
    for (const auto scale : {0.9, 1.0, 2.0}) {
      for (const auto width : {220, 336}) {
        for (const bool contrast : {false, true}) {
          settings->setProperty("interfaceDensity", density);
          settings->setProperty("interfaceScale", scale);
          settings->setProperty("highContrast", contrast);
          fixture->setProperty("specimenWidth", std::round(width * scale));
          window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
          settle();
          const auto context = QStringLiteral("%1 / %2x / %3px / contrast=%4: ").arg(QString::fromLatin1(density)).arg(scale).arg(width).arg(contrast);
          const auto style_errors = qml_popup_style_violations(fixture.get());
          if (!style_errors.isEmpty()) {
            for (const auto* name : {"policyExposure", "policyTemperature", "policyLongLabel"}) {
              auto* row = find(QString::fromLatin1(name));
              auto* field = find(QString::fromLatin1(name) + "ValueField");
              if (row && field) {
                const auto bounds = field->mapRectToItem(row, field->boundingRect());
                std::cerr << name << " row=" << row->width() << ',' << row->height()
                          << " field=" << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ',' << bounds.height()
                          << " wide=" << row->property("wideRailLayout").toBool() << '\n';
              }
            }
            return fail(context + style_errors.join('\n'));
          }
          find("policyPanel")->setProperty("selected", true); settle();
          const auto selected_errors = qml_popup_style_violations(fixture.get());
          if (!selected_errors.isEmpty()) return fail(context + selected_errors.join('\n'));
          find("policyPanel")->setProperty("selected", false); settle();
          for (const auto* name : {"policyExposure", "policyTemperature", "policyLongLabel"}) {
            auto* row = find(QString::fromLatin1(name));
            auto* label = find(QString::fromLatin1(name) + "Label");
            auto* field = find(QString::fromLatin1(name) + "ValueField");
            auto* reset = find(QString::fromLatin1(name) + "ResetButton");
            auto* track = find(QString::fromLatin1(name) + "Control");
            if (!row || !label || !field || !reset || !track) return fail(context + "named row parts must exist");
            if (!near(field->width(), std::round(80 * scale)) || !near(reset->width(), std::round(28 * scale))) return fail(context + "value/reset columns must scale consistently");
            if (!contains(row, label) || !contains(row, field) || !contains(row, reset) || !contains(row, track)) return fail(context + QString::fromLatin1(name) + " contains clipped controls");
            if (overlaps(row, label, field) || overlaps(row, label, reset) || overlaps(row, label, track) || overlaps(row, field, track) || overlaps(row, field, reset)) return fail(context + QString::fromLatin1(name) + " row parts overlap");
            const auto available = field->width() - read(field, "leftPadding").toDouble() - read(field, "rightPadding").toDouble();
            if (read(field, "contentWidth").toDouble() > available + 1 || read(label, "contentHeight").toDouble() > label->height() + 1) return fail(context + "labels and values must remain fully readable");
            auto* type_theme = fixture->findChild<QObject*>("inspectorPolicyTheme");
            if (!usesSharedNumericType(field, type_theme))
              return fail(context + "numeric values must use the shared UI face with tabular lining figures");
            const auto numeric_font = qvariant_cast<QFont>(read(field, "font"));
            const QFontMetricsF numeric_metrics(numeric_font);
            const auto digit_width = numeric_metrics.horizontalAdvance(QStringLiteral("0"));
            for (const auto digit : QStringLiteral("123456789")) {
              if (std::abs(numeric_metrics.horizontalAdvance(QString(digit)) - digit_width) > 0.05)
                return fail(context + "changing numeric digits must not change column widths");
            }
            if (!usesSharedNumericType(find("policySpin"), type_theme))
              return fail(context + "numeric steppers must use the shared UI face with tabular lining figures");
            if (read(label, "font.pixelSize").toInt() != fixture->findChild<QObject*>("inspectorPolicyTheme")->property("typeLabel").toInt()) return fail(context + "labels must not shrink to hide overflow");
            if (width == 220 && !row->property("wideRailLayout").toBool()) return fail(context + "narrow rows must use the full-width track");
            if (track->width() < std::round(40 * scale)) return fail(context + "slider track is too short for precise input");
            const auto value_x = field->x(); const auto reset_x = reset->x(); const auto track_width = track->width();
            row->setProperty("value", 0.0); settle();
            if (!near(field->x(), value_x) || !near(reset->x(), reset_x) || !near(track->width(), track_width)) return fail(context + "value changes must not shift row geometry");
          }
          for (const auto* name : {"policyNumeric", "policySpin", "policyCombo", "policyArea"}) {
            auto* control = find(QString::fromLatin1(name));
            auto* bg = part(control, "background");
            if (!bg || !near(read(bg, "radius").toDouble(), std::round(3 * scale)) || !near(read(bg, "border.width").toDouble(), contrast ? 1 : 0)) return fail(context + "input backgrounds must use shallow shared geometry");
            const auto size = control->size();
            control->forceActiveFocus(Qt::TabFocusReason); settle();
            if (read(bg, "border.width").toDouble() < std::round(2 * scale) || control->size() != size) return fail(context + "input focus must be visible without resizing");
            window->contentItem()->forceActiveFocus(Qt::OtherFocusReason); settle();
          }
          auto* title = find("policySectionTitle");
          if (!title || read(title, "contentHeight").toDouble() > title->height() + 1) return fail(context + "inspector headings must wrap without clipping");
          const auto frame = window->grabWindow();
          if (frame.isNull()) return fail(context + "style proof requires a rendered frame");
          if (!contrast) {
            auto* numeric = find("policyNumeric");
            const auto edge = numeric->mapToItem(window->contentItem(), QPointF(0, numeric->height() / 2));
            const auto pixel = frame.pixelColor(static_cast<int>(edge.x() * frame.devicePixelRatio()), static_cast<int>(edge.y() * frame.devicePixelRatio()));
            if (pixel != read(part(numeric, "background"), "color").value<QColor>()) return fail(context + "rendered input has an unwanted resting outline");
          }
          if (!output.isEmpty() && scale == 1.0 && width == 336 && !contrast &&
              !frame.save(QDir(output).filePath(QString::fromLatin1(density) + ".png"))) return fail(context + "could not capture inspector proof");
          ++cases;
        }
      }
    }
  }
  fixture->setProperty("brushSpecimenActive", true);
  for (const auto* density : {"compact", "balanced", "comfortable"}) {
    for (const auto scale : {0.9, 1.0, 2.0}) {
      for (const auto width : {320, 640}) {
        settings->setProperty("interfaceDensity", density);
        settings->setProperty("interfaceScale", scale);
        settings->setProperty("highContrast", false);
        fixture->setProperty("specimenWidth", std::round(width * scale));
        settle();
        auto* hud = find("policyBrushHud");
        if (!hud) return fail("The actual brush HUD specimen must exist");
        window->resize(static_cast<int>(std::round(width * scale)) + 40,
            static_cast<int>(std::ceil(hud->height())) + 40);
        settle();
        const auto context = QStringLiteral("brush %1 / %2x / %3px: ").arg(QString::fromLatin1(density)).arg(scale).arg(width);
        const auto violations = qml_popup_style_violations(fixture.get());
        if (!violations.isEmpty()) return fail(context + violations.join('\n'));
        for (const auto* name : {"brushHudHardnessVerticalSlider", "brushHudFlowVerticalSlider"}) {
          auto* row = find(QString::fromLatin1(name));
          auto* label = find(QString::fromLatin1(name) + "Label");
          auto* field = find(QString::fromLatin1(name) + "ValueField");
          auto* track = find(QString::fromLatin1(name) + "Control");
          if (!row || !label || !field || !track || !near(field->width(), std::round(80 * scale)) ||
              !contains(row, label) || !contains(row, field) || !contains(row, track) ||
              !contains(hud, row) || overlaps(row, label, field) || overlaps(row, field, track) ||
              overlaps(row, label, track) || track->width() < std::round(40 * scale))
            return fail(context + QString::fromLatin1(name) + " requires readable, contained, nonoverlapping controls");
          if (!usesSharedNumericType(field, fixture->findChild<QObject*>("inspectorPolicyTheme")))
            return fail(context + QString::fromLatin1(name) + " numeric value must use the shared UI face with tabular lining figures");
        }
        if (!usesSharedNumericType(find("brushHudSizeField"), fixture->findChild<QObject*>("inspectorPolicyTheme")))
          return fail(context + "brush size must use the shared UI face with tabular lining figures");
      }
    }
  }
  fixture->setProperty("brushSpecimenActive", false);
  window->resize(376, 940);
  settings->setProperty("interfaceDensity", "balanced");
  settings->setProperty("interfaceScale", 1.0);
  settings->setProperty("highContrast", false);
  fixture->setProperty("specimenWidth", 336);
  settle();
  auto* exposure = find("policyExposure");
  const auto edits_before = fixture->property("edits").toInt();
  QVariant committed;
  if (!QMetaObject::invokeMethod(exposure, "testCommitInputText", Q_RETURN_ARG(QVariant, committed),
                                Q_ARG(QVariant, QStringLiteral("-3.25 EV"))) ||
      std::abs(committed.toDouble() + 3.25) > 1e-6 || fixture->property("edits").toInt() != edits_before + 1)
    return fail("Formatted numeric input must commit exactly once with full precision");
  if (!QMetaObject::invokeMethod(exposure, "testCommitInputText", Q_RETURN_ARG(QVariant, committed),
                                Q_ARG(QVariant, QStringLiteral("not a value"))) ||
      std::abs(committed.toDouble() + 3.25) > 1e-6 || fixture->property("edits").toInt() != edits_before + 1)
    return fail("Invalid input must preserve the last accepted numeric value");
  const auto space = [&] {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press); QCoreApplication::sendEvent(window, &release); settle();
  };
  auto* reset = find("policyExposureResetButton");
  reset->forceActiveFocus(Qt::TabFocusReason); space();
  if (fixture->property("resets").toInt() != 1 || std::abs(exposure->property("value").toDouble()) > 1e-6)
    return fail("Keyboard reset must restore the default once");
  auto* group = find("policyGroup");
  find("policyGroupHeader")->forceActiveFocus(Qt::TabFocusReason); space();
  if (fixture->property("groupToggles").toInt() != 1 || group->property("expanded").toBool())
    return fail("Inspector disclosure must work from the keyboard exactly once");
  space();
  if (fixture->property("groupToggles").toInt() != 2 || !group->property("expanded").toBool())
    return fail("Inspector disclosure must reopen from the keyboard");
  auto* numeric = find("policyNumeric");
  const auto accepted_numeric_font = qvariant_cast<QFont>(read(numeric, "font"));
  auto wrong_numeric_font = accepted_numeric_font;
  wrong_numeric_font.setFamily(QStringLiteral("Courier"));
  QQmlProperty(numeric, QStringLiteral("font")).write(wrong_numeric_font);
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains(QStringLiteral("numeric control must use the shared UI face")))
    return fail("A runtime numeric font replacement escaped the application style audit");
  auto proportional_numeric_font = accepted_numeric_font;
  proportional_numeric_font.setFeature(QFont::Tag("tnum"), 0);
  QQmlProperty(numeric, QStringLiteral("font")).write(proportional_numeric_font);
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains(QStringLiteral("numeric control must use tabular lining figures")))
    return fail("A runtime numeric figure replacement escaped the application style audit");
  QQmlProperty(numeric, QStringLiteral("font")).write(accepted_numeric_font);
  if (!qml_popup_style_violations(fixture.get()).isEmpty())
    return fail("Restoring the shared numeric font must restore the evaluated style contract");
  exposure->setProperty("valueColumnWidth", 68);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Unscaled slider columns escaped the application style audit");
  exposure->setProperty("valueColumnWidth", 80);
  auto* input_surface = part(find("policyNumeric"), "background");
  input_surface->setProperty("radius", 12);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Rounded input replacement escaped the application style audit");
  input_surface->setProperty("radius", 3);
  QQmlProperty(find("policyPanel"), QStringLiteral("border.width")).write(1);
  if (qml_popup_style_violations(fixture.get()).isEmpty()) return fail("Bordered panel replacement escaped the application style audit");
  if (!errors.isEmpty()) return fail(errors.join('\n'));
  std::cout << cases << " inspector configurations passed with readable labels, aligned numeric fields, stable geometry, and accessible focus.\n";
  return 0;
}
