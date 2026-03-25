import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property string label
    required property color swatch
    property bool selected: false
    signal clicked

    implicitWidth: Math.max(76, labelText.implicitWidth + 30)
    implicitHeight: 32
    radius: 7
    color: root.selected ? Qt.rgba(root.swatch.r, root.swatch.g, root.swatch.b, 0.18) : "#14191e"

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
            color: root.selected ? "#f1f5f7" : "#cfd6dd"
            font.family: "Avenir Next"
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
