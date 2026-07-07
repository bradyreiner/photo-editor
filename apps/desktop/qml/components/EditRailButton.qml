import QtQuick
import QtQuick.Controls
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
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true"

    implicitWidth: 30
    implicitHeight: 30
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.label

    Keys.onReturnPressed: root.triggered()
    Keys.onEnterPressed: root.triggered()
    Keys.onSpacePressed: root.triggered()

    Rectangle {
        anchors.fill: parent
        radius: Math.round(Math.min(width, height) / 2)
        color: root.active
               ? theme.alpha(root.accent, 0.13)
               : (buttonMouse.containsMouse || root.activeFocus
                  ? theme.alpha(theme.glassLift, 0.32)
                  : "transparent")
        border.width: root.activeFocus ? 1 : 0
        border.color: root.activeFocus ? theme.focusRing : "transparent"
    }

    AppIcon {
        anchors.centerIn: parent
        name: root.iconName
        width: 14
        height: 14
        color: root.active ? root.accent
                           : (buttonMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted)
    }

    MouseArea {
        id: buttonMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.triggered()
    }

    ToolTip.visible: buttonMouse.containsMouse && root.label.length > 0
    ToolTip.delay: 350
    ToolTip.timeout: 2400
    ToolTip.text: root.label
}
