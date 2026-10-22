import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: "Stage"
    property string statusText: "Pending"
    property color statusColor: theme.textMuted

    Theme { id: theme }

    readonly property string agentRunStageRowContract: "agent-run-stage-row:v1|status-dot:true|label-fit:true|status-fit:true|hover:true|keyboard-focus:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|status-fit:true|tooltip-full:true|accessibility-full:true"

    Layout.fillWidth: true
    implicitHeight: 30
    radius: theme.radiusControl
    color: rowHover.hovered || activeFocus
           ? theme.alpha(theme.ink800, 0.34)
           : theme.alpha(theme.ink900, 0.24)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.focusRing
                  : theme.alpha(root.statusColor, root.statusText.toLowerCase() === "pending" ? 0.16 : 0.36)
    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: root.label
    Accessible.description: root.statusText

    HoverHandler {
        id: rowHover
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5

        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: root.statusColor
        }

        Text {
            Layout.fillWidth: true
            text: root.label
            color: theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 9
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            elide: Text.ElideNone
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            text: root.statusText
            color: root.statusColor
            font.family: theme.fontMono
            font.pixelSize: 8
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 6
            elide: Text.ElideNone
            verticalAlignment: Text.AlignVCenter
        }
    }

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (rowHover.hovered)
        delay: 300
        timeout: 3000
        text: root.label + " - " + root.statusText
    }
}
