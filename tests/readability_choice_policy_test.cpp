#include "qml_style_policy.h"
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QDir>
#include <QGuiApplication>
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
  for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(3); }
}
int fail(const QString& message) { std::cerr << message.toStdString() << '\n'; return 1; }
bool press_accessible(QObject* object) {
  auto* interface = QAccessible::queryAccessibleInterface(object);
  auto* actions = interface ? interface->actionInterface() : nullptr;
  if (!actions || !actions->actionNames().contains(QAccessibleActionInterface::pressAction())) return false;
  actions->doAction(QAccessibleActionInterface::pressAction()); settle(); return true;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QQmlEngine engine;
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, [&warnings](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
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
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), settings.get());
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
 id: fixture
 width: samples.width + 40; height: samples.implicitHeight + 40; visible: true; color: "#181818"
 property int activations: 0
 property int recoveries: 0
 property int logicalWidth: 360
 Theme { id: theme }
 Column {
  id: samples; x: 20; y: 20; width: theme.scaled(fixture.logicalWidth); spacing: theme.space16
  ToneCheckBox {
   objectName: "readableCheck"; width: parent.width
   text: "Include color adjustments and camera-specific corrections"
   description: "Preserve white balance, mixer adjustments, and the selected profile."
   onToggled: fixture.activations++
  }
  ToneSwitch {
   objectName: "readableSwitch"; width: parent.width
   text: "Reopen my previous workspace and selected photographs"
   onToggled: fixture.activations++
  }
  BodyLabel {
   objectName: "readablePath"; width: parent.width
   text: "/Volumes/Photographs/<Client & Co>/Northern Lights expedition/Unedited originals and matching RAW plus JPEG pairs/Final selected captures"
  }
  Item {
   width: parent.width; height: recovery.height
   StudioOfflineRecoveryBanner {
    id: recovery; objectName: "readableRecovery"
    studioTheme: theme; pageName: "edit"; selectedAssetId: "fixture-asset"
    availabilityState: "offline"; availabilityTitle: "The original photograph is offline"
    availabilityDetail: "Reconnect /Volumes/Photographs/Client projects/Northern Lights expedition/Unedited originals. Your edits and cached preview are preserved."
    importModeOpen: false; missingConfirmationArmed: false
    accentColor: theme.textStrong; dimTextColor: theme.textMuted
    onRelinkSelectedRequested: fixture.recoveries++
   }
  }
 }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/readability-choice-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) return fail(component.errorString());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_READABILITY_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  int cases = 0;
  for (const auto* density : {"compact", "balanced", "comfortable"})
    for (const auto scale : {0.9, 1.0, 2.0})
      for (const bool contrast : {false, true})
        for (const int width : {220, 360}) {
          settings->setProperty("interfaceDensity", density); settings->setProperty("interfaceScale", scale);
          settings->setProperty("highContrast", contrast); fixture->setProperty("logicalWidth", width); settle();
          const auto context = QStringLiteral("%1 / %2x / width=%3 / contrast=%4: ").arg(QString::fromLatin1(density)).arg(scale).arg(width).arg(contrast);
          auto issues = qml_popup_style_violations(fixture.get());
          if (!issues.isEmpty()) return fail(context + issues.join('\n'));
          for (const auto* name : {"readableCheck", "readableSwitch"}) {
            auto* control = fixture->findChild<QQuickItem*>(QString::fromLatin1(name));
            if (!control || control->width() < std::round(24 * scale) || control->height() < std::round(24 * scale)) return fail(context + "choice target is too small");
            const auto size = control->size();
            control->forceActiveFocus(Qt::TabFocusReason); settle();
            auto* background = qvariant_cast<QObject*>(QQmlProperty(control, QStringLiteral("background")).read());
            if (!background || background->property("borderWidth").toDouble() < 2) return fail(context + "choice needs visible focus");
            const auto before = fixture->property("activations").toInt();
            const auto checked = control->property("checked").toBool();
            QTest::keyClick(window, Qt::Key_Space); settle();
            if (control->property("checked").toBool() == checked || fixture->property("activations").toInt() != before + 1) return fail(context + "keyboard must toggle exactly once");
            if (!press_accessible(control) || control->property("checked").toBool() != checked || fixture->property("activations").toInt() != before + 2) return fail(context + "accessible press must toggle exactly once");
            const auto point = control->mapToItem(window->contentItem(), QPointF(control->width() / 2, control->height() / 2));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint()); settle();
            if (fixture->property("activations").toInt() != before + 3 || control->size() != size) return fail(context + "pointer toggle must preserve layout");
            control->setProperty("enabled", false); press_accessible(control); QTest::keyClick(window, Qt::Key_Space); settle();
            if (fixture->property("activations").toInt() != before + 3) return fail(context + "disabled choice activated");
            control->setProperty("enabled", true); control->setProperty("checked", checked);
          }
          auto* path = fixture->findChild<QQuickItem*>(QStringLiteral("readablePath"));
          if (path->property("textFormat").toInt() != 0) return fail(context + "paths must display literal filename characters");
          if (path->property("truncated").toBool() || path->property("contentHeight").toDouble() > path->height() + 1 || path->property("contentWidth").toDouble() > path->width() + 1) return fail(context + "long path must wrap without loss");
          auto* recovery = fixture->findChild<QQuickItem*>(QStringLiteral("readableRecovery"));
          for (auto* item : recovery->findChildren<QQuickItem*>()) {
            if (!item->isVisible() || item->objectName().isEmpty()) continue;
            if (!recovery->boundingRect().adjusted(-1, -1, 1, 1).contains(item->mapRectToItem(recovery, item->boundingRect()))) return fail(context + "recovery content escapes its surface: " + item->objectName());
          }
          issues = qml_popup_style_violations(fixture.get());
          if (!issues.isEmpty()) return fail(context + issues.join('\n'));
          window->contentItem()->forceActiveFocus(Qt::OtherFocusReason); settle();
          if (!output.isEmpty() && scale == 1 && !contrast && width == 360 && !window->grabWindow().save(QDir(output).filePath(QString::fromLatin1(density) + ".png"))) return fail("Cannot save readability proof");
          ++cases;
        }
  auto* body = fixture->findChild<QObject*>(QStringLiteral("readablePath"));
  QQmlProperty(body, QStringLiteral("font.pixelSize")).write(8);
  settle();
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains("Body text must retain")) return fail("Tiny body text escaped the evaluated gate");
  QQmlProperty(body, QStringLiteral("font.pixelSize")).write(26);
  body->setProperty("fontSizeMode", 1); settle();
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains("Body text must retain")) return fail("Shrinking body text escaped the evaluated gate");
  body->setProperty("fontSizeMode", 0); settle();
  auto* check = fixture->findChild<QObject*>(QStringLiteral("readableCheck"));
  check->setProperty("tristate", true);
  check->setProperty("checkState", Qt::PartiallyChecked); settle();
  bool mixed_mark = false;
  for (auto* item : check->findChildren<QQuickItem*>())
    mixed_mark = mixed_mark || (item->isVisible() && item->property("name").toString() == QStringLiteral("minus"));
  if (!mixed_mark) return fail("A partially checked group must display its mixed-state mark");
  auto* indicator = qvariant_cast<QObject*>(QQmlProperty(check, QStringLiteral("indicator")).read());
  indicator->setProperty("color", QColor("#123456"));
  if (!qml_popup_style_violations(fixture.get()).join('\n').contains("Choice indicator")) return fail("Tinted choice indicator escaped the evaluated gate");
  if (!warnings.isEmpty()) return fail(warnings.join('\n'));
  std::cout << cases << " readable choice/path/recovery configurations passed\n";
  return 0;
}
