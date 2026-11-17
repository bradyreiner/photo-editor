#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

#include <iostream>
#include <utility>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

class ComponentWorkspaceStub final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool canApply READ canApply NOTIFY stateChanged)
  Q_PROPERTY(bool textEditSessionActive READ textEditSessionActive NOTIFY stateChanged)
  Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)

 public:
  explicit ComponentWorkspaceStub(QObject* parent = nullptr) : QObject(parent) {}

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] bool busy() const { return busy_; }
  [[nodiscard]] bool canApply() const { return can_apply_ && !text_edit_session_active_; }
  [[nodiscard]] bool textEditSessionActive() const { return text_edit_session_active_; }
  [[nodiscard]] QVariantList breadcrumbs() const { return breadcrumbs_; }
  [[nodiscard]] QString error() const { return error_; }
  [[nodiscard]] int applyCalls() const { return apply_calls_; }
  [[nodiscard]] int cancelCalls() const { return cancel_calls_; }

  void setState(bool active, bool busy, bool can_apply,
                QVariantList breadcrumbs = {}, QString error = {},
                bool text_edit_session_active = false) {
    active_ = active;
    busy_ = busy;
    can_apply_ = can_apply;
    breadcrumbs_ = std::move(breadcrumbs);
    error_ = std::move(error);
    text_edit_session_active_ = text_edit_session_active;
    emit stateChanged();
  }

  [[nodiscard]] int textSessionResolveCalls() const { return text_session_resolve_calls_; }

  Q_INVOKABLE bool resolveTextSession() {
    ++text_session_resolve_calls_;
    text_edit_session_active_ = false;
    emit stateChanged();
    return true;
  }

  Q_INVOKABLE bool apply() {
    ++apply_calls_;
    return true;
  }
  Q_INVOKABLE bool cancel() {
    ++cancel_calls_;
    return true;
  }

 signals:
  void stateChanged();

 private:
  bool active_ = false;
  bool busy_ = false;
  bool can_apply_ = false;
  bool text_edit_session_active_ = false;
  QVariantList breadcrumbs_;
  QString error_;
  int apply_calls_ = 0;
  int cancel_calls_ = 0;
  int text_session_resolve_calls_ = 0;
};

namespace {

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (!root) return nullptr;
  if (root->objectName() == object_name) return root;
  for (auto* child : root->childItems()) {
    if (auto* match = find_item(child, object_name)) return match;
  }
  return nullptr;
}

bool click(QQuickWindow* window, QQuickItem* item) {
  if (!window || !item || !item->isVisible() || !item->isEnabled()) return false;
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(item->boundingRect().center()).toPoint());
  QTest::qWait(30);
  return true;
}

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  ComponentWorkspaceStub workspace;
  engine.rootContext()->setContextProperty(QStringLiteral("componentWorkspace"), &workspace);

  const QString components = QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components");
  const QString qml = QStringLiteral(R"qml(import QtQuick
import QtQuick.Controls.Basic
import "file:%1"
ApplicationWindow {
    width: 420
    height: 220
    visible: true
    PhotoComponentWorkspaceBar {
        objectName: "componentWorkspaceBar"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        orchestrator: componentWorkspace
        workspace: componentWorkspace
    }
})qml").arg(components);

  QQmlComponent component(&engine);
  component.setData(qml.toUtf8(), QUrl(QStringLiteral("file:///photo-component-workspace-ui.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (!object) {
    for (const auto& error : component.errors())
      std::cerr << error.toString().toStdString() << '\n';
    return fail(QStringLiteral("Create the component workspace bar fixture"));
  }

  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) return fail(QStringLiteral("The component workspace fixture is a window"));
  window->show();
  QTest::qWait(80);

  auto* bar = find_item(window->contentItem(), QStringLiteral("componentWorkspaceBar"));
  auto* apply = find_item(window->contentItem(), QStringLiteral("photoComponentWorkspaceApplyButton"));
  auto* cancel = find_item(window->contentItem(), QStringLiteral("photoComponentWorkspaceCancelButton"));
  auto* error = find_item(window->contentItem(), QStringLiteral("photoComponentWorkspaceError"));
  if (!bar || !apply || !cancel || !error)
    return fail(QStringLiteral("The component workspace bar exposes its actions and error status"));
  if (bar->isVisible()) return fail(QStringLiteral("The component bar stays hidden outside a component session"));

  workspace.setState(true, false, true,
                     {QVariantMap{{QStringLiteral("name"), QStringLiteral("Poster")},
                                  {QStringLiteral("depth"), 0}},
                      QVariantMap{{QStringLiteral("name"), QStringLiteral("Title group")},
                                  {QStringLiteral("depth"), 1}}});
  QTest::qWait(50);
  if (!bar->isVisible() || !apply->isEnabled() || !cancel->isEnabled())
    return fail(QStringLiteral("The ready child session presents Apply and Cancel"));
  if (!click(window, apply) || workspace.applyCalls() != 1)
    return fail(QStringLiteral("Pointer dispatch applies the child component draft"));

  workspace.setState(true, true, false,
                     {QVariantMap{{QStringLiteral("name"), QStringLiteral("Poster")},
                                  {QStringLiteral("depth"), 0}}});
  QTest::qWait(40);
  if (apply->isEnabled() || !cancel->isEnabled())
    return fail(QStringLiteral("Apply stays guarded while busy and Cancel remains available"));
  if (!click(window, cancel) || workspace.cancelCalls() != 1)
    return fail(QStringLiteral("A busy component session can cancel its opening or publishing worker"));

  workspace.setState(true, false, true,
                     {QVariantMap{{QStringLiteral("name"), QStringLiteral("Poster")},
                                  {QStringLiteral("depth"), 0}}}, QString(), true);
  QTest::qWait(40);
  if (!apply->isEnabled() || workspace.canApply())
    return fail(QStringLiteral("Apply offers to finish an active text session before commit"));
  if (!click(window, apply) || workspace.textSessionResolveCalls() != 1 || workspace.applyCalls() != 2)
    return fail(QStringLiteral("Apply flushes active text before invoking the component commit"));

  workspace.setState(true, false, true,
                     {QVariantMap{{QStringLiteral("name"), QStringLiteral("Poster")},
                                  {QStringLiteral("depth"), 0}}},
                     QStringLiteral("The child preview could not be prepared."));
  QTest::qWait(40);
  if (!error->isVisible() || error->property("text").toString().isEmpty())
    return fail(QStringLiteral("Component failures remain visible in the bar"));
  if (!click(window, cancel) || workspace.cancelCalls() != 2)
    return fail(QStringLiteral("Pointer dispatch cancels and returns from the component draft"));

  window->setWidth(340);
  workspace.setState(true, false, true,
                     {QVariantMap{{QStringLiteral("name"), QStringLiteral("Poster")},
                                  {QStringLiteral("depth"), 0}},
                      QVariantMap{{QStringLiteral("name"), QStringLiteral("Very long nested title group")},
                                  {QStringLiteral("depth"), 1}}});
  QTest::qWait(80);
  const QRectF apply_rect = apply->mapRectToItem(bar, apply->boundingRect());
  const QRectF cancel_rect = cancel->mapRectToItem(bar, cancel->boundingRect());
  if (apply_rect.left() < -1 || apply_rect.right() > bar->width() + 1 ||
      cancel_rect.left() < -1 || cancel_rect.right() > bar->width() + 1 ||
      apply_rect.intersects(cancel_rect))
    return fail(QStringLiteral("Narrow layouts keep both component actions inside the bar without overlap"));

  return 0;
}

#include "photo_component_workspace_ui_test.moc"
