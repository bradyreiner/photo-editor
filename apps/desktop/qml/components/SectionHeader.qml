import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: theme.accentWarm500

    implicitHeight: columnLayout.implicitHeight

    Theme { id: theme }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: theme.space4

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.gapDense + 1

            Rectangle {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 2
                radius: 2
                color: root.accent
            }

            Label {
                text: root.title
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }
        }

        Label {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
