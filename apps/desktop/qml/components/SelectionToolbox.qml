pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore

ColumnLayout {
    id: root
    property string activeTool: ""
    property bool objectArmed: false
    property bool objectBoxMode: false
    property bool boxAvailable: false
    property string objectStatus: ""
    property string hoveredTool: ""
    property string customizationNotice: ""
    property alias showTips: preferences.showTips
    readonly property alias modeContentItem: modeSlot
    readonly property var defaultPins: ["object", "subject", "brush-add", "lasso", "rectangle", "ellipse"]
    readonly property var tools: [
        {key: "object", label: qsTr("Object"), icon: "smart-select", group: "automatic", description: qsTr("Click one item, stroke across it, or draw a box around it.")},
        {key: "subject", label: qsTr("Subject"), icon: "subject", group: "automatic", description: qsTr("Automatically select the main foreground subject.")},
        {key: "sky", label: qsTr("Sky"), icon: "cloud", group: "automatic", description: qsTr("Estimate sky using image color and position.")},
        {key: "background", label: qsTr("Background"), icon: "compare", group: "automatic", description: qsTr("Select the background by inverting the detected subject.")},
        {key: "brush-add", label: qsTr("Brush"), icon: "brush-tool", group: "draw", description: qsTr("Paint a selection with adjustable edges and pressure.")},
        {key: "pixel-brush-add", label: qsTr("Pixel brush"), icon: "grid", group: "draw", description: qsTr("Paint or erase exact source pixels for precise selection work.")},
        {key: "lasso", label: qsTr("Lasso"), icon: "lasso", group: "draw", description: qsTr("Draw freely around an area to select it.")},
        {key: "polygon", label: qsTr("Polygon"), icon: "polygon-tool", group: "draw", description: qsTr("Click corner points to outline straight-edged objects.")},
        {key: "rectangle", label: qsTr("Rectangle"), icon: "rectangle", group: "draw", description: qsTr("Drag a rectangular selection on the image.")},
        {key: "ellipse", label: qsTr("Ellipse"), icon: "ellipse", group: "draw", description: qsTr("Drag a round or oval selection on the image.")},
        {key: "linear", label: qsTr("Linear gradient"), icon: "gradient-linear", group: "gradient", description: qsTr("Fade a selection smoothly from one side to the other.")},
        {key: "radial", label: qsTr("Radial gradient"), icon: "gradient-radial", group: "gradient", description: qsTr("Fade a selection around an elliptical center.")},
        {key: "luminance", label: qsTr("Brightness"), icon: "scope", group: "range", description: qsTr("Limit the selection to a range of light or dark pixels.")},
        {key: "color", label: qsTr("Color range"), icon: "swatch", group: "range", description: qsTr("Select pixels that match a chosen color range.")},
        {key: "depth", label: qsTr("Depth range"), icon: "layers", group: "range", description: qsTr("Select near or far areas using an available depth map.")}
    ]
    readonly property var pinnedKeys: {
        try {
            const decoded = JSON.parse(preferences.pinnedTools)
            const valid = Array.isArray(decoded) ? decoded.filter((key, index) =>
                decoded.indexOf(key) === index && root.tools.some(t => t.key === key)).slice(0, 8) : []
            return valid.length >= 2 ? valid : defaultPins
        } catch (_) { return defaultPins }
    }
    readonly property var quickKeys: {
        const keys = pinnedKeys.slice()
        if (activeTool && keys.indexOf(activeTool) < 0 && tools.some(t => t.key === activeTool)) keys.push(activeTool)
        return keys
    }
    signal toolRequested(string key)
    signal objectModeRequested(bool box)
    signal objectCancelRequested()
    Theme { id: theme }
    Settings {
        id: preferences
        category: "SelectionToolbox"
        property string pinnedTools: JSON.stringify(root.defaultPins)
        property bool showTips: true
        property bool moreTools: false
    }
    ButtonGroup {
        id: selectionToolGroup
        exclusive: true
    }
    spacing: theme.space6

    function tool(key) { return tools.find(t => t.key === key) || ({label: qsTr("Select an area"), description: qsTr("Choose a tool, then select on the image.")}) }
    function togglePin(key) {
        const next = pinnedKeys.slice(), index = next.indexOf(key)
        if (index >= 0 && next.length <= 2) { customizationNotice = qsTr("Keep at least two quick tools."); return false }
        if (index < 0 && next.length >= 8) { customizationNotice = qsTr("Choose up to eight quick tools."); return false }
        if (index >= 0) next.splice(index, 1)
        else next.push(key)
        preferences.pinnedTools = JSON.stringify(next)
        customizationNotice = ""
        return true
    }
    function resetCustomization() {
        preferences.pinnedTools = JSON.stringify(defaultPins)
        preferences.showTips = true
        preferences.moreTools = false
        customizationNotice = ""
    }
    function focusQuickTool(key) {
        const index = tools.findIndex(t => t.key === key)
        const item = quickToolRepeater.itemAt(index)
        if (item && item.visible) item.forceActiveFocus(Qt.TabFocusReason)
    }

    component ToolTile: ActionButton {
        id: tile
        required property var toolData
        objectName: visible ? "selectionSource_" + toolData.key : ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: Math.max(theme.scaled(theme.densityCompact ? 32 : 36),
                                 tileContents.implicitHeight + topPadding + bottomPadding)
        ButtonGroup.group: selectionToolGroup
        tone: checked ? "secondary" : "quiet"
        active: checked
        checkable: true
        checked: root.activeTool === toolData.key
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.name: toolData.label
        Accessible.description: toolData.description
        Accessible.role: Accessible.RadioButton
        Accessible.checked: checked
        onClicked: {
            const keyboardSelection = visualFocus
            const key = toolData.key
            root.toolRequested(key)
            if (keyboardSelection) Qt.callLater(() => root.focusQuickTool(key))
        }
        onHoveredChanged: root.hoveredTool = hovered ? toolData.key : ""
        contentItem: RowLayout {
            id: tileContents
            spacing: theme.space6
            AppIcon {
                Layout.preferredWidth: theme.scaled(17)
                Layout.preferredHeight: theme.scaled(17)
                name: tile.toolData.icon
                color: theme.textStrong
            }
            Label {
                objectName: "selectionToolTileLabel"
                Layout.fillWidth: true
                text: tile.toolData.label
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: theme.weightControl
            }
        }
        AppToolTip { targetItem: tile; visible: tile.hovered; text: tile.toolData.description }
    }
    ColumnLayout {
        id: modeSlot
        Layout.fillWidth: true
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: qsTr("Selection tools")
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: theme.weightControl
        }
        MiniButton {
            id: customizeButton
            objectName: "selectionCustomizeButton"
            square: true
            iconName: "settings"
            Accessible.name: qsTr("Customize selection tools")
            tooltip: qsTr("Choose quick tools and show or hide guidance.")
            active: customizePopup.visible
            onClicked: customizePopup.toggleTransient(customizeButton)
        }
    }
    GridLayout {
        Layout.fillWidth: true
        columns: root.width >= theme.scaled(560) ? 4 : root.width >= theme.scaled(420) ? 3 : 2
        rowSpacing: theme.space2
        columnSpacing: theme.space4
        Repeater {
            id: quickToolRepeater
            model: root.tools
            delegate: ToolTile {
                required property var modelData
                toolData: modelData
                visible: root.quickKeys.indexOf(modelData.key) >= 0
            }
        }
    }
    Label {
        objectName: "selectionToolHelp"
        Layout.fillWidth: true
        visible: root.showTips && !root.objectArmed
        text: root.tool(root.hoveredTool || root.activeTool).description
        wrapMode: Text.Wrap
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.objectArmed
        spacing: theme.space4
        RowLayout {
            Layout.fillWidth: true
            MiniButton {
                objectName: "selectionObjectClickMode"
                Layout.fillWidth: true
                text: qsTr("Click / stroke")
                iconName: "select"
                showLeadingIcon: true
                role: "segmented"
                active: !root.objectBoxMode
                tooltip: qsTr("Click one item or stroke through it to preview its boundary.")
                onClicked: root.objectModeRequested(false)
            }
            MiniButton {
                objectName: "selectionObjectBoxMode"
                Layout.fillWidth: true
                text: qsTr("Box")
                iconName: "rectangle"
                showLeadingIcon: true
                role: "segmented"
                active: root.objectBoxMode
                enabled: root.boxAvailable
                disabledReason: qsTr("Enclosing an item requires the local SAM2 model.")
                tooltip: qsTr("Drag a box around the item you want to select.")
                onClicked: root.objectModeRequested(true)
            }
        }
        Label {
            objectName: "selectionWorkflowHint"
            Layout.fillWidth: true
            text: root.objectBoxMode ? qsTr("Enclose one item. Review the outline, then choose Use selection.")
                : qsTr("Click or stroke across one item. Shift adds; Option subtracts.")
            wrapMode: Text.Wrap
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
        Label {
            objectName: "selectionObjectStatus"
            Layout.fillWidth: true
            visible: root.objectBoxMode && root.objectStatus.length > 0
            text: root.objectStatus
            wrapMode: Text.Wrap
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
        MiniButton {
            objectName: "selectionObjectCancelButton"
            text: qsTr("Cancel preview")
            iconName: "close"
            showLeadingIcon: true
            tooltip: qsTr("Discard the object preview and return to selection tools.")
            onClicked: root.objectCancelRequested()
        }
    }
    StudioCollapsibleGroup {
        objectName: "selectionAllTools"
        title: qsTr("More tools")
        expanded: preferences.moreTools
        onToggled: preferences.moreTools = !expanded
        Repeater {
            model: [{key: "automatic", title: qsTr("Automatic")}, {key: "draw", title: qsTr("Draw & shapes")},
                    {key: "gradient", title: qsTr("Gradients")}, {key: "range", title: qsTr("Select by range")}]
            delegate: ColumnLayout {
                id: toolGroup
                required property var modelData
                readonly property var remaining: root.tools.filter(t => t.group === modelData.key && root.quickKeys.indexOf(t.key) < 0)
                Layout.fillWidth: true
                visible: remaining.length > 0
                spacing: theme.space2
                Label {
                    text: toolGroup.modelData.title
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= theme.scaled(560) ? 4 : root.width >= theme.scaled(420) ? 3 : 2
                    columnSpacing: theme.space4
                    rowSpacing: theme.space2
                    Repeater {
                        model: root.tools.filter(t => t.group === toolGroup.modelData.key)
                        delegate: ToolTile {
                            required property var modelData
                            toolData: modelData
                            visible: root.quickKeys.indexOf(modelData.key) < 0
                        }
                    }
                }
            }
        }
    }
    property AppPopup customizationPopup: AppPopup {
        id: customizePopup
        objectName: "selectionCustomizePopup"
        parent: Overlay.overlay
        transientMenu: true
        focus: true
        width: Math.min(popupAvailableWidth, theme.scaled(320))
        x: Math.max(theme.space8, Math.min(parent.width - width - theme.space8, customizeButton.mapToItem(parent, 0, 0).x))
        y: Math.max(theme.space8, Math.min(parent.height - height - theme.space8, customizeButton.mapToItem(parent, 0, customizeButton.height).y))
        contentItem: ScrollView {
            implicitHeight: customization.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: customization
                width: customizePopup.availableWidth
                spacing: theme.space4
                Label {
                    text: qsTr("Your quick tools")
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    font.weight: theme.weightControl
                }
                Label {
                    Layout.fillWidth: true
                    text: root.customizationNotice || qsTr("Choose two to eight favorites. Every tool stays available in More tools.")
                    wrapMode: Text.Wrap
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Repeater {
                    model: root.tools
                    delegate: ToneCheckBox {
                        required property var modelData
                        objectName: "selectionPin_" + modelData.key
                        Layout.fillWidth: true
                        text: modelData.label
                        checked: root.pinnedKeys.indexOf(modelData.key) >= 0
                        onClicked: { root.togglePin(modelData.key); checked = Qt.binding(() => root.pinnedKeys.indexOf(modelData.key) >= 0) }
                        Accessible.description: modelData.description
                    }
                }
                ToneCheckBox {
                    objectName: "selectionTipsToggle"
                    Layout.fillWidth: true
                    text: qsTr("Show tool guidance")
                    checked: root.showTips
                    onClicked: root.showTips = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    MiniButton {
                        objectName: "selectionCustomizeReset"
                        text: qsTr("Reset layout")
                        iconName: "reset"
                        showLeadingIcon: true
                        onClicked: root.resetCustomization()
                    }
                    Item { Layout.fillWidth: true }
                    MiniButton {
                        objectName: "selectionCustomizeDone"
                        text: qsTr("Done")
                        role: "primary"
                        onClicked: customizePopup.closeTransient(true)
                    }
                }
            }
        }
    }
}
