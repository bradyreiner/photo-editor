import QtQuick
import QtQuick.Window

Item {
    id: root
    property Item sourceItem: null
    property rect captureRect: Qt.rect(0, 0, width, height)
    property bool active: true
    property bool reducedMotion: false
    property real signalTime: 0
    property real signalBreak: 0
    property real strength: 1
    property bool activity: false
    property real activityProgress: -1
    property int activityKind: 0
    property color activityColor: "#a5d9e0"
    property vector4d activityBounds: Qt.vector4d(0, 0, 0, 0)
    property vector4d eyeBounds: Qt.vector4d(0, 0, 0, 0)
    readonly property bool motionEnabled: active && visible && !reducedMotion && !theme.reducedMotion
    readonly property real pixelRatio: Math.max(1, Number(Screen.devicePixelRatio || 1))
    readonly property string renderingContract: "crt-signal:v1|pixels:device|scanlines:fine|rgb:edge-fringe|grain:none|glitch:event|alpha:preserved"
    Theme { id: theme }

    function blinkOnce() { if (motionEnabled) blinkSequence.restart() }
    function stopBlink() { blinkSequence.stop(); effect.blinkAmount = 1 }
    function react() {
        if (motionEnabled) signalReaction.restart()
    }
    onMotionEnabledChanged: {
        if (!motionEnabled) {
            signalReaction.stop()
            effect.signalBreak = 0
            stopBlink()
        }
    }
    SequentialAnimation {
        id: signalReaction
        UniformAnimator { target: effect; uniform: "signalBreak"; from: 0; to: 0.28; duration: 45 }
        UniformAnimator { target: effect; uniform: "signalBreak"; from: 0.28; to: 0; duration: 130; easing.type: Easing.OutCubic }
    }
    SequentialAnimation {
        id: blinkSequence
        UniformAnimator { target: effect; uniform: "blinkAmount"; from: 1; to: 0.15; duration: 70 }
        PauseAnimation { duration: 35 }
        UniformAnimator { target: effect; uniform: "blinkAmount"; from: 0.15; to: 1; duration: 130 }
    }
    // This uniform is driven by the scene graph's own animation clock. No QML
    // property notification, Canvas paint or GUI timer is needed per frame.
    UniformAnimator {
        target: effect; uniform: "activityPhase"
        running: root.motionEnabled && root.activity
        from: 0; to: 1; duration: 3000; loops: Animation.Infinite
    }
    ShaderEffectSource {
        id: capturedArt
        sourceItem: root.sourceItem
        sourceRect: root.captureRect
        textureSize: Qt.size(Math.max(1, Math.ceil(root.captureRect.width * root.pixelRatio)),
                             Math.max(1, Math.ceil(root.captureRect.height * root.pixelRatio)))
        hideSource: true
        live: root.visible
        visible: false
        smooth: true
    }
    ShaderEffect {
        id: effect
        objectName: "characterCrtEffect"
        x: root.captureRect.x; y: root.captureRect.y
        width: root.captureRect.width; height: root.captureRect.height
        property var source: capturedArt
        property vector4d screenMetrics: Qt.vector4d(width * root.pixelRatio, height * root.pixelRatio,
                                                     root.signalTime, 0)
        property real effectStrength: root.strength
        property real activityPhase: 0
        property real signalBreak: 0
        property real blinkAmount: 1
        property vector4d activityData: Qt.vector4d(root.activityKind, root.activityProgress,
                                                    root.activity && root.motionEnabled ? 1 : 0, 0)
        property vector4d activityBounds: root.activityBounds
        property vector4d eyeBounds: root.eyeBounds
        property color activityColor: root.activityColor
        vertexShader: "qrc:/photo-editor/shaders/agent_crt.vert.qsb"
        fragmentShader: "qrc:/photo-editor/shaders/agent_crt.frag.qsb"
        blending: true
    }
}
