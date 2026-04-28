import QtQuick

Rectangle {
    id: root

    property real progress: -1
    property color accent: theme.accentWarm400
    property color trackColor: theme.alpha(theme.glassBase, 0.76)
    property bool indeterminate: progress < 0

    Theme { id: theme }

    implicitHeight: 7
    radius: height / 2
    color: trackColor
    clip: true

    border.width: 1
    border.color: theme.alpha(theme.glassEdge, 0.18)

    Rectangle {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: root.indeterminate ? 0 : parent.width * Math.max(0, Math.min(1, root.progress))
        height: parent.height
        radius: parent.radius
        color: root.accent
        visible: !root.indeterminate

        Behavior on width {
            NumberAnimation { duration: theme.durationMedium }
        }

    }

    Rectangle {
        id: indeterminateBar
        visible: root.indeterminate
        width: Math.max(44, parent.width * 0.38)
        height: parent.height
        radius: parent.radius
        color: root.accent
        opacity: 0.78
        x: -width

        SequentialAnimation on x {
            running: root.indeterminate && root.visible
            loops: Animation.Infinite
            NumberAnimation {
                from: -indeterminateBar.width
                to: root.width
                duration: 1150
                easing.type: Easing.InOutCubic
            }
        }
    }

    Repeater {
        model: Math.max(0, Math.floor(root.width / 22))

        Rectangle {
            width: 1
            height: root.height * 1.8
            x: index * 22 + 9
            y: -root.height * 0.4
            rotation: 24
            color: theme.alpha(theme.polar, 0.08)
        }
    }
}
