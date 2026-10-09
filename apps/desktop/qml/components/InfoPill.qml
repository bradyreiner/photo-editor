import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property string label
    required property string value
    property color accent: theme.accentWarm400

    Theme { id: theme }

    implicitHeight: textColumn.implicitHeight + theme.space8
    implicitWidth: Math.max(theme.scaled(76), textColumn.implicitWidth + theme.space20)
    radius: theme.radiusControl
    color: theme.ink850
    border.width: theme.hairlineWidth
    border.color: theme.alpha(theme.lineSoft, 0.72)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        anchors.topMargin: theme.space4
        anchors.bottomMargin: theme.space4

        ColumnLayout {
            id: textColumn
            spacing: theme.space2

            Label {
                text: root.label
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                font.weight: Font.Medium
                font.letterSpacing: 0
            }

            Label {
                text: root.value
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.DemiBold
            }
        }
    }
}
