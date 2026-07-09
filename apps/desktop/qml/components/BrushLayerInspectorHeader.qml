import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    property string totalLayersText: "1 TOTAL LAYER"
    property bool isolationActive: false
    property string targetIconName: "adjust"
    property string targetName: "Layer"
    property string targetStateText: "Brush target"
    property string maskName: "Active selection"
    property string maskStateText: "Brush / Add / Overlay"
    property string operation: "add"
    property color accent: theme.accentCool400
    property var addMenuItems: []
    readonly property string brushLayerInspectorHeaderContract: "brush-layer-inspector-header:v1|brush-only:true|target-summary:true|mask-summary:true|add-menu:true|isolate-action:true|no-elide:true|functionality:preserved"

    signal isolateRequested()
    signal clearIsolationRequested()
    signal addLayerRequested()
    signal addLayerMenuItemTriggered(string key)

    Theme { id: theme }

    implicitHeight: brushLayerHeaderContent.implicitHeight + 14
    radius: 4
    color: theme.alpha(theme.panelLift, 0.66)
    border.width: theme.chromeBorderWidth
    border.color: theme.alpha(root.accent, 0.34)
    clip: true

    ColumnLayout {
        id: brushLayerHeaderContent
        anchors.fill: parent
        anchors.margins: 7
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                objectName: "brushLayerInspectorCountLabel"
                Layout.fillWidth: true
                text: root.isolationActive ? "ISOLATION ACTIVE" : root.totalLayersText
                color: root.isolationActive ? root.accent : theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.DemiBold
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
            }

            Components.MenuButton {
                objectName: "brushLayerInspectorAddMenuButton"
                Layout.preferredWidth: Math.max(86, implicitWidth)
                text: "Add"
                iconName: "adjust"
                compact: true
                tone: "primary"
                menuWidth: 236
                menuItems: root.addMenuItems
                tooltipText: "Add the default layer."
                menuTooltipText: "Choose layer type"
                onClicked: root.addLayerRequested()
                onMenuItemTriggered: function(key) { root.addLayerMenuItemTriggered(key) }
            }

            Components.MiniButton {
                objectName: "brushLayerInspectorIsolationButton"
                minimumReadableWidth: 76
                text: root.isolationActive ? "Clear" : "Isolate"
                iconName: root.isolationActive ? "close" : "preview"
                role: "segmented"
                active: root.isolationActive
                tooltip: root.isolationActive ? "Show all visible layers again." : "Show only the top layer."
                onClicked: {
                    if (root.isolationActive)
                        root.clearIsolationRequested()
                    else
                        root.isolateRequested()
                }
            }
        }

        Rectangle {
            objectName: "brushLayerInspectorTargetCard"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            radius: 3
            color: theme.alpha(root.accent, 0.085)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(root.accent, 0.42)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 7
                spacing: 7

                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 3
                    color: theme.alpha(root.accent, 0.14)
                    border.width: 1
                    border.color: theme.alpha(root.accent, 0.46)

                    Components.AppIcon {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        name: root.targetIconName
                        color: root.accent
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        objectName: "brushLayerInspectorTargetNameLabel"
                        Layout.fillWidth: true
                        text: root.targetName
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }

                    Label {
                        objectName: "brushLayerInspectorTargetStatusLabel"
                        Layout.fillWidth: true
                        text: root.targetStateText
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 6
                        elide: Text.ElideNone
                    }
                }
            }
        }

        Rectangle {
            objectName: "brushLayerInspectorMaskCard"
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            radius: 3
            color: theme.alpha(root.accent, root.operation === "subtract" ? 0.045 : 0.070)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(root.accent, 0.32)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 7
                spacing: 2

                Label {
                    objectName: "brushLayerInspectorMaskNameLabel"
                    Layout.fillWidth: true
                    text: root.maskName
                    color: root.accent
                    font.family: theme.fontUi
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Label {
                    objectName: "brushLayerInspectorMaskStatusLabel"
                    Layout.fillWidth: true
                    text: root.maskStateText
                    color: theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 8
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 6
                    elide: Text.ElideNone
                }
            }
        }
    }
}
