import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property string label: ""
    property string iconName: ""
    property color accent: theme.polar
    property bool active: false

    signal triggered()

    Theme { id: theme }

    readonly property string editRailButtonContract: "edit-rail-button:v2|active-state:true|rounded:true|centered-icons:true|hover:true|keyboard:true|tooltip-full:true|accessibility-full:true|icon-only:true"
    readonly property string editRailButtonVisualContract: "edit-rail-button-visual:v2|pressed:true|focus-ring:2px|target:density-aware-compact|selection:neutral-fill-and-accent-icon|motion:reduced-motion-aware"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true"

    implicitWidth: theme.railButtonSizeCompact
    implicitHeight: theme.railButtonSizeCompact
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.label

    Keys.onReturnPressed: root.triggered()
    Keys.onEnterPressed: root.triggered()
    Keys.onSpacePressed: root.triggered()

    Rectangle {
        anchors.fill: parent
        radius: theme.radiusControl
        color: theme.commandFill("secondary", root.active, buttonMouse.containsMouse,
                                 buttonMouse.pressed, root.enabled, true)
        border.width: theme.commandBorderWidth(root.activeFocus)
        border.color: root.activeFocus ? theme.focusRing : theme.controlEdge

        Behavior on color {
            ColorAnimation { duration: theme.reducedMotion ? 0 : theme.durationFast; easing.type: Easing.OutCubic }
        }
    }

    AppIcon {
        anchors.centerIn: parent
        name: root.iconName
        width: theme.scaled(18)
        height: theme.scaled(18)
        color: root.active ? root.accent
                           : (buttonMouse.pressed || buttonMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted)
    }

    MouseArea {
        id: buttonMouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.triggered()
    }

    ToolTip.visible: buttonMouse.containsMouse && root.label.length > 0
    ToolTip.delay: 350
    ToolTip.timeout: 2400
    ToolTip.text: root.label
}
