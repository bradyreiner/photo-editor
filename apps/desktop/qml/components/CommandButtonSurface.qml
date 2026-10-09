import QtQuick

Item {
    id: root

    property color fillColor: "transparent"
    property color borderColor: theme.controlEdge
    property color accentColor: theme.primaryAccent
    property real borderWidth: 0
    property real cut: 0
    property real borderOpacity: 1
    property real accentOpacity: 0
    property real accentWidth: 0
    property real topLineOpacity: 0.12
    property real cornerRadius: theme.radiusControl
    property bool hovered: false
    property bool pressed: false
    property bool selected: false
    property bool interactiveEnabled: true
    readonly property bool lit: interactiveEnabled && (hovered || selected)

    Theme { id: theme }

    Behavior on fillColor {
        enabled: !theme.reducedMotion
        ColorAnimation { duration: theme.reducedMotion ? 0 : 110; easing.type: Easing.OutCubic }
    }
    Behavior on accentOpacity {
        enabled: !theme.reducedMotion
        NumberAnimation { duration: theme.reducedMotion ? 0 : 140; easing.type: Easing.OutCubic }
    }
    Behavior on borderOpacity {
        enabled: !theme.reducedMotion
        NumberAnimation { duration: theme.reducedMotion ? 0 : 110; easing.type: Easing.OutCubic }
    }
    Behavior on topLineOpacity {
        enabled: !theme.reducedMotion
        NumberAnimation { duration: theme.reducedMotion ? 0 : 110; easing.type: Easing.OutCubic }
    }
    // Flat fills keep every command in the same visual family. Pointer hover
    // changes only the fill; callers reserve edges for keyboard focus/contrast.
    Rectangle {
        anchors.fill: parent
        radius: Math.max(0, Math.min(Math.min(root.cornerRadius, theme.radiusControl),
                                     Math.min(width, height) / 2))
        color: root.fillColor
        border.width: root.borderWidth
        border.color: theme.alpha(root.borderColor, root.borderColor.a * root.borderOpacity)
        antialiasing: true
    }

}
