import QtQuick
import QtQuick.Controls

Button {
    id: root

    property string tone: "secondary"
    property bool active: false
    property color accent: "#c99a52"

    implicitHeight: 34
    implicitWidth: Math.max(92, contentLabel.implicitWidth + 22)
    font.family: "Avenir Next"
    font.pixelSize: 11

    background: Rectangle {
        radius: 7
        color: root.down ? (root.tone === "primary" ? "#b98947" : "#23262a") : root.active ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.16) : root.hovered ? "#202429" : (root.tone === "primary" ? "#c99a52" : "#14191e")

        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }

    contentItem: Text {
        id: contentLabel
        text: root.text
        color: root.tone === "primary" ? "#111317" : (root.enabled ? "#eef1f4" : "#78808a")
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        font.weight: Font.Medium
        elide: Text.ElideRight
    }
}
