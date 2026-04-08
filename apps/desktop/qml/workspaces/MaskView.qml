import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root

    color: "transparent"
    property bool objectPickArmed: false
    property bool objectPickCreateNew: false
    property bool polygonDrawArmed: false
    property bool lassoDrawArmed: false
    property bool cloneSourcePickArmed: false
    property real selectionOutlinePhase: 0

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function clearTransientTools() {
        objectPickArmed = false
        objectPickCreateNew = false
        polygonDrawArmed = false
        lassoDrawArmed = false
        cloneSourcePickArmed = false
    }

    function selectTool(mode) {
        clearTransientTools()
        if (mode === "smart")
            objectPickArmed = true
        else if (mode === "smart-new") {
            objectPickArmed = true
            objectPickCreateNew = true
        } else if (mode === "polygon")
            polygonDrawArmed = true
        else if (mode === "lasso")
            lassoDrawArmed = true
    }

    function activeToolLabel() {
        if (lassoDrawArmed)
            return "Lasso"
        if (cloneSourcePickArmed)
            return "Pick Clone Source"
        if (polygonDrawArmed)
            return "Polygon"
        if (objectPickArmed && objectPickCreateNew)
            return "New Smart Selection"
        if (objectPickArmed)
            return "Smart Select"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "clone")
            return "Clone Stamp"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "heal")
            return "Heal Brush"
        if (editorPreview.activeMaskKind === "polygon")
            return "Polygon Selection"
        if (editorPreview.activeMaskKind === "object")
            return "Smart Selection"
        if (editorPreview.activeMaskKind === "subject")
            return "Subject Selection"
        if (editorPreview.activeMaskKind === "brush" &&
                (editorPreview.activeMaskRetouchMode === "remove" || editorPreview.activeMaskRemoveStrength > 0))
            return "Remove Brush"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskOperation === "subtract")
            return "Refine Subtract"
        if (editorPreview.activeMaskKind === "brush")
            return "Refine Add"
        return "Move"
    }

    function activeMaskHasAdjustments() {
        return Math.abs(editorPreview.activeMaskExposure) > 0.001 ||
               Math.abs(editorPreview.activeMaskContrast) > 0.001 ||
               Math.abs(editorPreview.activeMaskHighlights) > 0.001 ||
               Math.abs(editorPreview.activeMaskShadows) > 0.001 ||
               Math.abs(editorPreview.activeMaskWhites) > 0.001 ||
               Math.abs(editorPreview.activeMaskBlacks) > 0.001 ||
               Math.abs(editorPreview.activeMaskTemperature) > 0.001 ||
               Math.abs(editorPreview.activeMaskTint) > 0.001 ||
               Math.abs(editorPreview.activeMaskVibrance) > 0.001 ||
               Math.abs(editorPreview.activeMaskSaturation) > 0.001 ||
               Math.abs(editorPreview.activeMaskHueShift) > 0.001 ||
               Math.abs(editorPreview.activeMaskSaturationShift) > 0.001 ||
               Math.abs(editorPreview.activeMaskColorizeStrength) > 0.001 ||
               Math.abs(editorPreview.activeMaskRemoveStrength) > 0.001
    }

    function activeMaskSupportsSelectionRefine() {
        return editorPreview.hasActiveMask &&
               (editorPreview.activeMaskKind === "linear" ||
                editorPreview.activeMaskKind === "radial" ||
                editorPreview.activeMaskKind === "brush" ||
                editorPreview.activeMaskKind === "polygon" ||
                editorPreview.activeMaskKind === "subject" ||
                editorPreview.activeMaskKind === "object" ||
                editorPreview.activeMaskKind === "luminance" ||
                editorPreview.activeMaskKind === "color")
    }

    function previewImageX() {
        return maskPreview.x + (maskPreview.width - maskPreview.paintedWidth) / 2
    }

    function previewImageY() {
        return maskPreview.y + (maskPreview.height - maskPreview.paintedHeight) / 2
    }

    function previewPointX(normalizedX) {
        return normalizedX * previewItem.width
    }

    function previewPointY(normalizedY) {
        return normalizedY * previewItem.height
    }

    function maskAccent(mask) {
        if (!mask)
            return "#8ab8d8"
        if (mask.operation === "subtract")
            return "#d36a5e"
        if (mask.operation === "intersect")
            return "#6fa96d"
        if (mask.kind === "brush" && mask.retouchMode === "clone")
            return "#7cb5ff"
        if (mask.kind === "brush" && mask.retouchMode === "heal")
            return "#72cbb2"
        if (mask.kind === "brush" && mask.retouchMode === "remove")
            return "#d98962"
        if (mask.kind === "radial")
            return "#8ab8d8"
        if (mask.kind === "brush")
            return "#cf8f4d"
        if (mask.kind === "polygon")
            return "#7ec97a"
        if (mask.kind === "luminance")
            return "#d8d27a"
        if (mask.kind === "color")
            return "#c77dd8"
        if (mask.kind === "subject")
            return "#5fc9b5"
        if (mask.kind === "object")
            return "#5e95d3"
        return "#c99a52"
    }

    function activeMaskResetFeather() {
        if (editorPreview.activeMaskKind === "radial")
            return 0.25
        if (editorPreview.activeMaskKind === "brush")
            return 0.75
        if (editorPreview.activeMaskKind === "polygon")
            return 0.06
        if (editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object")
            return 0.5
        return 0.22
    }

    function nudgeActiveSelectionExpand(delta) {
        editorPreview.activeMaskSelectionExpand = clamp(
                    editorPreview.activeMaskSelectionExpand + delta,
                    -100,
                    100)
    }

    function nudgeActiveMaskFeather(delta) {
        const minimum = editorPreview.activeMaskKind === "polygon" ? 0.0 : 0.05
        const maximum = editorPreview.activeMaskKind === "polygon" ? 0.2 : 1.0
        editorPreview.activeMaskFeather = clamp(
                    editorPreview.activeMaskFeather + delta,
                    minimum,
                    maximum)
    }

    NumberAnimation on selectionOutlinePhase {
        from: 0
        to: 24
        duration: 900
        loops: Animation.Infinite
        running: editorPreview.hasActiveMask
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Mask Stack"
            subtitle: "Brush, shape, and range masks all live in one ordered selection stack."

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#14171a"
                border.width: 1
                border.color: "#2d3339"
                clip: true

                Image {
                    id: maskPreview
                    anchors.fill: parent
                    anchors.margins: 18
                    source: editorPreview.displayPreviewSource
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    visible: editorPreview.displayPreviewSource.length > 0
                }

                Item {
                    id: previewItem
                    visible: maskPreview.visible
                    x: previewImageX()
                    y: previewImageY()
                    width: maskPreview.paintedWidth
                    height: maskPreview.paintedHeight

                    Repeater {
                        model: editorPreview.maskStack

                        delegate: Item {
                            required property int index
                            required property var modelData

                            readonly property var mask: modelData
                            readonly property bool active: index === editorPreview.activeMaskIndex
                            readonly property color accent: root.maskAccent(mask)
                            readonly property real brushDiameter: Math.max(8, Math.min(previewItem.width, previewItem.height) * (mask.brushSize || 0.08) * 2)

                            anchors.fill: parent
                            visible: !!mask && mask.enabled

                            Item {
                                id: linearOverlay
                                visible: mask.kind === "linear"
                                readonly property real positionValue: mask && mask.position !== undefined ? mask.position : 0.5
                                readonly property real featherValue: mask && mask.feather !== undefined ? mask.feather : 0.22
                                readonly property real rotationValue: mask && mask.rotation !== undefined ? mask.rotation : 0
                                anchors.centerIn: parent
                                width: parent.width * 1.35
                                height: parent.height * 1.35
                                rotation: rotationValue
                                opacity: active ? 0.92 : 0.48

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 10
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: Qt.alpha(accent, 0.34) }
                                        GradientStop {
                                            position: Math.max(0.0, positionValue - featherValue * 0.5)
                                            color: Qt.alpha(accent, 0.14)
                                        }
                                        GradientStop { position: positionValue; color: Qt.rgba(0.95, 0.97, 0.98, 0.72) }
                                        GradientStop {
                                            position: Math.min(1.0, positionValue + featherValue * 0.5)
                                            color: Qt.alpha(accent, 0.06)
                                        }
                                        GradientStop { position: 1.0; color: Qt.alpha(accent, 0.0) }
                                    }
                                }

                                Rectangle {
                                    x: 0
                                    y: Math.max(0, Math.min(linearOverlay.height - 1, linearOverlay.height * positionValue))
                                    width: linearOverlay.width
                                    height: 1
                                    color: "#f4f7fa"
                                    opacity: 0.9
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: active ? Qt.CrossCursor : Qt.PointingHandCursor
                                    onPressed: function(mouse) {
                                        editorPreview.activeMaskIndex = index
                                        if (active)
                                            editorPreview.activeMaskPosition = root.clamp(mouse.y / linearOverlay.height, 0, 1)
                                    }
                                }

                                Rectangle {
                                    visible: active
                                    x: linearOverlay.width / 2 - width / 2
                                    y: linearOverlay.height * positionValue - height / 2
                                    width: 16
                                    height: 16
                                    radius: 8
                                    color: "#f4f7fa"
                                    border.width: 1
                                    border.color: "#101419"

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeVerCursor
                                        onPositionChanged: function(mouse) {
                                            if (!pressed)
                                                return
                                            editorPreview.activeMaskPosition = root.clamp(
                                                        (parent.y + mouse.y + parent.height / 2) / linearOverlay.height,
                                                        0,
                                                        1)
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: active
                                    x: linearOverlay.width / 2 - width / 2
                                    y: -28
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: accent
                                    border.width: 1
                                    border.color: "#101419"

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.CrossCursor
                                        onPositionChanged: function(mouse) {
                                            if (!pressed)
                                                return
                                            const dx = mouse.x + parent.x - linearOverlay.width / 2
                                            const dy = mouse.y + parent.y - linearOverlay.height / 2
                                            editorPreview.activeMaskRotation = Math.atan2(dy, dx) * 180 / Math.PI + 90
                                        }
                                    }
                                }
                            }

                            Item {
                                id: radialOverlay
                                visible: mask.kind === "radial"
                                readonly property real centerXValue: mask && mask.centerX !== undefined ? mask.centerX : 0.5
                                readonly property real centerYValue: mask && mask.centerY !== undefined ? mask.centerY : 0.5
                                readonly property real radiusXValue: mask && mask.radiusX !== undefined ? mask.radiusX : 0.2
                                readonly property real radiusYValue: mask && mask.radiusY !== undefined ? mask.radiusY : 0.2
                                readonly property real featherValue: mask && mask.feather !== undefined ? mask.feather : 0.25
                                readonly property real rotationValue: mask && mask.rotation !== undefined ? mask.rotation : 0
                                x: centerXValue * parent.width - width / 2
                                y: centerYValue * parent.height - height / 2
                                width: Math.max(24, parent.width * radiusXValue * 2)
                                height: Math.max(24, parent.height * radiusYValue * 2)
                                rotation: rotationValue
                                opacity: active ? 0.92 : 0.48

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: active ? Qt.SizeAllCursor : Qt.PointingHandCursor
                                    onPressed: editorPreview.activeMaskIndex = index
                                }

                                Canvas {
                                    id: radialCanvas
                                    anchors.fill: parent

                                    Connections {
                                        target: editorPreview

                                        function onControlsChanged() {
                                            radialCanvas.requestPaint()
                                        }
                                    }

                                    onPaint: {
                                        const ctx = getContext("2d")
                                        const innerScale = Math.max(0.0, 1.0 - featherValue)
                                        ctx.reset()
                                        ctx.strokeStyle = "#f4f7fa"
                                        ctx.lineWidth = active ? 1.8 : 1.2
                                        ctx.fillStyle = Qt.alpha(accent, active ? 0.18 : 0.10)
                                        ctx.beginPath()
                                        ctx.ellipse(width / 2, height / 2, Math.max(1, width / 2 - 1), Math.max(1, height / 2 - 1), 0, 0, Math.PI * 2)
                                        ctx.fill()
                                        ctx.stroke()
                                        ctx.strokeStyle = "rgba(244,247,250,0.45)"
                                        ctx.beginPath()
                                        ctx.ellipse(width / 2, height / 2,
                                                    Math.max(1, (width / 2 - 1) * innerScale),
                                                    Math.max(1, (height / 2 - 1) * innerScale),
                                                    0, 0, Math.PI * 2)
                                        ctx.stroke()
                                    }
                                }

                                Rectangle {
                                    visible: active
                                    x: radialOverlay.width / 2 - width / 2
                                    y: radialOverlay.height / 2 - height / 2
                                    width: 16
                                    height: 16
                                    radius: 8
                                    color: "#f4f7fa"
                                    border.width: 1
                                    border.color: "#101419"

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeAllCursor
                                        onPositionChanged: function(mouse) {
                                            if (!pressed)
                                                return
                                            editorPreview.activeMaskCenterX = root.clamp((radialOverlay.x + parent.x + mouse.x) / previewItem.width, 0, 1)
                                            editorPreview.activeMaskCenterY = root.clamp((radialOverlay.y + parent.y + mouse.y) / previewItem.height, 0, 1)
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: active
                                    x: radialOverlay.width - width / 2
                                    y: radialOverlay.height / 2 - height / 2
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: accent
                                    border.width: 1
                                    border.color: "#101419"

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeHorCursor
                                        onPositionChanged: function(mouse) {
                                            if (!pressed)
                                                return
                                            editorPreview.activeMaskRadiusX = root.clamp((parent.x + mouse.x) / previewItem.width / 2, 0.05, 1.0)
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: active
                                    x: radialOverlay.width / 2 - width / 2
                                    y: radialOverlay.height - height / 2
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: accent
                                    border.width: 1
                                    border.color: "#101419"

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeVerCursor
                                        onPositionChanged: function(mouse) {
                                            if (!pressed)
                                                return
                                            editorPreview.activeMaskRadiusY = root.clamp((parent.y + mouse.y) / previewItem.height / 2, 0.05, 1.0)
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: active
                                    x: radialOverlay.width / 2 - width / 2
                                    y: -30
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: "#f0c46e"
                                    border.width: 1
                                    border.color: "#101419"

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.CrossCursor
                                        onPositionChanged: function(mouse) {
                                            if (!pressed)
                                                return
                                            const dx = mouse.x + parent.x - radialOverlay.width / 2
                                            const dy = mouse.y + parent.y - radialOverlay.height / 2
                                            editorPreview.activeMaskRotation = Math.atan2(dy, dx) * 180 / Math.PI + 90
                                        }
                                    }
                                }
                            }

                            Item {
                                id: brushOverlay
                                visible: mask.kind === "brush"
                                anchors.fill: parent
                                opacity: active ? 0.92 : 0.52

                                Canvas {
                                    id: brushCanvas
                                    anchors.fill: parent
                                    renderStrategy: Canvas.Threaded

                                    function drawBrushPoint(ctx, point) {
                                        const diameter = Math.max(
                                                    8,
                                                    Math.min(previewItem.width, previewItem.height) *
                                                    (point.size || mask.brushSize || 0.08) * 2)
                                        const x = (point.x || 0.5) * brushOverlay.width
                                        const y = (point.y || 0.5) * brushOverlay.height
                                        const radius = diameter / 2
                                        ctx.beginPath()
                                        ctx.arc(x, y, radius, 0, Math.PI * 2, false)
                                        ctx.fillStyle = Qt.alpha(
                                                    accent,
                                                    Math.max(0.12, (point.flow || mask.brushFlow || 1.0) * 0.28))
                                        ctx.fill()
                                        if (active) {
                                            ctx.lineWidth = 1
                                            ctx.strokeStyle = Qt.alpha("#f4f7fa", 0.45)
                                            ctx.stroke()
                                        }
                                    }

                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        const points = mask.brushPoints || []
                                        for (let i = 0; i < points.length; ++i)
                                            drawBrushPoint(ctx, points[i])
                                    }
                                }

                                onVisibleChanged: brushCanvas.requestPaint()
                                onWidthChanged: brushCanvas.requestPaint()
                                onHeightChanged: brushCanvas.requestPaint()

                                Connections {
                                    target: editorPreview

                                    function onControlsChanged() {
                                        if (brushOverlay.visible)
                                            brushCanvas.requestPaint()
                                    }
                                }

                                Item {
                                    visible: active && mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
                                    x: (mask.cloneSourceX || 0.5) * brushOverlay.width - width / 2
                                    y: (mask.cloneSourceY || 0.5) * brushOverlay.height - height / 2
                                    width: 22
                                    height: 22

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        radius: 9
                                        color: "transparent"
                                        border.width: 1.5
                                        border.color: "#f4f7fa"
                                    }

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 2
                                        height: 22
                                        radius: 1
                                        color: "#f4f7fa"
                                    }

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 22
                                        height: 2
                                        radius: 1
                                        color: "#f4f7fa"
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    enabled: active
                                    cursorShape: Qt.CrossCursor
                                    onPressed: function(mouse) {
                                        editorPreview.activeMaskIndex = index
                                        if (mask.retouchMode === "clone" &&
                                                (root.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                            editorPreview.setActiveMaskCloneSource(
                                                        mouse.x / brushOverlay.width,
                                                        mouse.y / brushOverlay.height)
                                            root.cloneSourcePickArmed = false
                                            return
                                        }
                                        editorPreview.addBrushPoint(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height)
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (!pressed || root.cloneSourcePickArmed ||
                                                (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                            return
                                        editorPreview.addBrushPoint(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height)
                                    }
                                }
                            }

                            Item {
                                id: polygonOverlay
                                visible: mask.kind === "polygon"
                                anchors.fill: parent
                                opacity: active ? 0.95 : 0.56

                                Canvas {
                                    id: polygonCanvas
                                    anchors.fill: parent

                                    Connections {
                                        target: editorPreview

                                        function onControlsChanged() {
                                            polygonCanvas.requestPaint()
                                        }
                                    }

                                    onPaint: {
                                        const points = mask.polygonPoints || []
                                        const count = points.length
                                        const ctx = getContext("2d")
                                        ctx.reset()
                                        if (count === 0)
                                            return

                                        ctx.beginPath()
                                        ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                                        for (let i = 1; i < count; ++i) {
                                            ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                                        }
                                        if (count >= 3)
                                            ctx.closePath()

                                        if (count >= 3) {
                                            ctx.fillStyle = Qt.alpha(accent, active ? 0.16 : 0.08)
                                            ctx.fill()
                                        }

                                        ctx.strokeStyle = Qt.alpha("#f4f7fa", active ? 0.88 : 0.55)
                                        ctx.lineWidth = active ? 1.8 : 1.1
                                        if (ctx.setLineDash)
                                            ctx.setLineDash([8, 6])
                                        ctx.lineDashOffset = -root.selectionOutlinePhase
                                        ctx.stroke()
                                    }
                                }

                                Repeater {
                                    model: mask.polygonPoints || []

                                    delegate: Rectangle {
                                        required property var modelData

                                        width: 10
                                        height: 10
                                        radius: 5
                                        x: root.previewPointX(modelData.x || 0.5) - width / 2
                                        y: root.previewPointY(modelData.y || 0.5) - height / 2
                                        color: active ? "#f4f7fa" : accent
                                        border.width: 1
                                        border.color: "#101419"
                                        visible: active
                                    }
                                }
                            }

                            Rectangle {
                                visible: active && (mask.kind === "luminance" || mask.kind === "color" || mask.kind === "subject" || mask.kind === "object")
                                anchors.centerIn: parent
                                radius: 10
                                color: Qt.alpha(accent, 0.18)
                                border.width: 1
                                border.color: Qt.alpha("#f4f7fa", 0.35)
                                implicitWidth: rangeLabel.implicitWidth + 24
                                implicitHeight: rangeLabel.implicitHeight + 14

                                Label {
                                    id: rangeLabel
                                    anchors.centerIn: parent
                                    text: mask.kind === "luminance"
                                          ? "Luminance range active"
                                          : mask.kind === "color"
                                            ? "Color range active"
                                            : mask.kind === "subject"
                                              ? "AI subject mask active"
                                              : "Smart selection active"
                                    color: "#f2f4f6"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }

                    Canvas {
                        id: activeSelectionOutline
                        anchors.fill: parent
                        visible: editorPreview.hasActiveMask &&
                                 editorPreview.activeMaskOutlinePoints.length >= 2 &&
                                 (editorPreview.activeMaskKind === "subject" ||
                                  editorPreview.activeMaskKind === "object" ||
                                  editorPreview.activeMaskKind === "polygon")
                        z: 96

                        Connections {
                            target: editorPreview

                            function onControlsChanged() {
                                activeSelectionOutline.requestPaint()
                            }
                        }

                        onPaint: {
                            const points = editorPreview.activeMaskOutlinePoints
                            const count = points.length
                            const ctx = getContext("2d")
                            ctx.reset()
                            if (count < 2)
                                return

                            const accent = root.maskAccent(editorPreview.maskStack[editorPreview.activeMaskIndex])
                            ctx.beginPath()
                            ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                            for (let i = 1; i < count; ++i) {
                                ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                            }
                            if (count >= 3)
                                ctx.closePath()

                            if (count >= 3) {
                                ctx.fillStyle = Qt.alpha(accent, 0.12)
                                ctx.fill()
                            }

                            ctx.lineWidth = 2
                            if (ctx.setLineDash)
                                ctx.setLineDash([8, 6])
                            ctx.lineDashOffset = -root.selectionOutlinePhase
                            ctx.strokeStyle = "#f4f7fa"
                            ctx.stroke()
                            if (ctx.setLineDash)
                                ctx.setLineDash([8, 6])
                            ctx.lineDashOffset = 12 - root.selectionOutlinePhase
                            ctx.strokeStyle = Qt.alpha("#101419", 0.78)
                            ctx.stroke()
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.objectPickArmed || root.polygonDrawArmed || root.lassoDrawArmed
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        cursorShape: (root.polygonDrawArmed || root.lassoDrawArmed || root.objectPickArmed)
                                     ? Qt.CrossCursor
                                     : Qt.ArrowCursor
                        z: 100

                        onPressed: function(mouse) {
                            if (!root.lassoDrawArmed)
                                return
                            editorPreview.startPolygonSelection()
                            editorPreview.appendActivePolygonPoint(
                                        mouse.x / previewItem.width,
                                        mouse.y / previewItem.height)
                        }

                        onClicked: function(mouse) {
                            if (root.lassoDrawArmed)
                                return
                            if (root.polygonDrawArmed) {
                                editorPreview.appendActivePolygonPoint(
                                            mouse.x / previewItem.width,
                                            mouse.y / previewItem.height)
                            } else {
                                editorPreview.smartSelectObjectAt(
                                            mouse.x / previewItem.width,
                                            mouse.y / previewItem.height,
                                            root.objectPickCreateNew)
                                root.objectPickCreateNew = false
                            }
                        }

                        onPositionChanged: function(mouse) {
                            if (!root.lassoDrawArmed || !pressed)
                                return
                            editorPreview.appendActivePolygonPoint(
                                        mouse.x / previewItem.width,
                                        mouse.y / previewItem.height)
                        }

                        onDoubleClicked: function(mouse) {
                            if (!root.polygonDrawArmed)
                                return
                            editorPreview.appendActivePolygonPoint(
                                        mouse.x / previewItem.width,
                                        mouse.y / previewItem.height)
                            root.polygonDrawArmed = false
                        }

                        onReleased: {
                            if (!root.lassoDrawArmed)
                                return
                            root.lassoDrawArmed = false
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: root.objectPickArmed || root.polygonDrawArmed || root.lassoDrawArmed || root.cloneSourcePickArmed
                        z: 99
                        color: Qt.alpha("#0b0e11", 0.20)
                        border.width: 1
                        border.color: root.cloneSourcePickArmed ? "#7cb5ff"
                                     : root.lassoDrawArmed ? "#7ec97a"
                                     : root.polygonDrawArmed ? "#7ec97a"
                                     : "#5e95d3"
                        radius: 12

                        Label {
                            anchors.centerIn: parent
                            text: root.cloneSourcePickArmed
                                  ? "Click a clean source area for the Clone Stamp."
                                  : root.lassoDrawArmed
                                  ? "Drag to draw a freehand selection."
                                  : root.polygonDrawArmed
                                  ? "Click to place polygon points. Double-click to finish."
                                  : (root.objectPickCreateNew
                                     ? "Click any foreground object to start a new smart selection"
                                     : "Click objects to grow the current smart selection")
                            color: "#f2f4f6"
                            font.family: "Avenir Next"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 18
                    radius: 10
                    visible: editorPreview.displayPreviewSource.length === 0
                    color: "#181c20"
                    border.width: 1
                    border.color: "#2d3339"

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "Preview unavailable for this asset" : "Select an image in Library"
                            color: "#f2f4f6"
                            font.family: "Avenir Next"
                            font.pixelSize: 18
                            font.weight: Font.Medium
                        }

                        Label {
                            text: "Selections remain nondestructive and travel with the current asset."
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        BrandPanel {
            Layout.preferredWidth: 410
            Layout.fillHeight: true
            title: "Selection Inspector"
            subtitle: "Pick a tool from the preview rail, then refine the active selection here."

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 12

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Tools"
                        subtitle: "Select, refine, and retouch tools for the canvas."

                        Label {
                            Layout.fillWidth: true
                            text: "SELECT"
                            color: "#6b7580"
                            font.family: "Menlo"
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.8
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Smart Select"
                                active: root.objectPickArmed && !root.objectPickCreateNew
                                enabled: editorPreview.hasAsset
                                onClicked: root.selectTool(root.objectPickArmed && !root.objectPickCreateNew ? "" : "smart")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "New Selection"
                                active: root.objectPickArmed && root.objectPickCreateNew
                                enabled: editorPreview.hasAsset
                                onClicked: root.selectTool(root.objectPickArmed && root.objectPickCreateNew ? "" : "smart-new")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Lasso"
                                active: root.lassoDrawArmed
                                onClicked: root.selectTool(root.lassoDrawArmed ? "" : "lasso")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Polygon"
                                active: root.polygonDrawArmed
                                onClicked: {
                                    if (!root.polygonDrawArmed && editorPreview.activeMaskKind !== "polygon")
                                        editorPreview.startPolygonSelection()
                                    root.selectTool(root.polygonDrawArmed ? "" : "polygon")
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: !editorPreview.subjectMaskAvailable
                            text: "AI Subject requires macOS 14 or later."
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Masks"
                        subtitle: "Shape and range masks for local adjustments."

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Linear"
                                onClicked: editorPreview.addMask("linear")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Radial"
                                onClicked: editorPreview.addMask("radial")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Luma Range"
                                onClicked: editorPreview.addMask("luminance")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Color Range"
                                onClicked: editorPreview.addMask("color")
                            }

                            ActionButton {
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                text: "AI Subject"
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addSubjectMask()
                                }
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Mask Sets"

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: editorPreview.maskSets

                                delegate: Rectangle {
                                    required property int index
                                    required property var modelData

                                    Layout.fillWidth: true
                                    radius: 10
                                    color: "#171b1f"
                                    border.width: 1
                                    border.color: "#2d3339"
                                    implicitHeight: setColumn.implicitHeight + 20

                                    ColumnLayout {
                                        id: setColumn
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 8

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Rectangle {
                                                width: 10
                                                height: 10
                                                radius: 5
                                                color: modelData.sources.length > 0
                                                       ? root.maskAccent(modelData.sources[0])
                                                       : "#8ab8d8"
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1

                                                Label {
                                                    text: modelData.name
                                                    color: "#f2f4f6"
                                                    font.family: "Avenir Next"
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                }

                                                Label {
                                                    text: modelData.sourceCount.toString() + " sources"
                                                    color: "#8f98a2"
                                                    font.family: "Avenir Next"
                                                    font.pixelSize: 11
                                                }
                                            }
                                        }

                                        Repeater {
                                            model: modelData.sources

                                            delegate: Rectangle {
                                                required property var modelData

                                                Layout.fillWidth: true
                                                radius: 8
                                                color: modelData.flatIndex === editorPreview.activeMaskIndex
                                                       ? "#21272d"
                                                       : "#14181c"
                                                border.width: 1
                                                border.color: modelData.flatIndex === editorPreview.activeMaskIndex
                                                              ? root.maskAccent(modelData)
                                                              : "#242b31"
                                                implicitHeight: 44

                                                MouseArea {
                                                    anchors.fill: parent
                                                    onClicked: editorPreview.activeMaskIndex =
                                                                   (editorPreview.activeMaskIndex === modelData.flatIndex
                                                                    ? -1
                                                                    : modelData.flatIndex)
                                                }

                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    spacing: 8

                                                    Rectangle {
                                                        width: 8
                                                        height: 8
                                                        radius: 4
                                                        color: root.maskAccent(modelData)
                                                    }

                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 1

                                                        Label {
                                                            text: modelData.name
                                                            color: "#f2f4f6"
                                                            font.family: "Avenir Next"
                                                            font.pixelSize: 12
                                                            font.weight: Font.Medium
                                                        }

                                                        Label {
                                                            text: modelData.kind + " · " + modelData.operation
                                                            color: "#8f98a2"
                                                            font.family: "Avenir Next"
                                                            font.pixelSize: 10
                                                        }
                                                    }

                                                    ActionButton {
                                                        text: modelData.enabled ? "On" : "Off"
                                                        active: modelData.enabled
                                                        onClicked: {
                                                            editorPreview.activeMaskIndex = modelData.flatIndex
                                                            editorPreview.activeMaskEnabled = !modelData.enabled
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                visible: editorPreview.maskSetCount === 0
                                text: "No selection masks yet. Add a shape, brush, range selector, or AI mask to start."
                                wrapMode: Text.WordWrap
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 12
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Up"
                                onClicked: editorPreview.moveActiveMask(-1)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Down"
                                onClicked: editorPreview.moveActiveMask(1)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Duplicate"
                                onClicked: editorPreview.duplicateActiveMask()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Delete"
                                onClicked: editorPreview.removeActiveMask()
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Retouch"
                        subtitle: "Heal, clone, and remove live outside mask sets."

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: editorPreview.retouchStack

                                delegate: Rectangle {
                                    required property var modelData

                                    Layout.fillWidth: true
                                    radius: 10
                                    color: modelData.flatIndex === editorPreview.activeMaskIndex ? "#21272d" : "#171b1f"
                                    border.width: 1
                                    border.color: modelData.flatIndex === editorPreview.activeMaskIndex
                                                  ? root.maskAccent(modelData)
                                                  : "#2d3339"
                                    implicitHeight: 50

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: editorPreview.activeMaskIndex =
                                                       (editorPreview.activeMaskIndex === modelData.flatIndex
                                                        ? -1
                                                        : modelData.flatIndex)
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 8

                                        Rectangle {
                                            width: 10
                                            height: 10
                                            radius: 5
                                            color: root.maskAccent(modelData)
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1

                                            Label {
                                                text: modelData.name
                                                color: "#f2f4f6"
                                                font.family: "Avenir Next"
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                            }

                                            Label {
                                                text: modelData.retouchMode.length > 0
                                                      ? modelData.retouchMode + " · " +
                                                        (modelData.committed ? "applied" : "staged")
                                                      : "retouch"
                                                color: "#8f98a2"
                                                font.family: "Avenir Next"
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                visible: editorPreview.retouchCount === 0
                                text: "No retouch strokes yet."
                                wrapMode: Text.WordWrap
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 12
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Active Mask"
                        visible: editorPreview.hasActiveMask

                        ToneTextField {
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskName
                            placeholderText: "Mask name"
                            onEditingFinished: editorPreview.activeMaskName = text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Add"
                                active: editorPreview.activeMaskOperation === "add"
                                onClicked: editorPreview.activeMaskOperation = "add"
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Subtract"
                                active: editorPreview.activeMaskOperation === "subtract"
                                onClicked: editorPreview.activeMaskOperation = "subtract"
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Intersect"
                                active: editorPreview.activeMaskOperation === "intersect"
                                onClicked: editorPreview.activeMaskOperation = "intersect"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskEnabled ? "Enabled" : "Disabled"
                                active: editorPreview.activeMaskEnabled
                                onClicked: editorPreview.activeMaskEnabled = !editorPreview.activeMaskEnabled
                            }

                            Label {
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskKind.length > 0
                                      ? (editorPreview.activeMaskRemoveStrength > 0
                                         ? editorPreview.activeMaskKind + " remove mask"
                                         : editorPreview.activeMaskKind + " mask")
                                      : ""
                                horizontalAlignment: Text.AlignRight
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Quick Actions"
                        subtitle: "Selection tools first, then precise tuning only if needed."
                        visible: root.activeMaskSupportsSelectionRefine()

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Invert"
                                iconName: "invert"
                                active: editorPreview.activeMaskSelectionInverted
                                onClicked: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Reset Edge"
                                onClicked: {
                                    editorPreview.activeMaskSelectionInverted = false
                                    editorPreview.activeMaskSelectionExpand = 0
                                    editorPreview.activeMaskFeather = root.activeMaskResetFeather()
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Grow"
                                iconName: "plus"
                                onClicked: root.nudgeActiveSelectionExpand(12)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Shrink"
                                iconName: "move-back"
                                onClicked: root.nudgeActiveSelectionExpand(-12)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Softer"
                                iconName: "motion"
                                onClicked: root.nudgeActiveMaskFeather(0.04)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Harder"
                                iconName: "snap"
                                onClicked: root.nudgeActiveMaskFeather(-0.04)
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 8
                            rowSpacing: 8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Refine +"
                                iconName: "brush-tool"
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.startSelectionBrush(false)
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Refine -"
                                iconName: "brush-subtract"
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.startSelectionBrush(true)
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Remove"
                                iconName: "erase-tool"
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.startRemoveBrush()
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: true
                            text: editorPreview.activeMaskSelectionInverted
                                  ? "Selection is inverted. Image adjustments and removal now target everything outside the visible boundary."
                                  : "Pick with Smart Select or draw a shape, then use Refine +/- or quick edge actions before touching adjustments."
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Fine Tune"
                        subtitle: "Precise edge controls when the quick actions are not enough."
                        visible: root.activeMaskSupportsSelectionRefine()
                        expandable: true
                        expanded: false

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Expand / Contract"
                            valueText: Math.round(editorPreview.activeMaskSelectionExpand).toString()
                            from: -100
                            to: 100
                            value: editorPreview.activeMaskSelectionExpand
                            onValueDragged: editorPreview.activeMaskSelectionExpand = value
                            onResetRequested: editorPreview.activeMaskSelectionExpand = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Edge Softness"
                            valueText: editorPreview.activeMaskFeather.toFixed(2)
                            from: editorPreview.activeMaskKind === "polygon" ? 0.0 : 0.05
                            to: editorPreview.activeMaskKind === "polygon" ? 0.2 : 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskFeather
                            onValueDragged: editorPreview.activeMaskFeather = value
                            onResetRequested: editorPreview.activeMaskFeather = root.activeMaskResetFeather()
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Adjustment"
                        subtitle: root.activeMaskHasAdjustments()
                                  ? "The active mask is modifying tone, color, or removal."
                                  : "Collapsed until the active mask actually needs image adjustments."
                        expandable: true
                        expanded: root.activeMaskHasAdjustments()
                        visible: editorPreview.hasActiveMask

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Exposure"
                            valueText: editorPreview.activeMaskExposure.toFixed(2) + " EV"
                            from: -2
                            to: 2
                            value: editorPreview.activeMaskExposure
                            onValueDragged: editorPreview.activeMaskExposure = value
                            onResetRequested: editorPreview.activeMaskExposure = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Warmth"
                            valueText: Math.round(editorPreview.activeMaskTemperature).toString()
                            from: -100
                            to: 100
                            value: editorPreview.activeMaskTemperature
                            onValueDragged: editorPreview.activeMaskTemperature = value
                            onResetRequested: editorPreview.activeMaskTemperature = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Hue Shift"
                            valueText: Math.round(editorPreview.activeMaskHueShift * 1.8).toString() + "°"
                            from: -100
                            to: 100
                            value: editorPreview.activeMaskHueShift
                            onValueDragged: editorPreview.activeMaskHueShift = value
                            onResetRequested: editorPreview.activeMaskHueShift = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Saturation"
                            valueText: Math.round(editorPreview.activeMaskSaturationShift).toString()
                            from: -100
                            to: 100
                            value: editorPreview.activeMaskSaturationShift
                            onValueDragged: editorPreview.activeMaskSaturationShift = value
                            onResetRequested: editorPreview.activeMaskSaturationShift = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Tint Hue"
                            valueText: Math.round(editorPreview.activeMaskColorizeHue * 360).toString() + "°"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskColorizeHue
                            onValueDragged: editorPreview.activeMaskColorizeHue = value
                            onResetRequested: editorPreview.activeMaskColorizeHue = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Tint Amount"
                            valueText: Math.round(editorPreview.activeMaskColorizeStrength).toString() + "%"
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskColorizeStrength
                            onValueDragged: editorPreview.activeMaskColorizeStrength = value
                            onResetRequested: editorPreview.activeMaskColorizeStrength = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Remove"
                            valueText: Math.round(editorPreview.activeMaskRemoveStrength).toString() + "%"
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskRemoveStrength
                            onValueDragged: editorPreview.activeMaskRemoveStrength = value
                            onResetRequested: editorPreview.activeMaskRemoveStrength = 0
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Shape"
                        visible: editorPreview.hasActiveMask &&
                                 (editorPreview.activeMaskKind === "linear" || editorPreview.activeMaskKind === "radial")

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskKind === "linear"
                            label: "Position"
                            valueText: editorPreview.activeMaskPosition.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskPosition
                            onValueDragged: editorPreview.activeMaskPosition = value
                            onResetRequested: editorPreview.activeMaskPosition = 0.5
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Rotation"
                            valueText: Math.round(editorPreview.activeMaskRotation).toString() + "°"
                            from: -180
                            to: 180
                            stepSize: 1
                            value: editorPreview.activeMaskRotation
                            onValueDragged: editorPreview.activeMaskRotation = value
                            onResetRequested: editorPreview.activeMaskRotation = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskKind === "radial"
                            label: "Center X"
                            valueText: editorPreview.activeMaskCenterX.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskCenterX
                            onValueDragged: editorPreview.activeMaskCenterX = value
                            onResetRequested: editorPreview.activeMaskCenterX = 0.5
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskKind === "radial"
                            label: "Center Y"
                            valueText: editorPreview.activeMaskCenterY.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskCenterY
                            onValueDragged: editorPreview.activeMaskCenterY = value
                            onResetRequested: editorPreview.activeMaskCenterY = 0.5
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskKind === "radial"
                            label: "Radius X"
                            valueText: editorPreview.activeMaskRadiusX.toFixed(2)
                            from: 0.05
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskRadiusX
                            onValueDragged: editorPreview.activeMaskRadiusX = value
                            onResetRequested: editorPreview.activeMaskRadiusX = 0.25
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskKind === "radial"
                            label: "Radius Y"
                            valueText: editorPreview.activeMaskRadiusY.toFixed(2)
                            from: 0.05
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskRadiusY
                            onValueDragged: editorPreview.activeMaskRadiusY = value
                            onResetRequested: editorPreview.activeMaskRadiusY = 0.25
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Brush"
                        visible: editorPreview.activeMaskKind === "brush"

                        Label {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskRetouchMode === "remove" ||
                                     editorPreview.activeMaskRetouchMode === "heal" ||
                                     editorPreview.activeMaskRetouchMode === "clone"
                            text: editorPreview.activeMaskRetouchMode === "remove"
                                  ? "Paint over unwanted objects to remove them from the image."
                                  : editorPreview.activeMaskRetouchMode === "heal"
                                    ? "Heal Brush uses the retouch fill pass with softer defaults for blemishes and seams."
                                    : (editorPreview.activeMaskCloneSourceSet
                                       ? "Clone Stamp is ready. Paint to stamp from the marked source point."
                                       : "Clone Stamp needs a source point before you paint.")
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: editorPreview.activeMaskRetouchMode === "clone"

                            ActionButton {
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskCloneSourceSet ? "Repick Source" : "Pick Source"
                                iconName: "source"
                                active: root.cloneSourcePickArmed
                                onClicked: root.cloneSourcePickArmed = !root.cloneSourcePickArmed
                            }

                            Label {
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskCloneSourceSet
                                      ? ("Source: " + Math.round(editorPreview.activeMaskCloneSourceX * 100).toString() + ", " +
                                         Math.round(editorPreview.activeMaskCloneSourceY * 100).toString())
                                      : "Source not set"
                                horizontalAlignment: Text.AlignRight
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            visible: editorPreview.activeMaskRetouchMode === "clone"

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Rotate -15°"
                                iconName: "move-back"
                                onClicked: editorPreview.activeMaskCloneRotation = editorPreview.activeMaskCloneRotation - 15
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Rotate +15°"
                                iconName: "move-forward"
                                onClicked: editorPreview.activeMaskCloneRotation = editorPreview.activeMaskCloneRotation + 15
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Scale Down"
                                iconName: "step-back"
                                onClicked: editorPreview.activeMaskCloneScale = editorPreview.activeMaskCloneScale - 0.1
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Scale Up"
                                iconName: "step-forward"
                                onClicked: editorPreview.activeMaskCloneScale = editorPreview.activeMaskCloneScale + 0.1
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Brush Size"
                            valueText: Math.round(editorPreview.activeMaskBrushSize * 100).toString()
                            from: 0.01
                            to: 0.35
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushSize
                            onValueDragged: editorPreview.activeMaskBrushSize = value
                            onResetRequested: editorPreview.activeMaskBrushSize = 0.08
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Brush Flow"
                            valueText: Math.round(editorPreview.activeMaskBrushFlow * 100).toString() + "%"
                            from: 0.05
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushFlow
                            onValueDragged: editorPreview.activeMaskBrushFlow = value
                            onResetRequested: editorPreview.activeMaskBrushFlow = 1.0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskRetouchMode === "clone"
                            label: "Source Scale"
                            valueText: editorPreview.activeMaskCloneScale.toFixed(2) + "x"
                            from: 0.25
                            to: 4.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskCloneScale
                            onValueDragged: editorPreview.activeMaskCloneScale = value
                            onResetRequested: editorPreview.activeMaskCloneScale = 1.0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskRetouchMode === "clone"
                            label: "Source Rotation"
                            valueText: Math.round(editorPreview.activeMaskCloneRotation).toString() + "°"
                            from: -180
                            to: 180
                            stepSize: 1
                            value: editorPreview.activeMaskCloneRotation
                            onValueDragged: editorPreview.activeMaskCloneRotation = value
                            onResetRequested: editorPreview.activeMaskCloneRotation = 0
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskBrushPointCount.toString() + " brush points"
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                            }

                            ActionButton {
                                text: "Clear"
                                onClicked: editorPreview.clearActiveBrushPoints()
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Polygon"
                        visible: editorPreview.activeMaskKind === "polygon"

                        Label {
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskPolygonPointCount >= 3
                                  ? "Polygon selection closed. Toggle Polygon to keep adding points or use Refine +/- to clean edges."
                                  : "Click on the preview to place points. Double-click to finish the selection."
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskPolygonPointCount.toString() + " points"
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                            }

                            ActionButton {
                                text: "Undo Point"
                                onClicked: editorPreview.removeLastActivePolygonPoint()
                            }

                            ActionButton {
                                text: "Clear"
                                onClicked: editorPreview.clearActivePolygonPoints()
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Luminance Range"
                        visible: editorPreview.activeMaskKind === "luminance"

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Minimum"
                            valueText: editorPreview.activeMaskLuminanceMin.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskLuminanceMin
                            onValueDragged: editorPreview.activeMaskLuminanceMin = value
                            onResetRequested: editorPreview.activeMaskLuminanceMin = 0.2
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Maximum"
                            valueText: editorPreview.activeMaskLuminanceMax.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskLuminanceMax
                            onValueDragged: editorPreview.activeMaskLuminanceMax = value
                            onResetRequested: editorPreview.activeMaskLuminanceMax = 0.8
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Feather"
                            valueText: editorPreview.activeMaskRangeFeather.toFixed(2)
                            from: 0
                            to: 0.5
                            stepSize: 0.01
                            value: editorPreview.activeMaskRangeFeather
                            onValueDragged: editorPreview.activeMaskRangeFeather = value
                            onResetRequested: editorPreview.activeMaskRangeFeather = 0.1
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Color Range"
                        visible: editorPreview.activeMaskKind === "color"

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Hue Center"
                            valueText: Math.round(editorPreview.activeMaskHueCenter * 360).toString() + "°"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskHueCenter
                            onValueDragged: editorPreview.activeMaskHueCenter = value
                            onResetRequested: editorPreview.activeMaskHueCenter = 0.08
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Hue Width"
                            valueText: Math.round(editorPreview.activeMaskHueRange * 360).toString() + "°"
                            from: 0.02
                            to: 0.5
                            stepSize: 0.01
                            value: editorPreview.activeMaskHueRange
                            onValueDragged: editorPreview.activeMaskHueRange = value
                            onResetRequested: editorPreview.activeMaskHueRange = 0.12
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Sat Min"
                            valueText: editorPreview.activeMaskSaturationMin.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskSaturationMin
                            onValueDragged: editorPreview.activeMaskSaturationMin = value
                            onResetRequested: editorPreview.activeMaskSaturationMin = 0.1
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Sat Max"
                            valueText: editorPreview.activeMaskSaturationMax.toFixed(2)
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskSaturationMax
                            onValueDragged: editorPreview.activeMaskSaturationMax = value
                            onResetRequested: editorPreview.activeMaskSaturationMax = 1.0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Feather"
                            valueText: editorPreview.activeMaskRangeFeather.toFixed(2)
                            from: 0
                            to: 0.5
                            stepSize: 0.01
                            value: editorPreview.activeMaskRangeFeather
                            onValueDragged: editorPreview.activeMaskRangeFeather = value
                            onResetRequested: editorPreview.activeMaskRangeFeather = 0.08
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "AI Subject"
                        visible: editorPreview.activeMaskKind === "subject" || editorPreview.activeMaskKind === "object"

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Feather"
                            valueText: editorPreview.activeMaskFeather.toFixed(2)
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskFeather
                            onValueDragged: editorPreview.activeMaskFeather = value
                            onResetRequested: editorPreview.activeMaskFeather = 0.5
                        }

                        Label {
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskKind === "object"
                                  ? "Object masks are created by clicking a detected foreground object in the preview."
                                  : "Subject masks are generated automatically from the source image and stay editable as part of the mask stack."
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
