import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string label
    required property string value
    property string detail: ""
    property color accent: "#c99a52"

    radius: 12
    color: "#171a1e"
    border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.24)
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        Rectangle {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 2
            radius: 1
            color: root.accent
        }

        Label {
            text: root.label
            color: "#8f98a2"
            font.family: "Avenir Next"
            font.pixelSize: 10
            font.letterSpacing: 0.8
        }

        Label {
            text: root.value
            color: "#f2f4f6"
            font.family: "Avenir Next"
            font.pixelSize: 16
            font.weight: Font.Medium
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            visible: root.detail.length > 0
            text: root.detail
            color: "#9aa3ad"
            wrapMode: Text.WordWrap
            font.family: "Avenir Next"
            font.pixelSize: 11
            maximumLineCount: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
