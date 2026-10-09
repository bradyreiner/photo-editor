import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "uiPrimitivesOpticalDetails"
    property bool sampleActive: false
    property bool sampleBusy: true
    property bool tooltipPinned: false
    property int recoveryCount: 0
    property string lastAction: "Try a hover, a press, or keyboard focus."

    Theme { id: theme }
    spacing: theme.space12
    Layout.fillWidth: true

    Text {
        text: "Optical studio · friendly technical"
        color: theme.textStrong
        font.family: theme.fontUi
        font.pixelSize: theme.typeSection
        font.weight: theme.weightSection
    }
    Text {
        Layout.fillWidth: true
        text: "Sharp, compact controls with cool light, a shallow surface finish, and calm, useful feedback. These are the actual shared components; actions stay in this preview."
        color: theme.textMuted
        font.pixelSize: theme.typeBody
        wrapMode: Text.WordWrap
    }
    Flow {
        Layout.fillWidth: true
        spacing: theme.space8
        ActionButton {
            objectName: "uiPrimitivesOpticalSelected"
            text: "Brush"
            iconName: "brush-tool"
            active: root.sampleActive
            compact: true
            onClicked: { root.sampleActive = !root.sampleActive; root.lastAction = root.sampleActive ? "Brush selected" : "Brush deselected" }
        }
        ActionButton {
            objectName: "uiPrimitivesOpticalApply"
            text: "Apply"
            tone: "primary"
            iconName: "check"
            compact: true
            onClicked: root.lastAction = "Applied in this preview"
        }
        ActionButton { text: "Unavailable"; enabled: false; compact: true; tooltipText: "Select a layer first." }
        MenuButton {
            objectName: "uiPrimitivesOpticalMenu"
            text: "Layer actions"
            compact: true
            primaryOpensMenu: true
            menuItems: [
                { key: "duplicate", label: "Duplicate layer", icon: "copy", shortcut: "⌘D" },
                { key: "isolate", label: "Isolate layer", checked: root.sampleActive, shortcut: "⇧I" },
                { key: "flatten", label: "Flatten layers", enabled: false, disabledReason: "Add another layer first." },
                { key: "delete", label: "Delete layer", icon: "trash", danger: true }
            ]
            onMenuItemTriggered: function(key) { root.lastAction = "Layer action · " + key }
        }
        ActionButton {
            id: tooltipButton
            objectName: "uiPrimitivesOpticalTooltipButton"
            text: "Tooltip"
            compact: true
            active: root.tooltipPinned
            onClicked: root.tooltipPinned = !root.tooltipPinned
            AppToolTip {
                objectName: "uiPrimitivesOpticalTooltip"
                targetItem: tooltipButton
                visible: root.tooltipPinned || tooltipButton.hovered
                delay: 0
                text: "Brush · paint a local adjustment"
                shortcutText: "B   /   Hold Space to pan"
                timeout: 0
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.lastAction
        color: theme.textMuted
        font.pixelSize: theme.typeCaption
        wrapMode: Text.WordWrap
    }
    Flow {
        Layout.fillWidth: true
        spacing: theme.space8
        StatusChip { objectName: "uiPrimitivesOpticalBusy"; text: root.sampleBusy ? "Rendering preview" : "Preview ready"; busy: root.sampleBusy; tone: root.sampleBusy ? "info" : "success" }
        ActionButton { objectName: "uiPrimitivesOpticalBusyToggle"; text: root.sampleBusy ? "Pause sample" : "Start sample"; compact: true; onClicked: root.sampleBusy = !root.sampleBusy }
        StatusChip {
            objectName: "uiPrimitivesOpticalWarning"
            text: root.recoveryCount > 0 ? "File found" : "Original offline"
            detailText: "Your edits are kept. Relink the original to continue."
            tone: root.recoveryCount > 0 ? "success" : "warning"
            actionText: root.recoveryCount > 0 ? "" : "Relink"
            onActionTriggered: root.recoveryCount += 1
        }
    }
    ProgressStrip {
        objectName: "uiPrimitivesOpticalProgress"
        Layout.fillWidth: true
        progress: root.sampleBusy ? -1 : 1
        accent: theme.opticalLight
    }
    Text {
        Layout.fillWidth: true
        text: "Light for interaction · blue for selection · coral for the brand · amber for recovery · white for keyboard focus"
        color: theme.textMuted
        font.pixelSize: theme.typeCaption
        wrapMode: Text.WordWrap
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.lineSoft }
}
