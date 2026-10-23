import QtQuick

Item {
    id: root
    property bool active: true
    property bool reducedMotion: false
    property int kind: 0 // 0: caustic wavefront, 1: circular interference, 2: spectral bar
    property real variant: 0
    property real progress: -1
    property bool failed: false
    readonly property bool motionEnabled: active && visible && enabled && !failed && !reducedMotion && !theme.reducedMotion
    readonly property bool softwareRendering: GraphicsInfo.api === GraphicsInfo.Software
    Theme { id: theme }
    Accessible.ignored: true

    ShaderEffect {
        id: light
        anchors.fill: parent
        visible: !root.softwareRendering
        property size resolution: Qt.size(width, height)
        property real lightPhase: 0.16
        property vector4d parameters: Qt.vector4d(root.kind, root.variant, root.progress, root.failed ? 1 : 0)
        property color tint: "white"
        vertexShader: "qrc:/photo-editor/shaders/optical_light.vert.qsb"
        fragmentShader: "qrc:/photo-editor/shaders/optical_light.frag.qsb"
        blending: true
    }
    // Animation runs on the render thread, including while a workspace is opening.
    UniformAnimator {
        target: light; uniform: "lightPhase"
        running: root.motionEnabled && !root.softwareRendering
        from: 0; to: 1
        duration: root.kind === 1 ? 2100 : root.kind === 2 ? 1900 : 11000
        loops: Animation.Infinite
    }

    // A cached vector fallback keeps remote sessions and software-rendered UI
    // tests readable. It never repaints on an animation timer.
    Canvas {
        id: fallback
        anchors.fill: parent
        visible: root.softwareRendering
        antialiasing: true
        onAvailableChanged: if (available) requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        Connections {
            target: root
            function onProgressChanged() { fallback.requestPaint() }
            function onFailedChanged() { fallback.requestPaint() }
            function onVariantChanged() { fallback.requestPaint() }
            function onKindChanged() { fallback.requestPaint() }
        }
        onPaint: {
            if (!visible || width <= 0 || height <= 0) return
            const c = getContext("2d")
            c.reset()
            c.lineCap = "round"
            c.globalCompositeOperation = "lighter"
            const colors = root.failed ? ["#cc613e", "#cc613e", "#cc613e"]
                : ["#e56c60", "#66cf9b", "#688fe5"]
            if (root.kind === 2) {
                const extent = root.progress < 0 ? width : width * Math.max(0, Math.min(1, root.progress))
                const gradient = c.createLinearGradient(0, 0, Math.max(1, extent), 0)
                gradient.addColorStop(0, root.progress < 0 ? "transparent" : "#729fba")
                gradient.addColorStop(0.5, "#99cdc6")
                gradient.addColorStop(0.9, "#dce9e4")
                gradient.addColorStop(1, root.progress < 0 ? "transparent" : "#dce9e4")
                c.fillStyle = gradient; c.fillRect(0, 0, extent, height)
                return
            }
            for (let channel = 0; channel < 3; ++channel) {
                c.strokeStyle = colors[channel]
                c.lineWidth = Math.max(0.65, Math.min(width, height) * (root.kind === 1 ? 0.032 : 0.006))
                if (root.kind === 1) {
                    const radius = Math.min(width, height) * (0.35 + (channel - 1) * 0.014)
                    c.globalAlpha = 0.85
                    c.beginPath(); c.arc(width / 2, height / 2, radius, -Math.PI / 2,
                        -Math.PI / 2 + Math.PI * 2 * (root.progress < 0 ? 0.72 : Math.max(0, Math.min(1, root.progress))))
                    c.stroke()
                } else {
                    for (let ray = -2; ray <= 2; ++ray) {
                        c.beginPath()
                        for (let step = 0; step <= 120; ++step) {
                            const u = step / 120, x = (u * 2 - 1) * 2.5
                            const y = 0.34 * Math.sin(x * 1.5 + 1 + root.variant * 0.43)
                                + 0.20 * Math.sin(x * 2.3 - 1 + ray * 0.12)
                                + (channel - 1) * 0.055 * Math.cos(x + 1) + ray * 0.095 * Math.sin(x - 1)
                            if (step === 0) c.moveTo(0, (y + 1) * height / 2)
                            else c.lineTo(u * width, (y + 1) * height / 2)
                        }
                        c.globalAlpha = 0.38; c.stroke()
                    }
                }
            }
            if (root.kind === 0) {
                // Destination masking feathers the vector wave into its surface.
                c.globalCompositeOperation = "destination-in"
                const fade = c.createLinearGradient(0, 0, width, 0)
                fade.addColorStop(0, "transparent"); fade.addColorStop(0.24, "white")
                fade.addColorStop(0.76, "white"); fade.addColorStop(1, "transparent")
                c.fillStyle = fade; c.fillRect(0, 0, width, height)
            }
        }
    }
    RotationAnimator {
        target: fallback
        running: root.softwareRendering && root.motionEnabled && root.kind === 1 && root.progress < 0
        from: 0; to: 360; duration: 2100; loops: Animation.Infinite
    }
    SequentialAnimation {
        running: root.softwareRendering && root.motionEnabled && root.kind !== 1
        loops: Animation.Infinite
        OpacityAnimator { target: fallback; from: 0.45; to: 1; duration: 1500; easing.type: Easing.InOutSine }
        OpacityAnimator { target: fallback; from: 1; to: 0.45; duration: 1500; easing.type: Easing.InOutSine }
    }

}
