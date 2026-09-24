import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: root

    property color accent: theme.accentWarm400

    Theme { id: theme }

    implicitHeight: theme.controlHeightTall
    color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)
    placeholderTextColor: theme.textFaint
    selectedTextColor: theme.ink950
    selectionColor: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.85)
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    leftPadding: theme.space12
    rightPadding: theme.space12
    activeFocusOnTab: true
    hoverEnabled: true

    background: Rectangle {
        radius: theme.radiusControl
        color: !root.enabled ? theme.alpha(theme.fieldFill, 0.45)
               : root.hovered ? theme.fieldHoverFill : theme.fieldFill
        border.width: root.activeFocus ? theme.focusBorderWidth : 0
        border.color: theme.focusRing
    }
}
