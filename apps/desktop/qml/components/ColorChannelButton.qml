import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root

    required property string label
    required property color swatch
    property bool selected: false
    signal clicked
    activeFocusOnTab: true

    Theme { id: theme }

    implicitWidth: Math.max(68, labelText.implicitWidth + 24)
    implicitHeight: theme.controlHeightDense
    radius: theme.radius6
    color: root.selected ? theme.alpha(root.swatch, 0.10) : theme.alpha(theme.ink950, 0.28)
    border.width: root.activeFocus ? 2 : 0
    border.color: root.activeFocus
                  ? theme.alpha(theme.focusRing, 0.92)
                  : "transparent"

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        height: 2
        radius: 1
        visible: root.selected
        color: root.swatch
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()

    Row {
        anchors.centerIn: parent
        spacing: theme.gapControl

        Rectangle {
            width: 7
            height: 7
            radius: 3.5
            color: root.swatch
        }

        Label {
            id: labelText
            text: root.label
            color: root.selected ? theme.textStrong : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
