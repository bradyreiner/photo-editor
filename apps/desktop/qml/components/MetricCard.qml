import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string label
    required property string value
    property string detail: ""
    property color accent: "#c99a52"

    Theme { id: theme }

    radius: theme.radius12
    color: theme.ink850
    border.color: theme.alpha(root.accent, 0.18)
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
            color: theme.textFaint
            font.family: theme.fontMono
            font.pixelSize: 10
            font.letterSpacing: 0.8
        }

        Label {
            text: root.value
            color: theme.textStrong
            font.family: theme.fontDisplay
            font.pixelSize: 20
            font.weight: Font.Medium
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            visible: root.detail.length > 0
            text: root.detail
            color: theme.textMuted
            wrapMode: Text.WordWrap
            font.family: theme.fontUi
            font.pixelSize: 11
            maximumLineCount: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
