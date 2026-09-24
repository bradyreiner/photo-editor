import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property string label
    required property string value
    property color accent: theme.accentWarm400

    Theme { id: theme }

    implicitHeight: theme.iconButtonSizeCompact
    implicitWidth: Math.max(88, textColumn.implicitWidth + theme.space20)
    radius: 0
    color: theme.alpha(theme.glassBase, 0.24)
    border.width: theme.hairlineWidth
    border.color: theme.alpha(theme.glassEdge, 0.14)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6

        ColumnLayout {
            id: textColumn
            spacing: 0

            Label {
                text: root.label
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.DemiBold
                font.letterSpacing: 0
            }

            Label {
                text: root.value
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: Font.DemiBold
            }
        }
    }
}
