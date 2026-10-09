import QtQuick

Item {
    id: root

    property real progress: -1
    property color accent: theme.accentCool400
    property color trackColor: theme.alpha(theme.glassBase, 0.76)
    property bool failed: false
    property bool active: true
    property real phase: 0

    readonly property real boundedProgress: Math.max(0, Math.min(1, progress))
    readonly property bool indeterminate: progress < 0
    readonly property bool animate: active && visible && !theme.reducedMotion
    readonly property real sweepWidth: Math.min(width, Math.max(24, width * 0.28))
    readonly property string progressStripContract: "progress-strip:v4|channels:soft-color|composite:additive|scope:leading-glint|motion:eased-sweep|reduced-motion:static"

    Theme { id: theme }

    implicitHeight: 4
    clip: true

    function channelRgba(color, opacity) {
        return "rgba(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255)
                + "," + Math.round(color.b * 255) + "," + opacity + ")"
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.trackColor
    }

    Rectangle {
        id: determinateFill
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: root.indeterminate ? 0 : (root.failed ? root.width : root.width * root.boundedProgress)
        height: parent.height
        radius: height / 2
        color: root.failed ? theme.error : root.accent
        opacity: root.failed ? 0.9 : 0.82
        visible: width > 0

        Behavior on width {
            enabled: !theme.reducedMotion
            NumberAnimation {
                duration: theme.durationMedium
                easing.type: Easing.OutCubic
            }
        }
    }


    Rectangle {
        id: progressGlint
        anchors.verticalCenter: determinateFill.verticalCenter
        x: Math.max(0, determinateFill.width - width)
        width: Math.min(2, root.width * 0.012)
        height: Math.max(1, parent.height - 1)
        radius: height / 2
        color: theme.alpha(theme.brandPaper, 0.88)
        visible: !root.indeterminate && !root.failed && root.boundedProgress > 0.015
    }

    Item {
        width: determinateFill.width
        height: root.height
        clip: true
        visible: !root.indeterminate && !root.failed && root.boundedProgress > 0
        AdditiveLightWaves {
            anchors.fill: parent
            active: root.animate && root.boundedProgress < 1
            opacity: 0.65
        }
    }

    Canvas {
        id: additiveChannels
        width: root.sweepWidth
        height: root.height
        x: (root.width - width) / 2
        visible: root.indeterminate && !root.failed

        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            if (width <= 0 || height <= 0)
                return

            const movingWidth = width
            const x = 0
            const barHeight = Math.max(1, height * 0.62)
            const barY = (height - barHeight) / 2

            // A quiet blue-white body carries the motion; RGB only appears at
            // the leading edge, where the channels meet into a small light glint.
            const beam = context.createLinearGradient(x, 0, x + movingWidth, 0)
            beam.addColorStop(0.0, root.channelRgba(root.accent, 0))
            beam.addColorStop(0.22, root.channelRgba(root.accent, 0.12))
            beam.addColorStop(0.62, root.channelRgba(root.accent, 0.42))
            beam.addColorStop(0.82, root.channelRgba(theme.brandPaper, 0.86))
            beam.addColorStop(1.0, root.channelRgba(theme.brandPaper, 0))
            context.fillStyle = beam
            context.fillRect(x, barY, movingWidth, barHeight)

            const colors = [Qt.rgba(0.851, 0.627, 0.553, 1),
                            Qt.rgba(0.659, 0.839, 0.831, 1),
                            Qt.rgba(0.557, 0.682, 0.804, 1)]
            const headX = x + movingWidth * 0.82
            const centerY = height / 2
            const flareRadius = Math.max(3, height * 1.45)
            const offsets = [-height * 0.48, 0, height * 0.48]

            context.save()
            context.globalCompositeOperation = "lighter"
            for (let index = 0; index < colors.length; ++index) {
                const centerX = headX + offsets[index]
                const flare = context.createRadialGradient(centerX, centerY, 0,
                                                            centerX, centerY, flareRadius)
                flare.addColorStop(0.0, root.channelRgba(colors[index], 0.42))
                flare.addColorStop(0.42, root.channelRgba(colors[index], 0.16))
                flare.addColorStop(1.0, root.channelRgba(colors[index], 0.0))
                context.fillStyle = flare
                context.fillRect(centerX - flareRadius, centerY - flareRadius,
                                 flareRadius * 2, flareRadius * 2)
            }
            context.restore()
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: requestPaint()
    }

    XAnimator {
        target: additiveChannels
        running: root.indeterminate && root.animate && !root.failed && root.width > 0
        loops: Animation.Infinite
        from: -root.sweepWidth
        to: root.width
        duration: 1660
        easing.type: Easing.InOutSine
    }

    onAnimateChanged: additiveChannels.requestPaint()
    onAccentChanged: additiveChannels.requestPaint()
    onProgressChanged: additiveChannels.requestPaint()
    onFailedChanged: additiveChannels.requestPaint()
    onWidthChanged: additiveChannels.requestPaint()
    onHeightChanged: additiveChannels.requestPaint()
}
