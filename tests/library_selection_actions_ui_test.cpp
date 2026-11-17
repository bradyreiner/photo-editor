#include <QGuiApplication>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QVariant>
#include <iostream>
#include <memory>
#include <functional>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
bool wait_until(const std::function<bool()>& ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 2000) QTest::qWait(10);
  return ready();
}
QQuickItem* find_item(QQuickItem* root, const QString& name) {
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems())
    if (auto* found = find_item(child, name)) return found;
  return nullptr;
}
bool click(QQuickWindow* window, const QString& name, Qt::MouseButton button = Qt::LeftButton) {
  auto* item = find_item(window->contentItem(), name);
  if (!item || !item->isVisible() || !item->isEnabled()) return false;
  QTest::mouseClick(window, button, Qt::NoModifier,
      item->mapToScene(item->boundingRect().center()).toPoint());
  QTest::qWait(180);
  return true;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(QByteArray(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixtureWindow
    width: 600; height: 400; visible: true
    color: "#151515"
    property bool inspectorVisible: true
    title: "Library deletion verification - disposable fixture"
    QtObject {
        id: catalog
        objectName: "catalog"
        property var selectedAssetIds: []
        property var assets: ["a", "b", "c"]
        property int assetCount: assets.length
        property int selectedIndex: -1
        property int selectedAssetCount: selectedAssetIds.length
        property bool busy: false
        property bool deleting: false
        property int deleteProgress: 0
        property int deleteTargetCount: 0
        property string errorMessage: ""
        property int deleteCalls: 0
        property var deletedIds: []
        property bool failDelete: false
        signal selectionChanged()
        onSelectedAssetIdsChanged: selectionChanged()
        function assetAt(index) { return {assetId: assets[index]} }
        function assetSelected(id) { return selectedAssetIds.indexOf(id) >= 0 }
        function selectIndex(index) { selectedIndex = index }
        function selectIndexWithModifiers(index, modifiers, selectionMode) {
            selectedIndex = index
            selectedAssetIds = [assets[index]]
        }
        function deleteSelectedAssets() {
            deleteCalls++
            deletedIds = selectedAssetIds.slice(0)
            if (failDelete) errorMessage = "Injected catalog failure. Your images remain in the Library."
            else { errorMessage = ""; selectedAssetIds = [] }
        }
    }
    Repeater {
        model: 2
        Rectangle {
            required property int index
            objectName: index === 0 ? "image-a" : "image-b"
            x: 12 + index * 150; y: 60; width: 138; height: 100
            color: index === 0 ? "#79533e" : "#365d78"
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) { actions.openImageMenu(parent.index, parent, mouse.x, mouse.y) }
            }
        }
    }
    Rectangle {
        objectName: "inspector"
        anchors.top: parent.top; anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 200
        color: "#202020"
        visible: fixtureWindow.inspectorVisible
        LibrarySelectionActions {
            id: actions
            objectName: "actions"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 12
            catalogAssets: catalog
            visible: hasContent
        }
    }
})qml"), QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/library-delete-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (!object) {
    for (const auto& error : component.errors()) std::cerr << error.toString().toStdString() << '\n';
    return fail("create Library selection actions fixture");
  }
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  auto* catalog = object->findChild<QObject*>("catalog");
  auto* actions = object->findChild<QQuickItem*>("actions");
  if (!window || !catalog || !actions) return fail("find Library action fixture objects");
  window->show();
  QTest::qWait(120);
  const auto select = [&](const QStringList& ids) {
    catalog->setProperty("selectedAssetIds", ids);
    QTest::qWait(100);
  };
  const auto calls = [&] { return catalog->property("deleteCalls").toInt(); };
  const auto modal = [&] { return actions->property("confirmationVisible").toBool(); };
  if (actions->isVisible()) return fail("empty Library selection should not show deletion actions");

  select({"a"});
  if (!actions->isVisible() || !click(window, "libraryDeleteSelectedButton") || !modal() || calls() != 0)
    return fail("visible Delete Image should open confirmation without removing anything");
  auto* consequences = object->findChild<QObject*>("libraryDeleteConsequences");
  if (!consequences || !consequences->property("text").toString().contains("Source files stay on disk") ||
      !consequences->property("text").toString().contains("saved edits"))
    return fail("confirmation should disclose retained source files and removed saved edits");
  if (!click(window, "libraryDeleteCancelButton") || modal() || calls() != 0)
    return fail("Cancel should leave all selected images intact");

  select({"a", "b"});
  if (!click(window, "image-b", Qt::RightButton) || !actions->property("contextMenuVisible").toBool() ||
      catalog->property("selectedAssetIds").toStringList() != QStringList{"a", "b"})
    return fail("right-clicking a selected image must preserve the batch and open its menu");
  if (const auto capture = qEnvironmentVariable("PHOTO_EDITOR_LIBRARY_CONTEXT_CAPTURE"); !capture.isEmpty()) {
    if (!window->grabWindow().save(capture)) return fail("capture image context menu and sidebar");
  }
  if (!click(window, "libraryContextDeleteItem") || !modal() || calls() != 0 ||
      !click(window, "libraryDeleteCancelButton"))
    return fail("right-click Delete must share the cancelable confirmation without deleting immediately");

  select({"a"});
  window->setProperty("inspectorVisible", false);
  if (!click(window, "image-b", Qt::RightButton) ||
      catalog->property("selectedAssetIds").toStringList() != QStringList{"b"} ||
      !click(window, "libraryContextDeleteItem") || !modal() ||
      !click(window, "libraryDeleteCancelButton") || calls() != 0)
    return fail("right-clicking an unselected image must target it alone, including with the sidebar hidden");
  window->setProperty("inspectorVisible", true);

  if (!click(window, "libraryDeleteSelectedButton")) return fail("reopen deletion confirmation");
  select({"c"});
  if (!wait_until([&] { return !modal(); }) || calls() != 0)
    return fail("changing selection must invalidate an old confirmation");

  catalog->setProperty("busy", true);
  QTest::qWait(50);
  if (actions->property("canDelete").toBool() || click(window, "libraryDeleteSelectedButton"))
    return fail("catalog operations should disable deletion");
  catalog->setProperty("busy", false);
  select({"a", "b", "c"});
  if (!click(window, "libraryDeleteSelectedButton") || !modal()) return fail("confirm a multiple image selection");
  if (!click(window, "libraryDeleteConfirmButton") || calls() != 1 || modal() || actions->isVisible() ||
      catalog->property("deletedIds").toStringList() != QStringList{"a", "b", "c"})
    return fail("confirmation must remove exactly the displayed selection once");

  window->setWidth(340);
  select({"kept"});
  catalog->setProperty("failDelete", true);
  if (!click(window, "libraryDeleteSelectedButton") || !modal()) return fail("open narrow-window confirmation");
  if (const auto capture = qEnvironmentVariable("PHOTO_EDITOR_LIBRARY_DELETE_CAPTURE"); !capture.isEmpty()) {
    QTest::qWait(150);
    if (!window->grabWindow().save(capture)) return fail("capture rendered Library deletion confirmation");
  }
  if (!click(window, "libraryDeleteConfirmButton") || calls() != 2 ||
      catalog->property("selectedAssetCount").toInt() != 1)
    return fail("failed catalog deletion must retain the selection");
  auto* error = find_item(window->contentItem(), "libraryDeleteError");
  if (!error || !error->isVisible() || !error->property("text").toString().contains("Injected catalog failure"))
    return fail("failed deletion must remain visibly explained");

  if (!click(window, "libraryDeleteSelectedButton")) return fail("retry confirmation");
  actions->setProperty("actionContextActive", false);
  if (!wait_until([&] { return !modal(); }) || calls() != 2)
    return fail("leaving Library must dismiss confirmation without deleting");
  std::cout << "PASS: sidebar and image context deletion, selected batch preservation, unselected image targeting, hidden sidebar, cancel, stale selection, busy state, exact batch confirmation, narrow layout and failure feedback\n";
  return 0;
}
