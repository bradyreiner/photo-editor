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
    property color accentColor: "#a5d9e0"
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
    readonly property bool working: toolWorking || talking || ["thinking", "processing", "adjust", "scan", "sort", "assemble", "render", "export", "type"].indexOf(normalizedMode) >= 0
    readonly property real legSwing: motionEnabled && (walking || climbing) ? Math.sin(gaitPhase) * 9 : 0
    readonly property real armLength: height * 0.24
    readonly property point handContactPoint: {
        const angle = reachProgress * armReachAngle * Math.PI / 180
        const x = width * 0.77 + armLength * Math.sin(angle)
        return Qt.point(faceLeft ? width - x : x, height * 0.44 + armLength * Math.cos(angle))
    }
    readonly property point toolContactPoint: Qt.point(handContactPoint.x + toolContactOffsetX, feetBaseline + toolContactOffset)
    readonly property bool artReady: drawing.available
    readonly property string visualDesignContract:
        "looksy-agent-character:v6|form:single-body-line-character|art:procedural|texture:phosphor|activity:integrated-light|breathing:none|celebration:small-nod"
    Theme { id: theme }
    implicitWidth: 96
    implicitHeight: 112
    Accessible.role: Accessible.Graphic
    Accessible.name: "Looksy visual assistant, " + mode

    function repaint() { if (visible) drawing.requestPaint() }
    function react() {
        repaint()
        if (!motionEnabled) return
        signal.react()
        if (normalizedMode === "success" || celebrating) approvalNod.restart()
    }
    function blinkOnce() { if (motionEnabled && !glyphOnly) signal.blinkOnce() }
    onModeChanged: react()
    onCelebratingChanged: { if (celebrating) react() }
    onGaitPhaseChanged: repaint()
    onReachProgressChanged: repaint()
    onLookAngleChanged: repaint()
    onToolDeployProgressChanged: repaint()
    onToolContactOffsetChanged: repaint()
    onToolContactOffsetXChanged: repaint()
    onToolChanged: repaint()
    onToolWorkingChanged: repaint()
    onProgressChanged: repaint()
    onAccentColorChanged: repaint()
    onLineColorChanged: repaint()
    onGlyphOnlyChanged: repaint()
    onVisibleChanged: { if (visible) repaint() }
    onMotionEnabledChanged: {
        if (!motionEnabled) {
            signal.stopBlink()
            approvalNod.stop()
            blink = 1
            reactionLean = 0
            rotation = 0
        }
        repaint()
    }
    Timer {
        interval: 5900; repeat: true
        running: root.motionEnabled && !root.glyphOnly
        onTriggered: { root.frame += 1; root.blinkOnce() }
    }
    SequentialAnimation {
        id: approvalNod
        RotationAnimator { target: root; from: 0; to: 2; duration: 180; easing.type: Easing.OutCubic }
        RotationAnimator { target: root; from: 2; to: 0; duration: 260; easing.type: Easing.InOutCubic }
    }

    Item {
        id: artwork
        // Include real tool reach in the texture; the character's nominal bounds
        // and contact points continue to anchor it to the workspace controls.
        readonly property real paddingX: root.width * 0.6 + Math.abs(root.toolContactOffsetX)
        readonly property real paddingBottom: Math.max(root.height * 0.1, root.toolContactOffset + root.height * 0.2)
        x: -paddingX
        y: -root.height * 0.08
        width: root.width + paddingX * 2
        height: root.height * 1.08 + paddingBottom
        Canvas {
            id: drawing
            objectName: "agentLineCharacterDrawing"
            anchors.fill: parent
            readonly property real paddingX: artwork.paddingX
            antialiasing: true
            onWidthChanged: root.repaint()
            onHeightChanged: root.repaint()
            onAvailableChanged: root.repaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                if (!root.visible || root.width <= 0 || root.height <= 0) return
                const w = root.width, h = root.height
                c.translate(paddingX, h * 0.08)
                if (root.faceLeft) { c.translate(w, 0); c.scale(-1, 1) }
                c.translate(w / 2, h * 0.64)
                c.rotate((Math.max(-2, Math.min(2, root.lookAngle * 0.16))) * Math.PI / 180)
                c.translate(-w / 2, -h * 0.64)
                c.lineWidth = Math.max(1, Math.min(2, w * 0.019))
                c.lineCap = "round"
                c.lineJoin = "round"
                c.strokeStyle = root.lineColor
                function path(points) {
                    c.beginPath(); c.moveTo(points[0][0], points[0][1])
                    for (let i = 1; i < points.length; ++i) c.lineTo(points[i][0], points[i][1])
                    c.stroke()
                }
                function roundBox(x, y, width, height, radius) {
                    c.beginPath(); c.moveTo(x + radius, y)
                    c.lineTo(x + width - radius, y); c.quadraticCurveTo(x + width, y, x + width, y + radius)
                    c.lineTo(x + width, y + height - radius); c.quadraticCurveTo(x + width, y + height, x + width - radius, y + height)
                    c.lineTo(x + radius, y + height); c.quadraticCurveTo(x, y + height, x, y + height - radius)
                    c.lineTo(x, y + radius); c.quadraticCurveTo(x, y, x + radius, y)
                    c.closePath()
                }
                const bodyX = w * (root.glyphOnly ? 0.12 : 0.24)
                const bodyY = h * (root.glyphOnly ? 0.14 : 0.20)
                const bodyW = w * (root.glyphOnly ? 0.76 : 0.52)
                const bodyH = h * (root.glyphOnly ? 0.70 : 0.53)
                roundBox(bodyX, bodyY, bodyW, bodyH, w * (root.glyphOnly ? 0.14 : 0.11))
                c.stroke()
                const gaze = Math.max(-w * 0.025, Math.min(w * 0.025, root.lookAngle * w * 0.002))
                const eyeY = bodyY + bodyH * 0.29
                const eyeH = Math.max(1, h * (root.glyphOnly ? 0.09 : 0.048) * root.blink)
                for (const x of [w * 0.43, w * 0.57])
                    path([[x + gaze, eyeY], [x + gaze, eyeY + eyeH]])

                // A light filament belongs to the body. It carries measured progress
                // or a scan, never an invented percentage or simulated tool receipt.
                const lightY = bodyY + bodyH * 0.64
                const start = bodyX + bodyW * 0.23, end = bodyX + bodyW * 0.77
                c.strokeStyle = root.accentColor
                c.globalAlpha = root.working ? 0.28 : 0.55
                path([[start, lightY], [end, lightY]])
                c.globalAlpha = 1
                if (root.normalizedMode === "warning" || root.normalizedMode === "error") {
                    c.strokeStyle = "#e5ad89"
                    path([[w * 0.5, lightY - h * 0.04], [w * 0.5, lightY + h * 0.005]])
                    path([[w * 0.5, lightY + h * 0.04], [w * 0.5, lightY + h * 0.041]])
                } else if (root.normalizedMode === "success" || root.celebrating) {
                    path([[start, lightY], [w * 0.48, lightY + h * 0.023], [end, lightY - h * 0.024]])
                } else if (root.progress >= 0) {
                    path([[start, lightY], [start + (end - start) * Math.max(0, Math.min(1, root.progress)), lightY]])
                } else if (root.working && !root.motionEnabled) {
                    // Reduced motion retains an unmistakable static activity cue.
                    path([[w * 0.46, lightY], [w * 0.54, lightY]])
                }

                if (root.glyphOnly) return
                c.strokeStyle = root.lineColor
                const swing = root.legSwing * Math.PI / 180
                const lift = Math.sin(swing) * h * 0.06
                const activity = 0
                const leftX = w * 0.23 - (root.walking ? lift * 0.4 : 0)
                const leftY = h * 0.68 - (root.grounded ? 0 : activity * h * 0.025)
                path([[w * 0.23, h * 0.44], [w * 0.14, h * 0.5], [leftX, leftY]])
                const grip = Qt.point(root.faceLeft ? w - root.handContactPoint.x : root.handContactPoint.x, root.handContactPoint.y)
                path([[w * 0.77, h * 0.44], [w * 0.85, h * 0.51], [grip.x, grip.y]])
                if (root.grounded && root.toolDeployProgress > 0 && root.normalizedTool !== "none") {
                    const offsetX = root.faceLeft ? -root.toolContactOffsetX : root.toolContactOffsetX
                    const tipX = grip.x + offsetX * root.toolDeployProgress
                    const tipY = grip.y + (root.feetBaseline + root.toolContactOffset - grip.y) * root.toolDeployProgress
                    c.strokeStyle = root.accentColor
                    path([[grip.x, grip.y], [tipX, tipY]])
                    const r = Math.max(2, w * 0.04)
                    if (root.normalizedTool === "loupe") {
                        c.beginPath(); c.arc(tipX, tipY - r, r, 0, Math.PI * 2); c.stroke()
                    } else if (root.normalizedTool === "type") {
                        path([[tipX - r, tipY - r * 2], [tipX + r, tipY - r * 2]])
                        path([[tipX, tipY - r * 2], [tipX, tipY]])
                    } else {
                        path([[tipX - r * 0.7, tipY - r], [tipX, tipY], [tipX + r * 0.7, tipY - r]])
                    }
                }
            }
        }
        Repeater {
            model: root.glyphOnly ? 0 : 2
            delegate: Canvas {
                id: leg
                required property int index
                x: artwork.paddingX + root.width * (index === 0 ? 0.28 : 0.52)
                y: root.height * 0.82
                width: root.width * 0.20
                height: root.feetBaseline - root.height * 0.74 + 3
                antialiasing: true
                transformOrigin: Item.Top
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections {
                    target: root
                    function onLineColorChanged() { leg.requestPaint() }
                }
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    c.lineWidth = Math.max(1, Math.min(2, root.width * 0.019))
                    c.lineCap = "round"
                    c.strokeStyle = root.lineColor
                    const baseline = root.feetBaseline - root.height * 0.74
                    c.beginPath(); c.moveTo(width / 2, 0)
                    c.lineTo(width / 2, baseline)
                    c.lineTo(width / 2 + (index === 0 ? -1 : 1) * root.width * 0.07, baseline)
                    c.stroke()
                }
                SequentialAnimation on rotation {
                    running: root.motionEnabled && (root.walking || root.climbing)
                    loops: Animation.Infinite
                    RotationAnimator { from: leg.index === 0 ? -9 : 9; to: leg.index === 0 ? 9 : -9; duration: root.climbing ? 500 : 450; easing.type: Easing.InOutSine }
                    RotationAnimator { from: leg.index === 0 ? 9 : -9; to: leg.index === 0 ? -9 : 9; duration: root.climbing ? 500 : 450; easing.type: Easing.InOutSine }
                }
            }
        }
    }
    CrtSignalSurface {
        id: signal
        objectName: "agentCharacterCrtSignal"
        x: artwork.x; y: artwork.y
        width: drawing.width; height: drawing.height
        sourceItem: artwork
        active: root.motionEnabled
        reducedMotion: root.reducedMotion
        strength: root.glyphOnly ? 0.22 : 0.42
        activity: root.working
        activityProgress: root.progress
        activityKind: root.normalizedMode === "assemble" || root.normalizedMode === "render" ? 1
            : root.normalizedMode === "sort" || root.normalizedMode === "type" ? 2
            : root.normalizedMode === "export" ? 3 : 0
        activityColor: root.accentColor
        activityBounds: {
            const bx = root.width * (root.glyphOnly ? 0.12 : 0.24)
            const by = root.height * (root.glyphOnly ? 0.14 : 0.20)
            const bw = root.width * (root.glyphOnly ? 0.76 : 0.52)
            const bh = root.height * (root.glyphOnly ? 0.70 : 0.53)
            return Qt.vector4d((drawing.paddingX + bx + bw * 0.23) / drawing.width,
                               (drawing.paddingX + bx + bw * 0.77) / drawing.width,
                               (root.height * 0.08 + by + bh * 0.64) / drawing.height,
                               root.height / drawing.height)
        }
        eyeBounds: Qt.vector4d((drawing.paddingX + root.width * 0.43) / drawing.width,
                               (drawing.paddingX + root.width * 0.57) / drawing.width,
                               (root.height * 0.08 + root.height * (root.glyphOnly ? 0.14 + 0.70 * 0.29 : 0.20 + 0.53 * 0.29)) / drawing.height,
                               root.height * (root.glyphOnly ? 0.09 : 0.048) / drawing.height)
    }
}
