#include <QAccessible>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (auto index = 0; index < 16; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool send_key(QQuickWindow* window, Qt::Key key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  if (!QCoreApplication::sendEvent(window, &press))
    return false;
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &release);
  return true;
}

QQuickItem* find_visual_child(QQuickItem* parent, const QString& accessible_name) {
  if (parent == nullptr)
    return nullptr;
  for (auto* child : parent->childItems()) {
    auto* accessible = QAccessible::queryAccessibleInterface(child);
    if (accessible != nullptr && accessible->text(QAccessible::Name) == accessible_name)
      return child;
    if (auto* nested = find_visual_child(child, accessible_name))
      return nested;
  }
  return nullptr;
}

QQuickItem* find_named_visual_child(QQuickItem* parent, const QString& object_name) {
  if (parent == nullptr)
    return nullptr;
  for (auto* child : parent->childItems()) {
    if (child->objectName() == object_name)
      return child;
    if (auto* nested = find_named_visual_child(child, object_name))
      return nested;
  }
  return nullptr;
}

QRectF bounds_in_window(QQuickItem* item, QQuickWindow* window) {
  if (item == nullptr || window == nullptr)
    return {};
  const auto origin = item->mapToItem(window->contentItem(), QPointF(0, 0));
  return {origin, QSizeF(item->width(), item->height())};
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
  id: fixture
  width: 900
  height: 680
  visible: true
  color: "#161b20"
  property string activatedAssetId: ""
  property int projectScopeRequestCount: 0
  property int footerRequestCount: 0
  property var sourceAssets: [
    {assetId: "photo-01", cameraLabel: "Portrait One", mediaType: "photo", online: true,
     thumbnailSource: "", filePath: "/photos/portrait-one.jpg"},
    {assetId: "clip-02", cameraLabel: "Kitchen Test Clip", mediaType: "video", online: false,
     thumbnailSource: "", filePath: "/missing/kitchen.mov", durationText: "0:12"}
  ]
  Button {
    objectName: "galleryPickerOpener"
    text: "Choose timeline media"
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.margins: 16
  }
  Theme { id: pickerTheme }
  GalleryPickerDialog {
    id: picker
    theme: pickerTheme
    dialogTitle: "Choose a clip"
    subtitle: "Select one item for the timeline."
    emptyStateText: "No local media is available."
    showProjectScopeToggle: true
    scope: "all"
    assets: fixture.sourceAssets
    onAssetActivated: function(asset) { fixture.activatedAssetId = String(asset.assetId || "") }
    onProjectScopeRequested: fixture.projectScopeRequestCount += 1
    sidePanelFooter: Component {
      ColumnLayout {
        width: parent ? parent.width : 0
        ActionButton {
          objectName: "fixtureGalleryLoadAction"
          Layout.fillWidth: true
          text: "Load to source"
          tone: "primary"
          enabled: picker.hasSelection && picker.selectedOnline
          disabledReason: "Select available media first."
          onClicked: fixture.footerRequestCount += 1
        }
      }
    }
  }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/gallery-picker-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Gallery Picker fixture should instantiate");
  }

  auto* opener = qobject_cast<QQuickItem*>(window->findChild<QObject*>("galleryPickerOpener"));
  auto* dialog = window->findChild<QObject*>("galleryPickerDialog");
  if (!opener || !dialog)
    return fail("Gallery Picker should expose its opener and dialog");

  window->show();
  opener->forceActiveFocus(Qt::TabFocusReason);
  settle();
  if (window->activeFocusItem() != opener || !QMetaObject::invokeMethod(dialog, "openDialog"))
    return fail("Gallery Picker should open from its focused invoking control");
  settle();
  auto* first_card = find_visual_child(window->contentItem(), QStringLiteral("Portrait One"));
  auto* second_card = find_visual_child(window->contentItem(), QStringLiteral("Kitchen Test Clip"));
  auto* grid = find_named_visual_child(window->contentItem(), QStringLiteral("timelineVirtualizedGalleryGrid"));
  if (!first_card || !second_card || !grid)
    return fail("Gallery Picker should create its virtualized grid and named asset cards when opened");
  auto* background = qvariant_cast<QQuickItem*>(dialog->property("background"));
  auto* dialog_accessible = QAccessible::queryAccessibleInterface(background);
  auto* first_card_accessible = QAccessible::queryAccessibleInterface(first_card);
  if (!dialog->property("visible").toBool() || dialog->property("selectedAssetId") != "photo-01" ||
      dialog_accessible == nullptr || dialog_accessible->role() != QAccessible::Dialog ||
      dialog_accessible->text(QAccessible::Name) != QStringLiteral("Choose a clip") ||
      dialog_accessible->text(QAccessible::Description) != QStringLiteral("Select one item for the timeline.") ||
      first_card_accessible == nullptr || first_card_accessible->role() != QAccessible::Button ||
      first_card_accessible->text(QAccessible::Name) != QStringLiteral("Portrait One") ||
      window->activeFocusItem() != first_card) {
    std::cerr << "Gallery Picker initial state: dialog=" << dialog->property("visible").toBool()
              << " selected=" << dialog->property("selectedAssetId").toString().toStdString()
              << " focused=" << (window->activeFocusItem() == first_card)
              << " currentIndex=" << grid->property("currentIndex").toInt() << '\n';
    return fail("Gallery Picker should name the dialog, select the first asset, and show focus on its card");
  }

  auto* footer_action = find_named_visual_child(window->contentItem(), QStringLiteral("fixtureGalleryLoadAction"));
  if (!footer_action || !footer_action->isVisible() || !footer_action->isEnabled() ||
      !QMetaObject::invokeMethod(footer_action, "clicked") || window->property("footerRequestCount").toInt() != 1)
    return fail("Populated Gallery Picker should preserve its workspace action handoff");
  if (!send_key(window, Qt::Key_Tab) || window->activeFocusItem() != second_card)
    return fail("Tab should advance between Gallery Picker asset cards");
  if (!send_key(window, Qt::Key_Space) || dialog->property("selectedAssetId") != "clip-02" ||
      dialog->property("selectedOnline").toBool())
    return fail("Space should select the offline video and update its selection state");
  if (!send_key(window, Qt::Key_Return) ||
      window->property("activatedAssetId") != QStringLiteral("clip-02"))
    return fail("Return should activate the focused Gallery Picker asset");
  auto* project_scope_button = window->findChild<QObject*>("galleryPickerProjectScopeButton");
  if (!project_scope_button || !QMetaObject::invokeMethod(project_scope_button, "clicked") ||
      window->property("projectScopeRequestCount").toInt() != 1)
    return fail("Current Project should emit its scope request");

  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_GALLERY_PICKER_IMAGE");
  if (!output.isEmpty() && !window->grabWindow().save(output))
    return fail("Gallery Picker capture should save when requested");
  if (!send_key(window, Qt::Key_Escape))
    return fail("Gallery Picker should accept Escape");
  settle();
  if (dialog->property("visible").toBool() || window->activeFocusItem() != opener)
    return fail("Escape should close Gallery Picker and return focus to its opener");

  const auto original_assets = window->property("sourceAssets");
  window->setProperty("sourceAssets", QVariantList{});
  window->resize(560, 420);
  settle();
  if (!QMetaObject::invokeMethod(dialog, "openDialog"))
    return fail("Gallery Picker should reopen with an empty library");
  settle();
  auto* empty_card = window->findChild<QObject*>("galleryPickerEmptyStateCard");
  auto* close_button = window->findChild<QObject*>("galleryPickerCloseButton");
  project_scope_button = window->findChild<QObject*>("galleryPickerProjectScopeButton");
  auto* all_media_button = window->findChild<QObject*>("galleryPickerAllMediaScopeButton");
  if (!dialog->property("visible").toBool() || dialog->property("hasSelection").toBool() ||
      !empty_card || !empty_card->property("visible").toBool() || !close_button ||
      !project_scope_button || !all_media_button || window->activeFocusItem() != close_button)
    return fail("An empty Gallery Picker should show its guidance card and focus Close");
  auto* dialog_background = qvariant_cast<QQuickItem*>(dialog->property("background"));
  auto* main_panel = find_named_visual_child(window->contentItem(), QStringLiteral("galleryPickerMainPanel"));
  auto* selection_panel = find_named_visual_child(window->contentItem(), QStringLiteral("galleryPickerSelectionPanel"));
  auto* title_group = find_named_visual_child(window->contentItem(), QStringLiteral("galleryPickerTitleGroup"));
  auto* close_item = qobject_cast<QQuickItem*>(close_button);
  auto* project_item = qobject_cast<QQuickItem*>(project_scope_button);
  auto* all_media_item = qobject_cast<QQuickItem*>(all_media_button);
  const auto dialog_bounds = bounds_in_window(dialog_background, window);
  const auto main_bounds = bounds_in_window(main_panel, window);
  const auto selection_bounds = bounds_in_window(selection_panel, window);
  footer_action = find_named_visual_child(window->contentItem(), QStringLiteral("fixtureGalleryLoadAction"));
  if (!selection_panel || selection_panel->isVisible() || !footer_action || !footer_action->isVisible() ||
      footer_action->isEnabled() || !main_bounds.contains(bounds_in_window(footer_action, window)) ||
      main_bounds.width() < dialog_bounds.width() - 2)
    return fail("Empty Gallery Picker should reclaim preview space while retaining its disabled workspace action");
  const auto title_bounds = bounds_in_window(title_group, window);
  const auto close_bounds = bounds_in_window(close_item, window);
  const auto project_bounds = bounds_in_window(project_item, window);
  const auto all_media_bounds = bounds_in_window(all_media_item, window);
  if (!dialog_bounds.contains(close_bounds) || !dialog_bounds.contains(project_bounds) ||
      !dialog_bounds.contains(all_media_bounds) || close_bounds.intersects(project_bounds) ||
      close_bounds.intersects(all_media_bounds) || project_bounds.intersects(all_media_bounds) ||
      !main_bounds.contains(close_bounds) || !main_bounds.contains(project_bounds) ||
      !main_bounds.contains(all_media_bounds) ||
      (selection_panel->isVisible() && (close_bounds.intersects(selection_bounds) ||
          project_bounds.intersects(selection_bounds) || all_media_bounds.intersects(selection_bounds))) ||
      close_bounds.intersects(title_bounds) || project_bounds.intersects(title_bounds) ||
      all_media_bounds.intersects(title_bounds))
    return fail("Gallery Picker header actions should remain inside the dialog and not overlap at 560x420");
  const auto compact_output = qEnvironmentVariable("PHOTO_EDITOR_TEST_GALLERY_PICKER_COMPACT_IMAGE");
  if (!compact_output.isEmpty() && !window->grabWindow().save(compact_output))
    return fail("Compact Gallery Picker capture should save when requested");
  window->setProperty("sourceAssets", original_assets);
  settle();
  footer_action = find_named_visual_child(window->contentItem(), QStringLiteral("fixtureGalleryLoadAction"));
  if (!selection_panel->isVisible() || !dialog->property("hasSelection").toBool() ||
      !footer_action || !footer_action->isVisible() || !footer_action->isEnabled() ||
      !bounds_in_window(selection_panel, window).contains(bounds_in_window(footer_action, window)) ||
      !QMetaObject::invokeMethod(footer_action, "clicked") || window->property("footerRequestCount").toInt() != 2)
    return fail("Restored media should restore the preview and workspace action without duplicating handoffs");
  opener->setVisible(false);
  if (!send_key(window, Qt::Key_Escape))
    return fail("Empty Gallery Picker should accept Escape");
  settle();
  auto* focus_fallback = qvariant_cast<QQuickItem*>(dialog->property("focusFallbackItem"));
  if (dialog->property("visible").toBool() || window->activeFocusItem() != focus_fallback)
    return fail("Empty Gallery Picker should return focus to the window after its opener is hidden");

  std::cout << "Gallery Picker semantics, initial card focus, Tab/Space/Return behavior, project scope, empty state, Escape dismissal, opener focus return, and hidden-opener fallback passed\n";
  return 0;
}
