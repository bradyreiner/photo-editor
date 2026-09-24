import QtQuick

Item {
    id: root

    property color fillColor: "transparent"
    property color borderColor: theme.controlEdge
    property color accentColor: theme.primaryAccent
    property real borderWidth: 1
    property real cut: 0
    property real borderOpacity: 1
    property real accentOpacity: 0
    property real accentWidth: 0
    property real topLineOpacity: 0.12
    property real cornerRadius: theme.radiusControl

    Theme { id: theme }

    Behavior on fillColor {
        ColorAnimation { duration: theme.reducedMotion ? 0 : 110; easing.type: Easing.OutCubic }
    }
    Behavior on accentOpacity {
        NumberAnimation { duration: theme.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic }
    }

    // Flat surfaces leave hierarchy to placement, typography, and state.
    Rectangle {
        anchors.fill: parent
        radius: Math.min(3, root.cornerRadius)
        color: root.fillColor
        border.width: root.borderWidth
        border.color: theme.alpha(root.borderColor, root.borderColor.a * root.borderOpacity)
        antialiasing: true
    }
}
