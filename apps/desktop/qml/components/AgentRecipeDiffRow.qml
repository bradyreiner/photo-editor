import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: "Recipe group"
    property string summary: ""
    property string countText: "0"
    property color countColor: theme.accentCool500

    Theme { id: theme }

    readonly property string agentRecipeDiffRowContract: "agent-recipe-diff-row:v1|label-wrap:true|summary-wrap:true|count-badge:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-wrap:true|summary-wrap:true|badge-fit:true|tooltip-full:true|accessibility-full:true"

    Layout.fillWidth: true
    implicitHeight: Math.max(38, rowContent.implicitHeight + 10)
    radius: theme.radiusControl
    color: rowHover.hovered || activeFocus
           ? theme.alpha(theme.ink800, 0.34)
           : theme.alpha(theme.ink900, 0.28)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? theme.focusRing : theme.alpha(theme.lineSoft, 0.18)
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: root.label
    Accessible.description: root.summary

    HoverHandler {
        id: rowHover
    }

    RowLayout {
        id: rowContent
        anchors.fill: parent
        anchors.margins: 5
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: root.label
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Text {
                Layout.fillWidth: true
                text: root.summary
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 9
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        Rectangle {
            radius: theme.radiusControl
            color: theme.alpha(root.countColor, 0.12)
            border.width: 1
            border.color: theme.alpha(root.countColor, 0.34)
            implicitWidth: Math.max(54, countLabel.implicitWidth + 14)
            implicitHeight: 24

            Text {
                id: countLabel
                anchors.centerIn: parent
                text: root.countText
                color: root.countColor
                font.family: theme.fontMono
                font.pixelSize: 9
                horizontalAlignment: Text.AlignHCenter
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
            }
        }
    }

    ToolTip.visible: rowHover.hovered
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: root.summary.length > 0 ? root.label + "\n" + root.summary : root.label
}
