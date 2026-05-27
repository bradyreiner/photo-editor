import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import PhotoEditor
import "../components"

Rectangle {
    id: root

    color: "transparent"
    property bool objectPickArmed: false
    property bool objectPickCreateNew: false
    property string objectPickMode: "selection"
    property bool polygonDrawArmed: false
    property bool lassoDrawArmed: false
    property bool cloneSourcePickArmed: false
    property bool tabletBrushStrokeActive: false
    property var smartSelectStrokePoints: []
    property bool smartSelectStrokeActive: false
    property real selectionOutlinePhase: 0

    FileDialog {
        id: alphaMaskFileDialog
        title: "Import Alpha Mask"
        nameFilters: ["Image masks (*.png *.tif *.tiff *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: {
            root.clearTransientTools()
            editorPreview.addAlphaImageMask(selectedFile)
        }
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function pointerPressure(mouse) {
        if (!mouse || mouse.pressure === undefined || mouse.pressure <= 0)
            return 1.0
        return clamp(mouse.pressure, 0.0, 1.0)
    }

    function pointerTilt(mouse, axisName) {
        if (!mouse || mouse[axisName] === undefined)
            return 0.0
        const rawTilt = mouse[axisName]
        if (Math.abs(rawTilt) <= 1.0)
            return clamp(rawTilt, -1.0, 1.0)
        return clamp(rawTilt / 60.0, -1.0, 1.0)
    }

    function clearTransientTools() {
        objectPickArmed = false
        objectPickCreateNew = false
        objectPickMode = "selection"
        polygonDrawArmed = false
        lassoDrawArmed = false
        cloneSourcePickArmed = false
        smartSelectStrokePoints = []
        smartSelectStrokeActive = false
    }

    function normalizedStrokePoint(mouseX, mouseY) {
        return {
            "x": clamp(mouseX / Math.max(1, previewItem.width), 0.0, 1.0),
            "y": clamp(mouseY / Math.max(1, previewItem.height), 0.0, 1.0)
        }
    }

    function appendSmartSelectStrokePoint(mouseX, mouseY, force) {
        const point = normalizedStrokePoint(mouseX, mouseY)
        const points = smartSelectStrokePoints
        if (!force && points.length > 0) {
            const lastPoint = points[points.length - 1]
            const deltaX = (point.x - lastPoint.x) * previewItem.width
            const deltaY = (point.y - lastPoint.y) * previewItem.height
            if ((deltaX * deltaX) + (deltaY * deltaY) < 81)
                return false
        }
        smartSelectStrokePoints = points.concat([point])
        return true
    }

    function beginSmartSelectStroke(mouseX, mouseY) {
        smartSelectStrokeActive = true
        smartSelectStrokePoints = []
        appendSmartSelectStrokePoint(mouseX, mouseY, true)
        editorPreview.scheduleSmartSelectPreviewPath(smartSelectStrokePoints)
    }

    function updateSmartSelectStroke(mouseX, mouseY) {
        if (!smartSelectStrokeActive)
            return
        if (appendSmartSelectStrokePoint(mouseX, mouseY, false))
            editorPreview.scheduleSmartSelectPreviewPath(smartSelectStrokePoints)
    }

    function commitSmartSelectStroke() {
        if (!smartSelectStrokeActive || smartSelectStrokePoints.length === 0) {
            smartSelectStrokePoints = []
            smartSelectStrokeActive = false
            return
        }
        editorPreview.pickMaskPath(smartSelectStrokePoints, objectPickMode, objectPickCreateNew)
        objectPickCreateNew = false
        smartSelectStrokePoints = []
        smartSelectStrokeActive = false
    }

    function selectTool(mode) {
        clearTransientTools()
        if (mode === "smart") {
            objectPickArmed = true
            objectPickMode = "selection"
        } else if (mode === "smart-subtract") {
            objectPickArmed = true
            objectPickMode = "selection-subtract"
        } else if (mode === "smart-new") {
            objectPickArmed = true
            objectPickCreateNew = true
            objectPickMode = "selection"
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
        if (objectPickArmed && objectPickMode === "selection-subtract")
            return "Subtract Smart Selection"
        if (objectPickArmed)
            return "Smart Select"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "clone")
            return "Clone Stamp"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "heal")
            return "Heal Brush"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "patch")
            return "Patch Brush"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "dust")
            return "Dust Removal"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "red-eye")
            return "Red-Eye Removal"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "skin-cleanup")
            return "Skin Cleanup"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode === "frequency-smoothing")
            return "Frequency Smoothing"
        if (editorPreview.activeMaskKind === "polygon")
            return "Polygon Selection"
        if (editorPreview.activeMaskKind === "lasso")
            return "Lasso Selection"
        if (editorPreview.activeMaskKind === "depth")
            return "Depth Range"
        if (editorPreview.activeMaskKind === "alpha")
            return "Alpha Image Selection"
        if (isAiMaskKind(editorPreview.activeMaskKind))
            return aiMaskLabel(editorPreview.activeMaskKind) + " Selection"
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
                editorPreview.activeMaskKind === "alpha" ||
                isAiMaskKind(editorPreview.activeMaskKind) ||
                editorPreview.activeMaskKind === "luminance" ||
                editorPreview.activeMaskKind === "color")
    }

    function isAiMaskKind(kind) {
        return kind === "subject" || kind === "sky" || kind === "background" ||
               kind === "people" || kind === "face" || kind === "hair" ||
               kind === "clothing" || kind === "object"
    }

    function aiMaskLabel(kind) {
        if (kind === "sky")
            return "Sky"
        if (kind === "background")
            return "Background"
        if (kind === "people")
            return "People"
        if (kind === "face")
            return "Face"
        if (kind === "hair")
            return "Hair"
        if (kind === "clothing")
            return "Clothing"
        if (kind === "object")
            return "Object"
        return "Subject"
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
        if (mask.toolRole === "retouch" && mask.retouchMode === "dust")
            return "#c2b36a"
        if (mask.toolRole === "retouch" && mask.retouchMode === "red-eye")
            return "#d36a5e"
        if (mask.toolRole === "retouch" && mask.retouchMode === "skin-cleanup")
            return "#d7a08c"
        if (mask.toolRole === "retouch" && mask.retouchMode === "frequency-smoothing")
            return "#c994d8"
        if (mask.kind === "brush" && mask.retouchMode === "remove")
            return "#d98962"
        if (mask.kind === "radial")
            return "#8ab8d8"
        if (mask.kind === "brush")
            return "#cf8f4d"
        if (mask.kind === "polygon" || mask.kind === "lasso")
            return "#7ec97a"
        if (mask.kind === "luminance")
            return "#d8d27a"
        if (mask.kind === "color")
            return "#c77dd8"
        if (mask.kind === "depth")
            return "#72b7a6"
        if (mask.kind === "alpha")
            return "#a3b35f"
        if (mask.kind === "subject")
            return "#5fc9b5"
        if (mask.kind === "sky")
            return "#6db7e8"
        if (mask.kind === "background")
            return "#9b8fdc"
        if (mask.kind === "people" || mask.kind === "face")
            return "#d889a8"
        if (mask.kind === "hair" || mask.kind === "clothing")
            return "#c4a35f"
        if (mask.kind === "object")
            return "#5e95d3"
        return "#c99a52"
    }

    function activeMaskResetFeather() {
        if (editorPreview.activeMaskKind === "radial")
            return 0.25
        if (editorPreview.activeMaskKind === "brush")
            return 0.75
        if (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso")
            return 0.06
        if (editorPreview.activeMaskKind === "alpha" || isAiMaskKind(editorPreview.activeMaskKind))
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
        const minimum = (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.0 : 0.05
        const maximum = (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.2 : 1.0
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

                                TabletInputArea {
                                    anchors.fill: parent
                                    enabled: active
                                    z: 4

                                    onTabletPressed: function(x, y, pressure, tiltX, tiltY, eraser) {
                                        editorPreview.activeMaskIndex = index
                                        if (mask.retouchMode === "clone" &&
                                                (root.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                            editorPreview.setActiveMaskCloneSource(
                                                        x / brushOverlay.width,
                                                        y / brushOverlay.height)
                                            root.cloneSourcePickArmed = false
                                            root.tabletBrushStrokeActive = false
                                            return
                                        }
                                        editorPreview.beginActiveBrushStroke()
                                        root.tabletBrushStrokeActive = true
                                        editorPreview.addBrushPointWithTabletEraser(
                                                    x / brushOverlay.width,
                                                    y / brushOverlay.height,
                                                    pressure,
                                                    tiltX,
                                                    tiltY,
                                                    eraser)
                                    }

                                    onTabletMoved: function(x, y, pressure, tiltX, tiltY, eraser) {
                                        if (!root.tabletBrushStrokeActive || root.cloneSourcePickArmed ||
                                                (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                            return
                                        editorPreview.addBrushPointWithTabletEraser(
                                                    x / brushOverlay.width,
                                                    y / brushOverlay.height,
                                                    pressure,
                                                    tiltX,
                                                    tiltY,
                                                    eraser)
                                    }

                                    onTabletReleased: function(x, y, pressure, tiltX, tiltY, eraser) {
                                        if (root.tabletBrushStrokeActive)
                                            editorPreview.endActiveBrushStroke()
                                        root.tabletBrushStrokeActive = false
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
                                        editorPreview.addBrushPointWithTablet(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height, root.pointerPressure(mouse), root.pointerTilt(mouse, "xTilt"), root.pointerTilt(mouse, "yTilt"))
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (!pressed || root.cloneSourcePickArmed ||
                                                (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                            return
                                        editorPreview.addBrushPointWithTablet(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height, root.pointerPressure(mouse), root.pointerTilt(mouse, "xTilt"), root.pointerTilt(mouse, "yTilt"))
                                    }
                                }
                            }

                            Item {
                                id: polygonOverlay
                                visible: mask.kind === "polygon" || mask.kind === "lasso"
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
                                visible: active && (mask.kind === "luminance" || mask.kind === "color" || mask.kind === "depth" || mask.kind === "alpha" || root.isAiMaskKind(mask.kind))
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
                                            : mask.kind === "depth"
                                              ? "Depth range active"
                                              : mask.kind === "alpha"
                                                ? "Alpha image mask active"
                                            : "AI " + root.aiMaskLabel(mask.kind).toLowerCase() + " mask active"
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
                                 (editorPreview.activeMaskKind === "alpha" ||
                                  root.isAiMaskKind(editorPreview.activeMaskKind) ||
                                   editorPreview.activeMaskKind === "polygon" ||
                                   editorPreview.activeMaskKind === "lasso")
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
                            if (root.objectPickArmed) {
                                root.beginSmartSelectStroke(mouse.x, mouse.y)
                                return
                            }
                            if (!root.lassoDrawArmed)
                                return
                            editorPreview.startLassoSelection()
                            editorPreview.appendActivePolygonPoint(
                                        mouse.x / previewItem.width,
                                        mouse.y / previewItem.height)
                        }

                        onClicked: function(mouse) {
                            if (root.objectPickArmed)
                                return
                            if (root.lassoDrawArmed)
                                return
                            if (root.polygonDrawArmed) {
                                editorPreview.appendActivePolygonPoint(
                                            mouse.x / previewItem.width,
                                            mouse.y / previewItem.height)
                            }
                        }

                        onPositionChanged: function(mouse) {
                            if (root.objectPickArmed) {
                                if (pressed)
                                    root.updateSmartSelectStroke(mouse.x, mouse.y)
                                else
                                    editorPreview.scheduleSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
                            }
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

                        onReleased: function(mouse) {
                            if (root.objectPickArmed) {
                                root.commitSmartSelectStroke()
                                return
                            }
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
                                text: "Smart +"
                                active: root.objectPickArmed &&
                                        root.objectPickMode === "selection" &&
                                        !root.objectPickCreateNew
                                enabled: editorPreview.hasAsset
                                onClicked: root.selectTool(root.objectPickArmed &&
                                                           root.objectPickMode === "selection" &&
                                                           !root.objectPickCreateNew ? "" : "smart")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Smart -"
                                active: root.objectPickArmed &&
                                        root.objectPickMode === "selection-subtract"
                                enabled: editorPreview.hasAsset && editorPreview.hasActiveMask
                                onClicked: root.selectTool(root.objectPickArmed &&
                                                           root.objectPickMode === "selection-subtract" ? "" : "smart-subtract")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "New Selection"
                                active: root.objectPickArmed &&
                                        root.objectPickMode === "selection" &&
                                        root.objectPickCreateNew
                                enabled: editorPreview.hasAsset
                                onClicked: root.selectTool(root.objectPickArmed &&
                                                           root.objectPickMode === "selection" &&
                                                           root.objectPickCreateNew ? "" : "smart-new")
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
                                Layout.fillWidth: true
                                text: "Depth Range"
                                onClicked: editorPreview.addMask("depth")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Alpha Image"
                                onClicked: alphaMaskFileDialog.open()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Subject"
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("subject")
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Sky"
                                enabled: editorPreview.hasAsset
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("sky")
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Background"
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("background")
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "People"
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("people")
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Face"
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("face")
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Hair"
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("hair")
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Clothing"
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("clothing")
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

                            ConfirmActionButton {
                                Layout.fillWidth: true
                                text: "Delete"
                                helperText: "Delete the active mask"
                                onConfirmed: editorPreview.removeActiveMask()
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Retouch"
                        subtitle: "Heal, clone, skin, red-eye, dust, and remove live outside mask sets."

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

                                        RowLayout {
                                            spacing: 4

                                            ActionButton {
                                                text: "Rename"
                                                compact: true
                                                onClicked: {
                                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                                    activeMaskNameField.forceActiveFocus()
                                                    activeMaskNameField.selectAll()
                                                }
                                            }

                                            ActionButton {
                                                text: modelData.enabled ? "Off" : "On"
                                                compact: true
                                                accent: modelData.enabled ? "#d98962" : root.maskAccent(modelData)
                                                onClicked: {
                                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                                    editorPreview.activeMaskEnabled = !modelData.enabled
                                                }
                                            }

                                            ActionButton {
                                                text: "Up"
                                                compact: true
                                                enabled: modelData.flatIndex > 0
                                                onClicked: {
                                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                                    editorPreview.moveActiveMask(-1)
                                                }
                                            }

                                            ActionButton {
                                                text: "Down"
                                                compact: true
                                                enabled: modelData.flatIndex < editorPreview.maskCount - 1
                                                onClicked: {
                                                    editorPreview.activeMaskIndex = modelData.flatIndex
                                                    editorPreview.moveActiveMask(1)
                                                }
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

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                visible: editorPreview.retouchCount > 0

                                ActionButton {
                                    Layout.fillWidth: true
                                    text: "Before"
                                    compact: true
                                    accent: "#8fb7d8"
                                    tone: active ? "primary" : "secondary"
                                    active: editorPreview.retouchBeforeAfterEnabled
                                    onClicked: editorPreview.retouchBeforeAfterEnabled = true
                                }

                                ActionButton {
                                    Layout.fillWidth: true
                                    text: "After"
                                    compact: true
                                    accent: "#8fb7d8"
                                    tone: active ? "primary" : "secondary"
                                    active: !editorPreview.retouchBeforeAfterEnabled
                                    onClicked: editorPreview.retouchBeforeAfterEnabled = false
                                }
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Active Mask"
                        visible: editorPreview.hasActiveMask

                        ToneTextField {
                            id: activeMaskNameField
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskName
                            placeholderText: "Mask name"
                            onEditingFinished: editorPreview.activeMaskName = text
                        }

                        ToneTextField {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskToolRole === "retouch" &&
                                     (editorPreview.activeMaskRetouchMode === "remove" ||
                                      editorPreview.activeMaskRetouchMode === "heal" ||
                                      editorPreview.activeMaskRetouchMode === "patch")
                            text: editorPreview.inpaintPrompt
                            placeholderText: "Optional inpaint prompt"
                            enabled: !editorPreview.activeMaskCommitted
                            onEditingFinished: editorPreview.inpaintPrompt = text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: editorPreview.activeMaskToolRole === "retouch" &&
                                     (editorPreview.activeMaskRetouchMode === "remove" ||
                                      editorPreview.activeMaskRetouchMode === "heal" ||
                                      editorPreview.activeMaskRetouchMode === "patch")

                            ToneTextField {
                                Layout.fillWidth: true
                                text: editorPreview.inpaintSeed.toString()
                                placeholderText: "Seed"
                                enabled: !editorPreview.activeMaskCommitted
                                onEditingFinished: editorPreview.inpaintSeed = Math.max(1, parseInt(text) || 1)
                            }

                            ActionButton {
                                Layout.preferredWidth: 112
                                text: "Regenerate"
                                enabled: !editorPreview.activeMaskCommitted
                                onClicked: editorPreview.regenerateInpaintSeed()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: editorPreview.activeMaskToolRole === "retouch" &&
                                     (editorPreview.activeMaskRetouchMode === "remove" ||
                                      editorPreview.activeMaskRetouchMode === "heal" ||
                                      editorPreview.activeMaskRetouchMode === "patch")

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Preview"
                                active: editorPreview.inpaintQualityMode === "preview"
                                enabled: !editorPreview.activeMaskCommitted
                                onClicked: editorPreview.inpaintQualityMode = "preview"
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Final"
                                active: editorPreview.inpaintQualityMode === "final"
                                enabled: !editorPreview.activeMaskCommitted
                                onClicked: editorPreview.inpaintQualityMode = "final"
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskToolRole === "retouch" &&
                                     (editorPreview.activeMaskRetouchMode === "remove" ||
                                      editorPreview.activeMaskRetouchMode === "heal" ||
                                      editorPreview.activeMaskRetouchMode === "patch")
                            label: "INPAINT BLEND"
                            from: 0
                            to: 100
                            stepSize: 1
                            defaultValue: 100
                            value: Math.round(editorPreview.inpaintBlendAmount * 100)
                            valueText: Math.round(editorPreview.inpaintBlendAmount * 100) + "%"
                            accent: root.activeMaskAccent
                            onValueDragged: function(value) { editorPreview.inpaintBlendAmount = value / 100 }
                            onResetRequested: editorPreview.inpaintBlendAmount = 1.0
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskToolRole === "retouch" &&
                                     (editorPreview.activeMaskRetouchMode === "remove" ||
                                      editorPreview.activeMaskRetouchMode === "heal" ||
                                      editorPreview.activeMaskRetouchMode === "patch") &&
                                     editorPreview.activeInpaintPatchBounds.width > 0
                            text: editorPreview.inpaintPatchDiagnosticsEnabled ? "Hide Patch Bounds" : "Show Patch Bounds"
                            active: editorPreview.inpaintPatchDiagnosticsEnabled
                            accent: "#f2c56b"
                            onClicked: editorPreview.inpaintPatchDiagnosticsEnabled = !editorPreview.inpaintPatchDiagnosticsEnabled
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
                                    editorPreview.activeMaskSelectionSmooth = 0
                                    editorPreview.activeMaskSelectionFineEdge = 0
                                    editorPreview.activeMaskSelectionEdgeContrast = 0
                                    editorPreview.activeMaskSelectionDecontaminate = 0
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
                                    if (root.isAiMaskKind(editorPreview.activeMaskKind))
                                        editorPreview.startAIMaskRefineBrush(false)
                                    else
                                        editorPreview.startSelectionBrush(false)
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Refine -"
                                iconName: "brush-subtract"
                                onClicked: {
                                    root.clearTransientTools()
                                    if (root.isAiMaskKind(editorPreview.activeMaskKind))
                                        editorPreview.startAIMaskRefineBrush(true)
                                    else
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
                            onValueDragged: function(value) { editorPreview.activeMaskSelectionExpand = value }
                            onResetRequested: editorPreview.activeMaskSelectionExpand = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Edge Softness"
                            valueText: editorPreview.activeMaskFeather.toFixed(2)
                            from: (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.0 : 0.05
                            to: (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.2 : 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskFeather
                            onValueDragged: function(value) { editorPreview.activeMaskFeather = value }
                            onResetRequested: editorPreview.activeMaskFeather = root.activeMaskResetFeather()
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Smooth"
                            valueText: Math.round(editorPreview.activeMaskSelectionSmooth).toString()
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskSelectionSmooth
                            onValueDragged: function(value) { editorPreview.activeMaskSelectionSmooth = value }
                            onResetRequested: editorPreview.activeMaskSelectionSmooth = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Fine Edge"
                            valueText: Math.round(editorPreview.activeMaskSelectionFineEdge).toString()
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskSelectionFineEdge
                            onValueDragged: function(value) { editorPreview.activeMaskSelectionFineEdge = value }
                            onResetRequested: editorPreview.activeMaskSelectionFineEdge = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Edge Contrast"
                            valueText: Math.round(editorPreview.activeMaskSelectionEdgeContrast).toString()
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskSelectionEdgeContrast
                            onValueDragged: function(value) { editorPreview.activeMaskSelectionEdgeContrast = value }
                            onResetRequested: editorPreview.activeMaskSelectionEdgeContrast = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Decontaminate"
                            valueText: Math.round(editorPreview.activeMaskSelectionDecontaminate).toString()
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskSelectionDecontaminate
                            onValueDragged: function(value) { editorPreview.activeMaskSelectionDecontaminate = value }
                            onResetRequested: editorPreview.activeMaskSelectionDecontaminate = 0
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
                            onValueDragged: function(value) { editorPreview.activeMaskExposure = value }
                            onResetRequested: editorPreview.activeMaskExposure = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Warmth"
                            valueText: Math.round(editorPreview.activeMaskTemperature).toString()
                            from: -100
                            to: 100
                            value: editorPreview.activeMaskTemperature
                            onValueDragged: function(value) { editorPreview.activeMaskTemperature = value }
                            onResetRequested: editorPreview.activeMaskTemperature = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Hue Shift"
                            valueText: Math.round(editorPreview.activeMaskHueShift * 1.8).toString() + "°"
                            from: -100
                            to: 100
                            value: editorPreview.activeMaskHueShift
                            onValueDragged: function(value) { editorPreview.activeMaskHueShift = value }
                            onResetRequested: editorPreview.activeMaskHueShift = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Saturation"
                            valueText: Math.round(editorPreview.activeMaskSaturationShift).toString()
                            from: -100
                            to: 100
                            value: editorPreview.activeMaskSaturationShift
                            onValueDragged: function(value) { editorPreview.activeMaskSaturationShift = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskColorizeHue = value }
                            onResetRequested: editorPreview.activeMaskColorizeHue = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Tint Amount"
                            valueText: Math.round(editorPreview.activeMaskColorizeStrength).toString() + "%"
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskColorizeStrength
                            onValueDragged: function(value) { editorPreview.activeMaskColorizeStrength = value }
                            onResetRequested: editorPreview.activeMaskColorizeStrength = 0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Remove"
                            valueText: Math.round(editorPreview.activeMaskRemoveStrength).toString() + "%"
                            from: 0
                            to: 100
                            value: editorPreview.activeMaskRemoveStrength
                            onValueDragged: function(value) { editorPreview.activeMaskRemoveStrength = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskPosition = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskRotation = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskCenterX = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskCenterY = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskRadiusX = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskRadiusY = value }
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
                                     editorPreview.activeMaskRetouchMode === "patch" ||
                                     editorPreview.activeMaskRetouchMode === "clone"
                            text: editorPreview.activeMaskRetouchMode === "remove"
                                  ? "Paint over unwanted objects to remove them from the image."
                                  : editorPreview.activeMaskRetouchMode === "heal"
                                    ? "Heal Brush uses the retouch fill pass with softer defaults for blemishes and seams."
                                    : editorPreview.activeMaskRetouchMode === "patch"
                                      ? "Patch Brush fills a painted repair area with an auto-masked retouch pass."
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
                            visible: editorPreview.activeMaskKind === "brush"

                            Repeater {
                                model: editorPreview.brushPresetNames

                                ActionButton {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: modelData
                                    compact: true
                                    active: editorPreview.activeMaskBrushPreset === modelData
                                    accent: editorPreview.hasActiveMask ? root.maskAccent(editorPreview.maskStack[editorPreview.activeMaskIndex]) : "#8ab8d8"
                                    onClicked: editorPreview.applyActiveMaskBrushPreset(modelData)
                                }
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
                            onValueDragged: function(value) { editorPreview.activeMaskBrushSize = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskBrushFlow = value }
                            onResetRequested: editorPreview.activeMaskBrushFlow = 1.0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Brush Hardness"
                            valueText: Math.round(editorPreview.activeMaskBrushHardness * 100).toString() + "%"
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushHardness
                            onValueDragged: function(value) { editorPreview.activeMaskBrushHardness = value }
                            onResetRequested: editorPreview.activeMaskBrushHardness = 0.25
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Brush Spacing"
                            valueText: Math.round(editorPreview.activeMaskBrushSpacing * 100).toString() + "%"
                            from: 0.05
                            to: 2.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushSpacing
                            onValueDragged: function(value) { editorPreview.activeMaskBrushSpacing = value }
                            onResetRequested: editorPreview.activeMaskBrushSpacing = 0.75
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Stroke Smoothing"
                            valueText: Math.round(editorPreview.activeMaskBrushSmoothing * 100).toString() + "%"
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushSmoothing
                            onValueDragged: function(value) { editorPreview.activeMaskBrushSmoothing = value }
                            onResetRequested: editorPreview.activeMaskBrushSmoothing = 0.0
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskBrushBuildup ? "Flow Buildup" : "Single Pass Flow"
                            active: editorPreview.activeMaskBrushBuildup
                            compact: true
                            onClicked: editorPreview.activeMaskBrushBuildup = !editorPreview.activeMaskBrushBuildup
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskBrushEraseMode ? "Erase Mode" : "Paint Mode"
                            active: editorPreview.activeMaskBrushEraseMode
                            accent: editorPreview.activeMaskBrushEraseMode
                                    ? "#d98962"
                                    : (editorPreview.hasActiveMask ? root.maskAccent(editorPreview.maskStack[editorPreview.activeMaskIndex]) : "#8ab8d8")
                            compact: true
                            onClicked: editorPreview.activeMaskBrushEraseMode = !editorPreview.activeMaskBrushEraseMode
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskBrushAutoMask ? "Auto Mask" : "Freehand Mask"
                            active: editorPreview.activeMaskBrushAutoMask
                            compact: true
                            onClicked: editorPreview.activeMaskBrushAutoMask = !editorPreview.activeMaskBrushAutoMask
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskBrushAutoMask
                            label: "Auto-Mask Range"
                            valueText: Math.round(editorPreview.activeMaskBrushAutoMaskTolerance * 100).toString() + "%"
                            from: 0.02
                            to: 0.75
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushAutoMaskTolerance
                            onValueDragged: function(value) { editorPreview.activeMaskBrushAutoMaskTolerance = value }
                            onResetRequested: editorPreview.activeMaskBrushAutoMaskTolerance = 0.18
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
                            onValueDragged: function(value) { editorPreview.activeMaskCloneScale = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskCloneRotation = value }
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
                        title: editorPreview.activeMaskKind === "lasso" ? "Lasso" : "Polygon"
                        visible: editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso"

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
                            onValueDragged: function(value) { editorPreview.activeMaskLuminanceMin = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskLuminanceMax = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskRangeFeather = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskHueCenter = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskHueRange = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskSaturationMin = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskSaturationMax = value }
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
                            onValueDragged: function(value) { editorPreview.activeMaskRangeFeather = value }
                            onResetRequested: editorPreview.activeMaskRangeFeather = 0.08
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "AI Refine"
                        visible: root.isAiMaskKind(editorPreview.activeMaskKind)

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Feather"
                            valueText: editorPreview.activeMaskFeather.toFixed(2)
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskFeather
                            onValueDragged: function(value) { editorPreview.activeMaskFeather = value }
                            onResetRequested: editorPreview.activeMaskFeather = 0.5
                        }

                        Label {
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskKind === "object"
                                  ? "Object masks are created by clicking a detected foreground object in the preview."
                                  : "AI " + root.aiMaskLabel(editorPreview.activeMaskKind).toLowerCase() + " masks are generated automatically from the source image and stay editable as part of the mask stack."
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
