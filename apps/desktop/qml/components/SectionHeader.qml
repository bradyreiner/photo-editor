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
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 3
                radius: 2
                color: root.accent
            }

            Label {
                text: root.title
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }
        }

        Label {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
