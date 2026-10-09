import QtQuick

Rectangle {
    id: root

    property string swatchColor: "#ffffff"
    property bool selected: false
    property color accent: theme.accentWarm400
    property string label: swatchColor
    readonly property string colorSwatchButtonContract: "color-swatch-button:v1|selected:true|keyboard:true|hover:true|tooltip-full:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true|label-source:tooltip"
    readonly property string interactionStateContract: theme.interactionStateContract

    signal picked(string swatchColor)

    Theme { id: theme }

    implicitWidth: theme.iconButtonSizeCompact
    implicitHeight: implicitWidth
    width: implicitWidth
    height: implicitHeight
    radius: theme.radiusControl
    color: "transparent"
    border.width: activeFocus ? theme.focusBorderWidth : 0
    border.color: theme.focusRing
    opacity: enabled ? 1.0 : 0.48
    activeFocusOnTab: true
    Accessible.name: label
    Accessible.description: selected ? "Selected color swatch" : "Color swatch"
    Accessible.role: Accessible.Button
    Accessible.checked: selected
    Accessible.onPressAction: root.activate()

    function activate() {
        if (root.enabled)
            root.picked(root.swatchColor)
    }

    Keys.onReturnPressed: root.activate()
    Keys.onEnterPressed: root.activate()
    Keys.onSpacePressed: root.activate()

    AppToolTip {
        targetItem: root
        visible: swatchHover.hovered
        text: root.label
    }

    HoverHandler {
        id: swatchHover
        cursorShape: Qt.PointingHandCursor
    }

    Rectangle {
        id: swatchFace
        anchors.centerIn: parent
        width: Math.min(theme.scaled(20), Math.max(0, root.width - theme.space8))
        height: width
        radius: theme.radiusControl
        color: !root.enabled ? theme.alpha(root.swatchColor, 0.76)
               : swatchTap.pressed ? theme.alpha(root.swatchColor, 0.86)
               : root.swatchColor
        border.width: root.selected ? theme.scaled(2)
                      : swatchHover.hovered ? theme.hairlineWidth : theme.hairlineWidth
        border.color: root.selected ? root.accent
                     : swatchHover.hovered ? theme.controlEdge
                     : theme.alpha(theme.ink950, 0.68)

        Rectangle {
            anchors.fill: parent
            anchors.margins: theme.space3
            radius: Math.max(0, parent.radius - theme.scaled(2))
            color: "transparent"
            border.width: root.selected ? theme.hairlineWidth : 0
            border.color: theme.alpha(theme.ink950, 0.72)
        }
    }

    Behavior on border.color {
        ColorAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    TapHandler {
        id: swatchTap
        enabled: root.enabled
        onTapped: root.activate()
    }
}
