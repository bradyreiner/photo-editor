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
    Accessible.onPressAction: if (root.enabled) root.triggered()

    Keys.onReturnPressed: if (root.enabled) root.triggered()
    Keys.onEnterPressed: if (root.enabled) root.triggered()
    Keys.onSpacePressed: if (root.enabled) root.triggered()

    property Item background: CommandButtonSurface {
        id: railSurface
        parent: root
        anchors.fill: parent
        hovered: buttonMouse.containsMouse
        pressed: buttonMouse.pressed
        selected: root.active
        focused: root.activeFocus
        interactiveEnabled: root.enabled
        fillColor: theme.commandFill("secondary", root.active, buttonMouse.containsMouse, buttonMouse.pressed, root.enabled, true)
    }

    AppIcon {
        objectName: "sharedCommandIcon"
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

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (buttonMouse.containsMouse && root.label.length > 0)
        delay: 350
        timeout: 2400
        text: root.label
    }
}
