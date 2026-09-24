import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string title: "Tool call"
    property string detail: ""
    property color statusColor: theme.textMuted

    Theme { id: theme }

    readonly property string agentToolCallRowContract: "agent-tool-call-row:v1|status-dot:true|title-wrap:true|detail-wrap:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|tooltip-full:true|accessibility-full:true"

    Layout.fillWidth: true
    implicitHeight: Math.max(34, rowContent.implicitHeight + 10)
    radius: theme.radiusControl
    color: rowHover.hovered || activeFocus
           ? theme.alpha(theme.ink800, 0.36)
           : theme.alpha(theme.ink900, 0.30)
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
        spacing: 7

        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            radius: 4
            color: root.statusColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: root.title
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 9
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
    }

    ToolTip.visible: rowHover.hovered
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: root.detail.length > 0 ? root.title + "\n" + root.detail : root.title
}
