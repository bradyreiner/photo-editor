import QtQuick

Rectangle {
    id: root

    property bool selected: false
    property bool endpoint: false
    property color accent: theme.accentWarm400

    Theme { id: theme }

    readonly property string toneCurvePointHandleContract: "tone-curve-point-handle:v1|selected-state:true|endpoint-state:true|stable-size:true|premium-handle:true"

    width: endpoint ? (selected ? 12 : 10) : (selected ? 11 : 9)
    height: width
    radius: width / 2
    color: endpoint ? theme.alpha(theme.polar, 0.86) : root.accent
    border.width: 1
    border.color: selected ? theme.polar : theme.alpha(theme.ink950, 0.8)
    antialiasing: true

    Rectangle {
        anchors.centerIn: parent
        width: root.selected ? 4 : 3
        height: width
        radius: width / 2
        color: root.endpoint ? theme.ink950 : theme.polar
    }
}
