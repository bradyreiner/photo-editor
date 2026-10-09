pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    objectName: "editorCloseDialog"
    property var coordinator: null
    readonly property bool failed: coordinator && coordinator.error.length > 0
    property Item focusReturnItem: null
    property Item focusFallbackItem: null
    function focusForKeyboard(item) {
        if (!item || !item.visible || !item.enabled)
            return
        if (item.activeFocus && item["visualFocus"] === false)
            item.focus = false
        item.forceActiveFocus(Qt.TabFocusReason)
    }
    visible: coordinator !== null && (coordinator.closing || failed)
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: Math.min(480, parent ? parent.width - 32 : 480)
    height: contentItem.implicitHeight + 40
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(16, (parent.height - height) / 2) : 0
    padding: 20
    Theme { id: theme }
    onAboutToShow: {
        const popupWindow = root.parent ? root.parent.Window.window : null
        if (!root.focusReturnItem && popupWindow)
            root.focusReturnItem = popupWindow.activeFocusItem
        if (!root.focusFallbackItem && popupWindow)
            root.focusFallbackItem = popupWindow.contentItem
    }
    onOpened: focusForKeyboard(keepEditingButton)
    onClosed: focusRestoreTimer.restart()
    Timer {
        id: focusRestoreTimer
        interval: 1
        repeat: false
        onTriggered: {
            if (root.focusReturnItem && root.focusReturnItem.visible && root.focusReturnItem.enabled) {
                root.focusForKeyboard(root.focusReturnItem)
            } else if (root.focusFallbackItem && root.focusFallbackItem.visible && root.focusFallbackItem.enabled) {
                root.focusForKeyboard(root.focusFallbackItem)
            }
            root.focusReturnItem = null
        }
    }
    background: Rectangle {
        color: theme.ink900
        radius: theme.radiusPanel
        border.color: root.failed ? theme.error : theme.lineSoft
        Accessible.name: titleLabel.text
        Accessible.description: detailLabel.text
        Accessible.role: Accessible.Dialog
        Accessible.ignored: false
    }
    contentItem: ColumnLayout {
        spacing: 16
        Label {
            id: titleLabel
            objectName: "editorCloseTitle"
            Layout.fillWidth: true
            text: root.failed ? "Keep this window open" : "Finishing your save"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeDialogTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Label {
            id: detailLabel
            objectName: "editorCloseDetail"
            Layout.fillWidth: true
            text: root.failed ? root.coordinator.error
                              : "Your edits are checkpointed locally. Waiting briefly for the catalog save to finish."
            color: theme.textMuted
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton {
                id: keepEditingButton
                objectName: "editorCloseKeepEditing"
                text: "Keep editing"
                onClicked: root.coordinator.keepEditing()
            }
            ActionButton {
                objectName: "editorCloseRetry"
                visible: root.failed
                text: "Retry close"
                iconName: "retry"
                onClicked: root.coordinator.requestClose()
            }
        }
    }
}
