import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    property bool embedded: false
    property string operation: "add"
    property string statusText: ""
    property string brushPageContract: ""
    property string brushDefaultContract: ""
    property string sizePercentText: "6"
    property real flowValue: 1
    property real hardnessValue: 0.8
    readonly property bool inlineTuningVisible: true
    property bool sizeDragActive: false
    property var modeMenuItems: []
    property var tuningMenuItems: []
    property var acceptSizeText: null
    property var commitSizeText: null
    property color accent: theme.accentCool400
    readonly property string hudContract: "specialized-tool-hud:v5|tool:brush|component:BrushToolHud|surface:graphite|mode:add-erase-visible|tuning:size-hardness-flow-horizontal|responsive-wrap:true|advanced:inspector-or-menus|commit-action:true|shallow-corners:true|no-clipping:true"
    readonly property string modularityContract: "brush-tool-hud-component:v3|owns:mode-size-quick-tuning-done|precision:size-percent-field|advanced:inspector-or-shortcuts|signals:mode-tuning-size-done|object-names:preserved|workspace-state:external"

    signal modeClicked
    signal modeMenuItemTriggered(string key)
    signal addRequested
    signal eraseRequested
    signal tuningClicked
    signal tuningMenuItemTriggered(string key)
    signal resetDefaultsRequested
    signal doneRequested
    signal sizeDownRequested
    signal sizeUpRequested
    signal flowDownRequested
    signal flowUpRequested
    signal hardnessDownRequested
    signal hardnessUpRequested
    signal sizeDragPressed(real sceneX)
    signal sizeDragMoved(real sceneX)
    signal sizeDragReleased
    signal sizeTextAccepted(string text)
    signal sizeTextEditingFinished(string text)
    signal flowValueEdited(real value)
    signal hardnessValueEdited(real value)

    Theme {
        id: theme
    }

    readonly property string resolvedStatusText: {
        const parts = String(root.statusText || "").split(/\s{2,}/);
        const useful = [];
        for (let i = 0; i < parts.length; ++i) {
            const part = parts[i].trim();
            if (part.length === 0 || /^(Size|Flow|Hard)\b/i.test(part))
                continue;
            useful.push(part);
        }
        return useful.join("  ");
    }

    implicitWidth: theme.scaled(950)
    implicitHeight: hudSurface.implicitHeight
    height: implicitHeight

    Rectangle {
        id: hudSurface
        objectName: "brushHudSurface"
        anchors.fill: parent
        implicitHeight: controls.implicitHeight + (root.embedded ? 0 : theme.space16)
        radius: theme.radiusPanel
        color: root.embedded ? "transparent" : theme.alpha(theme.surfaceTop, 0.96)
        border.width: root.embedded ? 0 : theme.hairlineWidth
        border.color: theme.alpha(theme.glassEdge, 0.72)

        MouseArea {
            anchors.fill: parent
        }
    }

    Flow {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.embedded ? 0 : theme.space8
        flow: Flow.LeftToRight
        spacing: theme.space6

        GridLayout {
            id: brushCommandControls
            columns: root.embedded ? 3 : 5
            width: root.embedded ? controls.width : implicitWidth
            height: root.embedded ? implicitHeight : controls.width >= brushCommandControls.implicitWidth + brushQuickControls.width + controls.spacing ? brushQuickControls.implicitHeight : brushCommandControls.implicitHeight
            columnSpacing: theme.space2
            rowSpacing: theme.space4

            Components.MiniButton {
                objectName: "brushHudAddButton"
                Layout.alignment: Qt.AlignVCenter
                text: "Add"
                role: "segmented"
                active: root.operation === "add"
                accent: theme.accentCool400
                minimumReadableWidth: theme.scaled(58)
                tooltip: "Paint into the active selection."
                onClicked: root.addRequested()
            }

            Components.MiniButton {
                objectName: "brushHudEraseButton"
                Layout.alignment: Qt.AlignVCenter
                text: "Erase"
                role: "segmented"
                active: root.operation === "subtract"
                accent: theme.error
                minimumReadableWidth: theme.scaled(60)
                tooltip: "Erase from the active selection."
                onClicked: root.eraseRequested()
            }

            Components.MenuButton {
                objectName: "brushHudModeMenuButton"
                Layout.preferredWidth: theme.scaled(104)
                Layout.alignment: Qt.AlignVCenter
                text: "Options"
                iconName: "brush-tool"
                compact: true
                primaryOpensMenu: true
                active: false
                accent: root.accent
                menuWidth: 312
                menuItemMaxLines: 3
                menuItems: root.modeMenuItems
                tooltipText: "Choose brush mode and presets."
                menuTooltipText: "Brush options"
                onClicked: root.modeClicked()
                onMenuItemTriggered: function (key) {
                    root.modeMenuItemTriggered(key);
                }
            }

            Components.MiniButton {
                objectName: "brushHudResetDefaultsButton"
                Layout.alignment: Qt.AlignVCenter
                readonly property string brushDefaultContract: root.brushDefaultContract
                square: true
                iconName: "reset"
                role: "secondary"
                tooltip: "Reset brush size, flow, hardness, density, opacity, spacing, smoothing, and pressure response."
                onClicked: root.resetDefaultsRequested()
            }

            Components.MiniButton {
                objectName: "brushHudDoneButton"
                Layout.alignment: Qt.AlignVCenter
                minimumReadableWidth: theme.scaled(78)
                text: "Done"
                iconName: "check"
                role: "primary"
                onClicked: root.doneRequested()
            }
        }

        GridLayout {
            id: brushQuickControls
            columns: root.embedded ? 1 : 3
            width: root.embedded ? controls.width : theme.scaled(448)
            columnSpacing: theme.space12
            rowSpacing: theme.space8

            ColumnLayout {
                Layout.fillWidth: true
                id: brushSizeControls
                Layout.preferredWidth: theme.scaled(160)
                Layout.minimumWidth: theme.scaled(140)
                spacing: theme.space2

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Size")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    ToneTextField {
                        id: brushHudSizeField
                        objectName: "brushHudSizeField"
                        accessibleLabel: qsTr("Selection brush size percent")
                        Layout.preferredWidth: theme.scaled(48)
                        Layout.preferredHeight: theme.controlHeightDense
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        selectByMouse: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: IntValidator {
                            bottom: 1
                            top: 35
                        }
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeControl
                        font.weight: theme.weightControl
                        leftPadding: theme.space4
                        rightPadding: theme.space4
                        background: Rectangle {
                            radius: theme.radiusControl
                            color: brushHudSizeField.activeFocus ? theme.alpha(theme.accentCool400, 0.10) : theme.fieldFill
                            border.width: brushHudSizeField.activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
                            border.color: brushHudSizeField.activeFocus ? theme.focusRing : theme.controlEdge
                        }
                        onAccepted: {
                            root.sizeTextAccepted(text);
                            if (root.acceptSizeText === null || root.acceptSizeText(text))
                                focus = false;
                            else {
                                text = root.sizePercentText;
                                focus = false;
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus) {
                                root.sizeTextEditingFinished(text);
                                if (root.commitSizeText !== null)
                                    root.commitSizeText(text);
                            }
                        }
                    }
                    Binding {
                        target: brushHudSizeField
                        property: "text"
                        value: root.sizePercentText
                        when: !brushHudSizeField.activeFocus
                    }
                    Label {
                        text: "%"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                    }
                }

                Components.ToolValueSlider {
                    objectName: "brushHudSizeVerticalSlider"
                    Layout.fillWidth: true
                    minimumReadableWidth: theme.scaled(96)
                    label: qsTr("Selection brush size percent")
                    tooltipText: qsTr("Selection brush size: %1%").arg(root.sizePercentText)
                    from: 1
                    to: 35
                    stepSize: 1
                    value: Number(root.sizePercentText)
                    onMoved: {
                        if (root.acceptSizeText !== null)
                            root.acceptSizeText(String(Math.round(value)));
                        else
                            root.sizeTextAccepted(String(Math.round(value)));
                    }
                }
            }

            ColumnLayout {
                id: brushHardnessControls
                Layout.preferredWidth: theme.scaled(120)
                Layout.minimumWidth: theme.scaled(112)
                spacing: theme.space2

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Hardness")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Math.round(root.hardnessValue * 100) + "%"
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                    }
                }

                Components.ToolValueSlider {
                    objectName: "brushHudHardnessVerticalSlider"
                    Layout.fillWidth: true
                    minimumReadableWidth: theme.scaled(96)
                    label: qsTr("Selection brush hardness")
                    tooltipText: qsTr("Hardness: %1%").arg(Math.round(root.hardnessValue * 100))
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: root.hardnessValue
                    onMoved: root.hardnessValueEdited(value)
                }
            }

            ColumnLayout {
                id: brushFlowControls
                Layout.preferredWidth: theme.scaled(120)
                Layout.minimumWidth: theme.scaled(104)
                spacing: theme.space2

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Flow")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Math.round(root.flowValue * 100) + "%"
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                    }
                }

                Components.ToolValueSlider {
                    objectName: "brushHudFlowVerticalSlider"
                    Layout.fillWidth: true
                    minimumReadableWidth: theme.scaled(96)
                    label: qsTr("Selection brush flow")
                    tooltipText: qsTr("Flow: %1%").arg(Math.round(root.flowValue * 100))
                    from: 0.05
                    to: 1
                    stepSize: 0.01
                    value: root.flowValue
                    onMoved: root.flowValueEdited(value)
                }
            }
        }

        Components.MenuButton {
            objectName: "brushHudTuningMenuButton"
            width: theme.scaled(104)
            visible: false
            text: "Tune"
            iconName: "adjust"
            compact: true
            active: false
            accent: root.accent
            menuWidth: 300
            menuItemMaxLines: 3
            menuItems: root.tuningMenuItems
            tooltipText: "Adjust brush size, flow, or hardness."
            menuTooltipText: "Quick brush tuning"
            onClicked: root.tuningClicked()
            onMenuItemTriggered: function (key) {
                root.tuningMenuItemTriggered(key);
            }
        }

        Label {
            objectName: "brushHudStatusLabel"
            width: theme.scaled(132)
            visible: root.resolvedStatusText.length > 0
            text: root.resolvedStatusText
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: theme.typeMicro
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1
            elide: Text.ElideRight
        }
    }
}
