#include "qml_style_policy.h"
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QtTest/QTest>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (int i = 0; i < 30; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}
int fail(const QString& error) { std::cerr << error.toStdString() << '\n'; return 1; }
QQuickItem* find_item(QQuickItem* parent, const QString& name) {
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems())
    if (auto* found = find_item(child, name)) return found;
  return nullptr;
}
void click(QQuickWindow* window, QQuickItem* item) {
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
  settle();
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
    property bool highContrast: false
    property bool reducedMotion: true
})qml", QUrl());
  std::unique_ptr<QObject> settings(settings_component.create());
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), settings.get());
  QStringList errors;
  QObject::connect(&engine, &QQmlEngine::warnings, [&errors](const QList<QQmlError>& warnings) {
    for (const auto& warning : warnings) errors.append(warning.toString());
  });
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: 700; height: 720; visible: true; color: "#181818"
    property int activations: 0
    property int settingsCount: 0
    property string lastKey: ""
    ActionButton { id: opener; objectName: "optionsOpener"; x: 20; y: 12; text: "Options"; onClicked: options.open() }
    GroupedOptionsPopover {
        id: options
        objectName: "options"
        parent: Overlay.overlay
        x: Math.max(0, (parent.width - width) / 2); y: 60
        title: "Timeline options"
        settingsLabel: "Interface settings…"
        groups: [
            { key: "viewer", label: "Viewer", items: [
                { key: "guides", label: "Viewer guides" },
                { key: "compare", label: "Compare frames", enabled: false, disabledReason: "Open a photograph first." },
                { key: "fullscreen", label: "Full-screen monitor" }
            ] },
            { key: "workspace", label: "Workspace", items: [
                { key: "reset", label: "Reset workspace layout" }
            ] }
        ]
        onItemTriggered: function(key) { fixture.activations++; fixture.lastKey = key }
        onSettingsRequested: fixture.settingsCount++
    }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/grouped-options-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) return fail(component.errorString());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* options = fixture->findChild<QObject*>(QStringLiteral("options"));
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_GROUPED_OPTIONS_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  for (const auto scale : {1.0, 2.0}) {
    settings->setProperty("interfaceScale", scale);
    options->setProperty("currentGroup", QStringLiteral("viewer"));
    QMetaObject::invokeMethod(options, "open");
    settle();
    auto* content = qvariant_cast<QQuickItem*>(options->property("contentItem"));
    if (!content || !window->contentItem()->boundingRect().contains(
        content->mapRectToItem(window->contentItem(), content->boundingRect())))
      return fail("Grouped options must fit the window at normal and enlarged scale");
    const auto issues = qml_popup_style_violations(fixture.get());
    if (!issues.isEmpty()) return fail(issues.join('\n'));
    auto* first = find_item(window->contentItem(), QStringLiteral("groupedOptionsItem0"));
    auto* last = find_item(window->contentItem(), QStringLiteral("groupedOptionsItem2"));
    if (!first || !last) return fail("Viewer options are missing");
    first->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Down);
    settle();
    if (!last->hasActiveFocus()) return fail("Option navigation must skip unavailable actions");
    const auto before = fixture->property("activations").toInt();
    QTest::keyClick(window, Qt::Key_Return);
    settle();
    if (options->property("visible").toBool() || fixture->property("activations").toInt() != before + 1 ||
        fixture->property("lastKey").toString() != QStringLiteral("fullscreen"))
      return fail("Keyboard choice must activate once and dismiss options");
    QMetaObject::invokeMethod(options, "open");
    settle();
    auto* workspace = find_item(window->contentItem(), QStringLiteral("optionsGroup_workspace"));
    if (!workspace) return fail("Workspace category is missing");
    click(window, workspace);
    if (!options->property("visible").toBool() || fixture->property("activations").toInt() != before + 1)
      return fail("Changing category must keep options open without executing a command");
    first = find_item(window->contentItem(), QStringLiteral("groupedOptionsItem0"));
    if (!first) return fail("Workspace option is missing after category change");
    if (!output.isEmpty()) window->grabWindow().save(QDir(output).filePath(QStringLiteral("options-%1x.png").arg(scale)));
    click(window, first);
    if (options->property("visible").toBool() || fixture->property("lastKey").toString() != QStringLiteral("reset"))
      return fail("Pointer choice must use the newly selected category and dismiss options");
    QMetaObject::invokeMethod(options, "open");
    settle();
    QTest::keyClick(window, Qt::Key_Escape);
    settle();
    if (options->property("visible").toBool()) return fail("Escape must dismiss grouped options");
  }
  if (!errors.isEmpty()) return fail(errors.join('\n'));
  std::cout << "Grouped options: bounds, normal/enlarged scale, category changes, keyboard and pointer activation passed\n";
  return 0;
}
