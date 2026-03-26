import QtQuick
import QtQuick.Controls

TextField {
    id: root

    property color accent: "#c99a52"

    Theme { id: theme }

    implicitHeight: 38
    color: theme.textStrong
    placeholderTextColor: theme.textFaint
    selectedTextColor: theme.ink950
    selectionColor: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.85)
    font.family: theme.fontUi
    font.pixelSize: 12

    background: Rectangle {
        radius: theme.radius8
        color: theme.ink900
        border.width: 1
        border.color: root.activeFocus ? root.accent : theme.lineSoft
    }
}
