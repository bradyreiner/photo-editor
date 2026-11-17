import QtQuick

Rectangle {
    id: root
    property var inputControl: null
    property bool focused: Boolean(inputControl && inputControl.activeFocus)
    property bool hovered: Boolean(inputControl && inputControl.hovered)
    property bool interactiveEnabled: !inputControl || inputControl.enabled !== false
    property bool inlineField: false
    readonly property string inputFieldSurfaceContract: "input-field-surface:v1|shared|neutral|shallow|border:focus-or-high-contrast|hover:fill"
    Theme { id: theme }
    radius: theme.radiusControl
    color: !root.interactiveEnabled ? (root.inlineField ? "transparent" : theme.alpha(theme.fieldFill, 0.45))
           : root.focused || root.hovered ? theme.fieldHoverFill
           : root.inlineField ? "transparent" : theme.fieldFill
    border.width: theme.commandBorderWidth(root.focused)
    border.color: root.focused ? theme.focusRing : theme.controlEdge
}
