import QtQuick

Item {
    id: root
    property real progress: -1
    property color accent: theme.accentCool400
    property color trackColor: theme.alpha(theme.glassBase, 0.76)
    property bool failed: false
    property bool active: true
    property bool reducedMotion: false
    property real phase: 0
    readonly property real boundedProgress: Math.max(0, Math.min(1, progress))
    readonly property bool indeterminate: progress < 0
    readonly property bool animate: active && visible && enabled && !failed && !reducedMotion && !theme.reducedMotion
    readonly property string progressStripContract: "progress-strip:v5|channels:rgb-wavefront|composite:additive|progress:measured|reduced-motion:static"
    Theme { id: theme }
    implicitHeight: 4
    clip: true
    Accessible.role: Accessible.ProgressBar
    Accessible.name: failed ? "Failed" : indeterminate ? "Loading" : Math.round(boundedProgress * 100) + "%"

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.trackColor
    }
    OpticalLightField {
        anchors.fill: parent
        kind: 2
        progress: root.failed ? 1 : root.progress
        failed: root.failed
        active: root.animate && (root.indeterminate || root.boundedProgress < 1)
        reducedMotion: root.reducedMotion
    }
}
