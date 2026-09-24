import QtQuick
import QtQuick.Controls.Basic

Control {
    id: root

    property string mode: "idle"
    property string statusText: ""
    property bool active: false
    property bool docked: false
    property color accent: theme.primaryAccent
    property bool buttonHovered: false
    signal activated()

    Theme { id: theme }

    readonly property string resolvedStatusText: statusText.length > 0 ? statusText : "Open Looksy Agent Studio"
    readonly property string residentAgentContract:
        "looksy-agent-entry:v4|placement:left-rail|icon:terminal-rgb|character:agent-workspace-only|activation:full-screen-agent-studio"

    implicitWidth: 36
    implicitHeight: 36
    padding: 0
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: "Open Looksy Agent Studio"
    Accessible.description: resolvedStatusText
    Accessible.onPressAction: root.activated()
    ToolTip.visible: root.buttonHovered
    ToolTip.delay: 350
    ToolTip.timeout: 3200
    ToolTip.text: "Looksy Agent · " + root.resolvedStatusText

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            root.activated()
            event.accepted = true
        }
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: root.active ? theme.editorSelectionFill
             : root.buttonHovered ? theme.ink800 : "transparent"
        border.width: root.active || root.buttonHovered ? 1 : 0
        border.color: root.active ? root.accent : theme.controlEdge
        Behavior on color { ColorAnimation { duration: theme.durationFast } }
    }

    contentItem: Item {
        AppIcon {
            anchors.centerIn: parent
            width: 18
            height: 18
            name: "agent-terminal"
            color: root.buttonHovered || root.active ? theme.brandPaper : theme.textMain
        }

        LooksyMark {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 3
            anchors.bottomMargin: 3
            width: 9
            height: 9
            framed: false
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
            onHoveredChanged: root.buttonHovered = hovered
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: {
                root.forceActiveFocus()
                root.activated()
            }
        }
    }
}
