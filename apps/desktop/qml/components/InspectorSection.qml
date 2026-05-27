import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: theme.accentWarm400
    property bool showAccentMarker: true
    property bool uppercaseTitle: true
    property string titleFontFamily: theme.fontMono
    property int titlePixelSize: 10
    property int titleWeight: Font.DemiBold
    property real titleLetterSpacing: 0
    property bool expandable: false
    property bool expanded: true
    default property alias contentData: body.data

    Theme { id: theme }

    implicitHeight: sectionLayout.implicitHeight + theme.space12
    radius: 0
    color: "transparent"
    border.width: 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: theme.alpha(theme.lineSoft, 0.30)
    }

    ColumnLayout {
        id: sectionLayout
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        anchors.topMargin: theme.space10
        anchors.bottomMargin: theme.space4
        spacing: theme.space8

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
                spacing: theme.space4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    Rectangle {
                        visible: root.showAccentMarker
                        Layout.preferredWidth: root.showAccentMarker ? 10 : 0
                        Layout.preferredHeight: 1
                        radius: 1
                        color: theme.polar
                    }

                    Label {
                        text: root.uppercaseTitle ? root.title.toUpperCase() : root.title
                        color: theme.textStrong
                        font.family: root.titleFontFamily
                        font.pixelSize: root.titlePixelSize
                        font.weight: root.titleWeight
                        font.letterSpacing: root.titleLetterSpacing
                        verticalAlignment: Text.AlignVCenter
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    AppIcon {
                        visible: root.expandable
                        name: "chevron-right"
                        rotation: root.expanded ? 90 : 0
                        color: root.expanded ? theme.textMuted : theme.textFaint
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                    }
                }

                Label {
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
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
            spacing: theme.space5
        }
    }
}
