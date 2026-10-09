import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    property string totalLayersText: "1 layer"
    property bool isolationActive: false
    property bool canIsolate: true
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

    implicitHeight: brushLayerHeaderContent.implicitHeight + 16
    radius: 3
    color: theme.alpha(theme.surfaceTop, 0.66)
    border.width: theme.chromeBorderWidth
    border.color: theme.alpha(theme.controlEdge, 0.45)
    clip: true

    ColumnLayout {
        id: brushLayerHeaderContent
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                objectName: "brushLayerInspectorCountLabel"
                Layout.fillWidth: true
                text: root.isolationActive ? "Isolation active" : root.totalLayersText
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
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
                enabled: root.canIsolate || root.isolationActive
                text: root.isolationActive ? "Clear" : "Isolate"
                iconName: root.isolationActive ? "close" : "preview"
                role: "segmented"
                active: root.isolationActive
                tooltip: root.isolationActive ? "Show all visible layers again."
                                              : root.canIsolate ? "Show only the selected layer."
                                                                : "Choose a layer to isolate."
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
            Layout.preferredHeight: Math.max(48, targetSummary.implicitHeight + 16)
            radius: 3
            color: theme.alpha(theme.actionSecondaryFill, 0.45)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(theme.controlEdge, 0.45)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Components.AppIcon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    name: root.targetIconName
                    color: theme.textMuted
                }

                ColumnLayout {
                    id: targetSummary
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        objectName: "brushLayerInspectorTargetNameLabel"
                        Layout.fillWidth: true
                        text: root.targetName
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
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
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                }
            }
        }

        Rectangle {
            objectName: "brushLayerInspectorMaskCard"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(48, maskSummary.implicitHeight + 16)
            radius: 3
            color: theme.alpha(theme.actionSecondaryFill, 0.45)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(theme.controlEdge, 0.45)

            ColumnLayout {
                id: maskSummary
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2

                Label {
                    objectName: "brushLayerInspectorMaskNameLabel"
                    Layout.fillWidth: true
                    text: root.maskName
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Label {
                    objectName: "brushLayerInspectorMaskStatusLabel"
                    Layout.fillWidth: true
                    text: root.maskStateText
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }
        }
    }
}
