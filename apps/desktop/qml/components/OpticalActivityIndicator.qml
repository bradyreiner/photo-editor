import QtQuick

Item {
    id: root
    property bool active: true
    property color color: theme.opticalLight
    property real progress: -1
    readonly property bool animating: root.active && root.visible && root.enabled && !theme.reducedMotion
    Theme { id: theme }
    implicitWidth: theme.scaled(18)
    implicitHeight: implicitWidth

    LooksyAgentCharacter {
        objectName: "activityCharacterGlyph"
        anchors.fill: parent
        visible: root.active || root.progress < 1
        glyphOnly: true
        lineColor: root.color
        accentColor: root.color
        progress: root.progress
        animated: root.animating
        mode: root.active ? "scan" : root.progress >= 1 ? "success" : "idle"
        opacity: root.active ? 1 : 0.65
        Accessible.ignored: true
    }

    AppIcon {
        anchors.fill: parent
        visible: !root.active && root.progress >= 1
        name: "check"
        color: root.color
        Accessible.ignored: true
    }
}
