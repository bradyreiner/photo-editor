#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (int i = 0; i < 30; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent settings_component(&engine);
  settings_component.setData(R"qml(import QtQml
QtObject {
    property real interfaceScale: 1
    property string interfaceDensity: "balanced"
    property string systemUiFontFamily: ""
    property bool reducedMotion: true
    property bool highContrast: false
})qml", QUrl());
  std::unique_ptr<QObject> settings(settings_component.create());
  if (!settings) return fail("Create panel settings");
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty("appShell", settings.get());
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, [&warnings](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
Window {
    visible: true; width: 320; height: 520; color: "#181818"
    ModularToolPanel {
        id: panel; objectName: "panel"; anchors.fill: parent; active: true
        title: "Selection brush settings"
        targetText: "Foreground subject with a long descriptive selection name"
        contextKey: "brush"
    }
    ColumnLayout {
        parent: panel.contentItem; Layout.fillWidth: true
        Repeater {
            model: 24
            InspectorSlider {
                Layout.fillWidth: true; label: "Brush setting"; from: 0; to: 100; value: 50
            }
        }
    }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/modular-panel-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString();
    return 1;
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* panel = fixture->findChild<QQuickItem*>("panel");
  auto* target = fixture->findChild<QQuickItem*>("creativeToolsTargetLabel");
  auto* title = fixture->findChild<QQuickItem*>("creativeToolsTitle");
  auto* scroll = fixture->findChild<QQuickItem*>("creativeToolsScroll");
  if (!window || !panel || !target || !title || !scroll) return fail("Find production panel controls");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TOOL_PANEL_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  for (const auto scale : {1.0, 2.0}) {
    settings->setProperty("interfaceScale", scale);
    for (const auto& density : {QStringLiteral("compact"), QStringLiteral("balanced"), QStringLiteral("comfortable")}) {
      settings->setProperty("interfaceDensity", density);
      for (const auto logical_width : {220, 320}) {
        window->setWidth(qRound(logical_width * scale));
        window->setHeight(qRound(520 * scale));
        settle();
        for (auto* label : {target, title}) {
          if (label->property("contentWidth").toDouble() > label->width() + 1 ||
              label->property("contentHeight").toDouble() > label->height() + 1 ||
              !panel->boundingRect().contains(label->mapRectToItem(panel, label->boundingRect())))
            return fail("Panel title and target must wrap and remain fully readable");
        }
        auto* flickable = qvariant_cast<QQuickItem*>(scroll->property("contentItem"));
        if (!flickable || flickable->property("contentHeight").toDouble() <= flickable->height())
          return fail("Long settings must scroll inside the tool panel");
        flickable->setProperty("contentY", 100 * scale);
        panel->setProperty("contextKey", "text");
        settle();
        if (std::abs(flickable->property("contentY").toDouble()) > 1)
          return fail("Switching tools must reveal the first controls");
        panel->setProperty("contextKey", "brush");
        settle();
        if (!output.isEmpty() && !window->grabWindow().save(output +
            QStringLiteral("/tools-%1-%2-%3.png").arg(density).arg(scale).arg(logical_width)))
          return fail("Capture tool panel");
      }
    }
  }
  if (!warnings.isEmpty()) {
    std::cerr << warnings.join('\n').toStdString();
    return fail("Tool panel must load without QML warnings");
  }
  std::cout << "Tool panel wrapping, internal scrolling and tool-switch reset passed in 12 layouts.\n";
  return 0;
}
