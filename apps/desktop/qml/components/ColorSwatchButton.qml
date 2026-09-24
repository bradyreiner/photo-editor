import QtQuick
import QtQuick.Controls.Basic

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

    width: 22
    height: 22
    radius: theme.radius6
    color: swatchColor
    border.width: selected || activeFocus || swatchTap.pressed ? 2 : 1
    border.color: selected
                  ? accent
                  : activeFocus
                    ? theme.alpha(theme.focusRing, 0.9)
                    : swatchTap.pressed
                      ? theme.alpha(accent, 0.86)
                    : swatchHover.hovered
                      ? theme.alpha(accent, 0.42)
                      : theme.alpha(theme.lineSoft, 0.36)
    clip: true
    opacity: enabled ? 1.0 : 0.48
    activeFocusOnTab: true
    Accessible.name: label
    Accessible.description: selected ? "Selected color swatch" : "Color swatch"
    Accessible.role: Accessible.Button
    Accessible.checked: selected
    Keys.onReturnPressed: root.picked(root.swatchColor)
    Keys.onEnterPressed: root.picked(root.swatchColor)
    Keys.onSpacePressed: root.picked(root.swatchColor)

    ToolTip.visible: swatchHover.hovered
    ToolTip.delay: 350
    ToolTip.timeout: 3000
    ToolTip.text: label

    HoverHandler {
        id: swatchHover
        cursorShape: Qt.PointingHandCursor
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 3
        radius: Math.max(1, parent.radius - 3)
        color: "transparent"
        border.width: selected ? 1 : 0
        border.color: theme.alpha(theme.ink950, 0.72)
    }

    TapHandler {
        id: swatchTap
        enabled: root.enabled
        onTapped: root.picked(root.swatchColor)
    }
}
