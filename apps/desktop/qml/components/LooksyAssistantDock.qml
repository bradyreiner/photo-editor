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
        "looksy-agent-entry:v5|placement:left-rail|icon:optical-aperture|character:agent-view-only|activation:full-screen-agent-studio"

    implicitWidth: 36
    implicitHeight: 36
    padding: 0
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: "Open Looksy Agent Studio"
    Accessible.description: resolvedStatusText
    Accessible.onPressAction: root.activated()
    AppToolTip {
        targetItem: root
        visible: root.buttonHovered
        text: "Looksy Agent · " + root.resolvedStatusText
    }

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
            width: 20
            height: 20
            name: "agent-terminal"
            color: root.buttonHovered || root.active ? theme.brandPaper : theme.textMain
            Accessible.ignored: true
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
