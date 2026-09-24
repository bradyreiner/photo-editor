pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string preset: "off"
    property bool forceTitleSafe: false
    property real customVerticalA: 0.33
    property real customVerticalB: 0.67
    property real customHorizontalA: 0.33
    property real customHorizontalB: 0.67
    property bool hasClip: false
    property real frameScale: 1
    property real frameOffsetX: 0
    property real frameOffsetY: 0
    property real frameRotationDegrees: 0

    readonly property string guideContract: "timeline-viewer-guides:v1|presets:thirds-crosshair-custom-motion-path-crop-bounds-title-safe-platform-ui|geometry:normalized-and-transform-aware|custom:four-session-only-adjustable-guides|motion:real-clip-keyframes|crop:inverse-program-transform|export:program-only-never-rendered|density:off-by-default"
    readonly property bool showThirds: preset === "thirds"
    readonly property bool showCrosshair: preset === "crosshair"
    readonly property bool showCustom: preset === "custom"
    readonly property bool showCropBounds: preset === "crop-bounds" && hasClip
    readonly property bool showTitleSafe: preset === "title-safe" || forceTitleSafe
    readonly property color primaryGuideColor: theme.alpha(theme.textStrong, 0.62)
    readonly property color secondaryGuideColor: theme.alpha(theme.focusRing, 0.76)

    Theme { id: theme }

    enabled: false
    clip: true
    Accessible.ignored: true

    Canvas {
        id: guideCanvas
        objectName: "timelineViewerGuideCanvas"
        anchors.fill: parent

        function schedulePaint() { requestPaint() }

        onWidthChanged: schedulePaint()
        onHeightChanged: schedulePaint()

        Connections {
            target: root
            function onPresetChanged() { guideCanvas.schedulePaint() }
            function onForceTitleSafeChanged() { guideCanvas.schedulePaint() }
            function onCustomVerticalAChanged() { guideCanvas.schedulePaint() }
            function onCustomVerticalBChanged() { guideCanvas.schedulePaint() }
            function onCustomHorizontalAChanged() { guideCanvas.schedulePaint() }
            function onCustomHorizontalBChanged() { guideCanvas.schedulePaint() }
            function onHasClipChanged() { guideCanvas.schedulePaint() }
            function onFrameScaleChanged() { guideCanvas.schedulePaint() }
            function onFrameOffsetXChanged() { guideCanvas.schedulePaint() }
            function onFrameOffsetYChanged() { guideCanvas.schedulePaint() }
            function onFrameRotationDegreesChanged() { guideCanvas.schedulePaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (width <= 0 || height <= 0)
                return

            function line(x1, y1, x2, y2) {
                ctx.beginPath()
                ctx.moveTo(x1, y1)
                ctx.lineTo(x2, y2)
                ctx.stroke()
            }

            ctx.lineWidth = Math.max(1, theme.hairlineWidth)
            ctx.strokeStyle = root.primaryGuideColor
            ctx.setLineDash([])

            if (root.showThirds) {
                line(width / 3, 0, width / 3, height)
                line(width * 2 / 3, 0, width * 2 / 3, height)
                line(0, height / 3, width, height / 3)
                line(0, height * 2 / 3, width, height * 2 / 3)
            }

            if (root.showCrosshair) {
                const arm = Math.max(18, Math.min(width, height) * 0.075)
                line(width / 2 - arm, height / 2, width / 2 + arm, height / 2)
                line(width / 2, height / 2 - arm, width / 2, height / 2 + arm)
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, 2.5, 0, Math.PI * 2)
                ctx.fillStyle = root.primaryGuideColor
                ctx.fill()
            }

            if (root.showCustom) {
                ctx.strokeStyle = root.secondaryGuideColor
                ctx.setLineDash([4, 4])
                line(width * root.customVerticalA, 0,
                     width * root.customVerticalA, height)
                line(width * root.customVerticalB, 0,
                     width * root.customVerticalB, height)
                line(0, height * root.customHorizontalA,
                     width, height * root.customHorizontalA)
                line(0, height * root.customHorizontalB,
                     width, height * root.customHorizontalB)
                ctx.setLineDash([])
            }

            if (root.showTitleSafe) {
                const actionInset = Math.max(8, Math.min(width, height) * 0.05)
                const titleInset = Math.max(14, Math.min(width, height) * 0.10)
                ctx.strokeStyle = theme.alpha(theme.textStrong, 0.44)
                ctx.strokeRect(actionInset, actionInset,
                               width - actionInset * 2, height - actionInset * 2)
                ctx.strokeStyle = theme.alpha(theme.focusRing, 0.70)
                ctx.strokeRect(titleInset, titleInset,
                               width - titleInset * 2, height - titleInset * 2)
            }

            if (root.showCropBounds) {
                const scale = Math.max(1, Number(root.frameScale || 1))
                const cropWidth = width / scale
                const cropHeight = height / scale
                const centerX = width / 2 - Number(root.frameOffsetX || 0) * width * 0.25 / scale
                const centerY = height / 2 - Number(root.frameOffsetY || 0) * height * 0.25 / scale
                ctx.save()
                ctx.translate(centerX, centerY)
                ctx.rotate(-Number(root.frameRotationDegrees || 0) * Math.PI / 180)
                ctx.strokeStyle = root.secondaryGuideColor
                ctx.lineWidth = Math.max(1.25, theme.hairlineWidth)
                ctx.setLineDash([7, 4])
                ctx.strokeRect(-cropWidth / 2, -cropHeight / 2, cropWidth, cropHeight)
                const corner = Math.max(8, Math.min(cropWidth, cropHeight) * 0.04)
                ctx.setLineDash([])
                line(-cropWidth / 2, -cropHeight / 2,
                     -cropWidth / 2 + corner, -cropHeight / 2)
                line(-cropWidth / 2, -cropHeight / 2,
                     -cropWidth / 2, -cropHeight / 2 + corner)
                line(cropWidth / 2, cropHeight / 2,
                     cropWidth / 2 - corner, cropHeight / 2)
                line(cropWidth / 2, cropHeight / 2,
                     cropWidth / 2, cropHeight / 2 - corner)
                ctx.restore()
            }
        }
    }

    Rectangle {
        objectName: "timelineViewerGuideLabel"
        visible: root.preset !== "off" && root.preset !== "motion-path"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: theme.space8
        implicitWidth: guideLabel.implicitWidth + theme.space12
        implicitHeight: guideLabel.implicitHeight + theme.space6
        radius: theme.radius4
        color: theme.alpha(theme.ink950, 0.72)

        Text {
            id: guideLabel
            anchors.centerIn: parent
            text: root.preset === "thirds" ? "THIRDS"
                  : root.preset === "crosshair" ? "CENTER"
                  : root.preset === "custom" ? "CUSTOM GUIDES"
                  : root.preset === "crop-bounds" ? "OUTPUT CROP"
                  : root.preset === "title-safe" ? "TITLE / ACTION SAFE" : "GUIDES"
            color: theme.alpha(theme.textStrong, 0.80)
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: Font.DemiBold
        }
    }
}
