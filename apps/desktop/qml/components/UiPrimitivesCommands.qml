pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as LooksyComponents

ColumnLayout {
    id: root
    objectName: "uiPrimitivesCommandsPage"

    Theme { id: theme }

    property string selectedFilter: "All"
    property string selectedSwatch: "#3282FF"
    property string selectedTool: "select"
    property bool sampleActionActive: false
    property bool sampleMiniActive: false
    property string lastAction: "No sample command run"

    spacing: theme.gapUnrelated
    implicitWidth: Math.max(titleBlock.implicitWidth, commandGroup.implicitWidth,
                            toolGroup.implicitWidth, splitGroup.implicitWidth,
                            pickerGroup.implicitWidth, guidanceGroup.implicitWidth)
    implicitHeight: childrenRect.height

    function recordAction(action) {
        lastAction = action
    }

    Label {
        id: titleBlock
        Layout.fillWidth: true
        text: "UI primitives · commands"
        color: theme.textStrong
        font.family: theme.fontUi
        font.pixelSize: theme.typeTitle
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
    }

    Label {
        Layout.fillWidth: true
        text: "Live shared controls, arranged by job. Try each state; these examples update this page only."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeBody
        wrapMode: Text.Wrap
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.alpha(theme.lineSoft, 0.72)
    }

    ColumnLayout {
        id: commandGroup
        Layout.fillWidth: true
        spacing: theme.gapDense

        Label {
            text: "Command buttons"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: "Use ActionButton for a named action. Reserve primary for the next clear step; danger is deliberate and labeled."
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.Wrap
        }

        Flow {
            Layout.fillWidth: true
            spacing: theme.gapRelated
            flow: Flow.LeftToRight

            ColumnLayout {
                spacing: theme.space4
                Label { text: "Secondary"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                ActionButton {
                    objectName: "uiPrimitivesActionSecondary"
                    text: "Inspect"
                    iconName: "info"
                    compact: true
                    tooltipText: "Open the sample inspection action."
                    onClicked: root.recordAction("Secondary action · Inspect")
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "Primary · " + theme.controlHeightPrimary + " px"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                ActionButton {
                    objectName: "uiPrimitivesActionPrimary"
                    text: "Apply"
                    iconName: "check"
                    tone: "primary"
                    compact: true
                    tooltipText: "Run the sample primary action."
                    onClicked: root.recordAction("Primary action · Apply")
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "Danger"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                ActionButton {
                    objectName: "uiPrimitivesActionDanger"
                    text: "Remove sample"
                    iconName: "close"
                    tone: "danger"
                    compact: true
                    tooltipText: "Demonstration only; no project data is changed."
                    onClicked: root.recordAction("Danger action · sample removed")
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "Active toggle"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                ActionButton {
                    objectName: "uiPrimitivesActionActive"
                    text: root.sampleActionActive ? "Pinned" : "Pin"
                    iconName: "star"
                    compact: true
                    active: root.sampleActionActive
                    Accessible.checked: root.sampleActionActive
                    tooltipText: "Toggle the local sample state."
                    onClicked: {
                        root.sampleActionActive = !root.sampleActionActive
                        root.recordAction(root.sampleActionActive ? "Active toggle · on" : "Active toggle · off")
                    }
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "Disabled · reason"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                ActionButton {
                    objectName: "uiPrimitivesActionDisabled"
                    text: "Unavailable"
                    iconName: "lock"
                    compact: true
                    enabled: false
                    disabledReason: "Choose a clip before applying this action."
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.alpha(theme.lineSoft, 0.72)
    }

    ColumnLayout {
        id: toolGroup
        Layout.fillWidth: true
        spacing: theme.gapDense

        Label {
            text: "Tools and compact commands"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: "ToolButton communicates the active tool. MiniButton is for compact command groups; icon-only controls keep a full accessible name."
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.Wrap
        }

        Flow {
            Layout.fillWidth: true
            spacing: theme.gapRelated
            flow: Flow.LeftToRight

            ColumnLayout {
                spacing: theme.space4
                Label { text: "ToolButton · select"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                LooksyComponents.ToolButton {
                    objectName: "uiPrimitivesToolSelect"
                    label: "Select tool"
                    iconName: "select"
                    compact: true
                    active: root.selectedTool === "select"
                    Accessible.checked: root.selectedTool === "select"
                    statusText: "Select and move timeline items."
                    onClicked: {
                        root.selectedTool = "select"
                        root.recordAction("Tool selected · Select")
                    }
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "ToolButton · blade"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                LooksyComponents.ToolButton {
                    objectName: "uiPrimitivesToolBlade"
                    label: "Blade tool"
                    iconName: "cut"
                    compact: true
                    active: root.selectedTool === "blade"
                    Accessible.checked: root.selectedTool === "blade"
                    statusText: "Split a clip at the pointer."
                    onClicked: {
                        root.selectedTool = "blade"
                        root.recordAction("Tool selected · Blade")
                    }
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "MiniButton · active secondary"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                MiniButton {
                    objectName: "uiPrimitivesMiniActive"
                    text: root.sampleMiniActive ? "Snap on" : "Snap"
                    iconName: "snap"
                    role: "secondary"
                    active: root.sampleMiniActive
                    Accessible.checked: root.sampleMiniActive
                    a11yName: "Toggle snapping sample"
                    tooltip: "Toggle the local snapping state."
                    onClicked: {
                        root.sampleMiniActive = !root.sampleMiniActive
                        root.recordAction(root.sampleMiniActive ? "Mini command · snapping on" : "Mini command · snapping off")
                    }
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "MiniButton · icon"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                MiniButton {
                    objectName: "uiPrimitivesMiniIcon"
                    text: "Reset view"
                    iconName: "refresh"
                    role: "icon"
                    a11yName: "Reset view sample"
                    tooltip: "Reset the sample view."
                    onClicked: root.recordAction("Mini command · reset view")
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "MiniButton · destructive"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                MiniButton {
                    objectName: "uiPrimitivesMiniDanger"
                    text: "Clear sample"
                    iconName: "close"
                    role: "destructive"
                    a11yName: "Clear sample only"
                    tooltip: "Demonstration only; no project data is changed."
                    onClicked: root.recordAction("Mini command · sample cleared")
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.alpha(theme.lineSoft, 0.72)
    }

    ColumnLayout {
        id: splitGroup
        Layout.fillWidth: true
        spacing: theme.gapDense

        Label {
            text: "Menus and split actions"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: "MenuButton opens choices from the whole control or pairs a primary action with a separate menu. SplitButton keeps its main command and alternate routes together."
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.Wrap
        }

        Flow {
            Layout.fillWidth: true
            spacing: theme.gapRelated
            flow: Flow.LeftToRight

            ColumnLayout {
                spacing: theme.space4
                Label { text: "MenuButton · opens menu"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                MenuButton {
                    objectName: "uiPrimitivesMenuPrimary"
                    text: "Add media"
                    iconName: "plus"
                    tone: "primary"
                    compact: true
                    primaryOpensMenu: true
                    tooltipText: "Choose a sample add-media route."
                    menuWidth: 220
                    menuItems: [
                        {"key": "video", "label": "Add video to timeline", "icon": "image"},
                        {"key": "audio", "label": "Add audio to timeline", "icon": "audio"},
                        {"key": "browse", "label": "Browse library", "icon": "media"}
                    ]
                    onMenuItemTriggered: function(key) {
                        root.recordAction("Menu choice · " + key)
                    }
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "MenuButton · action + More"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                MenuButton {
                    objectName: "uiPrimitivesMenuMore"
                    text: "More"
                    iconName: "menu"
                    compact: true
                    secondaryActionMenu: true
                    accessibleName: "More sample actions"
                    menuTooltipText: "More sample actions"
                    tooltipText: "Run the sample action or open the menu."
                    menuWidth: 220
                    menuItems: [
                        {"key": "duplicate", "label": "Duplicate sample", "icon": "copy"},
                        {"key": "rename", "label": "Rename sample", "icon": "edit"},
                        {"key": "remove", "label": "Remove sample", "icon": "close", "danger": true}
                    ]
                    onClicked: root.recordAction("More primary action")
                    onMenuItemTriggered: function(key) {
                        root.recordAction("More menu choice · " + key)
                    }
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "SplitButton · insert route"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                SplitButton {
                    objectName: "uiPrimitivesSplitInsert"
                    text: "Insert"
                    iconName: "plus"
                    tone: "primary"
                    compact: true
                    menuTooltipText: "Alternate insert routes"
                    tooltipText: "Insert the sample at the playhead."
                    menuWidth: 230
                    menuItems: [
                        {"key": "append", "label": "Append to end", "icon": "step-forward"},
                        {"key": "overwrite", "label": "Overwrite at playhead", "icon": "edit", "danger": true}
                    ]
                    onClicked: root.recordAction("Split primary action · Insert")
                    onMenuItemTriggered: function(key) {
                        root.recordAction("Split route · " + key)
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.alpha(theme.lineSoft, 0.72)
    }

    ColumnLayout {
        id: pickerGroup
        Layout.fillWidth: true
        spacing: theme.gapDense

        Label {
            text: "Selection and values"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: "FilterChip exposes single-choice selection. ColorSwatchButton reports its color value and selected state."
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.Wrap
        }

        Flow {
            Layout.fillWidth: true
            spacing: theme.gapRelated
            flow: Flow.LeftToRight

            ColumnLayout {
                spacing: theme.space4
                Label { text: "FilterChip · single choice"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                RowLayout {
                    spacing: theme.space4
                    FilterChip {
                        objectName: "uiPrimitivesFilterAll"
                        text: "All"
                        active: root.selectedFilter === "All"
                        onClicked: {
                            root.selectedFilter = "All"
                            root.recordAction("Filter selected · All")
                        }
                    }
                    FilterChip {
                        objectName: "uiPrimitivesFilterVideo"
                        text: "Video"
                        active: root.selectedFilter === "Video"
                        onClicked: {
                            root.selectedFilter = "Video"
                            root.recordAction("Filter selected · Video")
                        }
                    }
                    FilterChip {
                        objectName: "uiPrimitivesFilterAudio"
                        text: "Audio"
                        active: root.selectedFilter === "Audio"
                        onClicked: {
                            root.selectedFilter = "Audio"
                            root.recordAction("Filter selected · Audio")
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: theme.space4
                Label { text: "ColorSwatchButton · value + selection"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
                RowLayout {
                    spacing: theme.space4
                    Repeater {
                        model: [
                            {"value": "#3282FF", "label": "Blue"},
                            {"value": "#30D158", "label": "Green"},
                            {"value": "#F6C945", "label": "Yellow"},
                            {"value": "#FF3B30", "label": "Red"}
                        ]
                        delegate: ColorSwatchButton {
                            required property var modelData
                            objectName: "uiPrimitivesColorSwatch" + String(modelData.label)
                            swatchColor: modelData.value
                            label: modelData.label + " · " + modelData.value
                            selected: root.selectedSwatch === modelData.value
                            onPicked: function(value) {
                                root.selectedSwatch = value
                                root.recordAction("Color selected · " + modelData.label + " " + value)
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.alpha(theme.lineSoft, 0.72)
    }

    ColumnLayout {
        id: guidanceGroup
        Layout.fillWidth: true
        spacing: theme.gapDense

        Label {
            text: "Planning notes"
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeSection
            font.weight: Font.DemiBold
        }

        Label {
            objectName: "uiPrimitivesCommandsUsageGuidance"
            Layout.fillWidth: true
            text: "Use one primary command per context. Keep destructive routes in an explicit menu choice. Use ToolButton for persistent tool state, and FilterChip only for a small, single-choice set."
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "uiPrimitivesCommandsOpenIssues"
            Layout.fillWidth: true
            text: "Primary actions are " + theme.controlHeightPrimary + " px at this density. Compact tools and swatches are " + theme.iconButtonSizeCompact + " px. Keep labels readable and distinguish selection from keyboard focus."
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.Wrap
        }

        StatusChip {
            objectName: "uiPrimitivesCommandsLastAction"
            Layout.alignment: Qt.AlignLeft
            text: root.lastAction
            tone: "neutral"
            compact: true
        }
    }
}
