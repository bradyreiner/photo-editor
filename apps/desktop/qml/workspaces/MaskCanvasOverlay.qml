import QtQuick
import QtQuick.Controls
import PhotoEditor
import "../components"

Item {
    id: root

    required property var editRoot
    Theme { id: theme }
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property real hoverX: 0
    property real hoverY: 0
    property bool hoverActive: false
    property bool brushResizeActive: false
    property real brushResizeStartX: 0
    property real brushResizeStartY: 0
    property real brushResizeStartSize: 0.08
    property real brushResizeStartFlow: 1.0
    property var smartSelectStrokePoints: []
    property bool smartSelectStrokeActive: false
    property bool tabletBrushStrokeActive: false
    property var pendingCanvasPaints: []

    visible: imageWidth > 0 && imageHeight > 0

    Timer {
        id: canvasPaintTimer
        interval: 16
        repeat: false
        onTriggered: {
            const canvases = root.pendingCanvasPaints
            root.pendingCanvasPaints = []
            for (let index = 0; index < canvases.length; ++index) {
                const canvas = canvases[index]
                if (canvas && canvas.visible)
                    canvas.requestPaint()
            }
        }
    }

    function requestCanvasPaint(canvas) {
        if (!canvas || !canvas.visible)
            return
        if (pendingCanvasPaints.indexOf(canvas) >= 0)
            return
        pendingCanvasPaints = pendingCanvasPaints.concat([canvas])
        if (!canvasPaintTimer.running)
            canvasPaintTimer.start()
    }

    function previewPointX(normalizedX) {
        return normalizedX * previewItem.width
    }

    function previewPointY(normalizedY) {
        return normalizedY * previewItem.height
    }

    function pointXValue(point, fallback) {
        const resolvedFallback = fallback === undefined ? 0.5 : fallback
        return point && point.x !== undefined ? point.x : resolvedFallback
    }

    function pointYValue(point, fallback) {
        const resolvedFallback = fallback === undefined ? 0.5 : fallback
        return point && point.y !== undefined ? point.y : resolvedFallback
    }

    function normalizedStrokePoint(mouseX, mouseY) {
        return {
            x: Math.max(0, Math.min(1, previewItem.width > 0 ? mouseX / previewItem.width : 0.5)),
            y: Math.max(0, Math.min(1, previewItem.height > 0 ? mouseY / previewItem.height : 0.5))
        }
    }

    function pointerPressure(mouse) {
        if (!mouse || mouse.pressure === undefined || mouse.pressure <= 0)
            return 1.0
        return Math.max(0.0, Math.min(1.0, mouse.pressure))
    }

    function pointerTilt(mouse, axisName) {
        if (!mouse || mouse[axisName] === undefined)
            return 0.0
        const rawTilt = mouse[axisName]
        if (Math.abs(rawTilt) <= 1.0)
            return Math.max(-1.0, Math.min(1.0, rawTilt))
        return Math.max(-1.0, Math.min(1.0, rawTilt / 60.0))
    }

    function brushResizeGestureArmed() {
        return editRoot.controlModifierActive &&
               editRoot.optionModifierActive &&
               editorPreview.hasActiveMask &&
               editorPreview.activeMaskKind === "brush" &&
               !editRoot.objectPickArmed &&
               !editRoot.polygonDrawArmed &&
               !editRoot.lassoDrawArmed
    }

    function beginBrushResize(mouseX, mouseY) {
        brushResizeActive = true
        brushResizeStartX = mouseX
        brushResizeStartY = mouseY
        brushResizeStartSize = editorPreview.activeMaskBrushSize
        brushResizeStartFlow = editorPreview.activeMaskBrushFlow
        hoverX = mouseX
        hoverY = mouseY
        hoverActive = true
    }

    function updateBrushResize(mouseX, mouseY, width, height) {
        if (!brushResizeActive)
            return
        hoverX = mouseX
        hoverY = mouseY
        const referenceSpan = Math.max(140, Math.min(420, Math.max(width, height) * 0.36))
        const sizeDelta = (mouseX - brushResizeStartX) / referenceSpan * 0.18
        const flowDelta = -(mouseY - brushResizeStartY) / 220
        editorPreview.activeMaskBrushSize = editRoot.clamp(brushResizeStartSize + sizeDelta, 0.01, 0.35)
        editorPreview.activeMaskBrushFlow = editRoot.clamp(brushResizeStartFlow + flowDelta, 0.05, 1.0)
    }

    function endBrushResize() {
        brushResizeActive = false
    }

    function resetSmartSelectStroke() {
        smartSelectStrokePoints = []
        smartSelectStrokeActive = false
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
            resetSmartSelectStroke()
            return
        }
        const outcome = editorPreview.pickMaskPath(
                    smartSelectStrokePoints,
                    editRoot.objectPickMode,
                    editRoot.objectPickCreateNew)
        resetSmartSelectStroke()
        editRoot.handleMaskPickOutcome(outcome)
    }

    function activeSelectionSetId() {
        if (!editorPreview.hasActiveMask ||
                editorPreview.activeMaskToolRole === "retouch" ||
                editorPreview.activeMaskIndex < 0 ||
                editorPreview.activeMaskIndex >= editorPreview.maskStack.length)
            return ""
        const activeMask = editorPreview.maskStack[editorPreview.activeMaskIndex]
        return activeMask && activeMask.maskSetId ? activeMask.maskSetId : ""
    }

    function objectPickAccentColor() {
        if (editRoot.objectPickMode === "remove")
            return "#d98962"
        if (editRoot.objectPickMode === "selection-subtract")
            return "#d36a5e"
        return "#5e95d3"
    }

    function objectPickActionLabel() {
        if (editRoot.objectPickMode === "remove")
            return editRoot.objectPickCreateNew ? "New Remove Target" : "Remove Object"
        if (editRoot.objectPickMode === "selection-subtract")
            return "Subtract Object"
        return editRoot.objectPickCreateNew ? "New Object Selection" : "Add Object"
    }

    function objectPickInstructionText() {
        const previewReady = editorPreview.smartSelectPreviewActive
        const cycleHint = editorPreview.smartSelectCanCycleCandidates
                ? " Use Tab to cycle candidates."
                : ""
        if (editRoot.objectPickMode === "remove") {
            return previewReady
                    ? "Release to stage the highlighted object for removal." + cycleHint
                    : (editRoot.objectPickCreateNew
                       ? "Hover an object, then click or drag to start a new removal target."
                       : "Hover an object, then click or drag to add it to the staged removal.")
        }
        if (editRoot.objectPickMode === "selection-subtract") {
            return previewReady
                    ? "Release to subtract the highlighted object from the selection." + cycleHint
                    : "Hover an object, then click or drag to subtract it from the selection."
        }
        return previewReady
                ? (editRoot.objectPickCreateNew
                   ? "Release to create a new selection from the highlighted object." + cycleHint
                   : "Release to add the highlighted object to the current selection." + cycleHint)
                : (editRoot.objectPickCreateNew
                   ? "Hover an object, then click or drag to create a separate selection."
                   : "Hover an object, then click or drag to add it to the current selection.")
    }

    function isOutlinePoint(value) {
        return value && value.x !== undefined && value.y !== undefined
    }

    function normalizedOutlinePath(value) {
        if (!value)
            return []

        if (isOutlinePoint(value))
            return [value]

        const normalized = []
        const items = Array.isArray(value) ? value : []
        for (let i = 0; i < items.length; ++i) {
            const item = items[i]
            if (isOutlinePoint(item))
                normalized.push(item)
        }
        return normalized
    }

    function normalizedOutlinePaths(value) {
        if (!value)
            return []

        if (isOutlinePoint(value))
            return [[value]]

        const items = Array.isArray(value) ? value : []
        if (items.length > 0 && isOutlinePoint(items[0]))
            return [normalizedOutlinePath(items)]

        const normalized = []
        for (let i = 0; i < items.length; ++i) {
            const path = normalizedOutlinePath(items[i])
            if (path.length > 0)
                normalized.push(path)
        }
        return normalized
    }

    function overlayOpacity(mask, active, sameSet) {
        if (active)
            return 0.96
        if (!editorPreview.hasActiveMask)
            return 0.58
        if (sameSet)
            return 0.38
        if (mask && mask.toolRole === "retouch")
            return 0.0
        return 0.22
    }

    function overlayVisible(mask, active, sameSet) {
        if (!mask || !mask.enabled)
            return false
        return active && mask.kind === "brush"
    }

    function previewMatteOpacity(isCandidate) {
        if (editorPreview.maskPreviewMode === "overlay")
            return 1.0
        if (editorPreview.maskPreviewMode === "grayscale")
            return isCandidate ? 0.58 : 0.70
        return isCandidate ? 0.86 : 0.92
    }

    function liveBrushOverlayColor(mask) {
        return "#cf3b32"
    }

    function liveBrushOverlayOpacity(mask) {
        return 0.30
    }

    function outlineBaseColor() {
        return "#f6f3ed"
    }

    function outlineHaloColor() {
        return theme.alpha(theme.ink950, 0.58)
    }

    function outlineAccentColor(accentColor, isCandidate) {
        return theme.alpha(accentColor, isCandidate ? 0.84 : 0.76)
    }

    function buildOutlinePath(ctx, points) {
        const count = points.length
        if (count < 2)
            return false
        ctx.beginPath()
        ctx.moveTo(root.previewPointX(root.pointXValue(points[0])), root.previewPointY(root.pointYValue(points[0])))
        for (let i = 1; i < count; ++i)
            ctx.lineTo(root.previewPointX(root.pointXValue(points[i])), root.previewPointY(root.pointYValue(points[i])))
        if (count >= 3)
            ctx.closePath()
        return true
    }

    function strokeOutlinePaths(ctx, outlinePaths, accentColor, isCandidate) {
        if (!outlinePaths || outlinePaths.length === 0)
            return

        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        const marching = editorPreview.maskPreviewMode === "marching-ants"

        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const haloPoints = root.normalizedOutlinePath(outlinePaths[pathIndex])
            if (!root.buildOutlinePath(ctx, haloPoints))
                continue
            if (ctx.setLineDash)
                ctx.setLineDash([])
            ctx.lineWidth = isCandidate ? 7 : 6
            ctx.strokeStyle = root.outlineHaloColor()
            ctx.stroke()
        }

        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const basePoints = root.normalizedOutlinePath(outlinePaths[pathIndex])
            if (!root.buildOutlinePath(ctx, basePoints))
                continue
            if (ctx.setLineDash)
                ctx.setLineDash(marching ? [10, 7] : [])
            ctx.lineDashOffset = marching ? -editRoot.selectionOutlinePhase : 0
            ctx.lineWidth = isCandidate ? 3.4 : 3
            ctx.strokeStyle = root.outlineBaseColor()
            ctx.stroke()
        }

        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const accentPoints = root.normalizedOutlinePath(outlinePaths[pathIndex])
            if (!root.buildOutlinePath(ctx, accentPoints))
                continue
            if (ctx.setLineDash)
                ctx.setLineDash(marching ? [10, 7] : [])
            ctx.lineDashOffset = marching ? 10 - editRoot.selectionOutlinePhase : 0
            ctx.lineWidth = isCandidate ? 1.8 : 1.5
            ctx.strokeStyle = root.outlineAccentColor(accentColor, isCandidate)
            ctx.stroke()
        }
    }

    function previewInstructionTitle() {
        if (brushResizeActive)
            return "Resize Brush"
        if (editRoot.cloneSourcePickArmed)
            return "Pick Clone Source"
        if (editRoot.objectPickArmed)
            return root.objectPickActionLabel()
        if (editRoot.lassoDrawArmed)
            return "Lasso Selection"
        if (editRoot.polygonDrawArmed)
            return "Polygon Selection"
        return editRoot.activeToolLabel()
    }

    function previewInstructionBody() {
        if (brushResizeActive)
            return "Drag left/right for size and up/down for flow."
        if (editRoot.cloneSourcePickArmed)
            return "Click a clean source area. Release Option to paint."
        if (editRoot.lassoDrawArmed)
            return editRoot.manualSelectionSubtractActive()
                    ? "Drag on the image to subtract. Release Option to add instead."
                    : "Drag on the image. Hold Option to subtract."
        if (editRoot.polygonDrawArmed)
            return editRoot.manualSelectionSubtractActive()
                    ? "Click points to subtract. Double-click to finish. Backspace removes the last point."
                    : "Click points. Hold Option to subtract. Double-click to finish. Backspace removes the last point."
        return root.objectPickInstructionText()
    }

    Item {
        id: previewItem
        x: root.imageX
        y: root.imageY
        width: root.imageWidth
        height: root.imageHeight
        visible: root.visible

        MouseArea {
            anchors.fill: parent
            enabled: editorPreview.hasActiveMask &&
                     !editorPreview.brushStrokeActive &&
                     !editRoot.objectPickArmed &&
                     !editRoot.polygonDrawArmed &&
                     !editRoot.lassoDrawArmed &&
                     !editRoot.cloneSourcePickArmed
            acceptedButtons: Qt.LeftButton
            onClicked: editRoot.deselectMaskEditing()
        }

        Repeater {
            model: editorPreview.maskStack

            delegate: Item {
                required property int index
                required property var modelData

                readonly property var mask: modelData
                readonly property bool active: index === editorPreview.activeMaskIndex
                readonly property string activeSetId: root.activeSelectionSetId()
                readonly property bool sameSet: mask.toolRole !== "retouch" &&
                                                activeSetId.length > 0 &&
                                                mask.maskSetId === activeSetId
                readonly property color accent: editRoot.maskAccent(mask)
                readonly property real emphasis: root.overlayOpacity(mask, active, sameSet)

                anchors.fill: parent
                visible: root.overlayVisible(mask, active, sameSet)

                Item {
                    id: linearOverlay
                    visible: mask.kind === "linear"
                    readonly property real positionValue: mask && mask.position !== undefined ? mask.position : 0.5
                    readonly property real featherValue: mask && mask.feather !== undefined ? mask.feather : 0.22
                    readonly property real rotationValue: mask && mask.rotation !== undefined ? mask.rotation : 0
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    rotation: rotationValue
                    opacity: emphasis

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.alpha(accent, 0.36) }
                            GradientStop {
                                position: Math.max(0.0, linearOverlay.positionValue - linearOverlay.featherValue * 0.5)
                                color: Qt.alpha(accent, 0.16)
                            }
                            GradientStop { position: linearOverlay.positionValue; color: Qt.rgba(0.95, 0.97, 0.98, 0.74) }
                            GradientStop {
                                position: Math.min(1.0, linearOverlay.positionValue + linearOverlay.featherValue * 0.5)
                                color: Qt.alpha(accent, 0.08)
                            }
                            GradientStop { position: 1.0; color: Qt.alpha(accent, 0.0) }
                        }
                    }

                    Rectangle {
                        x: 0
                        y: Math.max(0, Math.min(linearOverlay.height - 1, linearOverlay.height * linearOverlay.positionValue))
                        width: linearOverlay.width
                        height: 1
                        color: "#f4f7fa"
                        opacity: 0.92
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: active ? Qt.CrossCursor : Qt.PointingHandCursor
                        onPressed: function(mouse) {
                            editorPreview.activeMaskIndex = index
                            if (active)
                                editorPreview.activeMaskPosition = editRoot.clamp(mouse.y / linearOverlay.height, 0, 1)
                        }
                    }

                    Rectangle {
                        visible: active
                        x: linearOverlay.width / 2 - width / 2
                        y: linearOverlay.height * linearOverlay.positionValue - height / 2
                        width: 20
                        height: 20
                        radius: 10
                        color: "#f4f7fa"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.SizeVerCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskPosition = editRoot.clamp(
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
                        width: 18
                        height: 18
                        radius: 9
                        color: accent
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
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
                    opacity: emphasis

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

                            function onMaskOverlayChanged() {
                                root.requestCanvasPaint(radialCanvas)
                            }
                        }

                        onPaint: {
                            const ctx = getContext("2d")
                            const innerScale = Math.max(0.0, 1.0 - radialOverlay.featherValue)
                            ctx.reset()
                            ctx.strokeStyle = "#f4f7fa"
                            ctx.lineWidth = active ? 1.8 : 1.2
                            ctx.fillStyle = Qt.alpha(accent, active ? 0.20 : 0.11)
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
                        width: 20
                        height: 20
                        radius: 10
                        color: "#f4f7fa"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.SizeAllCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskCenterX = editRoot.clamp((radialOverlay.x + parent.x + mouse.x) / previewItem.width, 0, 1)
                                editorPreview.activeMaskCenterY = editRoot.clamp((radialOverlay.y + parent.y + mouse.y) / previewItem.height, 0, 1)
                            }
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width - width / 2
                        y: radialOverlay.height / 2 - height / 2
                        width: 18
                        height: 18
                        radius: 9
                        color: accent
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.SizeHorCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskRadiusX = editRoot.clamp((radialOverlay.x + parent.x + mouse.x) / previewItem.width / 2, 0.05, 1.0)
                            }
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width / 2 - width / 2
                        y: radialOverlay.height - height / 2
                        width: 18
                        height: 18
                        radius: 9
                        color: accent
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.SizeVerCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskRadiusY = editRoot.clamp((radialOverlay.y + parent.y + mouse.y) / previewItem.height / 2, 0.05, 1.0)
                            }
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width / 2 - width / 2
                        y: -30
                        width: 18
                        height: 18
                        radius: 9
                        color: "#f0c46e"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
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
                    readonly property bool showStrokePoints: active && editorPreview.brushStrokeActive
                    readonly property bool isStaged: mask.toolRole === "retouch" && !(mask.committed || false)
                    anchors.fill: parent
                    opacity: isStaged ? Math.max(emphasis * 0.7, 0.2) : emphasis
                    z: active && editorPreview.brushStrokeActive ? 98 : 0

                    Canvas {
                        id: brushCanvas
                        anchors.fill: parent
                        visible: brushOverlay.showStrokePoints &&
                                 !(active && editorPreview.brushStrokeActive &&
                                   editorPreview.activeMaskLiveBrushPreviewSource.length > 0)

                        function pointX(point) {
                            return root.pointXValue(point) * brushOverlay.width
                        }

                        function pointY(point) {
                            return root.pointYValue(point) * brushOverlay.height
                        }

                        function pointDiameter(point) {
                            return Math.max(8, Math.min(previewItem.width, previewItem.height) * (point.size || mask.brushSize || 0.08) * 2)
                        }

                        function pointAlpha(point) {
                            return Math.max(0.08, (point.flow || mask.brushFlow || 1.0) * 0.22)
                        }

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()

                            const liveStroke = active && editorPreview.brushStrokeActive
                            const points = liveStroke
                                           ? (editorPreview.activeMaskLiveBrushPoints || [])
                                           : (brushOverlay.showStrokePoints ? (mask.brushPoints || []) : [])
                            if (points.length === 0)
                                return

                            const overlayColor = liveStroke
                                                 ? root.liveBrushOverlayColor(mask)
                                                 : accent
                            const overlayOpacity = liveStroke
                                                   ? root.liveBrushOverlayOpacity(mask)
                                                   : 0.34

                            const first = points[0]
                            ctx.fillStyle = Qt.alpha(overlayColor, liveStroke ? overlayOpacity : pointAlpha(first))
                            ctx.beginPath()
                            ctx.arc(pointX(first), pointY(first), pointDiameter(first) / 2, 0, Math.PI * 2)
                            ctx.fill()

                            for (let i = 1; i < points.length; ++i) {
                                const previous = points[i - 1]
                                const current = points[i]
                                const strokeDiameter = (pointDiameter(previous) + pointDiameter(current)) / 2

                                ctx.strokeStyle = Qt.alpha(
                                                    overlayColor,
                                                    liveStroke
                                                    ? overlayOpacity
                                                    : Math.max(pointAlpha(previous), pointAlpha(current)))
                                ctx.lineCap = "round"
                                ctx.lineJoin = "round"
                                ctx.lineWidth = strokeDiameter
                                ctx.beginPath()
                                ctx.moveTo(pointX(previous), pointY(previous))
                                ctx.lineTo(pointX(current), pointY(current))
                                ctx.stroke()

                                ctx.fillStyle = Qt.alpha(overlayColor, liveStroke ? overlayOpacity : pointAlpha(current))
                                ctx.beginPath()
                                ctx.arc(pointX(current), pointY(current), pointDiameter(current) / 2, 0, Math.PI * 2)
                                ctx.fill()
                            }
                        }

                        onVisibleChanged: root.requestCanvasPaint(brushCanvas)
                        onWidthChanged: root.requestCanvasPaint(brushCanvas)
                        onHeightChanged: root.requestCanvasPaint(brushCanvas)

                        Connections {
                            target: editorPreview

                            function onMaskOverlayChanged() {
                                root.requestCanvasPaint(brushCanvas)
                            }

                            function onActiveBrushStrokeChanged() {
                                root.requestCanvasPaint(brushCanvas)
                            }
                        }
                    }

                    Image {
                        anchors.fill: parent
                        visible: active &&
                                 editorPreview.brushStrokeActive &&
                                 !editorPreview.activeMaskLiveBrushPreviewNativeAvailable &&
                                 editorPreview.activeMaskLiveBrushPreviewSource.length > 0
                        source: editorPreview.activeMaskLiveBrushPreviewSource
                        fillMode: Image.Stretch
                        smooth: true
                        mipmap: false
                        cache: false
                    }

                    BrushLivePreviewItem {
                        anchors.fill: parent
                        visible: active &&
                                 editorPreview.brushStrokeActive &&
                                 editorPreview.activeMaskLiveBrushPreviewNativeAvailable
                        controller: editorPreview
                    }

                    Item {
                        visible: mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
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
                        enabled: active && !editRoot.spacePanActive
                        z: 4

                        onTabletPressed: function(x, y, pressure, tiltX, tiltY, eraser) {
                            editRoot.forceActiveFocus()
                            editorPreview.activeMaskIndex = index
                            root.hoverX = x
                            root.hoverY = y
                            root.hoverActive = true
                            if (root.brushResizeGestureArmed()) {
                                root.beginBrushResize(x, y)
                                root.tabletBrushStrokeActive = false
                                return
                            }
                            if (mask.retouchMode === "clone" &&
                                    (editRoot.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                editorPreview.setActiveMaskCloneSource(
                                            x / brushOverlay.width,
                                            y / brushOverlay.height)
                                editRoot.clearTransientTools()
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
                            root.hoverX = x
                            root.hoverY = y
                            root.hoverActive = true
                            if (root.brushResizeActive) {
                                root.updateBrushResize(x, y, brushOverlay.width, brushOverlay.height)
                                return
                            }
                            if (!root.tabletBrushStrokeActive || editRoot.cloneSourcePickArmed ||
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
                            root.hoverX = x
                            root.hoverY = y
                            if (root.brushResizeActive) {
                                root.endBrushResize()
                                root.tabletBrushStrokeActive = false
                                return
                            }
                            if (root.tabletBrushStrokeActive)
                                editorPreview.endActiveBrushStroke()
                            root.tabletBrushStrokeActive = false
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        enabled: active && !editRoot.spacePanActive
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: root.brushResizeGestureArmed() || root.brushResizeActive
                                     ? Qt.SizeAllCursor
                                     : Qt.CrossCursor
                        onEntered: root.hoverActive = true
                        onExited: root.hoverActive = false
                        onPositionChanged: function(mouse) {
                            root.hoverX = mouse.x
                            root.hoverY = mouse.y
                            root.hoverActive = true
                            if (root.brushResizeActive) {
                                root.updateBrushResize(mouse.x, mouse.y, brushOverlay.width, brushOverlay.height)
                                return
                            }
                            if (!pressed || editRoot.cloneSourcePickArmed ||
                                    (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                return
                            editorPreview.addBrushPointWithTablet(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height, root.pointerPressure(mouse), root.pointerTilt(mouse, "xTilt"), root.pointerTilt(mouse, "yTilt"))
                        }
                        onPressed: function(mouse) {
                            editRoot.forceActiveFocus()
                            editorPreview.activeMaskIndex = index
                            if (root.brushResizeGestureArmed()) {
                                root.beginBrushResize(mouse.x, mouse.y)
                                return
                            }
                            editorPreview.beginActiveBrushStroke()
                            root.hoverX = mouse.x
                            root.hoverY = mouse.y
                            root.hoverActive = true
                            if (mask.retouchMode === "clone" &&
                                    (editRoot.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                editorPreview.setActiveMaskCloneSource(
                                            mouse.x / brushOverlay.width,
                                            mouse.y / brushOverlay.height)
                                editRoot.clearTransientTools()
                                return
                            }
                            editorPreview.addBrushPointWithTablet(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height, root.pointerPressure(mouse), root.pointerTilt(mouse, "xTilt"), root.pointerTilt(mouse, "yTilt"))
                        }
                        onReleased: {
                            if (root.brushResizeActive) {
                                root.endBrushResize()
                                return
                            }
                            editorPreview.endActiveBrushStroke()
                        }
                        onCanceled: {
                            if (root.brushResizeActive) {
                                root.endBrushResize()
                                return
                            }
                            editorPreview.endActiveBrushStroke()
                        }
                    }
                }

                Item {
                    id: polygonOverlay
                    visible: mask.kind === "polygon" || mask.kind === "lasso"
                    anchors.fill: parent
                    opacity: emphasis

                    Canvas {
                        id: polygonCanvas
                        anchors.fill: parent

                        Connections {
                            target: editorPreview

                            function onMaskOverlayChanged() {
                                root.requestCanvasPaint(polygonCanvas)
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
                            ctx.moveTo(root.previewPointX(root.pointXValue(points[0])), root.previewPointY(root.pointYValue(points[0])))
                            for (let i = 1; i < count; ++i)
                                ctx.lineTo(root.previewPointX(root.pointXValue(points[i])), root.previewPointY(root.pointYValue(points[i])))
                            if (count >= 3)
                                ctx.closePath()

                            if (count >= 3) {
                                root.strokeOutlinePaths(ctx, [points], accent, false)
                            } else {
                                ctx.strokeStyle = Qt.alpha("#f4f7fa", active ? 0.88 : 0.55)
                                ctx.lineWidth = active ? 1.8 : 1.1
                                if (ctx.setLineDash)
                                    ctx.setLineDash([])
                                ctx.stroke()
                            }
                        }
                    }

                    Repeater {
                        model: mask.polygonPoints || []

                        delegate: Rectangle {
                            required property var modelData

                            width: 10
                            height: 10
                            radius: 5
                            x: root.previewPointX(root.pointXValue(modelData)) - width / 2
                            y: root.previewPointY(root.pointYValue(modelData)) - height / 2
                            color: active ? "#f4f7fa" : accent
                            border.width: 1
                            border.color: "#101419"
                            visible: active
                        }
                    }
                }

                Rectangle {
                    visible: active &&
                              (mask.kind === "luminance" ||
                               mask.kind === "color" ||
                               mask.kind === "depth")
                    anchors.fill: parent
                    color: Qt.alpha(accent, 0.08)
                    border.width: 1
                    border.color: Qt.alpha(accent, 0.28)
                    radius: 4
                    opacity: emphasis

                    Rectangle {
                        anchors.centerIn: parent
                        radius: 10
                        color: Qt.alpha("#101419", 0.72)
                        border.width: 1
                        border.color: Qt.alpha(accent, 0.55)
                        implicitWidth: rangeLabel.implicitWidth + 24
                        implicitHeight: rangeLabel.implicitHeight + 14

                        Label {
                            id: rangeLabel
                            anchors.centerIn: parent
                            text: mask.kind === "luminance"
                                  ? "Luminance range · " +
                                    Math.round((mask.luminanceMin || 0.2) * 100) + "–" +
                                    Math.round((mask.luminanceMax || 0.8) * 100) + "%"
                                  : mask.kind === "color"
                                    ? "Color range · " +
                                      Math.round((mask.hueCenter || 0.08) * 360) + "° ±" +
                                      Math.round((mask.hueRange || 0.12) * 360) + "°"
                                    : mask.kind === "depth"
                                      ? "Depth range · " +
                                        Math.round((mask.depthMin || 0.2) * 100) + "-" +
                                        Math.round((mask.depthMax || 0.8) * 100) + "%"
                                    : ""
                            color: "#f2f4f6"
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            enabled: editRoot.objectPickArmed ||
                     (editorPreview.hasActiveMask &&
                      editorPreview.activeMaskKind === "brush" &&
                      !editRoot.objectPickArmed &&
                      !editRoot.polygonDrawArmed &&
                      !editRoot.lassoDrawArmed)
            z: 95

            onEntered: root.hoverActive = true
            onExited: root.hoverActive = false
            onPositionChanged: function(mouse) {
                root.hoverX = mouse.x
                root.hoverY = mouse.y
                root.hoverActive = true
            }
        }

        Rectangle {
            visible: ((editorPreview.hasActiveMask &&
                       editorPreview.activeMaskKind === "brush" &&
                       !editRoot.objectPickArmed &&
                       !editRoot.polygonDrawArmed &&
                       !editRoot.lassoDrawArmed) ||
                      editRoot.objectPickArmed) &&
                     root.hoverActive
            x: root.hoverX - width / 2
            y: root.hoverY - height / 2
            width: editRoot.objectPickArmed
                   ? 42
                   : Math.max(8, Math.min(previewItem.width, previewItem.height) * editorPreview.activeMaskBrushSize * 2)
            height: width
            radius: width / 2
            z: 98
            color: Qt.alpha(
                       editRoot.objectPickArmed
                       ? root.objectPickAccentColor()
                       : theme.accentWarm300,
                       editRoot.objectPickArmed ? 0.10 : (root.brushResizeActive ? 0.0 : 0.02))
            border.width: 1
            border.color: root.brushResizeActive
                          ? theme.alpha("#f6f3ed", 0.98)
                          : editRoot.objectPickArmed
                          ? root.objectPickAccentColor()
                          : (editorPreview.activeMaskRetouchMode === "remove"
                             ? "#d98962"
                             : editorPreview.activeMaskRetouchMode === "clone"
                               ? "#7cb5ff"
                               : editorPreview.activeMaskRetouchMode === "heal"
                                 ? "#72cbb2"
                                 : "#cf3b32")
        }

        Image {
            id: activeSelectionMatte
            anchors.fill: parent
            visible: editorPreview.hasActiveMask &&
                     editorPreview.maskPreviewMode !== "marching-ants" &&
                     editorPreview.activeMaskPreviewSource.length > 0
            source: editorPreview.activeMaskPreviewSource
            fillMode: Image.Stretch
            smooth: true
            mipmap: false
            cache: false
            opacity: root.previewMatteOpacity(false)
            z: 95
        }

        Canvas {
            id: activeSelectionOutline
            anchors.fill: parent
            visible: editorPreview.hasActiveMask &&
                     (editorPreview.maskPreviewMode === "marching-ants" ||
                      editorPreview.maskPreviewMode === "edge") &&
                     editorPreview.activeMaskOutlinePaths.length > 0
            z: 96

            Connections {
                target: editorPreview

                function onMaskOverlayChanged() {
                    root.requestCanvasPaint(activeSelectionOutline)
                }

                function onControlsChanged() {
                    root.requestCanvasPaint(activeSelectionOutline)
                }
            }

            Connections {
                target: editRoot

                function onSelectionOutlinePhaseChanged() {
                    root.requestCanvasPaint(activeSelectionOutline)
                }
            }

            onPaint: {
                const outlinePaths = root.normalizedOutlinePaths(editorPreview.activeMaskOutlinePaths)
                const ctx = getContext("2d")
                ctx.reset()
                if (!outlinePaths || outlinePaths.length === 0)
                    return

                const activeMask = editorPreview.activeMaskIndex >= 0
                                   ? editorPreview.maskStack[editorPreview.activeMaskIndex]
                                   : null
                const accent = editRoot.maskAccent(activeMask)
                root.strokeOutlinePaths(ctx, outlinePaths, accent, false)
            }
        }

        Rectangle {
            readonly property rect patchBounds: editorPreview.activeInpaintPatchBounds

            visible: editorPreview.inpaintPatchDiagnosticsEnabled &&
                     patchBounds.width > 0 &&
                     patchBounds.height > 0
            x: patchBounds.x * previewItem.width
            y: patchBounds.y * previewItem.height
            width: Math.max(1, patchBounds.width * previewItem.width)
            height: Math.max(1, patchBounds.height * previewItem.height)
            z: 97
            color: "transparent"
            border.width: 2
            border.color: "#f2c56b"

            Rectangle {
                x: -1
                y: -1
                width: parent.width + 2
                height: parent.height + 2
                color: "transparent"
                border.width: 1
                border.color: "#18130b"
            }
        }

        Image {
            id: smartPreviewMatte
            anchors.fill: parent
            visible: editRoot.objectPickArmed &&
                     editorPreview.smartSelectPreviewActive &&
                     editorPreview.smartSelectPreviewSource.length > 0
            source: editorPreview.smartSelectPreviewSource
            fillMode: Image.Stretch
            smooth: true
            mipmap: false
            cache: false
            opacity: root.previewMatteOpacity(true)
            z: 96
        }

        Canvas {
            id: smartPreviewOutline
            anchors.fill: parent
            visible: false
            z: 97

            Connections {
                target: editorPreview

                function onSmartSelectPreviewChanged() {
                    root.requestCanvasPaint(smartPreviewOutline)
                }
            }

            Connections {
                target: editRoot

                function onSelectionOutlinePhaseChanged() {
                    root.requestCanvasPaint(smartPreviewOutline)
                }
            }

            onPaint: {
                const points = root.normalizedOutlinePath(editorPreview.smartSelectPreviewOutlinePoints)
                const count = points.length
                const ctx = getContext("2d")
                ctx.reset()
                if (count < 2)
                    return

                root.strokeOutlinePaths(ctx, [points], root.objectPickAccentColor(), true)
            }
        }

        MouseArea {
            x: 0
            y: 0
            width: previewItem.width
            height: previewItem.height
            enabled: !editRoot.spacePanActive &&
                     (editRoot.objectPickArmed || editRoot.polygonDrawArmed || editRoot.lassoDrawArmed)
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: (editRoot.polygonDrawArmed || editRoot.lassoDrawArmed || editRoot.objectPickArmed)
                         ? Qt.CrossCursor
                         : Qt.ArrowCursor
            z: 100

            onEntered: function(mouse) {
                if (editRoot.objectPickArmed)
                    editorPreview.scheduleSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onExited: {
                if (editRoot.objectPickArmed && !root.smartSelectStrokeActive)
                    editorPreview.clearSmartSelectPreview()
            }

            onPressed: function(mouse) {
                editRoot.forceActiveFocus()
                if (editRoot.objectPickArmed) {
                    root.beginSmartSelectStroke(mouse.x, mouse.y)
                    return
                }
                if (!editRoot.lassoDrawArmed)
                    return
                editorPreview.startLassoSelection(editRoot.manualSelectionSubtractActive())
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onClicked: function(mouse) {
                if (editRoot.objectPickArmed)
                    return
                if (editRoot.lassoDrawArmed)
                    return
                if (editRoot.polygonDrawArmed) {
                    if (editorPreview.activeMaskKind === "polygon")
                        editorPreview.activeMaskOperation = editRoot.manualSelectionSubtractActive() ? "subtract" : "add"
                    editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
                }
            }

            onPositionChanged: function(mouse) {
                if (editRoot.objectPickArmed) {
                    if (pressed)
                        root.updateSmartSelectStroke(mouse.x, mouse.y)
                    else
                        editorPreview.scheduleSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
                }

                if (!editRoot.lassoDrawArmed || !pressed)
                    return
                if (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso")
                    editorPreview.activeMaskOperation = editRoot.manualSelectionSubtractActive() ? "subtract" : "add"
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onDoubleClicked: function(mouse) {
                if (!editRoot.polygonDrawArmed)
                    return
                editRoot.clearTransientTools()
            }

            onReleased: function(mouse) {
                if (editRoot.objectPickArmed) {
                    root.commitSmartSelectStroke()
                    if (containsMouse)
                        editorPreview.scheduleSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
                    return
                }
                if (!editRoot.lassoDrawArmed)
                    return
                if (editorPreview.activeMaskPolygonPointCount < 3) {
                    editorPreview.clearActivePolygonPoints()
                    editorPreview.removeActiveMask()
                }
                editRoot.clearTransientTools()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 12
            visible: false
            z: 99
            radius: 10
            color: theme.alpha(theme.ink950, 0.82)
            border.width: 1
            border.color: editRoot.cloneSourcePickArmed ? "#7cb5ff"
                         : (editorPreview.hasActiveMask ? theme.alpha(editRoot.maskWorkspaceAccentColor(), 0.52)
                            : theme.alpha(root.objectPickAccentColor(), 0.52))
            implicitWidth: overlayInfo.implicitWidth + 20
            implicitHeight: overlayInfo.implicitHeight + 14

            Label {
                id: overlayInfo
                anchors.centerIn: parent
                text: root.previewInstructionTitle()
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            visible: editRoot.objectPickArmed &&
                     editorPreview.smartSelectCanCycleCandidates
            z: 99
            radius: 10
            color: Qt.alpha("#101419", 0.82)
            border.width: 1
            border.color: Qt.alpha(root.objectPickAccentColor(), 0.58)
            implicitWidth: candidateControls.implicitWidth + 18
            implicitHeight: candidateControls.implicitHeight + 14

            Row {
                id: candidateControls
                anchors.centerIn: parent
                spacing: 6

                ActionButton {
                    text: "Prev"
                    compact: true
                    accent: root.objectPickAccentColor()
                    tone: "secondary"
                    onClicked: editorPreview.cycleSmartSelectCandidate(-1)
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (editorPreview.smartSelectCandidateIndex + 1).toString() +
                          " / " + editorPreview.smartSelectCandidateCount.toString()
                    color: "#f2f4f6"
                    font.family: theme.fontUi
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                ActionButton {
                    text: "Next"
                    compact: true
                    accent: root.objectPickAccentColor()
                    tone: "secondary"
                    onClicked: editorPreview.cycleSmartSelectCandidate(1)
                }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            visible: false
            z: 99
            color: theme.alpha(theme.ink950, 0.86)
            border.width: 1
            border.color: editRoot.cloneSourcePickArmed ? "#7cb5ff"
                         : editRoot.lassoDrawArmed ? "#7ec97a"
                         : editRoot.polygonDrawArmed ? "#7ec97a"
                         : root.objectPickAccentColor()
            radius: 12
            width: Math.min(previewItem.width - 24, 280)
            height: 56

            Column {
                id: instructionColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2

                Label {
                    width: parent.width
                    text: root.previewInstructionTitle()
                    color: theme.textStrong
                    font.family: theme.fontDisplay
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    width: parent.width
                    text: root.previewInstructionBody()
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 14
            anchors.bottomMargin: 14
            visible: root.brushResizeActive
            z: 100
            radius: 12
            color: theme.alpha(theme.ink950, 0.88)
            border.width: 1
            border.color: theme.alpha(theme.accentWarm300, 0.38)
            implicitWidth: brushResizeReadout.implicitWidth + 20
            implicitHeight: brushResizeReadout.implicitHeight + 14

            Label {
                id: brushResizeReadout
                anchors.centerIn: parent
                text: "Size " + Math.round(editorPreview.activeMaskBrushSize * 1000).toString() +
                      "  Flow " + Math.round(editorPreview.activeMaskBrushFlow * 100).toString() + "%"
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
    }
}
