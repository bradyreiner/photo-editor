import QtQuick

Rectangle {
    id: root

    property color accent: theme.accentWarm400
    property bool accentRail: false
    property bool elevated: false
    property real surfaceOpacity: elevated ? 0.68 : 0.48
    property real edgeOpacity: elevated ? 0.22 : 0.10
    property real highlightOpacity: 0

    Theme { id: theme }

    radius: theme.radius8
    color: theme.alpha(theme.glassBase, root.surfaceOpacity)
    border.width: 1
    border.color: theme.alpha(theme.glassEdge, root.edgeOpacity)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: 1
        color: theme.alpha(theme.polar, root.highlightOpacity)
    }
}
