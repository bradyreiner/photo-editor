#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QPointer>
#include <iostream>
#include <memory>
#include <utility>

namespace {

void settle() {
  for (auto i = 0; i < 30; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QVariantMap make_tool(QString label, QString action_key, QString revision) {
  return {{"id", "develop"},
          {"railObjectName", "stableDevelopTool"},
          {"label", std::move(label)},
          {"icon", "adjustments"},
          {"enabled", true},
          {"active", true},
          {"variant", "Standard"},
          {"statusText", "Ready"},
          {"revision", std::move(revision)},
          {"menuActions", QVariantList{
                               QVariantMap{{"key", std::move(action_key)},
                                           {"label", "Current action"},
                                           {"enabled", true}}}}};
}

QObject* find_item_key(QObject* root, const QString& key) {
  auto* item = qobject_cast<QQuickItem*>(root);
  if (item == nullptr)
    return nullptr;
  const auto item_key = item->property("itemKey");
  if (item_key.isValid() && item_key.toString() == key)
    return item;
  for (auto* child : item->childItems()) {
    if (auto* found = find_item_key(child, key))
      return found;
  }
  return nullptr;
}

QQuickItem* find_visual_object_name(QQuickItem* root, const QString& name) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == name)
    return root;
  for (auto* child : root->childItems()) {
    if (auto* found = find_visual_object_name(child, name))
      return found;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(
      &engine,
      QUrl::fromLocalFile(QStringLiteral(
          PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/ToolRail.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* rail = qobject_cast<QQuickItem*>(object.get());
  if (rail == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return 1;
  }

  QQuickWindow window;
  window.resize(420, 540);
  rail->setParentItem(window.contentItem());
  rail->setWidth(72);
  rail->setHeight(500);
  rail->setProperty("menusEnabled", true);
  rail->setProperty("tools", QVariantList{make_tool("Develop", "old-action", "old")});
  window.show();
  settle();

  auto* original_button = find_visual_object_name(
      rail, QStringLiteral("stableDevelopTool"));
  auto* original_button_item = qobject_cast<QQuickItem*>(original_button);
  if (original_button_item == nullptr ||
      original_button->property("label").toString() != QStringLiteral("Develop") ||
      !original_button->property("active").toBool()) {
    std::cerr << "Initial ToolRail delegate did not expose its production tool state\n";
    return 1;
  }
  QPointer<QObject> stable_delegate = original_button;

  // Updating all payload fields while retaining the ordered tool ID must
  // update the existing delegate's bindings without replacing its instance.
  rail->setProperty("tools", QVariantList{make_tool("Develop Updated", "fresh-action", "fresh")});
  settle();
  if (stable_delegate.isNull() || stable_delegate.data() != original_button ||
      original_button->property("label").toString() !=
          QStringLiteral("Develop Updated") ||
      original_button->property("variantLabel").toString() !=
          QStringLiteral("Standard") ||
      find_visual_object_name(rail, QStringLiteral("stableDevelopTool")) !=
          original_button) {
    std::cerr << "Same-ID ToolRail payload updates should preserve the delegate and refresh its bindings\n";
    return 1;
  }

  QQmlComponent signal_capture_component(&engine);
  signal_capture_component.setData(R"qml(
import QtQuick
Item {
    id: root
    property var target
    property string activatedKey: ""
    property var activatedTool: ({})
    Connections {
        target: root.target
        function onMenuItemTriggered(tool, key) {
            root.activatedTool = tool
            root.activatedKey = key
        }
    }
}
)qml", QUrl(QStringLiteral("inmemory:tool-rail-signal-capture.qml")));
  for (auto i = 0; i < 200 && signal_capture_component.isLoading(); ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (signal_capture_component.status() != QQmlComponent::Ready) {
    std::cerr << signal_capture_component.errorString().toStdString() << '\n';
    return 1;
  }
  std::unique_ptr<QObject> signal_capture(signal_capture_component.create());
  if (!signal_capture) {
    std::cerr << signal_capture_component.errorString().toStdString() << '\n';
    return 1;
  }
  signal_capture->setProperty("target", QVariant::fromValue<QObject*>(rail));
  if (!QMetaObject::invokeMethod(original_button, "menuRequested")) {
    std::cerr << "Could not open the production ToolRail menu\n";
    return 1;
  }
  settle();
  auto* fresh_row = find_item_key(window.contentItem(), QStringLiteral("fresh-action"));
  auto* fresh_row_item = qobject_cast<QQuickItem*>(fresh_row);
  if (fresh_row_item == nullptr ||
      fresh_row->property("itemLabel").toString() != QStringLiteral("Current action")) {
    std::cerr << "Same-ID update should expose its current menu payload\n";
    return 1;
  }
  fresh_row_item->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent activate(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
  QCoreApplication::sendEvent(fresh_row_item, &activate);
  settle();
  const auto emitted_tool = signal_capture->property("activatedTool").toMap();
  if (signal_capture->property("activatedKey").toString() !=
          QStringLiteral("fresh-action") ||
      emitted_tool.value("revision").toString() != QStringLiteral("fresh") ||
      emitted_tool.value("label").toString() != QStringLiteral("Develop Updated")) {
    std::cerr << "Menu activation should report the latest action and tool payload\n";
    return 1;
  }

  rail->setParentItem(nullptr);
  std::cout << "ToolRail keeps delegate identity across same-ID payload updates and activates current menu payloads\n";
  return 0;
}
