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

    function revealGroup(group) {
        const flick = groupScroll.contentItem
        if (!flick || !group) return
        const point = group.mapToItem(groupsBody, 0, 0)
        if (point.y < flick.contentY) flick.contentY = point.y
        else if (point.y + group.height > flick.contentY + flick.height)
            flick.contentY = Math.max(0, point.y + group.height - flick.height)
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
    width: Math.min(popupAvailableWidth, theme.scaled(480))
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
        spacing: theme.space8
        BodyLabel {
            text: "Copy Edits"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeDialogTitle
            font.weight: Font.DemiBold
        }
        BodyLabel {
            Layout.fillWidth: true
            text: "From " + root.sourceLabel
            color: theme.textMuted
            wrapMode: Text.NoWrap
            elide: Text.ElideMiddle
        }
        BodyLabel {
            Layout.fillWidth: true
            text: "Paste changes only the groups you select."
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
        ScrollView {
            id: groupScroll
            objectName: "copyPhotoEditsGroupScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: theme.controlHeightDense
            Layout.preferredHeight: groupsBody.implicitHeight
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: groupsBody
                width: groupScroll.availableWidth
                spacing: theme.space4
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
                        description: modelData.detail
                        Accessible.name: modelData.title
                        Accessible.description: modelData.detail
                        Accessible.role: Accessible.CheckBox
                        Accessible.checked: checked
                        onClicked: root.toggleGroup(modelData.key)
                        onActiveFocusChanged: if (activeFocus) Qt.callLater(function() { root.revealGroup(choice) })
                    }
                }
            }
        }
        BodyLabel {
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
