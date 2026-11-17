#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <iostream>
#include <memory>

namespace {
bool wait_for(const std::function<bool()>& predicate) {
  QElapsedTimer timer;
  timer.start();
  do {
    QTest::qWait(10);
    if (predicate()) return true;
  } while (timer.elapsed() < 3000);
  return false;
}

void settle() { QTest::qWait(80); }

QList<QObject*> objects_in_tree(QObject* root) {
  QList<QObject*> objects;
  QSet<QObject*> visited;
  std::function<void(QObject*)> visit = [&](QObject* object) {
    if (object == nullptr || visited.contains(object)) return;
    visited.insert(object);
    objects.push_back(object);
    for (auto* child : object->children()) visit(child);
    if (auto* item = qobject_cast<QQuickItem*>(object))
      for (auto* child_item : item->childItems()) visit(child_item);
  };
  visit(root);
  return objects;
}

QList<QObject*> named_objects(QObject* root, const QString& name) {
  QList<QObject*> matches;
  for (auto* object : objects_in_tree(root)) {
    if (object->objectName() == name) matches.push_back(object);
  }
  return matches;
}

QObject* unique_named_object(QObject* root, const QString& name) {
  const auto matches = named_objects(root, name);
  return matches.size() == 1 ? matches.front() : nullptr;
}

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return 1;
}

bool click_item(QObject* object, bool allow_disabled = false) {
  auto* item = qobject_cast<QQuickItem*>(object);
  if (item == nullptr || !item->isVisible() ||
      (!allow_disabled && !item->isEnabled()) || item->width() <= 1.0 ||
      item->height() <= 1.0 || item->window() == nullptr)
    return false;
  const auto center = item->mapToScene(
      QPointF(item->width() * 0.5, item->height() * 0.5));
  QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier,
                    QPoint(qRound(center.x()), qRound(center.y())));
  settle();
  return true;
}

bool capture_item_window(QObject* object, QQuickWindow* fallback,
                         const QString& path) {
  auto* item = qobject_cast<QQuickItem*>(object);
  auto* window = item != nullptr ? item->window() : fallback;
  if (window == nullptr) return false;
  const auto image = window->grabWindow();
  return !image.isNull() && image.save(path, "PNG");
}

bool verify_action(QObject* menu, const QString& key, bool visible,
                   bool enabled) {
  const auto object_name = QStringLiteral("layerAction_") + key;
  auto* object = unique_named_object(menu, object_name);
  auto* item = qobject_cast<QQuickItem*>(object);
  return item != nullptr && item->isVisible() == visible &&
         item->isEnabled() == enabled;
}

bool verify_unique_actions(QObject* menu, const QStringList& visible_keys,
                           const QStringList& hidden_keys) {
  for (const auto& key : visible_keys) {
    if (named_objects(menu, QStringLiteral("layerAction_") + key).size() != 1)
      return false;
  }
  for (const auto& key : hidden_keys) {
    if (!named_objects(menu, QStringLiteral("layerAction_") + key).isEmpty())
      return false;
  }
  return true;
}

bool has_readable_menu_text(QObject* action) {
  if (action == nullptr) return false;
  const auto label = action->property("text").toString();
  if (label.isEmpty()) return false;
  for (auto* object : objects_in_tree(action)) {
    if (object == action || object->property("text").toString() != label)
      continue;
    auto* text_item = qobject_cast<QQuickItem*>(object);
    if (text_item == nullptr || !text_item->isVisible() || text_item->width() <= 0.0 ||
        text_item->height() <= 0.0)
      continue;
    const auto color_value = object->property("color");
    if (!color_value.isValid()) continue;
    const QColor color = color_value.value<QColor>();
    if (color.isValid() && color.alpha() >= 200 && color.lightness() >= 120)
      return true;
  }
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(
      QStringLiteral("appShell"),
      QVariantMap{{QStringLiteral("interfaceScale"), 1.0},
                  {QStringLiteral("interfaceDensity"), QStringLiteral("balanced")},
                  {QStringLiteral("reducedMotion"), true},
                  {QStringLiteral("highContrast"), false},
                  {QStringLiteral("systemUiFontFamily"), app.font().family()}});

  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: 760
    height: 720
    visible: true
    color: theme.ink950
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    palette.window: theme.ink900
    palette.windowText: theme.textMain
    palette.base: theme.fieldFill
    palette.text: theme.textMain
    palette.button: theme.ink900
    palette.buttonText: theme.textMain
    palette.highlight: theme.editorControlHover
    palette.highlightedText: theme.textStrong
    palette.mid: theme.controlEdge
    palette.disabled.text: theme.textFaint
    palette.disabled.buttonText: theme.textFaint
    palette.disabled.windowText: theme.textFaint
    property bool enableDelete: false
    Theme { id: theme }
    function actionsForMenu() {
        return [
            { key: "edit", label: "Edit", enabled: true },
            { key: "properties", label: "Properties", enabled: false,
              disabledReason: "Select a layer first." },
            { key: "lock-transparency", label: "Lock transparency", checked: true },
            { key: "edit-vector-path", label: "Edit vector path" },
            { key: "component-create", label: "Create component", visible: false },
            { key: "component-edit", label: "Edit component" },
            { key: "component-independent", label: "Make independent", enabled: false,
              disabledReason: "This layer is already independent." },
            { key: "duplicate-with-masks", label: "Duplicate with masks" },
            { key: "toggle-mask", label: "Enable mask", checked: true },
            { key: "use-selection-mask", label: "Use selection as mask" },
            { key: "erase-background", label: "Erase background" },
            { key: "duplicate-without-masks", label: "Duplicate without masks" },
            { key: "delete", label: "Delete Layer", enabled: fixture.enableDelete }
        ];
    }
    function openLayerMenu() { layerMenu.openTransient(opener, opener); }
    ActionButton {
        id: opener
        objectName: "layerMenuOpener"
        x: 24
        y: 20
        text: "Layer actions"
        tone: "secondary"
        onClicked: layerMenu.toggleFrom(opener, opener)
    }
    LayerActionsMenu {
        id: layerMenu
        objectName: "layerActionsMenuUnderTest"
        parent: Overlay.overlay
        x: 170
        y: 64
        menuItemsProvider: function() { return fixture.actionsForMenu(); }
    }
}
)qml", QUrl::fromLocalFile(QStringLiteral(
                 PHOTO_EDITOR_SOURCE_DIR "/tests/layer-actions-menu-ui-fixture.qml")));

  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) return fail(component.errorString());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* menu = unique_named_object(fixture.get(),
                                   QStringLiteral("layerActionsMenuUnderTest"));
  auto* opener = qobject_cast<QQuickItem*>(unique_named_object(
      fixture.get(), QStringLiteral("layerMenuOpener")));
  if (window == nullptr || menu == nullptr || opener == nullptr)
    return fail(QStringLiteral("Create the production layer actions menu fixture"));
  window->requestActivate();

  QSignalSpy triggered(menu, SIGNAL(menuItemTriggered(QString)));
  if (!triggered.isValid())
    return fail(QStringLiteral("Observe LayerActionsMenu action dispatch"));

  const auto proof_directory =
      qEnvironmentVariable("PHOTO_EDITOR_LAYER_MENU_PROOF_DIR").trimmed();
  if (!proof_directory.isEmpty() && !QDir().mkpath(proof_directory))
    return fail(QStringLiteral("Create layer menu proof directory"));
  auto capture = [&](QObject* item, const QString& name) {
    return proof_directory.isEmpty() ||
           capture_item_window(item, window,
                              QDir(proof_directory).filePath(name));
  };

  const QStringList visible_keys{
      QStringLiteral("edit"), QStringLiteral("properties"),
      QStringLiteral("lock-transparency"), QStringLiteral("edit-vector-path"),
      QStringLiteral("component-edit"), QStringLiteral("component-independent"),
      QStringLiteral("duplicate-with-masks"), QStringLiteral("toggle-mask"),
      QStringLiteral("use-selection-mask"), QStringLiteral("erase-background"),
      QStringLiteral("duplicate-without-masks"), QStringLiteral("delete")};
  const QStringList hidden_keys{QStringLiteral("component-create")};

  QMetaObject::invokeMethod(fixture.get(), "openLayerMenu");
  if (!wait_for([&] { return menu->property("visible").toBool(); }))
    return fail(QStringLiteral("Open the production layer actions menu"));
  if (!verify_action(menu, QStringLiteral("edit"), true, true) ||
      !verify_action(menu, QStringLiteral("lock-transparency"), true, true) ||
      !verify_action(menu, QStringLiteral("duplicate-with-masks"), true, true) ||
      !verify_action(menu, QStringLiteral("delete"), true, false) ||
      !verify_action(menu, QStringLiteral("properties"), true, false))
    return fail(QStringLiteral("Primary actions and disabled Delete must render with their supplied states"));

  auto* edit = unique_named_object(menu, QStringLiteral("layerAction_edit"));
  auto* properties = unique_named_object(menu, QStringLiteral("layerAction_properties"));
  if (!has_readable_menu_text(edit))
    return fail(QStringLiteral("Enabled menu labels must use readable theme text against the graphite surface"));
  if (!capture(edit, QStringLiteral("layer-actions-primary.png")))
    return fail(QStringLiteral("Capture the opened primary layer actions menu"));
  if (!click_item(properties, true) || triggered.size() != 0 ||
      !menu->property("visible").toBool())
    return fail(QStringLiteral("Disabled Properties must be inert and leave the menu open"));
  if (!click_item(unique_named_object(menu, QStringLiteral("layerAction_delete")), true) ||
      triggered.size() != 0 || !menu->property("visible").toBool())
    return fail(QStringLiteral("Disabled Delete must be inert and leave the menu open"));
  if (!click_item(edit) || !wait_for([&] { return !menu->property("visible").toBool(); }) ||
      triggered.size() != 1 || triggered.at(0).at(0).toString() != QStringLiteral("edit"))
    return fail(QStringLiteral("Pointer activation must dispatch Edit exactly once and dismiss the menu"));

  QMetaObject::invokeMethod(fixture.get(), "openLayerMenu");
  if (!wait_for([&] { return menu->property("visible").toBool(); }))
    return fail(QStringLiteral("Reopen the layer actions menu for Contents"));
  auto* content_menu = unique_named_object(menu, QStringLiteral("layerActions_content"));
  if (content_menu == nullptr || !QMetaObject::invokeMethod(content_menu, "open") ||
      !wait_for([&] { return content_menu->property("visible").toBool(); }))
    return fail(QStringLiteral("Open the Contents submenu"));
  for (const auto& key : {QStringLiteral("edit-vector-path"),
                           QStringLiteral("component-edit"),
                           QStringLiteral("component-independent")}) {
    if (!verify_action(menu, key, true, key != QStringLiteral("component-independent")))
      return fail(QStringLiteral("Contents submenu action is missing or has the wrong state: ") + key);
  }
  if (!capture(unique_named_object(menu, QStringLiteral("layerAction_component-edit")),
               QStringLiteral("layer-actions-contents.png")))
    return fail(QStringLiteral("Capture the opened Contents submenu"));
  const auto before_disabled_content = triggered.size();
  if (!click_item(unique_named_object(menu,
                                      QStringLiteral("layerAction_component-independent")),
                  true) || triggered.size() != before_disabled_content ||
      !menu->property("visible").toBool())
    return fail(QStringLiteral("Disabled Make independent must be inert"));
  if (!click_item(unique_named_object(menu, QStringLiteral("layerAction_component-edit"))) ||
      !wait_for([&] { return !menu->property("visible").toBool(); }) ||
      triggered.size() != 2 ||
      triggered.at(1).at(0).toString() != QStringLiteral("component-edit"))
    return fail(QStringLiteral("Pointer activation must dispatch the Contents action exactly once"));

  QMetaObject::invokeMethod(fixture.get(), "openLayerMenu");
  if (!wait_for([&] { return menu->property("visible").toBool(); }))
    return fail(QStringLiteral("Reopen the layer actions menu for masks"));
  auto* selection_menu = unique_named_object(menu, QStringLiteral("layerActions_selections"));
  if (selection_menu == nullptr || !QMetaObject::invokeMethod(selection_menu, "open") ||
      !wait_for([&] { return selection_menu->property("visible").toBool(); }))
    return fail(QStringLiteral("Open the selections and masks submenu"));
  for (const auto& key : {QStringLiteral("toggle-mask"),
                           QStringLiteral("use-selection-mask"),
                           QStringLiteral("erase-background"),
                           QStringLiteral("duplicate-without-masks")}) {
    if (!verify_action(menu, key, true, true))
      return fail(QStringLiteral("Selections and masks action is missing: ") + key);
  }
  if (!capture(unique_named_object(menu, QStringLiteral("layerAction_toggle-mask")),
               QStringLiteral("layer-actions-masks.png")))
    return fail(QStringLiteral("Capture the opened selections and masks submenu"));
  if (!verify_unique_actions(menu, visible_keys, hidden_keys))
    return fail(QStringLiteral("Every visible supported action must instantiate once; hidden actions must be absent"));
  QMetaObject::invokeMethod(menu, "close");
  if (!wait_for([&] { return !menu->property("visible").toBool(); }))
    return fail(QStringLiteral("Close the selections and masks submenu"));

  fixture->setProperty("enableDelete", true);
  opener->forceActiveFocus(Qt::TabFocusReason);
  QMetaObject::invokeMethod(fixture.get(), "openLayerMenu");
  if (!wait_for([&] { return menu->property("visible").toBool(); }) ||
      !verify_action(menu, QStringLiteral("delete"), true, true))
    return fail(QStringLiteral("Delete must become enabled when the provider enables it"));
  if (!click_item(unique_named_object(menu, QStringLiteral("layerAction_delete"))) ||
      !wait_for([&] { return !menu->property("visible").toBool(); }) ||
      triggered.size() != 3 || triggered.at(2).at(0).toString() != QStringLiteral("delete"))
    return fail(QStringLiteral("Pointer activation must dispatch Delete exactly once"));

  QMetaObject::invokeMethod(fixture.get(), "openLayerMenu");
  if (!wait_for([&] { return menu->property("visible").toBool(); }))
    return fail(QStringLiteral("Open the menu for Escape focus restoration"));
  QTest::keyClick(window, Qt::Key_Escape);
  if (!wait_for([&] { return !menu->property("visible").toBool(); }) ||
      !wait_for([&] { return opener->hasActiveFocus(); }))
    return fail(QStringLiteral("Escape must dismiss the menu and restore focus to its opener"));

  std::cout << "Layer actions menu: visible and hidden keys, disabled states, primary/Contents/mask rendering, pointer dispatch, Delete, and Escape focus return passed\n";
  return 0;
}
