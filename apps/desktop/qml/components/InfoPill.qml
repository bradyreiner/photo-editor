import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string label
    required property string value
    property color accent: theme.accentWarm400

    Theme { id: theme }

    implicitHeight: 32
    implicitWidth: Math.max(88, textColumn.implicitWidth + 20)
    radius: 0
    color: theme.alpha(theme.glassBase, 0.24)
    border.width: 1
    border.color: theme.alpha(theme.glassEdge, 0.14)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 6
        anchors.bottomMargin: 6

        ColumnLayout {
            id: textColumn
            spacing: 0

            Label {
                text: root.label
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.letterSpacing: 0
            }

            Label {
                text: root.value
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }
    }
}
