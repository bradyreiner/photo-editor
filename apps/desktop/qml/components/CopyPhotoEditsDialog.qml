pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    objectName: "copyPhotoEditsDialog"
    property string sourceLabel: ""
    property string sourcePatch: ""
    property var selectedGroups: ["tone", "color", "detail"]
    property string errorText: ""
    property Item focusReturnItem: null
    property Item focusFallbackItem: null
    signal copyRequested(string patch, var groups)
    readonly property var choices: [
        { key: "tone", title: "Light", detail: "Exposure, contrast, levels and curves" },
        { key: "color", title: "Color", detail: "White balance, mixer and color grading" },
        { key: "detail", title: "Detail", detail: "Texture, sharpening, noise and lens corrections" },
        { key: "crop", title: "Crop & Geometry", detail: "Crop, rotation and perspective" },
        { key: "raw", title: "RAW", detail: "RAW processing and calibration" },
        { key: "masks", title: "Masks & Layers", detail: "Replace masks and layers, including local adjustments" }
    ]

    onAboutToShow: {
        const popupWindow = root.parent ? root.parent.Window.window : null
        if (!root.focusReturnItem && popupWindow)
            root.focusReturnItem = popupWindow.activeFocusItem
    }

    onOpened: {
        const firstGroup = groupChoices.itemAt(0)
        if (firstGroup)
            firstGroup.forceActiveFocus(Qt.TabFocusReason)
    }

    onClosed: focusRestoreTimer.restart()

    Timer {
        id: focusRestoreTimer
        interval: 1
        repeat: false
        onTriggered: {
            if (root.focusReturnItem && root.focusReturnItem.visible && root.focusReturnItem.enabled) {
                root.focusReturnItem.forceActiveFocus(Qt.TabFocusReason)
            } else if (root.focusFallbackItem && root.focusFallbackItem.visible && root.focusFallbackItem.enabled) {
                root.focusFallbackItem.forceActiveFocus(Qt.TabFocusReason)
            }
            root.focusReturnItem = null
        }
    }

    function toggleGroup(key) {
        const groups = selectedGroups.slice()
        const index = groups.indexOf(key)
        if (index < 0) groups.push(key)
        else groups.splice(index, 1)
        selectedGroups = groups
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(480, parent ? parent.width - 32 : 480)))
    height: Math.min(popupAvailableHeight, Math.max(1, Math.min(contentItem.implicitHeight + padding * 2, parent ? parent.height - 32 : 660)))
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(16, (parent.height - height) / 2) : 0
    padding: standardPopupPadding
    Theme { id: theme }
    background: PopupSurface {
        Accessible.name: qsTr("Copy Edits")
        Accessible.description: qsTr("Choose the edit groups to copy to selected photos.")
        Accessible.role: Accessible.Dialog
        Accessible.ignored: false
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: "Copy Edits"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeDialogTitle
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            text: "From " + root.sourceLabel
            color: theme.textMuted
            elide: Text.ElideMiddle
        }
        Label {
            Layout.fillWidth: true
            text: "Choose what to copy. Paste onto one or more selected photos; other edit groups stay as they are."
            color: theme.textMuted
            wrapMode: Text.WordWrap
        }
        RowLayout {
            ActionButton {
                text: "Select all"
                compact: true
                onClicked: root.selectedGroups = root.choices.map(choice => choice.key)
            }
            ActionButton {
                objectName: "copyPhotoEditsClear"
                text: "Clear"
                compact: true
                onClicked: root.selectedGroups = []
            }
        }
        Repeater {
            id: groupChoices
            model: root.choices
            ToneCheckBox {
                id: choice
                required property var modelData
                objectName: "copyPhotoEditsGroup_" + modelData.key
                Layout.fillWidth: true
                checked: root.selectedGroups.indexOf(modelData.key) >= 0
                text: modelData.title
                Accessible.name: modelData.title
                Accessible.description: modelData.detail
                Accessible.role: Accessible.CheckBox
                Accessible.checked: checked
                onClicked: root.toggleGroup(modelData.key)
                padding: 6
                spacing: 12
                implicitHeight: Math.max(theme.controlHeightDense, groupCaption.implicitHeight + topPadding + bottomPadding)
                contentItem: Column {
                    id: groupCaption
                    leftPadding: 0
                    spacing: 3
                    Label { text: choice.modelData.title; color: theme.textStrong; font.weight: Font.Medium }
                    Label {
                        width: parent.width - parent.leftPadding
                        text: choice.modelData.detail
                        color: theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        Label {
            visible: root.errorText.length > 0
            Layout.fillWidth: true
            text: root.errorText
            color: theme.error
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { text: "Cancel"; onClicked: root.close() }
            ActionButton {
                objectName: "copyPhotoEditsConfirm"
                text: "Copy Edits"
                tone: "primary"
                enabled: root.selectedGroups.length > 0
                disabledReason: "Choose at least one edit group."
                onClicked: root.copyRequested(root.sourcePatch, root.selectedGroups)
            }
        }
    }
}
