import QtQuick
import QtQuick.Controls

Button {
    id: root

    property bool active: false
    property color accent: theme.accentWarm400

    Theme { id: theme }

    implicitHeight: theme.controlHeightDense
    implicitWidth: Math.max(72, contentLabel.implicitWidth + theme.space16)
    font.family: theme.fontUi
    font.pixelSize: 12
    hoverEnabled: true
    activeFocusOnTab: true

    background: Rectangle {
        radius: theme.radiusControl
        border.width: root.visualFocus ? 2 : 1
        border.color: !root.enabled
                      ? theme.alpha(theme.lineSoft, 0.12)
                      : root.visualFocus
                        ? theme.alpha(theme.focusRing, 0.92)
                        : (root.active ? theme.alpha(root.accent, 0.62) : theme.alpha(theme.glassEdge, root.hovered ? 0.4 : 0.2))
        color: !root.enabled
               ? theme.alpha(theme.glassBase, 0.4)
               : root.down
                 ? theme.alpha(root.accent, 0.14)
                 : root.active
                   ? theme.alpha(root.accent, 0.1)
                   : root.hovered
                     ? theme.alpha(theme.glassLift, 0.36)
                     : theme.alpha(theme.glassBase, 0.38)

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, 0.22)
        }
    }

    contentItem: Text {
        id: contentLabel
        text: root.text
        color: !root.enabled ? theme.alpha(theme.textMuted, 0.65) : (root.active ? theme.textStrong : theme.textMain)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        font.weight: root.active ? Font.Bold : Font.Medium
        font.letterSpacing: 0.25
        elide: Text.ElideRight
    }
}
