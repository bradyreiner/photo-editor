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

    LooksyMark {
        id: mark
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 0.64
        height: width
        framed: false
        SequentialAnimation on scale {
            running: root.motionEnabled && (root.talking || root.mode === "processing" || root.mode === "thinking")
            loops: Animation.Infinite
            ScaleAnimator { from: 1; to: 1.025; duration: 560; easing.type: Easing.InOutSine }
            ScaleAnimator { from: 1.025; to: 1; duration: 560; easing.type: Easing.InOutSine }
        }
    }
}
