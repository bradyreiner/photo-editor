import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string label
    required property string value
    property color accent: "#c99a52"

    Theme { id: theme }

    implicitHeight: 34
    implicitWidth: Math.max(96, textColumn.implicitWidth + 18)
    radius: theme.radiusChip
    color: theme.alpha(theme.ink950, 0.18)
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.18)

    RowLayout {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 8

        Rectangle {
            Layout.preferredWidth: 5
            Layout.preferredHeight: 5
            radius: 2
            color: root.accent
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 16
            color: theme.alpha(theme.lineSoft, 0.12)
        }

        ColumnLayout {
            id: textColumn
            spacing: 1

            Label {
                text: root.label
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                font.letterSpacing: 0.7
            }

            Label {
                text: root.value
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }
}
