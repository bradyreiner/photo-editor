pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root
    objectName: "editorCloseDialog"
    property var coordinator: null
    readonly property bool failed: coordinator && coordinator.error.length > 0
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
    background: Rectangle { color: theme.ink900; radius: 18; border.color: root.failed ? theme.error : theme.lineSoft }
    contentItem: ColumnLayout {
        spacing: 16
        Label {
            objectName: "editorCloseTitle"
            Layout.fillWidth: true
            text: root.failed ? "Keep this window open" : "Finishing your save"
            color: theme.textStrong
            font.pixelSize: 18
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Label {
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
                objectName: "editorCloseKeepEditing"
                text: "Keep editing"
                onClicked: root.coordinator.keepEditing()
            }
            ActionButton {
                objectName: "editorCloseRetry"
                visible: root.failed
                text: "Retry close"
                onClicked: root.coordinator.requestClose()
            }
        }
    }
}
