import QtQuick

Rectangle {
    id: root

    property color accent: theme.accentWarm400
    property bool accentRail: false

    Theme { id: theme }

    radius: theme.radius8
    color: theme.alpha(theme.glassBase, 0.92)
    border.width: 1
    border.color: theme.alpha(theme.glassEdge, 0.5)
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: 1
        color: theme.alpha(theme.polar, 0.08)
    }
}
