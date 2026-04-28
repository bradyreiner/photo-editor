import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root

    required property string title
    property string detail: ""
    property real progressValue: -1
    property color accent: theme.accentWarm400
    property bool emphasized: false
    default property alias actionData: actionRow.data

    Theme { id: theme }

    radius: theme.radius10
    color: root.emphasized ? theme.alpha(root.accent, 0.14) : theme.alpha(theme.glassBase, 0.58)
    border.width: 1
    border.color: root.emphasized ? theme.alpha(root.accent, 0.42) : theme.alpha(theme.glassEdge, 0.18)

    implicitHeight: contentColumn.implicitHeight + (theme.space10 * 2)

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: theme.space10
        spacing: theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: root.title
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.detail.length > 0
                    text: root.detail
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                id: actionRow
                spacing: theme.space6
            }
        }

        ProgressStrip {
            Layout.fillWidth: true
            progress: root.progressValue
            accent: root.accent
        }
    }
}
