import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property string label
    required property color swatch
    property bool selected: false
    signal clicked

    Theme { id: theme }

    implicitWidth: Math.max(76, labelText.implicitWidth + 30)
    implicitHeight: 32
    radius: theme.radius8
    color: root.selected ? theme.alpha(root.swatch, 0.18) : theme.ink900
    border.width: 1
    border.color: root.selected ? theme.alpha(root.swatch, 0.26) : theme.lineSoft

    Row {
        anchors.centerIn: parent
        spacing: 8

        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: root.swatch
        }

        Label {
            id: labelText
            text: root.label
            color: root.selected ? theme.textStrong : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 10
            font.weight: Font.Medium
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
