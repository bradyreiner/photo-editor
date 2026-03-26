import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: "#c99a52"
    property bool expandable: false
    property bool expanded: true
    default property alias contentData: body.data

    Theme { id: theme }

    implicitHeight: sectionLayout.implicitHeight + 18
    radius: theme.radius8
    color: theme.alpha(theme.ink800, 0.88)
    border.width: 1
    border.color: expanded ? theme.alpha(theme.lineSoft, 0.32) : theme.alpha(theme.lineSoft, 0.2)

    ColumnLayout {
        id: sectionLayout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Item {
            Layout.fillWidth: true
            implicitHeight: headerLayout.implicitHeight

            MouseArea {
                anchors.fill: parent
                enabled: root.expandable
                cursorShape: root.expandable ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.expanded = !root.expanded
            }

            ColumnLayout {
                id: headerLayout
                anchors.fill: parent
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 2
                        radius: 1
                        color: root.accent
                    }

                    Label {
                        text: root.title.toUpperCase()
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.1
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Label {
                        visible: root.expandable
                        text: root.expanded ? "˅" : ">"
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: 12
                    }
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

                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            id: body
            visible: !root.expandable || root.expanded
            Layout.fillWidth: true
            spacing: 8
        }
    }
}
