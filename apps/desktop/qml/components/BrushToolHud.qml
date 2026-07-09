import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    property string operation: "add"
    property string statusText: ""
    property string brushPageContract: ""
    property string brushDefaultContract: ""
    property string sizePercentText: "6"
    property bool sizeDragActive: false
    property var modeMenuItems: []
    property var tuningMenuItems: []
    property var acceptSizeText: null
    property var commitSizeText: null
    property color accent: theme.accentCool400
    readonly property string hudContract: "specialized-tool-hud:v4|tool:brush|component:BrushToolHud|mode-switch:menu|quick-tuning:size-flow-hardness|advanced:inspector-or-shortcuts|status-visible:true|commit-action:true|subdued-chrome:true|no-elide:true|no-overlap:true"
    readonly property string modularityContract: "brush-tool-hud-component:v2|owns:mode-size-quick-tuning-done|hidden-precision:diagnostic-only|signals:mode-tuning-size-done|object-names:preserved|workspace-state:external"

    signal modeClicked()
    signal modeMenuItemTriggered(string key)
    signal addRequested()
    signal eraseRequested()
    signal tuningClicked()
    signal tuningMenuItemTriggered(string key)
    signal resetDefaultsRequested()
    signal doneRequested()
    signal sizeDownRequested()
    signal sizeUpRequested()
    signal flowDownRequested()
    signal flowUpRequested()
    signal hardnessDownRequested()
    signal hardnessUpRequested()
    signal sizeDragPressed(real sceneX)
    signal sizeDragMoved(real sceneX)
    signal sizeDragReleased()
    signal sizeTextAccepted(string text)
    signal sizeTextEditingFinished(string text)

    Theme { id: theme }

    implicitWidth: brushToolControls.implicitWidth + 18
    implicitHeight: 44
    radius: 3
    color: theme.alpha(theme.ink950, 0.84)
    border.width: theme.chromeBorderWidth
    border.color: root.accent
    clip: true

    ColumnLayout {
        id: brushToolControls
        anchors.centerIn: parent
        spacing: 6

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 6

            Label {
                text: "Brush"
                color: root.accent
                font.family: theme.fontUi
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 0
            }

            Components.MenuButton {
                objectName: "brushHudModeMenuButton"
                Layout.preferredWidth: Math.max(136, implicitWidth)
                text: root.operation === "subtract" ? "Brush Erase" : "Brush Add"
                iconName: root.operation === "subtract" ? "brush-subtract" : "brush-tool"
                compact: true
                allowLabelWrap: true
                active: true
                accent: root.accent
                menuWidth: 312
                menuItemMaxLines: 3
                menuItems: root.modeMenuItems
                tooltipText: "Choose brush mode and presets."
                menuTooltipText: "Brush options"
                onClicked: root.modeClicked()
                onMenuItemTriggered: function(key) { root.modeMenuItemTriggered(key) }
            }

            Components.MiniButton {
                objectName: "brushHudAddButton"
                visible: false
                text: "Add"
                iconName: "brush-tool"
                role: "segmented"
                active: root.operation === "add"
                accent: theme.accentCool400
                tooltip: "Paint into the active selection."
                onClicked: root.addRequested()
            }

            Components.MiniButton {
                objectName: "brushHudEraseButton"
                visible: false
                text: "Erase"
                iconName: "brush-subtract"
                role: "segmented"
                active: root.operation === "subtract"
                accent: "#d98962"
                tooltip: "Erase from the active selection."
                onClicked: root.eraseRequested()
            }

            Label {
                objectName: "brushHudStatusLabel"
                Layout.preferredWidth: 260
                text: root.statusText
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 10
                font.weight: Font.DemiBold
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 8
                elide: Text.ElideNone
            }

            Components.MenuButton {
                objectName: "brushHudTuningMenuButton"
                Layout.preferredWidth: 112
                text: "Tuning"
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
                onMenuItemTriggered: function(key) { root.tuningMenuItemTriggered(key) }
            }

            Components.MiniButton {
                objectName: "brushHudResetDefaultsButton"
                visible: false
                readonly property string brushDefaultContract: root.brushDefaultContract
                square: true
                iconName: "reset"
                role: "secondary"
                tooltip: "Reset brush size, flow, hardness, density, opacity, spacing, smoothing, and pressure response."
                onClicked: root.resetDefaultsRequested()
            }

            Components.MiniButton {
                objectName: "brushHudDoneButton"
                minimumReadableWidth: 78
                text: "Done"
                iconName: "check"
                role: "primary"
                onClicked: root.doneRequested()
            }
        }

        RowLayout {
            visible: false
            Layout.alignment: Qt.AlignHCenter
            spacing: 6

            Components.MiniButton {
                objectName: "brushHudSizeDownButton"
                square: true
                iconName: "minus"
                tooltip: "Decrease brush size."
                onClicked: root.sizeDownRequested()
            }

            Rectangle {
                id: brushHudSizeDragHandle
                objectName: "brushHudSizeDragHandle"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 24
                radius: 3
                color: root.sizeDragActive ? theme.alpha(theme.accentCool400, 0.20) : theme.alpha(theme.ink950, 0.72)
                border.width: 1
                border.color: root.sizeDragActive ? theme.alpha(theme.accentCool400, 0.72) : theme.alpha(theme.glassEdge, 0.24)

                Label {
                    anchors.centerIn: parent
                    text: "Size " + root.sizePercentText + "%"
                    color: theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0
                }

                MouseArea {
                    objectName: "brushHudSizeDragArea"
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    onPressed: function(mouse) {
                        const mapped = brushHudSizeDragHandle.mapToItem(root, mouse.x, mouse.y)
                        root.sizeDragPressed(mapped.x)
                    }
                    onPositionChanged: function(mouse) {
                        const mapped = brushHudSizeDragHandle.mapToItem(root, mouse.x, mouse.y)
                        root.sizeDragMoved(mapped.x)
                    }
                    onReleased: root.sizeDragReleased()
                    onCanceled: root.sizeDragReleased()
                }
            }

            TextField {
                id: brushHudSizeField
                objectName: "brushHudSizeField"
                Layout.preferredWidth: 48
                Layout.preferredHeight: 24
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: root.sizePercentText
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 0
                leftPadding: 6
                rightPadding: 6
                background: Rectangle {
                    radius: 3
                    color: brushHudSizeField.activeFocus ? theme.alpha(theme.accentCool400, 0.10) : theme.alpha(theme.ink950, 0.72)
                    border.width: 1
                    border.color: brushHudSizeField.activeFocus ? theme.alpha(theme.accentCool400, 0.76) : theme.alpha(theme.glassEdge, 0.24)
                }
                onAccepted: {
                    root.sizeTextAccepted(text)
                    if (root.acceptSizeText === null || root.acceptSizeText(text))
                        focus = false
                    else
                        selectAll()
                }
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        root.sizeTextEditingFinished(text)
                        if (root.commitSizeText !== null)
                            root.commitSizeText(text)
                    }
                }
            }

            Components.MiniButton {
                objectName: "brushHudSizeUpButton"
                square: true
                iconName: "plus"
                tooltip: "Increase brush size."
                onClicked: root.sizeUpRequested()
            }

            Components.MiniButton {
                objectName: "brushHudFlowDownButton"
                visible: false
                text: "Flow -"
                role: "segmented"
                tooltip: "Lower brush flow."
                onClicked: root.flowDownRequested()
            }

            Components.MiniButton {
                objectName: "brushHudFlowUpButton"
                visible: false
                text: "Flow +"
                role: "segmented"
                tooltip: "Raise brush flow."
                onClicked: root.flowUpRequested()
            }

            Components.MiniButton {
                objectName: "brushHudHardnessDownButton"
                visible: false
                text: "Soft"
                role: "segmented"
                tooltip: "Soften the brush edge."
                onClicked: root.hardnessDownRequested()
            }

            Components.MiniButton {
                objectName: "brushHudHardnessUpButton"
                visible: false
                text: "Hard"
                role: "segmented"
                tooltip: "Harden the brush edge."
                onClicked: root.hardnessUpRequested()
            }
        }
    }
}
