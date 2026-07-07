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
    property real cropX: editorPreview.cropRectX
    property real cropY: editorPreview.cropRectY
    property real cropWidth: editorPreview.cropRectWidth
    property real cropHeight: editorPreview.cropRectHeight
    property real transformRotation: editorPreview.transformRotation
    property real hoverX: 0
    property real hoverY: 0
    property bool hoverActive: false
    property bool brushCursorPositionKnown: false
    property bool brushResizeActive: false
    property real brushResizeStartX: 0
    property real brushResizeStartY: 0
    property real brushResizeStartSize: 0.08
    property real brushResizeStartFlow: 1.0
    property bool smartSelectModifierPromptHandled: false
    property real pendingSmartSelectPreviewX: -1
    property real pendingSmartSelectPreviewY: -1
    property real lastSmartSelectPreviewX: -1
    property real lastSmartSelectPreviewY: -1
    readonly property real smartSelectPreviewPauseMs: 180
    readonly property real smartSelectPreviewMoveEpsilon: 0.003
    property bool tabletBrushStrokeActive: false
    property var pendingCanvasPaints: []
    property real outlineVisualScale: 1.0
    readonly property string outlineRenderContract: "anti-aliased|zoom-stable|pixel-aligned:v1"
    readonly property string outlineAnimationContract: editRoot.selectionOutlineAnimationContract
    readonly property bool outlinePhaseAnimationEnabled: editRoot.selectionOutlineAnimationEnabled === true
    readonly property string brushCursorRepaintContract: "cursor-item-position-binding|no-static-canvas-repaint-on-hover:v1"
    readonly property string handleReadabilityContract: "dual-contrast-halo|large-hit-target|color-independent:v1"
    readonly property string overlayGeometryContract: "image-rect-clipped|crop-rotation-aware:v1"
    readonly property string maskRasterPreviewPolicy: "editor-canvas-raster-mask-disabled|clean-exterior-vector-outline:v2"
    readonly property string settledMaskOutlineContract: "outline-only|fragment-filtered|no-fill:v1"
    readonly property string outlineQualityLadderContract: "hidden|live-vector|preview-alpha|vector-path|settled-alpha|unavailable:v1"
    readonly property string activeOutlineQualityTier: editorPreview.activeMaskOutlineQualityTier || "hidden"
    readonly property string outlineTraceContract: "public-outline-trace|source|tolerance|provider|path-count|image-normalized|final-alpha:v2"
    readonly property var activeOutlineTrace: editorPreview.activeMaskOutlineTrace || ({})
    readonly property string activeOutlineTraceSource: String(activeOutlineTrace.outline_trace_source || "")
    readonly property int activeOutlineTracePathCount: Math.max(0, Number(activeOutlineTrace.outline_trace_path_count || 0))
    readonly property real activeOutlineTraceTolerancePixels: Math.max(0, Number(activeOutlineTrace.outline_trace_tolerance_pixels || 0))
    readonly property real activeOutlineTraceToleranceNormalized: Math.max(0, Number(activeOutlineTrace.outline_trace_tolerance_normalized || 0))
    readonly property bool activeOutlineTraceWithinBudget: activeOutlineTrace.outline_trace_within_latency_budget === true
    readonly property string activeOutlineCoordinateSpace: String(activeOutlineTrace.outline_coordinate_space || "")
    readonly property string activeOutlineCoordinateContract: String(activeOutlineTrace.outline_coordinate_contract || "")
    readonly property string activeOutlineAlphaSourceContract: String(activeOutlineTrace.outline_alpha_source_contract || "")
    readonly property string activeOutlineTraceProviderContract: String(activeOutlineTrace.mask_alpha_provider_contract || "")
    readonly property string activeOutlineTraceProvider: String(activeOutlineTrace.mask_alpha_provider || "")
    readonly property string activeOutlineTraceProviderSignature: String(activeOutlineTrace.mask_alpha_provider_signature || "")
    readonly property string activeOutlineTraceMaskAlphaSignature: String(activeOutlineTrace.mask_alpha_trace_signature || "")
    readonly property string smartSelectHoverPrewarmContract: "pointer-pause|jitter-gated|no-enter-prewarm:v1"

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

    Timer {
        id: smartSelectPreviewPauseTimer
        interval: root.smartSelectPreviewPauseMs
        repeat: false
        onTriggered: {
            if (!editRoot.objectPickArmed || editorPreview.smartSelectStrokeActive)
                return
            root.lastSmartSelectPreviewX = root.pendingSmartSelectPreviewX
            root.lastSmartSelectPreviewY = root.pendingSmartSelectPreviewY
            editorPreview.scheduleSmartSelectPreview(root.pendingSmartSelectPreviewX,
                                                     root.pendingSmartSelectPreviewY)
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

    function smartSelectPreviewPointNear(leftX, leftY, rightX, rightY) {
        if (leftX < 0 || leftY < 0 || rightX < 0 || rightY < 0)
            return false
        return Math.abs(leftX - rightX) <= root.smartSelectPreviewMoveEpsilon &&
               Math.abs(leftY - rightY) <= root.smartSelectPreviewMoveEpsilon
    }

    function stopPendingSmartSelectPreview() {
        smartSelectPreviewPauseTimer.stop()
        root.pendingSmartSelectPreviewX = -1
        root.pendingSmartSelectPreviewY = -1
    }

    function scheduleSmartSelectPreviewDebounced(normalizedX, normalizedY, force) {
        if (!editRoot.objectPickArmed)
            return
        const clampedX = Math.max(0.0, Math.min(1.0, Number(normalizedX || 0)))
        const clampedY = Math.max(0.0, Math.min(1.0, Number(normalizedY || 0)))
        if (!force) {
            if (smartSelectPreviewPauseTimer.running &&
                    root.smartSelectPreviewPointNear(
                        clampedX, clampedY,
                        root.pendingSmartSelectPreviewX,
                        root.pendingSmartSelectPreviewY)) {
                return
            }
            if (root.smartSelectPreviewPointNear(
                    clampedX, clampedY,
                    root.lastSmartSelectPreviewX,
                    root.lastSmartSelectPreviewY)) {
                return
            }
        }
        root.pendingSmartSelectPreviewX = clampedX
        root.pendingSmartSelectPreviewY = clampedY
        if (force) {
            smartSelectPreviewPauseTimer.stop()
            root.lastSmartSelectPreviewX = clampedX
            root.lastSmartSelectPreviewY = clampedY
            editorPreview.scheduleSmartSelectPreview(clampedX, clampedY)
            return
        }
        smartSelectPreviewPauseTimer.restart()
    }

    function previewPointX(normalizedX) {
        return normalizedX * previewItem.width
    }

    function previewPointY(normalizedY) {
        return normalizedY * previewItem.height
    }

    function outlineScaleFactor() {
        const scale = Number(outlineVisualScale || 1.0)
        return Math.max(0.25, isFinite(scale) ? scale : 1.0)
    }

    function stableOutlineWidth(logicalPixels) {
        return Math.max(0.75, Number(logicalPixels || 1.0) / root.outlineScaleFactor())
    }

    function stableOutlineDash(logicalPixels) {
        return Math.max(1.0, Number(logicalPixels || 1.0) / root.outlineScaleFactor())
    }

    function handleOuterHaloColor() {
        return theme.alpha("#101419", 0.94)
    }

    function handleInnerHaloColor() {
        return theme.alpha("#f4f7fa", 0.96)
    }

    function handleWarningFillColor() {
        return theme.alpha("#f0c46e", 0.92)
    }

    function pixelAligned(value) {
        const ratio = Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
        return (Math.round(Number(value || 0) * ratio) + 0.5) / ratio
    }

    function previewOutlinePointX(normalizedX) {
        return root.pixelAligned(root.previewPointX(normalizedX))
    }

    function previewOutlinePointY(normalizedY) {
        return root.pixelAligned(root.previewPointY(normalizedY))
    }

    function overlayGeometrySignature() {
        return root.overlayGeometryContract +
               "|clip:" + String(previewItem.clip === true) +
               "|rect:" + Math.round(root.imageX) + "," + Math.round(root.imageY) + "," +
               Math.round(root.imageWidth) + "x" + Math.round(root.imageHeight) +
               "|crop:" + Number(root.cropX || 0).toFixed(3) + "," +
               Number(root.cropY || 0).toFixed(3) + "," +
               Number(root.cropWidth || 1).toFixed(3) + "x" +
               Number(root.cropHeight || 1).toFixed(3) +
               "|rotation:" + Number(root.transformRotation || 0).toFixed(2)
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
        brushCursorPositionKnown = true
    }

    function updateBrushResize(mouseX, mouseY, width, height) {
        if (!brushResizeActive)
            return
        hoverX = mouseX
        hoverY = mouseY
        brushCursorPositionKnown = true
        const referenceSpan = Math.max(140, Math.min(420, Math.max(width, height) * 0.36))
        const sizeDelta = (mouseX - brushResizeStartX) / referenceSpan * 0.18
        const flowDelta = -(mouseY - brushResizeStartY) / 220
        editorPreview.activeMaskBrushSize = editRoot.clamp(brushResizeStartSize + sizeDelta, 0.01, 0.35)
        editorPreview.activeMaskBrushFlow = editRoot.clamp(brushResizeStartFlow + flowDelta, 0.05, 1.0)
    }

    function endBrushResize() {
        brushResizeActive = false
    }

    function beginSmartSelectStroke(mouseX, mouseY) {
        const point = normalizedStrokePoint(mouseX, mouseY)
        editorPreview.beginSmartSelectStroke(point.x, point.y, previewItem.width, previewItem.height)
    }

    function updateSmartSelectStroke(mouseX, mouseY) {
        const point = normalizedStrokePoint(mouseX, mouseY)
        editorPreview.updateSmartSelectStroke(point.x, point.y, previewItem.width, previewItem.height)
    }

    function commitSmartSelectStroke() {
        const outcome = editorPreview.commitSmartSelectStroke(
                    editRoot.objectPickMode,
                    editRoot.objectPickCreateNew)
        smartSelectModifierPromptHandled = false
        editRoot.handleMaskPickOutcome(outcome)
    }

    function handleSmartSelectPromptModifier(mouse) {
        if (!editRoot.objectPickArmed)
            return false
        if (editRoot.objectPickMode === "selection-subtract")
            return false
        const addPositivePrompt = Boolean(mouse.modifiers & Qt.ShiftModifier)
        const addNegativePrompt = Boolean(mouse.modifiers & Qt.AltModifier)
        if (!addPositivePrompt && !addNegativePrompt)
            return false
        const point = normalizedStrokePoint(mouse.x, mouse.y)
        if (addNegativePrompt) {
            editorPreview.refineSmartSelectPreviewWithPrompt(point.x, point.y, true)
        } else if (editorPreview.smartSelectPreviewActive) {
            editorPreview.refineSmartSelectPreviewWithPrompt(point.x, point.y, false)
        } else {
            editorPreview.updateSmartSelectPreviewPath([{
                type: "positive-point",
                x: point.x,
                y: point.y
            }])
        }
        smartSelectModifierPromptHandled = true
        editorPreview.resetSmartSelectStroke()
        return true
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
            return selectionOperationOverlayColor("subtract")
        return selectionOperationOverlayColor("add")
    }

    function selectionOperationOverlayColor(operationName) {
        const operation = String(operationName || "add").trim().toLowerCase()
        if (operation === "subtract")
            return "#ff8a4c"
        if (operation === "intersect")
            return "#8fa8ff"
        return "#24dcc2"
    }

    function defaultMaskOverlayColor() {
        return "#24dcc2"
    }

    function resolvedMaskOverlayColor() {
        const customColor = String(editorPreview.maskPreviewOverlayColor || "")
        return customColor.length > 0 ? customColor : defaultMaskOverlayColor()
    }

    function objectPickActionLabel() {
        if (editRoot.objectPickMode === "remove")
            return editRoot.objectPickCreateNew ? "New Remove Target" : "Remove Object"
        if (editRoot.objectPickMode === "selection-subtract")
            return "Subtract Object"
        return editRoot.objectPickCreateNew ? "New Object Selection" : "Add Object"
    }

    function brushCursorState(mask) {
        if (root.brushResizeActive)
            return "brush-resize"
        if (mask && mask.retouchMode === "clone" &&
                (editRoot.cloneSourcePickArmed || !(mask.cloneSourceSet || false)))
            return "clone-source"
        if (mask && mask.toolRole === "retouch")
            return "retouch-apply"
        return editorPreview.activeMaskOperation === "subtract" ? "erase" : "add"
    }

    function selectionCursorState() {
        if (editRoot.cloneSourcePickArmed)
            return "clone-source"
        if (editRoot.objectPickArmed) {
            if (editRoot.objectPickMode === "remove")
                return "retouch-apply"
            if (editRoot.objectPickMode === "selection-subtract")
                return "subtract"
            return "add"
        }
        if (editRoot.polygonDrawArmed)
            return editorPreview.activeMaskPolygonPointCount >= 3 ? "close-polygon" : "polygon-point"
        if (editRoot.lassoDrawArmed)
            return editRoot.manualSelectionSubtractActive() ? "subtract" : "add"
        return "add"
    }

    function cursorShapeForState(cursorState) {
        if (cursorState === "clone-source" || cursorState === "close-polygon")
            return Qt.PointingHandCursor
        if (cursorState === "brush-resize")
            return Qt.SizeAllCursor
        return Qt.CrossCursor
    }

    function brushPercentText(value, minimum, maximum) {
        const numericValue = Number(value)
        if (!isFinite(numericValue))
            return "--%"
        const lowerBound = minimum === undefined ? 0.0 : minimum
        const upperBound = maximum === undefined ? 1.0 : maximum
        return Math.round(Math.max(lowerBound, Math.min(upperBound, numericValue)) * 100).toString() + "%"
    }

    function brushCursorReadout() {
        const sizeText = editorPreview.activeMaskBrushSizeSummary || "Size --"
        return sizeText +
               "  Hard " + brushPercentText(editorPreview.activeMaskBrushHardness, 0.0, 1.0) +
               "  Flow " + brushPercentText(editorPreview.activeMaskBrushFlow, 0.05, 1.0) +
               "  Density " + brushPercentText(editorPreview.activeMaskDensity, 0.0, 1.0)
    }

    function objectPickInstructionText() {
        const previewReady = editorPreview.smartSelectPreviewActive
        const cycleHint = editorPreview.smartSelectCanCycleCandidates
                ? " Use Tab to cycle candidates."
                : ""
        if (editRoot.objectPickMode === "remove") {
            return previewReady
                    ? "Release to stage the highlighted object plus likely edge, shadow, or reflection pixels for removal." + cycleHint
                    : (editRoot.objectPickCreateNew
                       ? "Hover an object, then click or drag to start a new assisted removal target."
                       : "Hover an object, then click or drag to add assisted object detail to the staged removal.")
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
        if (editorPreview.maskPreviewTemporarilyHidden === true ||
                editorPreview.maskPreviewMode === "none")
            return false
        if (active)
            return true
        if (editorPreview.maskPreviewShowAll !== true)
            return false
        return mask.toolRole !== "retouch"
    }

    function previewMatteOpacity(isCandidate) {
        if (editorPreview.maskPreviewTemporarilyHidden === true)
            return 0.0
        const opacityScale = Math.max(0.0, Math.min(1.0, Number(editorPreview.maskPreviewOverlayOpacity || 0)))
        if (editorPreview.maskPreviewMode === "none")
            return 0.0
        if (editorPreview.maskPreviewMode === "overlay")
            return opacityScale
        if (editorPreview.maskPreviewMode === "color-on-bw" ||
                editorPreview.maskPreviewMode === "image-on-bw" ||
                editorPreview.maskPreviewMode === "on-black" ||
                editorPreview.maskPreviewMode === "on-white" ||
                editorPreview.maskPreviewMode === "black-on-white" ||
                editorPreview.maskPreviewMode === "white-on-black")
            return 1.0 * opacityScale
        if (editorPreview.maskPreviewMode === "grayscale")
            return (isCandidate ? 0.58 : 0.70) * opacityScale
        return (isCandidate ? 0.86 : 0.92) * opacityScale
    }

    function previewImageLayerVisible() {
        return false
    }

    function cleanVectorMaskFillVisible() {
        return editorPreview.maskPreviewMode !== "none" &&
               editorPreview.maskPreviewMode !== "outline" &&
               editorPreview.maskPreviewMode !== "marching-ants"
    }

    function usesBlackWhiteImageContext() {
        return editorPreview.maskPreviewTemporarilyHidden !== true &&
               (editorPreview.maskPreviewMode === "color-on-bw" ||
               editorPreview.maskPreviewMode === "image-on-bw"
               )
    }

    function liveBrushOverlayColor(mask) {
        if (mask && mask.toolRole === "retouch") {
            if (mask.retouchMode === "clone")
                return "#7cb5ff"
            if (mask.retouchMode === "heal")
                return "#72cbb2"
            return "#d98962"
        }
        return resolvedMaskOverlayColor()
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
        ctx.moveTo(root.previewOutlinePointX(root.pointXValue(points[0])),
                   root.previewOutlinePointY(root.pointYValue(points[0])))
        for (let i = 1; i < count; ++i)
            ctx.lineTo(root.previewOutlinePointX(root.pointXValue(points[i])),
                       root.previewOutlinePointY(root.pointYValue(points[i])))
        if (count >= 3)
            ctx.closePath()
        return true
    }

    function displayOutlinePathLargeEnough(points, isCandidate) {
        if (isCandidate || !points || points.length < 2)
            return true
        let minX = 1.0
        let minY = 1.0
        let maxX = 0.0
        let maxY = 0.0
        for (let i = 0; i < points.length; ++i) {
            const x = root.pointXValue(points[i])
            const y = root.pointYValue(points[i])
            minX = Math.min(minX, x)
            minY = Math.min(minY, y)
            maxX = Math.max(maxX, x)
            maxY = Math.max(maxY, y)
        }
        const pixelWidth = Math.abs(maxX - minX) * previewItem.width
        const pixelHeight = Math.abs(maxY - minY) * previewItem.height
        return Math.max(pixelWidth, pixelHeight) >= 3.0 && pixelWidth * pixelHeight >= 18.0
    }

    function strokeOutlinePaths(ctx, outlinePaths, accentColor, isCandidate) {
        if (!outlinePaths || outlinePaths.length === 0)
            return

        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        const marching = editorPreview.maskPreviewMode === "marching-ants"
        const dashOn = root.stableOutlineDash(10)
        const dashOff = root.stableOutlineDash(7)
        const phase = marching && root.outlinePhaseAnimationEnabled
                      ? root.stableOutlineDash(editRoot.selectionOutlinePhase)
                      : 0

        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const accentPoints = outlinePaths[pathIndex]
            if (!root.displayOutlinePathLargeEnough(accentPoints, isCandidate))
                continue
            if (!root.buildOutlinePath(ctx, accentPoints))
                continue
            if (ctx.setLineDash)
                ctx.setLineDash(marching ? [dashOn, dashOff] : [])
            ctx.lineDashOffset = marching ? -phase : 0
            ctx.lineWidth = root.stableOutlineWidth(isCandidate ? 2.2 : 2)
            ctx.strokeStyle = Qt.alpha(accentColor, marching ? 0.95 : (isCandidate ? 0.84 : 0.76))
            ctx.stroke()
        }
        if (ctx.setLineDash)
            ctx.setLineDash([])
    }

    function fillOutlinePaths(ctx, outlinePaths, accentColor, opacity) {
        if (!root.cleanVectorMaskFillVisible() ||
                !outlinePaths || outlinePaths.length === 0)
            return
        const opacityScale = Math.max(0.0, Math.min(1.0, Number(editorPreview.maskPreviewOverlayOpacity || 0)))
        ctx.fillStyle = theme.alpha(accentColor, Math.max(0.0, Math.min(1.0, opacity)) * opacityScale)
        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const points = outlinePaths[pathIndex]
            if (!root.buildOutlinePath(ctx, points))
                continue
            ctx.fill()
        }
    }

    function distanceToSegmentPixels(px, py, ax, ay, bx, by) {
        const dx = bx - ax
        const dy = by - ay
        const lengthSquared = dx * dx + dy * dy
        if (lengthSquared <= 0.0001) {
            const pointDx = px - ax
            const pointDy = py - ay
            return Math.sqrt(pointDx * pointDx + pointDy * pointDy)
        }
        const t = Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / lengthSquared))
        const nearestX = ax + t * dx
        const nearestY = ay + t * dy
        const hitDx = px - nearestX
        const hitDy = py - nearestY
        return Math.sqrt(hitDx * hitDx + hitDy * hitDy)
    }

    function outlinePathsHitTest(mouseX, mouseY, outlinePaths, slopPixels) {
        if (!outlinePaths || outlinePaths.length === 0)
            return false
        const slop = Math.max(6, Number(slopPixels || 10))
        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const points = outlinePaths[pathIndex] || []
            if (points.length < 2)
                continue
            for (let pointIndex = 1; pointIndex < points.length; ++pointIndex) {
                const previous = points[pointIndex - 1]
                const current = points[pointIndex]
                if (root.distanceToSegmentPixels(
                            mouseX, mouseY,
                            root.previewPointX(root.pointXValue(previous)),
                            root.previewPointY(root.pointYValue(previous)),
                            root.previewPointX(root.pointXValue(current)),
                            root.previewPointY(root.pointYValue(current))) <= slop) {
                    return true
                }
            }
            if (points.length >= 3) {
                const first = points[0]
                const last = points[points.length - 1]
                if (root.distanceToSegmentPixels(
                            mouseX, mouseY,
                            root.previewPointX(root.pointXValue(last)),
                            root.previewPointY(root.pointYValue(last)),
                            root.previewPointX(root.pointXValue(first)),
                            root.previewPointY(root.pointYValue(first))) <= slop) {
                    return true
                }
            }
        }
        return false
    }

    function selectActiveOutlineFromCanvas(mouseX, mouseY) {
        if (!editorPreview.hasActiveMask || !activeSelectionOutline.visible)
            return false
        if (!root.outlinePathsHitTest(
                    mouseX, mouseY,
                    activeSelectionOutline.normalizedOutlinePathsCache,
                    12)) {
            return false
        }
        if (!editRoot.activeMaskSetData || !editRoot.selectMaskSetAndLayer)
            return false
        editRoot.selectMaskSetAndLayer(editRoot.activeMaskSetData(), false, true)
        return true
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
                    ? "Click points to subtract. Click the first point or double-click to finish. Backspace removes the last point."
                    : "Click points. Hold Option to subtract. Click the first point or double-click to finish. Backspace removes the last point."
        return root.objectPickInstructionText()
    }

    Item {
        id: previewItem
        x: root.imageX
        y: root.imageY
        width: root.imageWidth
        height: root.imageHeight
        visible: root.visible
        clip: true
        readonly property string overlayGeometryContract: root.overlayGeometryContract

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
                readonly property color overlayAccent: root.resolvedMaskOverlayColor()
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
                            GradientStop { position: 0.0; color: Qt.alpha(overlayAccent, 0.36) }
                            GradientStop {
                                position: Math.max(0.0, linearOverlay.positionValue - linearOverlay.featherValue * 0.5)
                                color: Qt.alpha(overlayAccent, 0.16)
                            }
                            GradientStop { position: linearOverlay.positionValue; color: Qt.rgba(0.95, 0.97, 0.98, 0.74) }
                            GradientStop {
                                position: Math.min(1.0, linearOverlay.positionValue + linearOverlay.featherValue * 0.5)
                                color: Qt.alpha(overlayAccent, 0.08)
                            }
                            GradientStop { position: 1.0; color: Qt.alpha(overlayAccent, 0.0) }
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
                            ctx.fillStyle = Qt.alpha(overlayAccent, active ? 0.20 : 0.11)
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
                    readonly property int liveOverlayFrameBudgetMs: 16
                    readonly property int liveOverlayPointBudget: 512
                    readonly property string liveOverlayPerformanceContract: "live-brush-overlay|ui-frame-budget:16ms|point-budget:512|no-preview-render-block:v1"
                    readonly property var liveBrushPoints: editorPreview.activeMaskLiveBrushPoints || []
                    readonly property bool showStrokePoints:
                        active && (editorPreview.brushStrokeActive || liveBrushPoints.length > 0)
                    readonly property bool isStaged: mask.toolRole === "retouch" && !(mask.committed || false)
                    anchors.fill: parent
                    opacity: isStaged ? Math.max(emphasis * 0.7, 0.2) : emphasis
                    z: brushOverlay.showStrokePoints ? 98 : 0

                    Canvas {
                        id: brushCanvas
                        anchors.fill: parent
                        visible: brushOverlay.showStrokePoints

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

                            const liveStroke = brushOverlay.showStrokePoints
                            const points = liveStroke
                                            ? brushOverlay.liveBrushPoints
                                            : (brushOverlay.showStrokePoints ? (mask.brushPoints || []) : [])
                            if (points.length === 0)
                                return
                            const paintCount = Math.min(points.length, brushOverlay.liveOverlayPointBudget)
                            const paintStart = Math.max(0, points.length - paintCount)

                            const overlayColor = liveStroke
                                                  ? root.liveBrushOverlayColor(mask)
                                                  : accent
                            const overlayOpacity = liveStroke
                                                   ? root.liveBrushOverlayOpacity(mask)
                                                   : 0.34

                            const first = points[paintStart]
                            ctx.fillStyle = Qt.alpha(overlayColor, liveStroke ? overlayOpacity : pointAlpha(first))
                            ctx.beginPath()
                            ctx.arc(pointX(first), pointY(first), pointDiameter(first) / 2, 0, Math.PI * 2)
                            ctx.fill()

                            for (let i = paintStart + 1; i < points.length; ++i) {
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
                                 brushOverlay.showStrokePoints &&
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
                                 brushOverlay.showStrokePoints &&
                                 editorPreview.activeMaskLiveBrushPreviewNativeAvailable
                        controller: editorPreview
                    }

                    Rectangle {
                        property real sourceX: (mask.cloneSourceX || 0.5) * brushOverlay.width
                        property real sourceY: (mask.cloneSourceY || 0.5) * brushOverlay.height
                        property real targetX: Math.max(0, Math.min(1, (mask.cloneSourceX || 0.5) - (mask.cloneOffsetX || 0))) * brushOverlay.width
                        property real targetY: Math.max(0, Math.min(1, (mask.cloneSourceY || 0.5) - (mask.cloneOffsetY || 0))) * brushOverlay.height
                        property real offsetLength: Math.sqrt(Math.pow(targetX - sourceX, 2) + Math.pow(targetY - sourceY, 2))
                        visible: mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
                        x: sourceX
                        y: sourceY - height / 2
                        width: Math.max(1, offsetLength)
                        height: 2
                        radius: 1
                        color: "#7cb5ff"
                        opacity: 0.78
                        rotation: Math.atan2(targetY - sourceY, targetX - sourceX) * 180 / Math.PI
                        transformOrigin: Item.Left
                    }

                    Item {
                        visible: mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
                        x: Math.max(0, Math.min(1, (mask.cloneSourceX || 0.5) - (mask.cloneOffsetX || 0))) * brushOverlay.width - width / 2
                        y: Math.max(0, Math.min(1, (mask.cloneSourceY || 0.5) - (mask.cloneOffsetY || 0))) * brushOverlay.height - height / 2
                        width: Math.max(18, Math.min(58, (mask.brushSize || 0.07) * brushOverlay.width * (mask.cloneScale || 1.0)))
                        height: width

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: "#d4fff814"
                            border.width: 1.5
                            border.color: "#d4fff8"
                            rotation: mask.cloneRotation || 0
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width * 0.58
                            height: 2
                            radius: 1
                            color: "#d4fff8"
                            rotation: mask.cloneRotation || 0
                        }
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
                        objectName: "maskCanvasBrushStrokeTabletInput"
                        readonly property string inputHandlerRole: "brush-stroke"
                        readonly property string pointerGrabPolicy: "exclusive-brush-stroke"
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
                                editRoot.clearTransientMaskTools()
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
                        objectName: "maskCanvasBrushStrokeMouseInput"
                        readonly property string inputHandlerRole: root.brushResizeActive
                                                                   ? "brush-resize"
                                                                   : "brush-stroke"
                        readonly property string pointerGrabPolicy: "exclusive-brush-stroke"
                        readonly property string cursorState: root.brushCursorState(mask)
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        enabled: active && !editRoot.spacePanActive
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: root.cursorShapeForState(cursorState)
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
                            editRoot.applyMaskOperationForModifierState(mouse.modifiers)
                            editorPreview.beginActiveBrushStroke()
                            root.hoverX = mouse.x
                            root.hoverY = mouse.y
                            root.hoverActive = true
                            if (mask.retouchMode === "clone" &&
                                    (editRoot.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                editorPreview.setActiveMaskCloneSource(
                                            mouse.x / brushOverlay.width,
                                            mouse.y / brushOverlay.height)
                                editRoot.clearTransientMaskTools()
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
                        antialiasing: true
                        anchors.fill: parent
                        readonly property bool polygonFeatherVisualizationActive:
                            active &&
                            (mask.kind === "polygon" || mask.kind === "lasso") &&
                            (mask.polygonPoints || []).length >= 3 &&
                            Number(mask.feather || 0) > 0.001
                        readonly property real polygonFeatherVisualizationWidth:
                            polygonFeatherVisualizationActive
                            ? Math.max(6, Math.min(96, Math.min(width, height) * Number(mask.feather || 0) * 2))
                            : 0

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
                                if (polygonCanvas.polygonFeatherVisualizationActive) {
                                    if (ctx.setLineDash)
                                        ctx.setLineDash([])
                                    ctx.save()
                                    ctx.lineWidth = root.stableOutlineWidth(polygonCanvas.polygonFeatherVisualizationWidth)
                                    ctx.strokeStyle = Qt.alpha(overlayAccent, 0.16)
                                    ctx.stroke()
                                    ctx.restore()
                                }
                                root.strokeOutlinePaths(ctx, [points], overlayAccent, false)
                            } else {
                                ctx.strokeStyle = Qt.alpha("#f4f7fa", active ? 0.88 : 0.55)
                                ctx.lineWidth = root.stableOutlineWidth(active ? 1.8 : 1.1)
                                if (ctx.setLineDash)
                                    ctx.setLineDash([])
                                ctx.stroke()
                            }
                        }
                    }

                    Repeater {
                        model: active && mask.kind === "polygon" && (mask.polygonPoints || []).length >= 3
                               ? (mask.polygonPoints || [])
                               : []

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            property int edgeIndex: index
                            readonly property var allPoints: mask.polygonPoints || []
                            readonly property var nextPoint: allPoints.length > 0
                                                             ? allPoints[(edgeIndex + 1) % allPoints.length]
                                                             : modelData
                            readonly property real normalizedX: (root.pointXValue(modelData) + root.pointXValue(nextPoint)) / 2
                            readonly property real normalizedY: (root.pointYValue(modelData) + root.pointYValue(nextPoint)) / 2

                            objectName: "polygonEdgeInsertHandle" + edgeIndex
                            readonly property string handleReadabilityContract: root.handleReadabilityContract
                            width: 14
                            height: 14
                            radius: 3
                            rotation: 45
                            x: root.previewPointX(normalizedX) - width / 2
                            y: root.previewPointY(normalizedY) - height / 2
                            color: "transparent"
                            border.width: 0
                            visible: active

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 8
                                height: parent.height + 8
                                radius: parent.radius + 2
                                color: root.handleOuterHaloColor()
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 4
                                height: parent.height + 4
                                radius: parent.radius + 1
                                color: root.handleInnerHaloColor()
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                color: Qt.alpha(accent, 0.86)
                            }

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -8
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton

                                onClicked: function(mouse) {
                                    mouse.accepted = true
                                    editorPreview.insertActivePolygonPointAfter(
                                        parent.edgeIndex,
                                        parent.normalizedX,
                                        parent.normalizedY)
                                }
                            }
                        }
                    }

                    Repeater {
                        model: mask.polygonPoints || []

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            property int pointIndex: index
                            property real grabOffsetX: width / 2
                            property real grabOffsetY: height / 2

                            objectName: "polygonVertexHandle" + pointIndex
                            readonly property string handleReadabilityContract: root.handleReadabilityContract
                            width: 14
                            height: width
                            radius: width / 2
                            x: root.previewPointX(root.pointXValue(modelData)) - width / 2
                            y: root.previewPointY(root.pointYValue(modelData)) - height / 2
                            color: "transparent"
                            border.width: 0
                            visible: active

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 8
                                height: parent.height + 8
                                radius: width / 2
                                color: root.handleOuterHaloColor()
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 4
                                height: parent.height + 4
                                radius: width / 2
                                color: root.handleInnerHaloColor()
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: width / 2
                                color: active ? Qt.alpha(accent, 0.88) : root.handleWarningFillColor()
                            }

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -8
                                enabled: active && mask.kind === "polygon"
                                hoverEnabled: true
                                cursorShape: Qt.SizeAllCursor
                                acceptedButtons: Qt.LeftButton

                                onPressed: function(mouse) {
                                    mouse.accepted = true
                                    parent.grabOffsetX = mouse.x
                                    parent.grabOffsetY = mouse.y
                                }

                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return
                                    const previewX = parent.x + mouse.x - parent.grabOffsetX + parent.width / 2
                                    const previewY = parent.y + mouse.y - parent.grabOffsetY + parent.height / 2
                                    const nextX = Math.max(0, Math.min(1, previewX / Math.max(1, previewItem.width)))
                                    const nextY = Math.max(0, Math.min(1, previewY / Math.max(1, previewItem.height)))
                                    editorPreview.moveActivePolygonPoint(parent.pointIndex, nextX, nextY)
                                    mouse.accepted = true
                                }
                            }
                        }
                    }

                    Rectangle {
                        objectName: "polygonSnapCloseTarget"
                        readonly property string handleReadabilityContract: root.handleReadabilityContract
                        readonly property var snapPoints: mask.polygonPoints || []
                        readonly property var firstPoint: snapPoints.length > 0 ? snapPoints[0] : ({ "x": 0.5, "y": 0.5 })
                        visible: active &&
                                 mask.kind === "polygon" &&
                                 snapPoints.length >= 3 &&
                                 !editorPreview.activeMaskPolygonSelfIntersecting
                        x: root.previewPointX(root.pointXValue(firstPoint)) - width / 2
                        y: root.previewPointY(root.pointYValue(firstPoint)) - height / 2
                        width: 32
                        height: 32
                        radius: 16
                        color: Qt.alpha("#f0c46e", 0.14)
                        border.width: 3
                        border.color: root.handleInnerHaloColor()

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -4
                            radius: width / 2
                            color: "transparent"
                            border.width: 2
                            border.color: root.handleOuterHaloColor()
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 3
                            radius: width / 2
                            color: "transparent"
                            border.width: 2
                            border.color: root.handleWarningFillColor()
                        }
                    }
                }

                Rectangle {
                    visible: active &&
                              (mask.kind === "luminance" ||
                               mask.kind === "color" ||
                               mask.kind === "depth")
                    anchors.fill: parent
                    color: Qt.alpha(overlayAccent, 0.08)
                    border.width: 1
                    border.color: Qt.alpha(overlayAccent, 0.28)
                    radius: 4
                    opacity: emphasis

                    Rectangle {
                        anchors.centerIn: parent
                        radius: 10
                        color: Qt.alpha("#101419", 0.72)
                        border.width: 1
                        border.color: Qt.alpha(overlayAccent, 0.55)
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

        HoverHandler {
            objectName: "maskCanvasHoverInputHandler"
            readonly property string inputHandlerRole: "hover"
            enabled: editRoot.objectPickArmed ||
                     (editorPreview.hasActiveMask &&
                      editorPreview.activeMaskKind === "brush" &&
                      !editRoot.objectPickArmed &&
                      !editRoot.polygonDrawArmed &&
                      !editRoot.lassoDrawArmed)

            onHoveredChanged: root.hoverActive = hovered
            onPointChanged: {
                root.hoverX = point.position.x
                root.hoverY = point.position.y
                root.hoverActive = true
                root.brushCursorPositionKnown = true
            }
        }

        Item {
            id: brushCursor
            objectName: "maskCanvasBrushCursor"
            readonly property bool brushMode: editorPreview.hasActiveMask &&
                                             editorPreview.activeMaskKind === "brush" &&
                                             !editRoot.objectPickArmed &&
                                             !editRoot.polygonDrawArmed &&
                                             !editRoot.lassoDrawArmed
            readonly property real brushDiameter: Math.max(
                8,
                Math.min(previewItem.width, previewItem.height) *
                    editorPreview.activeMaskBrushSize * 2)
            readonly property real hardnessDiameter: Math.max(
                2,
                brushDiameter * Math.max(0.0, Math.min(1.0, editorPreview.activeMaskBrushHardness)))
            readonly property string brushCursorVisibilityContract: "persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1"
            readonly property bool cursorPositionReady: root.brushCursorPositionKnown ||
                                                       root.hoverActive ||
                                                       root.brushResizeActive ||
                                                       editorPreview.brushStrokeActive
            readonly property color accentColor: editRoot.objectPickArmed
                                                ? root.objectPickAccentColor()
                                                : (editorPreview.activeMaskRetouchMode === "remove"
                                                   ? "#d98962"
                                                   : editorPreview.activeMaskRetouchMode === "clone"
                                                     ? "#7cb5ff"
                                                     : editorPreview.activeMaskRetouchMode === "heal"
                                                       ? "#72cbb2"
                                                       : root.selectionOperationOverlayColor(editorPreview.activeMaskOperation))
            visible: ((brushMode && cursorPositionReady) ||
                      (editRoot.objectPickArmed && root.hoverActive))
            x: root.hoverX - width / 2
            y: root.hoverY - height / 2
            width: editRoot.objectPickArmed
                   ? 42
                   : brushDiameter
            height: width
            z: 98

            Rectangle {
                objectName: "maskCanvasBrushCursorOuterHaloRing"
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: brushCursor.brushMode ? 3.0 : 2.0
                border.color: theme.alpha(theme.ink950, 0.92)
            }

            Rectangle {
                objectName: "maskCanvasBrushCursorFeatherRing"
                anchors.fill: parent
                anchors.margins: 1
                radius: width / 2
                color: Qt.alpha(
                           editRoot.objectPickArmed
                           ? brushCursor.accentColor
                           : theme.accentWarm300,
                           editRoot.objectPickArmed ? 0.10 : (root.brushResizeActive ? 0.0 : 0.035))
                border.width: brushCursor.brushMode ? 1.5 : 1
                border.color: root.brushResizeActive
                              ? theme.alpha("#f6f3ed", 0.98)
                              : brushCursor.accentColor
            }

            Rectangle {
                objectName: "maskCanvasBrushCursorHardnessRing"
                anchors.centerIn: parent
                visible: brushCursor.brushMode
                width: brushCursor.hardnessDiameter
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 1.5
                border.color: theme.alpha("#f4f7fa", 0.92)
            }

            Label {
                objectName: "maskCanvasBrushCursorReadout"
                visible: brushCursor.brushMode
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: 5
                text: root.brushCursorReadout()
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 10
                font.letterSpacing: 0
                padding: 4
                background: Rectangle {
                    color: theme.alpha(theme.ink950, 0.82)
                    border.width: theme.chromeBorderWidth
                    border.color: theme.alpha("#f4f7fa", 0.24)
                    radius: 2
                }
            }
        }

        Image {
            id: activeSelectionMatte
            objectName: "activeSelectionMatte"
            anchors.fill: parent
            readonly property bool rasterPreviewVisible:
                editorPreview.hasActiveMask &&
                editorPreview.maskPreviewTemporarilyHidden !== true &&
                root.previewImageLayerVisible()
            visible: rasterPreviewVisible
            source: ""
            fillMode: Image.Stretch
            smooth: true
            mipmap: false
            cache: false
            opacity: root.previewMatteOpacity(false)
            z: 95
        }

        Rectangle {
            objectName: "activeSelectionBwBackdrop"
            anchors.fill: parent
            visible: root.usesBlackWhiteImageContext() &&
                      editorPreview.hasActiveMask &&
                      editorPreview.maskPreviewTemporarilyHidden !== true &&
                      root.previewImageLayerVisible()
            color: "#d8dce0"
            opacity: 0.34
            z: 94
        }

        Canvas {
            id: activeSelectionOutline
            objectName: "activeSelectionOutline"
            antialiasing: true
            anchors.fill: parent
            readonly property string outlineQualityTier: root.activeOutlineQualityTier
            readonly property string outlineQualityLadderContract: root.outlineQualityLadderContract
            readonly property string outlineTraceContract: root.outlineTraceContract
            readonly property string outlineTraceSource: root.activeOutlineTraceSource
            readonly property int outlineTracePathCount: root.activeOutlineTracePathCount
            readonly property real outlineTraceTolerancePixels: root.activeOutlineTraceTolerancePixels
            readonly property real outlineTraceToleranceNormalized: root.activeOutlineTraceToleranceNormalized
            readonly property bool outlineTraceWithinBudget: root.activeOutlineTraceWithinBudget
            readonly property string outlineCoordinateSpace: root.activeOutlineCoordinateSpace
            readonly property string outlineCoordinateContract: root.activeOutlineCoordinateContract
            readonly property string outlineAlphaSourceContract: root.activeOutlineAlphaSourceContract
            readonly property string outlineTraceProviderContract: root.activeOutlineTraceProviderContract
            readonly property string outlineTraceProvider: root.activeOutlineTraceProvider
            readonly property string outlineTraceProviderSignature: root.activeOutlineTraceProviderSignature
            readonly property string outlineTraceMaskAlphaSignature: root.activeOutlineTraceMaskAlphaSignature
            readonly property var outlinePaths:
                editorPreview.hasActiveMask &&
                editorPreview.maskPreviewTemporarilyHidden !== true &&
                editorPreview.maskPreviewMode !== "none"
                ? editorPreview.activeMaskOutlinePaths
                : []
            readonly property var normalizedOutlinePathsCache:
                root.normalizedOutlinePaths(outlinePaths)
            visible: editorPreview.hasActiveMask &&
                      editorPreview.maskPreviewTemporarilyHidden !== true &&
                      editorPreview.maskPreviewMode !== "none" &&
                      normalizedOutlinePathsCache.length > 0
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
                    if (root.outlinePhaseAnimationEnabled)
                        root.requestCanvasPaint(activeSelectionOutline)
                }

                function onSelectionOutlineAnimationEnabledChanged() {
                    root.requestCanvasPaint(activeSelectionOutline)
                }
            }

            onPaint: {
                const outlinePaths = activeSelectionOutline.normalizedOutlinePathsCache
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                if (!outlinePaths || outlinePaths.length === 0)
                    return

                const activeMask = editorPreview.activeMaskIndex >= 0
                                   ? editorPreview.maskStack[editorPreview.activeMaskIndex]
                                   : null
                root.strokeOutlinePaths(ctx, outlinePaths, root.resolvedMaskOverlayColor(), false)
            }
        }

        MouseArea {
            objectName: "maskCanvasActiveOutlineHitTarget"
            readonly property string selectionSyncContract: "canvas-outline-hit-test|selectMaskSetAndLayer|preserve-inspector:v1"
            x: 0
            y: 0
            width: previewItem.width
            height: previewItem.height
            enabled: activeSelectionOutline.visible &&
                     !editRoot.spacePanActive &&
                     !editRoot.objectPickArmed &&
                     !editRoot.polygonDrawArmed &&
                     !editRoot.lassoDrawArmed &&
                     editRoot.editTool !== "brush"
            acceptedButtons: Qt.LeftButton
            hoverEnabled: false
            preventStealing: false
            cursorShape: Qt.PointingHandCursor
            z: 98

            onClicked: function(mouse) {
                if (!root.selectActiveOutlineFromCanvas(mouse.x, mouse.y))
                    mouse.accepted = false
            }
        }

        Rectangle {
            readonly property rect patchBounds: editorPreview.activeInpaintPatchBounds

            visible: editorPreview.inpaintPatchDiagnosticsAvailable &&
                     editorPreview.inpaintPatchDiagnosticsEnabled &&
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
            readonly property string smartPreviewRasterContract: "disabled|outline-only-candidate|no-speckled-matte:v1"
            visible: false
            source: ""
            fillMode: Image.Stretch
            smooth: true
            mipmap: false
            cache: false
            opacity: root.previewMatteOpacity(true)
            z: 96
        }

        Canvas {
            id: smartPreviewOutline
            objectName: "smartSelectPreviewOutline"
            anchors.fill: parent
            readonly property string candidateOutlineRenderContract: "smart-selection-hover|candidate-outline|precommit:v1"
            readonly property string candidatePreviewPipelineSignature: editorPreview.smartSelectPreviewPipelineSignature
            readonly property bool candidateOutlineVisible: editRoot.objectPickArmed &&
                                                            editorPreview.smartSelectPreviewActive &&
                                                            root.normalizedOutlinePath(editorPreview.smartSelectPreviewOutlinePoints).length >= 2
            visible: candidateOutlineVisible
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
                    if (root.outlinePhaseAnimationEnabled)
                        root.requestCanvasPaint(smartPreviewOutline)
                }

                function onSelectionOutlineAnimationEnabledChanged() {
                    root.requestCanvasPaint(smartPreviewOutline)
                }
            }

            onVisibleChanged: root.requestCanvasPaint(smartPreviewOutline)

            onPaint: {
                const points = root.normalizedOutlinePath(editorPreview.smartSelectPreviewOutlinePoints)
                const count = points.length
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                if (count < 2)
                    return

                root.fillOutlinePaths(ctx, [points], root.objectPickAccentColor(), 0.20)
                root.strokeOutlinePaths(ctx, [points], root.objectPickAccentColor(), true)
            }
        }

        MouseArea {
            objectName: "maskCanvasSelectionDragMouseInput"
            readonly property string inputHandlerRole: editRoot.objectPickArmed
                                                       ? "smart-selection-drag"
                                                       : editRoot.lassoDrawArmed
                                                         ? "lasso-drag"
                                                         : "polygon-tap"
            readonly property string pointerGrabPolicy: "exclusive-selection-drag"
            readonly property string cursorState: root.selectionCursorState()
            x: 0
            y: 0
            width: previewItem.width
            height: previewItem.height
            enabled: !editRoot.spacePanActive &&
                     (editRoot.objectPickArmed || editRoot.polygonDrawArmed || editRoot.lassoDrawArmed)
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            preventStealing: true
            cursorShape: root.cursorShapeForState(cursorState)
            z: 100

            onEntered: function(mouse) {
                if (editRoot.objectPickArmed)
                    root.scheduleSmartSelectPreviewDebounced(mouse.x / previewItem.width, mouse.y / previewItem.height, false)
            }

            onExited: {
                root.stopPendingSmartSelectPreview()
                if (editRoot.objectPickArmed && !editorPreview.smartSelectStrokeActive)
                    editorPreview.clearSmartSelectPreview()
            }

            onPressed: function(mouse) {
                editRoot.forceActiveFocus()
                if (editRoot.objectPickArmed) {
                    root.stopPendingSmartSelectPreview()
                    editRoot.applyObjectPickModeForModifierState(mouse.modifiers)
                    if (root.handleSmartSelectPromptModifier(mouse))
                        return
                    root.beginSmartSelectStroke(mouse.x, mouse.y)
                    return
                }
                if (!editRoot.lassoDrawArmed)
                    return
                const operation = editRoot.applyMaskOperationForModifierState(mouse.modifiers)
                editRoot.beginMaskShapeEditTransaction()
                if (!editorPreview.hasActiveMask || editorPreview.activeMaskKind !== "lasso")
                    editorPreview.startLassoSelection(operation === "subtract")
                if (editorPreview.hasActiveMask)
                    editorPreview.activeMaskOperation = operation
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onClicked: function(mouse) {
                if (editRoot.objectPickArmed)
                    return
                if (editRoot.lassoDrawArmed)
                    return
                if (editRoot.polygonDrawArmed) {
                    const operation = editRoot.applyMaskOperationForModifierState(mouse.modifiers)
                    if (editRoot.completeActivePolygonIfSnappingToFirst &&
                            editRoot.completeActivePolygonIfSnappingToFirst(
                                mouse.x / previewItem.width,
                                mouse.y / previewItem.height,
                                previewItem.width,
                                previewItem.height))
                        return
                    if (editorPreview.activeMaskKind === "polygon")
                        editorPreview.activeMaskOperation = operation
                    editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
                }
            }

            onPositionChanged: function(mouse) {
                if (editRoot.objectPickArmed) {
                    if (pressed)
                        root.updateSmartSelectStroke(mouse.x, mouse.y)
                    else
                        root.scheduleSmartSelectPreviewDebounced(mouse.x / previewItem.width, mouse.y / previewItem.height, false)
                }

                if (!editRoot.lassoDrawArmed || !pressed)
                    return
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onDoubleClicked: function(mouse) {
                if (!editRoot.polygonDrawArmed)
                    return
                if (editRoot.completeActivePolygonSelection && editRoot.completeActivePolygonSelection())
                    return
                editRoot.clearTransientMaskTools()
            }

            onReleased: function(mouse) {
                if (editRoot.objectPickArmed) {
                    if (root.smartSelectModifierPromptHandled) {
                        root.smartSelectModifierPromptHandled = false
                        return
                    }
                    root.commitSmartSelectStroke()
                    if (containsMouse)
                        root.scheduleSmartSelectPreviewDebounced(mouse.x / previewItem.width, mouse.y / previewItem.height, false)
                    return
                }
                if (!editRoot.lassoDrawArmed)
                    return
                if (editorPreview.activeMaskPolygonPointCount < 3) {
                    editorPreview.clearActivePolygonPoints()
                    editorPreview.removeActiveMask()
                } else {
                    editorPreview.closeActiveLassoPath()
                }
                editRoot.clearTransientMaskTools()
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
            objectName: "smartSelectPreviewCommitControls"
            readonly property string previewCommitContract: "preview-only|accept-reject|required-before-layer:v1"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 12
            visible: editRoot.objectPickArmed &&
                     editorPreview.smartSelectPreviewActive
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
                    objectName: "smartSelectPreviewPreviousButton"
                    text: "Prev"
                    compact: true
                    enabled: editorPreview.smartSelectCanCycleCandidates
                    accent: root.objectPickAccentColor()
                    tone: "secondary"
                    onClicked: editorPreview.cycleSmartSelectCandidate(-1)
                }

                Label {
                    objectName: "smartSelectCandidateHudLabel"
                    anchors.verticalCenter: parent.verticalCenter
                    text: editorPreview.smartSelectCandidateHudText
                    color: "#f2f4f6"
                    font.family: theme.fontUi
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                ActionButton {
                    objectName: "smartSelectPreviewNextButton"
                    text: "Next"
                    compact: true
                    enabled: editorPreview.smartSelectCanCycleCandidates
                    accent: root.objectPickAccentColor()
                    tone: "secondary"
                    onClicked: editorPreview.cycleSmartSelectCandidate(1)
                }

                ActionButton {
                    objectName: "smartSelectPreviewRejectButton"
                    text: "Reject"
                    compact: true
                    accent: root.objectPickAccentColor()
                    tone: "secondary"
                    onClicked: editorPreview.rejectSmartSelectPreview()
                }

                ActionButton {
                    objectName: "smartSelectPreviewAcceptButton"
                    text: "Accept"
                    compact: true
                    accent: root.objectPickAccentColor()
                    tone: "primary"
                    onClicked: {
                        editorPreview.acceptSmartSelectPreview(editRoot.objectPickMode,
                                                               editRoot.objectPickCreateNew)
                        editRoot.objectPickCreateNew = false
                    }
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
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 8
                    elide: Text.ElideNone
                }

                Label {
                    width: parent.width
                    text: root.previewInstructionBody()
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideNone
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
                text: "Size " + (editorPreview.activeMaskBrushSizeSummary || "Preview -- px | Stored -- radius | Final -- px") +
                      "  Flow " + Math.round(editorPreview.activeMaskBrushFlow * 100).toString() + "%"
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
    }
}
