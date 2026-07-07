import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string title: "Run"
    property string detail: ""
    property string statusText: "ok"
    property color statusColor: theme.textMuted

    Theme { id: theme }

    readonly property string agentRunHistoryRowContract: "agent-run-history-row:v1|title-wrap:true|detail-wrap:true|status-badge:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|badge-fit:true|tooltip-full:true|accessibility-full:true"

    Layout.fillWidth: true
    implicitHeight: Math.max(36, rowContent.implicitHeight + 10)
    radius: theme.radiusControl
    color: rowHover.hovered || activeFocus
           ? theme.alpha(theme.ink800, 0.34)
           : theme.alpha(theme.ink900, 0.28)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? theme.focusRing : theme.alpha(root.statusColor, 0.34)
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: root.title
    Accessible.description: root.detail

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
                text: root.title
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Text {
                Layout.fillWidth: true
                text: root.detail
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
            color: theme.alpha(root.statusColor, 0.14)
            border.width: 1
            border.color: theme.alpha(root.statusColor, 0.44)
            implicitWidth: Math.max(54, statusLabel.implicitWidth + 14)
            implicitHeight: 24

            Text {
                id: statusLabel
                anchors.centerIn: parent
                text: root.statusText
                color: root.statusColor
                font.family: theme.fontMono
                font.pixelSize: 9
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
            }
        }
    }

    ToolTip.visible: rowHover.hovered
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: root.detail.length > 0 ? root.title + "\n" + root.detail : root.title
}
