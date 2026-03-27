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

    background: Rectangle {
        radius: theme.radiusChip
        color: root.down
               ? theme.alpha(root.accent, 0.22)
               : root.active
                 ? theme.alpha(root.accent, 0.12)
                 : root.hovered
                   ? theme.ink800
                   : theme.ink850
        border.width: 1
        border.color: root.active ? theme.alpha(root.accent, 0.34) : theme.alpha(theme.lineSoft, 0.92)

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
        color: root.active ? theme.textStrong : theme.textMain
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        font.weight: Font.Medium
        elide: Text.ElideRight
    }
}
