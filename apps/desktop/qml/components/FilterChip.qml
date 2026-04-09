import QtQuick
import QtQuick.Controls

Button {
    id: root

    property bool active: false
    property color accent: "#c99a52"

    Theme { id: theme }

    implicitHeight: theme.controlHeightDense
    implicitWidth: Math.max(72, contentLabel.implicitWidth + theme.space16)
    font.family: theme.fontUi
    font.pixelSize: 11
    hoverEnabled: true
    activeFocusOnTab: true

    background: Rectangle {
        radius: theme.radiusChip
        color: !root.enabled
               ? theme.alpha(theme.ink900, 0.6)
               : root.down
                 ? theme.alpha(root.accent, 0.22)
               : root.active
                 ? theme.alpha(root.accent, 0.12)
                 : root.hovered
                   ? theme.ink800
                   : theme.ink850
        border.width: root.visualFocus ? 2 : 1
        border.color: !root.enabled
                      ? theme.alpha(theme.lineSoft, 0.12)
                      : root.visualFocus
                        ? theme.alpha(theme.focusRing, 0.92)
                        : (root.active ? theme.alpha(root.accent, 0.34) : theme.alpha(theme.lineSoft, 0.42))

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, 0.22)
        }

        Behavior on color {
            ColorAnimation { duration: theme.durationFast }
        }

        Behavior on border.color {
            ColorAnimation { duration: theme.durationFast }
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
        font.weight: Font.Medium
        elide: Text.ElideRight
    }
}
