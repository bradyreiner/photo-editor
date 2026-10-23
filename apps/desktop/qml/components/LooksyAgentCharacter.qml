import QtQuick

Item {
    id: root
    property string mode: "idle"
    property string tool: "auto"
    property bool talking: false
    property bool celebrating: false
    property bool animated: true
    property bool articulated: true
    property bool grounded: false
    property string locomotion: "idle"
    property real toolDeployProgress: 0
    property real toolContactOffset: 0
    property real toolContactOffsetX: 0
    property bool toolWorking: false
    property real toolTurnPhase: 0
    property real reachProgress: 0
    property real lookAngle: 0
    property real gaitPhase: 0
    property bool faceLeft: false
    property color accentColor: "#a5f3d6"
    property color lineColor: "#e8ece9"
    property bool glyphOnly: false
    property bool reducedMotion: false
    property real progress: -1
    property real phase: 0
    property real blink: 1
    property real reactionLean: 0
    property int frame: 0
    readonly property real feetBaseline: height * 0.909
    readonly property real armReachAngle: 36
    readonly property bool motionEnabled: animated && visible && enabled && !reducedMotion && !theme.reducedMotion
    readonly property string normalizedMode: String(mode || "idle").toLowerCase()
    readonly property string normalizedTool: String(tool || "auto").toLowerCase()
    readonly property bool walking: normalizedMode === "walk" || normalizedMode === "walking"
    readonly property bool climbing: locomotion === "climb"
    readonly property bool working: {
        switch (normalizedMode) {
        case "thinking": case "processing": case "adjust": case "scan":
        case "sort": case "assemble": case "render": case "export": case "type":
            return true
        default: return toolWorking
        }
    }
    readonly property real legSwing: motionEnabled && (walking || climbing) ? Math.sin(gaitPhase) * 9 : 0
    readonly property real armLength: height * 0.24
    readonly property point handContactPoint: {
        const angle = reachProgress * armReachAngle * Math.PI / 180
        const x = width * 0.77 + armLength * Math.sin(angle)
        return Qt.point(faceLeft ? width - x : x, height * 0.44 + armLength * Math.cos(angle))
    }
    readonly property point toolContactPoint: Qt.point(handContactPoint.x + toolContactOffsetX, feetBaseline + toolContactOffset)
    readonly property bool artReady: drawing.status === ShaderEffect.Compiled
    readonly property string visualDesignContract:
        "looksy-agent-character:v8|form:crt-robot|art:gpu-solid-geometry|texture:live-crt|activity:screen|breathing:none|celebration:small-nod"
    readonly property int expression: normalizedMode === "warning" || normalizedMode === "error" ? 4
        : celebrating || normalizedMode === "success" ? 3
        : normalizedMode === "greeting" || normalizedMode === "wave" ? 5
        : walking || climbing ? 6 : talking ? 2 : working ? 1 : 0
    Theme { id: theme }
    implicitWidth: 96
    implicitHeight: 112
    Accessible.role: Accessible.Graphic
    Accessible.name: "Looksy visual assistant, " + mode

    // Continuous motion stays on the render thread. Input properties remain
    // compatible with the walkthrough and contact/locomotion controllers.
    function repaint() { /* Geometry and materials follow bound shader uniforms. */ }
    function react() {
        if (motionEnabled) {
            reaction.restart()
            signal.react()
        }
    }
    function blinkOnce() { if (motionEnabled && !glyphOnly) blinkSequence.restart() }
    onModeChanged: react()
    onCelebratingChanged: { if (celebrating) react() }
    onMotionEnabledChanged: {
        if (!motionEnabled) {
            blinkSequence.stop()
            reaction.stop()
            drawing.blinkAmount = 1
            drawing.reactionAmount = 0
        }
    }
    Timer {
        interval: 4700; repeat: true
        running: root.motionEnabled && !root.glyphOnly
        onTriggered: { root.frame += 1; root.blinkOnce() }
    }
    SequentialAnimation {
        id: blinkSequence
        UniformAnimator { target: drawing; uniform: "blinkAmount"; from: 1; to: 0.055; duration: 85 }
        PauseAnimation { duration: 55 }
        UniformAnimator { target: drawing; uniform: "blinkAmount"; from: 0.055; to: 1; duration: 140 }
    }
    SequentialAnimation {
        id: reaction
        UniformAnimator { target: drawing; uniform: "reactionAmount"; from: 0; to: 1; duration: 65 }
        UniformAnimator { target: drawing; uniform: "reactionAmount"; from: 1; to: 0; duration: 230; easing.type: Easing.OutCubic }
    }
    ShaderEffect {
        id: drawing
        objectName: "agentRobotSolidRenderer"
        anchors.fill: parent
        property vector4d viewport: Qt.vector4d(width, height, 0, 0)
        property vector4d pose: Qt.vector4d(root.expression, root.lookAngle, root.progress, 0)
        property vector4d behavior: Qt.vector4d(root.reachProgress, root.faceLeft ? 1 : 0, root.glyphOnly ? 1 : 0, 0)
        property color tint: root.accentColor
        property real motionPhase: 0
        property real blinkAmount: 1
        property real reactionAmount: 0
        vertexShader: "qrc:/photo-editor/shaders/agent_robot.vert.qsb"
        fragmentShader: "qrc:/photo-editor/shaders/agent_robot.frag.qsb"
        blending: true
        UniformAnimator {
            target: drawing; uniform: "motionPhase"
            from: 0; to: 1; duration: 12000; loops: Animation.Infinite
            running: root.motionEnabled
        }
    }
    CrtSignalSurface {
        id: signal
        objectName: "agentCharacterCrtSignal"
        anchors.fill: parent
        sourceItem: drawing
        active: root.motionEnabled
        reducedMotion: root.reducedMotion
        strength: 0.65
        // Screen scanlines and triads are mapped onto the curved 3D glass by
        // agent_robot.frag. This pass adds only a restrained highlight bloom.
        scanlineStrength: 0
        phosphorStrength: 0
        bloomStrength: 0.12
        chromaticOffset: 0.15
    }
}
