import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: row

    property string label: ""
    property string value: ""
    property bool active: false
    property string iconName: ""
    signal clicked()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: 34
    radius: 3
    color: active ? theme.alpha(theme.polar, 0.075)
                  : (mouse.containsMouse ? theme.alpha(theme.polar, 0.045) : "transparent")
    border.width: 0
    border.color: "transparent"

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        visible: row.active
        color: theme.alpha(theme.polar, 0.55)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        AppIcon {
            visible: row.iconName.length > 0
            name: row.iconName
            color: row.active ? theme.textStrong : theme.textMuted
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
        }

        Label {
            Layout.fillWidth: true
            text: row.label
            color: row.active ? theme.textStrong
                              : (mouse.containsMouse ? theme.textMain : "#A8B1BA")
            font.family: theme.fontUi
            font.pixelSize: 12
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
        }

        Label {
            visible: row.value.length > 0
            text: row.value
            color: row.active ? theme.textMain : theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: 10
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: row.clicked()
    }
}
