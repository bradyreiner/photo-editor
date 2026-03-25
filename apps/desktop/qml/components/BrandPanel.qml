import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: "#c99a52"
    property bool headerVisible: true
    property int bodyMargin: root.headerVisible ? 18 : 12
    property int bodySpacing: root.headerVisible ? 12 : 10
    default property alias contentData: body.data

    radius: 14
    color: "#16191d"

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: root.bodyMargin
        spacing: root.bodySpacing

        ColumnLayout {
            visible: root.headerVisible
            spacing: 5

            Rectangle {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 2
                radius: 1
                color: root.accent
            }

            Label {
                text: root.title
                font.family: "Avenir Next"
                font.pixelSize: 16
                font.weight: Font.Medium
                color: "#f2f4f6"
            }

            Label {
                visible: root.subtitle.length > 0
                text: root.subtitle
                wrapMode: Text.WordWrap
                color: "#8f98a2"
                font.family: "Avenir Next"
                font.pixelSize: 12
            }
        }
    }
}
