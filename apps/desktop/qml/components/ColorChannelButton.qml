import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property string label
    required property color swatch
    property bool selected: false
    signal clicked
    activeFocusOnTab: true

    Theme { id: theme }

    implicitWidth: Math.max(72, labelText.implicitWidth + 28)
    implicitHeight: theme.controlHeight
    radius: theme.radiusChip
    color: root.selected ? theme.alpha(root.swatch, 0.18) : theme.alpha(theme.ink900, 0.92)
    border.width: root.activeFocus ? 2 : 1
    border.color: root.activeFocus
                  ? theme.alpha(theme.focusRing, 0.92)
                  : (root.selected ? theme.alpha(root.swatch, 0.3) : theme.alpha(theme.lineSoft, 0.42))

    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()

    Row {
        anchors.centerIn: parent
        spacing: theme.gapControl

        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: root.swatch
        }

        Label {
            id: labelText
            text: root.label
            color: root.selected ? theme.textStrong : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
