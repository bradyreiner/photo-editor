import QtQuick

Item {
    id: root

    property string mode: "idle"
    property string tool: "auto"
    property bool animated: true
    property bool talking: false
    property bool inverse: true
    property color inkColor: theme.brandInk
    property color paperColor: theme.brandPaper
    property color accentColor: theme.brandBlue
    property int frame: 0

    readonly property bool motionEnabled: animated && !theme.reducedMotion && visible
    readonly property string visualDesignContract:
        "looksy-light:v3|form:rgb-optical-orb|render:photographic-light-asset|motion:subtle-status-pulse|character:separate"

    Theme { id: theme }

    implicitWidth: 48
    implicitHeight: 48
    Accessible.role: Accessible.Graphic
    Accessible.name: "Looksy RGB light, " + mode

    Timer {
        interval: 32
        repeat: true
        running: root.motionEnabled && (root.talking || root.mode === "processing" || root.mode === "thinking")
        onTriggered: root.frame = (root.frame + 1) % 360
    }

    LooksyMark {
        id: mark
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.64
        height: width
        framed: false
        scale: root.motionEnabled && (root.talking || root.mode === "processing" || root.mode === "thinking")
               ? 1 + Math.sin(root.frame * 0.09) * 0.025 : 1

        Behavior on scale {
            NumberAnimation { duration: theme.durationMedium; easing.type: Easing.InOutSine }
        }
    }
}
