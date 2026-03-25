import QtQuick
import QtQuick.Controls

Button {
    id: root

    property bool active: false
    property color accent: "#c99a52"

    implicitHeight: 30
    implicitWidth: Math.max(82, contentLabel.implicitWidth + 20)
    font.family: "Avenir Next"
    font.pixelSize: 11

    background: Rectangle {
        radius: height / 2
        color: root.down
               ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.3)
               : root.active
                 ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.2)
                 : root.hovered
                   ? "#252b31"
                   : "#171d22"
        border.width: 1
        border.color: root.active ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.65) : "#2a3239"

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }

    contentItem: Text {
        id: contentLabel
        text: root.text
        color: root.active ? "#f7ead2" : "#d3dae1"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        font.weight: Font.Medium
        elide: Text.ElideRight
    }
}
