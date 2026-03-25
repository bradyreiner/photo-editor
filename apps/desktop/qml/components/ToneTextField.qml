import QtQuick
import QtQuick.Controls

TextField {
    id: root

    property color accent: "#c99a52"

    implicitHeight: 38
    color: "#eef1f4"
    placeholderTextColor: "#7f8892"
    selectedTextColor: "#111317"
    selectionColor: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.85)
    font.family: "Avenir Next"
    font.pixelSize: 12

    background: Rectangle {
        radius: 10
        color: "#171a1e"
        border.width: 1
        border.color: root.activeFocus ? root.accent : "#2d3339"
    }
}
