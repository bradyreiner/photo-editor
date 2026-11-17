#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlEngine>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <iostream>
#include <memory>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool set(QObject& policy, const char* name, const QVariant& value) {
  return policy.setProperty(name, value);
}

bool expect(QObject& policy, bool expected, const char* message) {
  const auto actual = policy.property("scopesVisible").toBool();
  if (actual == expected) return true;
  std::cerr << message << " (expected " << expected << ", got " << actual << ")\n";
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QStringList qml_warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                   [&qml_warnings](const QList<QQmlError>& warnings) {
                     for (const auto& warning : warnings) {
                       qml_warnings.push_back(warning.toString());
                       std::cerr << "QML warning: " << warning.toString().toStdString() << '\n';
                     }
                   });
  const QString component_path = QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/StudioScopeVisibility.qml");
  QQmlComponent component(&engine, QUrl::fromLocalFile(component_path));
  std::unique_ptr<QObject> policy(component.create());
  if (!policy) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("StudioScopeVisibility should instantiate");
  }
  if (!qml_warnings.isEmpty())
    return fail("StudioScopeVisibility should load without QML warnings");

  const QString workspace_path = QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml");
  QFile workspace_file(workspace_path);
  if (!workspace_file.open(QIODevice::ReadOnly))
    return fail("StudioWorkspace.qml should be readable for the binding contract");
  const auto workspace_source = QString::fromUtf8(workspace_file.readAll());
  if (!workspace_source.contains(QStringLiteral("value: scopeVisibilityPolicy.scopesVisible")) ||
      !workspace_source.contains(QStringLiteral("Components.StudioScopeVisibility")) ||
      !workspace_source.contains(QStringLiteral("page: root.page")) ||
      !workspace_source.contains(QStringLiteral("workspaceActive: root.workspaceActive")) ||
      !workspace_source.contains(QStringLiteral("dockVisible: root.scopeDockVisible")) ||
      !workspace_source.contains(QStringLiteral("largeScopesMode: root.largeScopesMode")) ||
      !workspace_source.contains(QStringLiteral("dockPosition: root.scopeDockPosition")) ||
      !workspace_source.contains(QStringLiteral("contextPanelVisible: root.editContextPanelActive")) ||
      !workspace_source.contains(QStringLiteral("inspectorPanelVisible: root.editInspectorPanelActive")) ||
      !workspace_source.contains(QStringLiteral("inspectorTab: root.editInspectorTab")))
    return fail("StudioWorkspace should bind scope sampling to the visibility policy component");

  const auto set_state = [&](const QString& page, bool workspace_active, bool dock_visible,
                             bool large, const QString& position, bool context_visible,
                             bool inspector_visible, const QString& tab) {
    return set(*policy, "page", page) &&
        set(*policy, "workspaceActive", workspace_active) &&
        set(*policy, "dockVisible", dock_visible) &&
        set(*policy, "largeScopesMode", large) &&
        set(*policy, "dockPosition", position) &&
        set(*policy, "contextPanelVisible", context_visible) &&
        set(*policy, "inspectorPanelVisible", inspector_visible) &&
        set(*policy, "inspectorTab", tab);
  };

  if (!set_state(QStringLiteral("edit"), true, true, false, QStringLiteral("right"),
                 false, true, QStringLiteral("layers")) ||
      !expect(*policy, false, "A right-docked scope panel is inactive while Layers is selected"))
    return 1;
  if (!set_state(QStringLiteral("edit"), true, true, false, QStringLiteral("right"),
                 false, true, QStringLiteral("scopes")) ||
      !expect(*policy, true, "A visible right-docked Scopes tab requests scope data"))
    return 1;
  if (!set_state(QStringLiteral("edit"), true, true, false, QStringLiteral("right"),
                 false, false, QStringLiteral("scopes")) ||
      !expect(*policy, false, "A hidden right inspector does not request scope data"))
    return 1;
  if (!set_state(QStringLiteral("edit"), true, true, false, QStringLiteral("left"),
                 true, true, QStringLiteral("layers")) ||
      !expect(*policy, true, "A visible left-docked scope panel requests scope data"))
    return 1;
  if (!set_state(QStringLiteral("edit"), true, true, false, QStringLiteral("left"),
                 false, true, QStringLiteral("layers")) ||
      !expect(*policy, false, "A collapsed left context panel does not request scope data"))
    return 1;
  if (!set_state(QStringLiteral("edit"), true, true, false, QStringLiteral("bottom"),
                 false, true, QStringLiteral("layers")) ||
      !expect(*policy, true, "A visible bottom-docked scope panel requests scope data"))
    return 1;
  if (!set_state(QStringLiteral("edit"), true, true, true, QStringLiteral("right"),
                 false, false, QStringLiteral("layers")) ||
      !expect(*policy, true, "The full-screen scope surface requests scope data"))
    return 1;
  if (!set_state(QStringLiteral("edit"), false, true, true, QStringLiteral("right"),
                 false, false, QStringLiteral("layers")) ||
      !expect(*policy, false, "An inactive workspace does not request scope data"))
    return 1;
  if (!set_state(QStringLiteral("library"), true, true, true, QStringLiteral("right"),
                 false, false, QStringLiteral("layers")) ||
      !expect(*policy, false, "The library page does not request edit scopes"))
    return 1;
  if (!qml_warnings.isEmpty())
    return fail("Scope visibility transitions should not emit QML warnings");

  return 0;
}
