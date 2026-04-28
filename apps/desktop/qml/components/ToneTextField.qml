import QtQuick
import QtQuick.Controls

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
    font.pixelSize: 12
    leftPadding: theme.space12
    rightPadding: theme.space12
    activeFocusOnTab: true

    background: Rectangle {
        radius: theme.radiusControl
        color: root.enabled ? theme.alpha(theme.glassBase, 0.38) : theme.alpha(theme.glassBase, 0.26)
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? theme.alpha(theme.focusRing, 0.9) : theme.alpha(theme.glassEdge, 0.2)

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.activeFocus
            color: theme.alpha(theme.focusFill, 0.2)
        }
    }
}
