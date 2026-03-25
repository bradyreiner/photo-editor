import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string label
    required property string value
    property color accent: "#c99a52"

    implicitHeight: 38
    implicitWidth: Math.max(116, textColumn.implicitWidth + 24)
    radius: 10
    color: "#171a1e"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: root.accent
        }

        ColumnLayout {
            id: textColumn
            spacing: 1

            Label {
                text: root.label
                color: "#8f98a2"
                font.family: "Avenir Next"
                font.pixelSize: 9
                font.letterSpacing: 0.8
            }

            Label {
                text: root.value
                color: "#f1f4f7"
                font.family: "Avenir Next"
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }
}
