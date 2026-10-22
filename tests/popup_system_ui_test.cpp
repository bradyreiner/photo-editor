#include "qml_popup_wait.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QStringList>
#include <QQuickItem>
#include <QQuickWindow>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (int i = 0; i < 40; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
void escape(QQuickWindow* window) {
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &press);
  QCoreApplication::sendEvent(window, &release);
  settle();
}
QQuickItem* item(QObject* object, const char* property) {
  return qvariant_cast<QQuickItem*>(object->property(property));
}
bool inside(QQuickWindow* window, QQuickItem* content) {
  return content && window->contentItem()->boundingRect().contains(
      content->mapRectToItem(window->contentItem(), content->boundingRect()));
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QStringList binding_errors;
  QObject::connect(&engine, &QQmlEngine::warnings, [&binding_errors](const QList<QQmlError>& errors) {
    for (const auto& error : errors)
      if (error.description().contains("Binding loop") || error.description().contains("ReferenceError") ||
          error.description().contains("TypeError") || error.description().contains("Unable to assign"))
        binding_errors.append(error.toString());
  });
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: 1000; height: 680; visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "#17191c"
    property int acceptedCount: 0
    property int rejectedCount: 0
    property int menuCount: 0
    function acceptStandardDialog() { dialog.standardButton(Dialog.Ok).click() }
    Theme { id: theme }
    Text {
        x: 28; y: 24; text: "LOOKSY / SHARED POPUP SYSTEM"
        font.family: theme.fontMono; font.pixelSize: 12; color: theme.textFaint
    }
    ActionButton { id: opener; objectName: "popupOpener"; x: 28; y: 64; text: "Open settings" }
    AppPopup {
        id: popover
        focus: true
        objectName: "proofPopover"
        parent: Overlay.overlay
        width: Math.min(popupAvailableWidth, 380)
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        contentItem: ColumnLayout {
            spacing: theme.space12
            Label { text: "Export options"; color: theme.textStrong; font.pixelSize: theme.typeDialogTitle; font.weight: Font.DemiBold }
            Label { Layout.fillWidth: true; text: "Choose a name for this delivery."; color: theme.textMuted; wrapMode: Text.Wrap }
            ToneTextField { id: nameField; objectName: "popupNameField"; Layout.fillWidth: true; text: "Portraits - final"; accessibleLabel: "Delivery name" }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancel"; onClicked: popover.close() }
                ActionButton { text: "Save"; tone: "primary"; onClicked: popover.close() }
            }
        }
        onOpened: nameField.forceActiveFocus(Qt.TabFocusReason)
        onClosed: opener.forceActiveFocus(Qt.TabFocusReason)
    }
    AppDialog {
        id: dialog
        objectName: "proofDialog"
        parent: Overlay.overlay
        title: "Remove selected photos?"
        width: Math.min(popupAvailableWidth, 440)
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label {
            text: "This removes the selection from the Library. Your original files stay on disk."
            color: theme.textMuted; wrapMode: Text.Wrap
        }
        onAccepted: fixture.acceptedCount += 1
        onRejected: fixture.rejectedCount += 1
        onClosed: opener.forceActiveFocus(Qt.TabFocusReason)
    }
    AppDialog {
        id: intrinsicDialog
        objectName: "proofIntrinsicDialog"
        parent: Overlay.overlay
        title: "Rename selection"
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ToneTextField { implicitWidth: 300; text: "Foreground"; accessibleLabel: "Selection name" }
    }
    MenuPopover {
        id: menu
        objectName: "proofMenu"
        parent: opener
        ownerObjectName: "proofMenu"
        menuItems: [ {key: "copy", label: "Copy settings", shortcut: "⌘C"},
                     {key: "paste", label: "Paste settings", enabled: false, disabledReason: "Copy settings first."},
                     {key: "remove", label: "Remove from Library", danger: true} ]
        onItemTriggered: fixture.menuCount += 1
    }
    AppToolTip {
        id: tip
        objectName: "proofTooltip"
        targetItem: opener
        delay: 0; timeout: -1
        text: "Copy the selected photo's settings. Original files stay unchanged."
        shortcutText: "⌘C"
    }
    AppDrawer {
        id: drawer
        objectName: "proofDrawer"
        parent: Overlay.overlay
        edge: Qt.RightEdge
        modal: true
        width: Math.min(300, parent.width - 24); height: parent.height
        padding: theme.panelPaddingStandard
        contentItem: Label { text: "Library filters"; color: theme.textStrong; font.pixelSize: theme.typeDialogTitle }
    }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/popup-system-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) { std::cerr << component.errorString().toStdString(); return fail("Shared popup fixture must load"); }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* opener = window->findChild<QQuickItem*>("popupOpener");
  auto* popover = window->findChild<QObject*>("proofPopover");
  auto* dialog = window->findChild<QObject*>("proofDialog");
  auto* menu = window->findChild<QObject*>("proofMenu");
  auto* intrinsic = window->findChild<QObject*>("proofIntrinsicDialog");
  auto* tooltip = window->findChild<QObject*>("proofTooltip");
  auto* drawer = window->findChild<QObject*>("proofDrawer");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_POPUP_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  const auto capture = [&](const QString& name) {
    return output.isEmpty() || window->grabWindow().save(QDir(output).filePath(name + ".png"));
  };
  settle();
  for (const auto& size : {QSize(1000, 680), QSize(360, 480)}) {
    window->resize(size); settle();
    opener->setPosition(QPointF(28, 64)); opener->forceActiveFocus(Qt::TabFocusReason);
    QMetaObject::invokeMethod(popover, "open"); settle();
    if (!inside(window, item(popover, "contentItem")) ||
        window->activeFocusItem() != window->findChild<QQuickItem*>("popupNameField"))
      return fail("Popover content and initial keyboard focus must fit the viewport");
    if (!capture(QString("popover-%1").arg(size.width()))) return fail("Popover capture failed");
    escape(window);
    if (popover->property("visible").toBool() || window->activeFocusItem() != opener)
      return fail("Popover Escape must dismiss and return focus to the opener");
    QMetaObject::invokeMethod(intrinsic, "open"); settle();
    if (!inside(window, item(intrinsic, "contentItem")) || !inside(window, item(intrinsic, "footer")))
      return fail("Content-sized dialogs must fit without a sizing cycle");
    escape(window);
    QMetaObject::invokeMethod(dialog, "open"); settle();
    if (!inside(window, item(dialog, "header")) || !inside(window, item(dialog, "footer")) ||
        !inside(window, item(dialog, "contentItem"))) return fail("Dialog header, content and actions must fit");
    if (!capture(QString("dialog-%1").arg(size.width()))) return fail("Dialog capture failed");
    escape(window);
    if (dialog->property("visible").toBool()) return fail("Dialog Escape must reject it");
    for (const auto corner : {QPointF(0, 0), QPointF(size.width() - 80, size.height() - 36)}) {
      opener->setPosition(corner);
      QMetaObject::invokeMethod(menu, "open"); settle();
      if (!inside(window, item(menu, "contentItem"))) return fail("Menu must remain inside screen edges");
      if (!capture(QString("menu-%1-%2").arg(size.width()).arg(corner.y() == 0 ? "top" : "bottom"))) return fail("Menu capture failed");
      escape(window);
      QMetaObject::invokeMethod(tooltip, "open"); settle();
      if (!inside(window, item(tooltip, "contentItem"))) return fail("Tooltip must remain inside screen edges");
      if (!capture(QString("tooltip-%1-%2").arg(size.width()).arg(corner.y() == 0 ? "top" : "bottom"))) return fail("Tooltip capture failed");
      QMetaObject::invokeMethod(tooltip, "close"); settle();
    }
    QMetaObject::invokeMethod(drawer, "open"); settle();
    if (!inside(window, item(drawer, "contentItem"))) return fail("Drawer content must remain inside the window");
    if (!capture(QString("drawer-%1").arg(size.width()))) return fail("Drawer capture failed");
    escape(window);
  }
  if (fixture->property("rejectedCount").toInt() != 2) return fail("Dialog Escape must reject exactly once per open");
  QMetaObject::invokeMethod(dialog, "open"); settle();
  if (!QMetaObject::invokeMethod(fixture.get(), "acceptStandardDialog"))
    return fail("The standard dialog accept action must remain wired");
  settle();
  if (fixture->property("acceptedCount").toInt() != 1 || dialog->property("visible").toBool())
    return fail("The dialog action must accept exactly once and close");
  if (!binding_errors.isEmpty()) {
    std::cerr << binding_errors.join('\n').toStdString() << '\n';
    return fail("Shared popup families must not emit binding errors");
  }
  std::cout << "Shared popup, dialog, menu, tooltip and drawer bounds, action routing, Escape and focus checks passed.\n";
}
