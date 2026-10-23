import QtQuick

Item {
    id: root
    property bool active: true
    property color color: theme.opticalLight
    property real progress: -1
    property bool reducedMotion: false
    readonly property bool animating: active && visible && enabled && !reducedMotion && !theme.reducedMotion
    Theme { id: theme }
    implicitWidth: theme.scaled(18)
    implicitHeight: implicitWidth
    Accessible.role: Accessible.ProgressBar
    Accessible.name: progress >= 1 ? "Complete" : progress >= 0 ? Math.round(Math.max(0, progress) * 100) + "%" : "Loading"

    OpticalLightField {
        objectName: "activityInterferenceRing"
        anchors.fill: parent
        visible: root.active || root.progress < 1
        kind: 1
        active: root.animating
        reducedMotion: root.reducedMotion
        progress: root.progress
        opacity: root.active ? 1 : 0.55
    }
    AppIcon {
        anchors.fill: parent
        visible: !root.active && root.progress >= 1
        name: "check"
        color: root.color
        Accessible.ignored: true
    }
}
