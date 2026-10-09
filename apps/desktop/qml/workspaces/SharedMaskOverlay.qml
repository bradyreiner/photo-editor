pragma ComponentBehavior: Bound
import QtQuick
import PhotoEditor
import "../components"

Item {
    id: root

    objectName: objectNamePrefix.length > 0 ? objectNamePrefix + "SharedMaskOverlay" : "sharedMaskOverlay"
    clip: true

    Theme { id: theme }

    property string objectNamePrefix: ""
    property bool masked: false
    property bool hasActiveMask: editorPreview.hasActiveMask
    property bool compareMode: editorPreview.compareMode
    property bool activeBrushPainting: editorPreview.brushStrokeActive
    property bool enablePolygonHandles: true
    property bool enableOutlineSelection: true
    property color accentColor: "#24DCC2"
    property real outlinePhase: 0
    property bool outlineAnimationEnabled: false
    property real outlineVisualScale: 1.0
    property real cropX: editorPreview.cropRectX
    property real cropY: editorPreview.cropRectY
    property real cropWidth: editorPreview.cropRectWidth
    property real cropHeight: editorPreview.cropRectHeight
    property real transformRotation: editorPreview.transformRotation
    property real genericTintOpacity: 0.18
    property bool outlineActivationDeferred: false
    property bool selectionFillVisible: false
    property int externalFocusSerial: 0
    property var externalFocusRegion: ({})
    property var externalCandidateRegions: []
    property bool agentPointTargetingEnabled: false
    property var agentPointTargetRegion: ({})
    readonly property bool hasExternalFocusRegion: Number.isFinite(Number(externalFocusRegion.centerX)) &&
                                                   Number.isFinite(Number(externalFocusRegion.centerY)) &&
                                                   Number(externalFocusRegion.radiusX || 0) > 0 &&
                                                   Number(externalFocusRegion.radiusY || 0) > 0
    readonly property int sharedOverlayVersion: 1
    readonly property string overlayGeometryContract: "image-rect-clipped|crop-rotation-aware:v1"
    readonly property string outlineRenderContract: "anti-aliased|zoom-stable|subpixel-contours:v2"
    readonly property string outlineScaleBucketContract: "outline-scale-buckets:v1|power-of-two-ceil|precision-never-below-display-scale"
    readonly property string outlineAnimationContract: "visibility-gated|motion-budgeted|point-budgeted:v1"
    readonly property string handleReadabilityContract: "dual-contrast-halo|large-hit-target|color-independent:v1"
    readonly property string maskOverlayColorContract: "color-safe-overlay:v2|palette:standard-protanopia-deuteranopia-tritanopia-grayscale|operations:add-subtract-intersect|dual-contrast-halo:true|custom-color-override|retouch-exception"
    readonly property string maskRasterPreviewPolicy: "editor-canvas-vector-mask-preview|final-alpha-contour-fill-and-outline:v3"
    readonly property string liveOverlayPerformanceContract: "live-brush-overlay|ui-frame-budget:16ms|point-budget:512|no-preview-render-block:v1"
    readonly property string deepZoomCanvasPerformanceContract: "deep-zoom-canvas:v2|item:viewport-bounded|coordinates:window-translated|hidden:skip-paint"
    readonly property int maximumCanvasBackingEdge: 2048
    readonly property string settledMaskOutlineContract: "final-alpha-contour|overlay-fill|marching-ants:v2"
    readonly property string outlineTraceContract: "public-outline-trace|source|tolerance|provider|path-count|image-normalized|final-alpha:v2"
    readonly property var activeOutlineTrace: outlineActivationDeferred ? ({}) : (editorPreview.activeMaskOutlineTrace || ({}))
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
    readonly property color defaultMaskOverlayColor: theme.selectionPalette("add")
    readonly property color activeOperationColor: resolvedOverlayColor(
                                                      defaultMaskOverlayColor)
    readonly property bool deepZoomCanvasMode: Number(outlineVisualScale || 1.0) > 16.0
    property bool liveBrushVectorCanvasPaintPending: false
    property bool geometricMaskCanvasPaintPending: false
    property bool activeSelectionOutlinePaintPending: false
    readonly property rect visibleCanvasWindow: {
        const parentWidth = root.parent ? Math.max(0, Number(root.parent.width || 0)) : Math.max(0, root.width)
        const parentHeight = root.parent ? Math.max(0, Number(root.parent.height || 0)) : Math.max(0, root.height)
        const left = root.clamp(-root.x, 0, Math.max(0, root.width))
        const top = root.clamp(-root.y, 0, Math.max(0, root.height))
        const right = Math.min(
                    root.clamp(parentWidth - root.x, 0, Math.max(0, root.width)),
                    left + root.maximumCanvasBackingEdge)
        const bottom = Math.min(
                    root.clamp(parentHeight - root.y, 0, Math.max(0, root.height)),
                    top + root.maximumCanvasBackingEdge)
        if (right <= left || bottom <= top)
            return Qt.rect(0, 0, 1, 1)
        return Qt.rect(left, top, right - left, bottom - top)
    }

    signal activeOutlineClicked(real normalizedX, real normalizedY)
    signal agentPointTargeted(real normalizedX, real normalizedY)

    onExternalFocusSerialChanged: {
        if (externalFocusSerial <= 0 || (!hasExternalFocusRegion && !hasActiveMask))
            return
        agentFindingFocusPulse.opacity = 0.94
        agentFindingFocusPulse.scale = 0.72
        agentFindingFocusAnimation.restart()
    }

    Timer {
        id: sharedCanvasPaintTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (root.liveBrushVectorCanvasPaintPending &&
                    maskCanvasOverlay.visible && liveBrushVectorCanvas.visible) {
                liveBrushVectorCanvas.recordFramePhase("timer")
                liveBrushVectorCanvas.requestPaint()
            }
            if (root.geometricMaskCanvasPaintPending &&
                    maskCanvasOverlay.visible && geometricMaskCanvas.visible)
                geometricMaskCanvas.requestPaint()
            if (root.activeSelectionOutlinePaintPending && activeSelectionOutline.visible)
                activeSelectionOutline.requestPaint()
            root.liveBrushVectorCanvasPaintPending = false
            root.geometricMaskCanvasPaintPending = false
            root.activeSelectionOutlinePaintPending = false
        }
    }

    // New brush strokes and their traced alpha already use the visible photo's
    // normalized coordinates. Applying the source crop again displaces them.
    readonly property bool brushViewportCoordinates: editorPreview.activeMaskKind === "brush" &&
        ((activeBrushPainting && editorPreview.activeMaskToolRole !== "retouch") ||
         activeOutlineTrace.outline_brush_viewport_coordinates === true)

    CanvasCoordinateMapper {
        id: sharedCoordinateMapper
        contentX: 0
        contentY: 0
        contentWidth: root.width
        contentHeight: root.height
        cropX: root.brushViewportCoordinates ? 0 : root.cropX
        cropY: root.brushViewportCoordinates ? 0 : root.cropY
        cropWidth: root.brushViewportCoordinates ? 1 : root.cropWidth
        cropHeight: root.brushViewportCoordinates ? 1 : root.cropHeight
        rotationDegrees: root.brushViewportCoordinates ? 0 : root.transformRotation
        fitMode: "overlay"
        devicePixelRatio: Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
    }

    function scheduleLiveBrushVectorCanvasPaint() {
        root.liveBrushVectorCanvasPaintPending = true
        if (!sharedCanvasPaintTimer.running)
            sharedCanvasPaintTimer.start()
    }

    function scheduleGeometricMaskCanvasPaint() {
        root.geometricMaskCanvasPaintPending = true
        if (!sharedCanvasPaintTimer.running)
            sharedCanvasPaintTimer.start()
    }

    function scheduleActiveSelectionOutlinePaint() {
        root.activeSelectionOutlinePaintPending = true
        if (!sharedCanvasPaintTimer.running)
            sharedCanvasPaintTimer.start()
    }

    onVisibleCanvasWindowChanged: {
        scheduleLiveBrushVectorCanvasPaint()
        scheduleGeometricMaskCanvasPaint()
        scheduleActiveSelectionOutlinePaint()
    }

    function overlayObjectName(suffix, fallback) {
        return objectNamePrefix.length > 0 ? objectNamePrefix + suffix : fallback
    }

    function overlayGeometrySignature() {
        return root.overlayGeometryContract +
               "|clip:" + String(root.clip === true) +
               "|rect:" + Math.round(root.x) + "," + Math.round(root.y) + "," +
               Math.round(root.width) + "x" + Math.round(root.height) +
               "|crop:" + Number(root.cropX || 0).toFixed(3) + "," +
               Number(root.cropY || 0).toFixed(3) + "," +
               Number(root.cropWidth || 1).toFixed(3) + "x" +
               Number(root.cropHeight || 1).toFixed(3) +
               "|rotation:" + Number(root.transformRotation || 0).toFixed(2)
    }

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value))
    }

    function viewPointForImage(normalizedX, normalizedY) {
        const point = sharedCoordinateMapper.imageToViewPoint(normalizedX, normalizedY)
        return { valid: point.valid, x: point.x, y: point.y }
    }

    function imagePointFromLocal(localX, localY) {
        const point = sharedCoordinateMapper.viewToImagePoint(localX, localY)
        return { valid: point.valid, x: point.x, y: point.y }
    }

    function pointerMappingDiagnosticSignature(localX, localY) {
        return "shared-mask-overlay-pointer:v1|" +
               sharedCoordinateMapper.pointerMappingDiagnosticSignature(localX, localY)
    }

    function selectionOperationOverlayColor(operationName) {
        return theme.selectionPalette(operationName)
    }

    function resolvedOverlayColor(fallbackColor) {
        const customColor = String(editorPreview.maskPreviewOverlayColor || "")
        return customColor.length > 0 ? customColor : fallbackColor
    }

    function overlayOpacityScale() {
        return Math.max(0.0, Math.min(1.0, Number(editorPreview.maskPreviewOverlayOpacity || 0)))
    }

    function stableOutlineWidth(logicalPixels) {
        return Math.max(0.75, Number(logicalPixels || 1.0))
    }

    function stableOutlineDash(logicalPixels) {
        return Math.max(1.0, Number(logicalPixels || 1.0))
    }

    function outlineScaleFactor() {
        const scale = Number(outlineVisualScale || 1.0)
        const safeScale = Math.max(1.0, Math.min(256.0, isFinite(scale) ? scale : 1.0))
        return Math.pow(2.0, Math.ceil(Math.log(safeScale) / Math.LN2))
    }

    Binding {
        target: editorPreview
        property: "activeMaskOutlineDisplayScale"
        value: root.outlineScaleFactor()
        when: root.visible && root.masked && root.hasActiveMask
    }

    function handleOuterHaloColor() {
        return theme.alpha(theme.ink950, 0.94)
    }

    function handleInnerHaloColor() {
        return theme.alpha(theme.polar, 0.96)
    }

    function handleWarningFillColor() {
        return theme.alpha(theme.warning, 0.92)
    }

    function pixelAligned(value) {
        const ratio = Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
        return (Math.round(Number(value || 0) * ratio) + 0.5) / ratio
    }

    function pointXValue(point, fallback) {
        const resolvedFallback = fallback === undefined ? 0.5 : fallback
        return point && point.x !== undefined ? point.x : resolvedFallback
    }

    function pointYValue(point, fallback) {
        const resolvedFallback = fallback === undefined ? 0.5 : fallback
        return point && point.y !== undefined ? point.y : resolvedFallback
    }

    function pointSizeValue(point, fallback) {
        const resolvedFallback = fallback === undefined ? editorPreview.activeMaskBrushSize : fallback
        return point && point.size !== undefined ? point.size : resolvedFallback
    }

    function normalizedOutlinePath(path) {
        if (!path)
            return []
        if (path.x !== undefined && path.y !== undefined)
            return [path]
        if (path.length === undefined)
            return []
        const points = []
        for (let i = 0; i < path.length; ++i) {
            const point = path[i]
            if (point && point.x !== undefined && point.y !== undefined)
                points.push(point)
        }
        return points
    }

    function normalizedOutlinePaths(paths) {
        if (!paths || paths.length === undefined)
            return []
        if (paths.length > 0 && paths[0] && paths[0].x !== undefined && paths[0].y !== undefined)
            return [root.normalizedOutlinePath(paths)]
        const normalized = []
        for (let i = 0; i < paths.length; ++i) {
            const path = root.normalizedOutlinePath(paths[i])
            if (path.length >= 2)
                normalized.push(path)
        }
        return normalized
    }

    function buildOutlinePath(ctx, points) {
        return buildOutlinePathWithOffset(ctx, points, 0, 0)
    }

    function buildOutlinePathWithOffset(ctx, points, offsetX, offsetY, appendToCurrentPath) {
        const count = points.length
        if (count < 2)
            return false
        const resolvedOffsetX = Number(offsetX || 0)
        const resolvedOffsetY = Number(offsetY || 0)
        if (appendToCurrentPath !== true)
            ctx.beginPath()
        const firstViewPoint = root.viewPointForImage(
                root.pointXValue(points[0]),
                root.pointYValue(points[0]))
        ctx.moveTo(firstViewPoint.x - resolvedOffsetX,
                   firstViewPoint.y - resolvedOffsetY)
        for (let i = 1; i < count; ++i) {
            const viewPoint = root.viewPointForImage(
                    root.pointXValue(points[i]),
                    root.pointYValue(points[i]))
            ctx.lineTo(viewPoint.x - resolvedOffsetX,
                       viewPoint.y - resolvedOffsetY)
        }
        if (count >= 3)
            ctx.closePath()
        return true
    }

    function displayOutlinePathLargeEnough(points) {
        if (!points || points.length < 2)
            return false
        let minX = Number.POSITIVE_INFINITY
        let minY = Number.POSITIVE_INFINITY
        let maxX = Number.NEGATIVE_INFINITY
        let maxY = Number.NEGATIVE_INFINITY
        for (let i = 0; i < points.length; ++i) {
            const point = root.viewPointForImage(
                        root.pointXValue(points[i]),
                        root.pointYValue(points[i]))
            if (!Number.isFinite(Number(point.x)) || !Number.isFinite(Number(point.y)))
                return false
            minX = Math.min(minX, Number(point.x))
            minY = Math.min(minY, Number(point.y))
            maxX = Math.max(maxX, Number(point.x))
            maxY = Math.max(maxY, Number(point.y))
        }
        const devicePixelRatio = Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
        const minimumVisibleSpan = 0.5 / devicePixelRatio
        const pixelWidth = maxX - minX
        const pixelHeight = maxY - minY
        return pixelWidth >= minimumVisibleSpan || pixelHeight >= minimumVisibleSpan
    }

    function strokeOutlinePaths(ctx, outlinePaths) {
        strokeOutlinePathsWithOffset(ctx, outlinePaths, 0, 0)
    }

    function strokeOutlinePathsWithOffset(ctx, outlinePaths, offsetX, offsetY) {
        if (!outlinePaths || outlinePaths.length === 0)
            return
        const marching = editorPreview.maskPreviewMode === "marching-ants"
        const dashOn = root.stableOutlineDash(7)
        const dashOff = root.stableOutlineDash(7)
        const dashCycle = dashOn + dashOff
        const rawPhase = marching && root.outlineAnimationEnabled ? Number(root.outlinePhase || 0) : 0
        const phase = ((rawPhase % dashCycle) + dashCycle) % dashCycle
        const strokeColor = Qt.alpha(root.resolvedOverlayColor(root.activeOperationColor),
                                     marching ? 0.95 : 0.76)
        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const points = outlinePaths[pathIndex]
            if (!root.displayOutlinePathLargeEnough(points))
                continue
            if (!root.buildOutlinePathWithOffset(ctx, points, offsetX, offsetY))
                continue
            if (ctx.setLineDash) ctx.setLineDash([])
            ctx.lineDashOffset = 0
            ctx.lineWidth = root.stableOutlineWidth(2.5)
            ctx.strokeStyle = theme.alpha(theme.ink950, 0.92)
            ctx.stroke()
            ctx.lineWidth = root.stableOutlineWidth(1.25)
            if (marching) {
                if (ctx.setLineDash) ctx.setLineDash([dashOn, dashOff])
                ctx.lineDashOffset = -phase
                ctx.strokeStyle = theme.alpha(theme.polar, 0.98)
                ctx.stroke()
                ctx.lineDashOffset = -(phase + dashOn)
                ctx.strokeStyle = theme.alpha(strokeColor, 0.98)
                ctx.stroke()
            } else {
                ctx.lineDashOffset = 0
                ctx.strokeStyle = strokeColor
                ctx.stroke()
            }
        }
        if (ctx.setLineDash) ctx.setLineDash([])
        ctx.lineDashOffset = 0
    }

    function fillOutlinePaths(ctx, outlinePaths) {
        fillOutlinePathsWithOffset(ctx, outlinePaths, 0, 0)
    }

    function fillOutlinePathsWithOffset(ctx, outlinePaths, offsetX, offsetY) {
        if (!root.selectionFillVisible ||
                editorPreview.maskPreviewMode === "none" ||
                editorPreview.maskPreviewMode === "outline" ||
                editorPreview.maskPreviewMode === "marching-ants" ||
                !outlinePaths || outlinePaths.length === 0)
            return
        ctx.beginPath()
        ctx.fillStyle = Qt.alpha(root.resolvedOverlayColor(root.activeOperationColor),
                                 0.46 * root.overlayOpacityScale())
        let hasClosedContour = false
        for (let pathIndex = 0; pathIndex < outlinePaths.length; ++pathIndex) {
            const points = outlinePaths[pathIndex]
            if (!root.buildOutlinePathWithOffset(ctx, points, offsetX, offsetY, true))
                continue
            hasClosedContour = true
        }
        if (hasClosedContour) {
            const previousFillRule = ctx.fillRule
            ctx.fillRule = Qt.OddEvenFill
            ctx.fill()
            ctx.fillRule = previousFillRule
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
                const previousViewPoint = root.viewPointForImage(
                        root.pointXValue(previous),
                        root.pointYValue(previous))
                const currentViewPoint = root.viewPointForImage(
                        root.pointXValue(current),
                        root.pointYValue(current))
                if (root.distanceToSegmentPixels(
                            mouseX, mouseY,
                            previousViewPoint.x,
                            previousViewPoint.y,
                            currentViewPoint.x,
                            currentViewPoint.y) <= slop) {
                    return true
                }
            }
            if (points.length >= 3) {
                const first = points[0]
                const last = points[points.length - 1]
                const firstViewPoint = root.viewPointForImage(
                        root.pointXValue(first),
                        root.pointYValue(first))
                const lastViewPoint = root.viewPointForImage(
                        root.pointXValue(last),
                        root.pointYValue(last))
                if (root.distanceToSegmentPixels(
                            mouseX, mouseY,
                            lastViewPoint.x,
                            lastViewPoint.y,
                            firstViewPoint.x,
                            firstViewPoint.y) <= slop) {
                    return true
                }
            }
        }
        return false
    }

    function selectActiveOutlineFromCanvas(mouseX, mouseY) {
        if (!activeSelectionOutline.visible)
            return false
        if (!root.outlinePathsHitTest(
                    mouseX, mouseY,
                    activeSelectionOutline.normalizedOutlinePathsCache,
                    12)) {
            return false
        }
        const point = root.imagePointFromLocal(mouseX, mouseY)
        if (!point.valid)
            return false
        root.activeOutlineClicked(point.x, point.y)
        return true
    }

    function previewTemporarilyHidden() {
        return editorPreview.maskPreviewTemporarilyHidden === true
    }

    function usesBlackWhiteImageContext() {
        return !root.previewTemporarilyHidden() &&
               (editorPreview.maskPreviewMode === "color-on-bw" ||
               editorPreview.maskPreviewMode === "image-on-bw"
               )
    }

    function previewSourceOpacity(activeBrushPainting) {
        if (root.previewTemporarilyHidden())
            return 0.0
        if (editorPreview.maskPreviewMode === "overlay")
            return root.overlayOpacityScale()
        if (editorPreview.maskPreviewMode === "on-black" ||
                editorPreview.maskPreviewMode === "on-white" ||
                editorPreview.maskPreviewMode === "black-on-white" ||
                editorPreview.maskPreviewMode === "white-on-black")
            return root.overlayOpacityScale()
        return 0.64 * root.overlayOpacityScale()
    }

    function previewImageLayerVisible() {
        return false
    }

    Rectangle {
        objectName: root.overlayObjectName("GenericMaskTint", "genericMaskTint")
        readonly property real targetOpacity: root.genericTintOpacity
        anchors.fill: parent
        visible: false
        color: root.activeOperationColor
        opacity: targetOpacity * root.overlayOpacityScale()
    }

    Item {
        id: maskCanvasOverlay
        objectName: root.overlayObjectName("MaskCanvasOverlay", "maskCanvasOverlay")
        anchors.fill: parent
        readonly property string previewSource: ""
        readonly property string liveBrushPreviewSource: String(editorPreview.activeMaskLiveBrushPreviewSource || "")
        readonly property var liveBrushPoints: editorPreview.activeMaskLiveBrushPoints || []
        readonly property bool activeBrushPainting:
            root.activeBrushPainting || liveBrushPoints.length > 0
        readonly property bool nativeLiveBrushPreviewVisible:
            activeBrushPainting && editorPreview.activeMaskLiveBrushPreviewNativeAvailable
        readonly property bool immediateLiveBrushPreviewVisible:
            activeBrushPainting && liveBrushPoints.length > 0 &&
            !editorPreview.activeMaskBrushAutoMask && liveBrushPreviewSource.length === 0
        readonly property bool vectorLiveBrushPreviewVisible:
            activeBrushPainting && liveBrushPoints.length > 0 &&
            editorPreview.activeMaskBrushAutoMask && liveBrushPreviewSource.length === 0
        readonly property string effectivePreviewSource:
            activeBrushPainting
            ? liveBrushPreviewSource
            : previewSource
        readonly property bool fullMaskMatteVisible:
            effectivePreviewSource.length > 0 ||
            nativeLiveBrushPreviewVisible ||
            vectorLiveBrushPreviewVisible
        readonly property real previewOverlayOpacity: 0.42
        readonly property real selectionOverlayOpacity: 0.64
        readonly property real geometricOverlayOpacity: 0.52
        readonly property real geometricFillOpacity: 0.10
        readonly property real geometricStrokeOpacity: 0.74
        readonly property string outlineRenderContract: root.outlineRenderContract
        visible: root.masked &&
                 root.hasActiveMask &&
                 !root.previewTemporarilyHidden() &&
                 !root.compareMode &&
                 (editorPreview.maskPreviewMode !== "none" || activeBrushPainting)

        Rectangle {
            objectName: root.overlayObjectName("MaskPreviewBwBackdrop", "maskPreviewBwBackdrop")
            anchors.fill: parent
            visible: root.previewImageLayerVisible() && root.usesBlackWhiteImageContext()
            color: "#d8dce0"
            opacity: 0.34
        }

        Image {
            objectName: root.overlayObjectName("LiveBrushRasterImage", "liveBrushRasterImage")
            anchors.fill: parent
            visible: parent.effectivePreviewSource.length > 0
            source: visible ? parent.effectivePreviewSource : ""
            fillMode: Image.Stretch
            smooth: true
            mipmap: false
            cache: false
            opacity: root.previewSourceOpacity(parent.activeBrushPainting)
        }

        BrushLivePreviewItem {
            anchors.fill: parent
            visible: parent.nativeLiveBrushPreviewVisible
            controller: editorPreview
            opacity: root.previewSourceOpacity(parent.activeBrushPainting)
        }

        BrushLivePreviewItem {
            objectName: root.overlayObjectName("ImmediateBrushCoverage", "immediateBrushCoverage")
            anchors.fill: parent
            controller: editorPreview
            immediate: true
            tint: root.activeOperationColor
            visible: parent.immediateLiveBrushPreviewVisible
            opacity: root.previewSourceOpacity(parent.activeBrushPainting)
        }

        Canvas {
            id: liveBrushVectorCanvas
            objectName: root.overlayObjectName("LiveBrushVectorCanvas", "liveBrushVectorCanvas")
            // Receipt used by the workflow benchmark to reject unrelated frames.
            property bool frameReceiptEnabled: false
            property int completedPaintRevision: 0
            property int paintedPointCount: 0
            property int submittedPointCount: 0
            property bool framePhaseTracingEnabled: false
            property double framePhaseReferenceEpochMs: 0
            property double framePhaseStrokeSignalOffsetMs: -1
            property double framePhaseTimerOffsetMs: -1
            property double framePhasePaintStartOffsetMs: -1
            property double framePhasePaintEndOffsetMs: -1
            property double framePhasePaintedOffsetMs: -1
            function recordFramePhase(phase) {
                if (!framePhaseTracingEnabled || framePhaseReferenceEpochMs <= 0)
                    return
                const propertyName = "framePhase" + phase.charAt(0).toUpperCase() + phase.slice(1) + "OffsetMs"
                if (Number(liveBrushVectorCanvas[propertyName]) < 0)
                    liveBrushVectorCanvas[propertyName] = Date.now() - framePhaseReferenceEpochMs
            }
            onPainted: {
                recordFramePhase("painted")
                if (frameReceiptEnabled) {
                    paintedPointCount = submittedPointCount
                    completedPaintRevision += 1
                }
            }
            x: root.visibleCanvasWindow.x
            y: root.visibleCanvasWindow.y
            width: root.visibleCanvasWindow.width
            height: root.visibleCanvasWindow.height
            visible: parent.vectorLiveBrushPreviewVisible && !root.deepZoomCanvasMode
            opacity: Math.max(root.previewSourceOpacity(true), 0.54)

            function pointX(point) {
                return root.pointXValue(point) * root.width - x
            }

            function pointY(point) {
                return root.pointYValue(point) * root.height - y
            }

            function pointDiameter(point) {
                return Math.max(6, Math.min(root.width, root.height) * root.pointSizeValue(point, 0.08) * 2)
            }

            onPaint: {
                recordFramePhase("paintStart")
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                const points = parent.liveBrushPoints || []
                if (frameReceiptEnabled)
                    submittedPointCount = points.length
                if (points.length === 0) {
                    recordFramePhase("paintEnd")
                    return
                }

                ctx.fillStyle = root.activeOperationColor
                ctx.strokeStyle = root.activeOperationColor
                ctx.lineCap = "round"
                ctx.lineJoin = "round"

                // Auto Mask needs source colors. Show a thin input trail until
                // its canonical coverage arrives, without claiming a solid mask.
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.moveTo(pointX(points[0]), pointY(points[0]))
                for (let i = 1; i < points.length; ++i)
                    ctx.lineTo(pointX(points[i]), pointY(points[i]))
                ctx.stroke()
                recordFramePhase("paintEnd")
            }

            onVisibleChanged: root.scheduleLiveBrushVectorCanvasPaint()
            onWidthChanged: root.scheduleLiveBrushVectorCanvasPaint()
            onHeightChanged: root.scheduleLiveBrushVectorCanvasPaint()

            Connections {
                target: editorPreview

                function onActiveBrushStrokeChanged() {
                    liveBrushVectorCanvas.recordFramePhase("strokeSignal")
                    root.scheduleLiveBrushVectorCanvasPaint()
                }

                function onMaskOverlayChanged() {
                    root.scheduleLiveBrushVectorCanvasPaint()
                }
            }
        }

        Canvas {
            id: geometricMaskCanvas
            objectName: root.overlayObjectName("GeometricMaskCanvas", "geometricMaskCanvas")
            antialiasing: true
            x: root.visibleCanvasWindow.x
            y: root.visibleCanvasWindow.y
            width: root.visibleCanvasWindow.width
            height: root.visibleCanvasWindow.height
            readonly property string outlineRenderContract: root.outlineRenderContract
            readonly property string liveLassoRenderContract: "live-lasso|solid-fill|high-contrast-outline:v1"
            readonly property bool liveLassoFillVisible:
                visible &&
                editorPreview.activeMaskKind === "lasso" &&
                editorPreview.activeMaskPolygonPointCount >= 3 &&
                parent.geometricFillOpacity > 0.0
            readonly property bool liveLassoOutlineVisible:
                visible &&
                editorPreview.activeMaskKind === "lasso" &&
                editorPreview.activeMaskPolygonPointCount > 0 &&
                parent.geometricStrokeOpacity > 0.0
            visible: parent.previewSource.length === 0 &&
                     !parent.activeBrushPainting &&
                     !root.deepZoomCanvasMode
            opacity: parent.activeBrushPainting ? 0.0 : parent.geometricOverlayOpacity
            readonly property bool polygonFeatherVisualizationActive:
                visible &&
                (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") &&
                editorPreview.activeMaskPolygonPointCount >= 3 &&
                editorPreview.activeMaskFeather > 0.001
            readonly property real polygonFeatherVisualizationWidth:
                polygonFeatherVisualizationActive
                ? Math.max(6, Math.min(96, Math.min(width, height) * editorPreview.activeMaskFeather * 2))
                : 0
            onVisibleChanged: root.scheduleGeometricMaskCanvasPaint()
            onWidthChanged: root.scheduleGeometricMaskCanvasPaint()
            onHeightChanged: root.scheduleGeometricMaskCanvasPaint()

            Connections {
                target: editorPreview
                function onControlsChanged() { root.scheduleGeometricMaskCanvasPaint() }
                function onMaskOverlayChanged() { root.scheduleGeometricMaskCanvasPaint() }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                ctx.lineWidth = root.stableOutlineWidth(2)
                ctx.strokeStyle = theme.alpha(root.activeOperationColor, parent.geometricStrokeOpacity)
                ctx.fillStyle = theme.alpha(root.activeOperationColor, parent.geometricFillOpacity)
                if (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") {
                    const points = editorPreview.activeMaskPolygonPoints || []
                    const count = points.length
                    if (count > 0) {
                        const warning = editorPreview.activeMaskPolygonSelfIntersecting
                        ctx.strokeStyle = warning ? theme.alpha(theme.warning, 0.88)
                                                  : theme.alpha(root.activeOperationColor, parent.geometricStrokeOpacity)
                        ctx.fillStyle = warning ? theme.alpha(theme.warning, 0.08)
                                                : theme.alpha(root.activeOperationColor, parent.geometricFillOpacity)
                        ctx.lineWidth = root.stableOutlineWidth(warning ? 2.4 : 2)
                        ctx.lineJoin = "round"
                        ctx.lineCap = "round"
                        if (ctx.setLineDash)
                            ctx.setLineDash(count >= 3 ? [] : [6, 5])
                        ctx.beginPath()
                        let firstViewPoint = root.viewPointForImage(Number(points[0].x || 0), Number(points[0].y || 0))
                        ctx.moveTo(firstViewPoint.x - x, firstViewPoint.y - y)
                        for (let i = 1; i < count; ++i) {
                            const viewPoint = root.viewPointForImage(
                                        Number(points[i].x || 0),
                                        Number(points[i].y || 0))
                            ctx.lineTo(viewPoint.x - x, viewPoint.y - y)
                        }
                        if (count >= 3) {
                            ctx.closePath()
                            if (geometricMaskCanvas.polygonFeatherVisualizationActive) {
                                if (ctx.setLineDash)
                                    ctx.setLineDash([])
                                ctx.save()
                                ctx.lineWidth = root.stableOutlineWidth(geometricMaskCanvas.polygonFeatherVisualizationWidth)
                                ctx.strokeStyle = warning ? theme.alpha(theme.warning, 0.16)
                                                          : theme.alpha(root.activeOperationColor, 0.16)
                                ctx.stroke()
                                ctx.restore()
                            }
                            ctx.fill()
                        }
                        ctx.stroke()
                        if (ctx.setLineDash)
                            ctx.setLineDash([])
                        for (let pointIndex = 0; pointIndex < count; ++pointIndex) {
                            const radius = pointIndex === 0 && count >= 3 ? 6 : 4
                            const pointViewPoint = root.viewPointForImage(
                                        Number(points[pointIndex].x || 0),
                                        Number(points[pointIndex].y || 0))
                            ctx.beginPath()
                            ctx.arc(root.pixelAligned(pointViewPoint.x - x),
                                    root.pixelAligned(pointViewPoint.y - y),
                                    radius, 0, Math.PI * 2)
                            ctx.fillStyle = pointIndex === 0 && count >= 3
                                            ? theme.alpha(theme.ink950, 0.88)
                                            : theme.alpha(theme.ink950, 0.62)
                            ctx.fill()
                            ctx.lineWidth = root.stableOutlineWidth(pointIndex === 0 && count >= 3 ? 2.2 : 1.4)
                            ctx.strokeStyle = pointIndex === 0 && count >= 3
                                              ? theme.alpha(theme.warning, 0.95)
                                              : theme.alpha(theme.polar, 0.82)
                            ctx.stroke()
                        }
                    }
                } else if (editorPreview.activeMaskKind === "rectangle") {
                    const cx = root.width * editorPreview.activeMaskCenterX - x
                    const cy = root.height * editorPreview.activeMaskCenterY - y
                    const rx = Math.max(12, root.width * editorPreview.activeMaskRadiusX)
                    const ry = Math.max(12, root.height * editorPreview.activeMaskRadiusY)
                    ctx.save()
                    ctx.translate(cx, cy)
                    ctx.rotate(editorPreview.activeMaskRotation * Math.PI / 180.0)
                    ctx.beginPath()
                    ctx.rect(-rx, -ry, rx * 2, ry * 2)
                    ctx.fill()
                    ctx.stroke()
                    ctx.restore()
                } else if (editorPreview.activeMaskKind === "radial" || editorPreview.activeMaskKind === "ellipse") {
                    const cx = root.width * editorPreview.activeMaskCenterX - x
                    const cy = root.height * editorPreview.activeMaskCenterY - y
                    const rx = Math.max(12, root.width * editorPreview.activeMaskRadiusX)
                    const ry = Math.max(12, root.height * editorPreview.activeMaskRadiusY)
                    ctx.save()
                    ctx.translate(cx, cy)
                    ctx.rotate(editorPreview.activeMaskRotation * Math.PI / 180.0)
                    ctx.beginPath()
                    ctx.ellipse(-rx, -ry, rx * 2, ry * 2)
                    ctx.fill()
                    ctx.stroke()
                    ctx.restore()
                } else if (editorPreview.activeMaskKind === "linear") {
                    const position = root.height * editorPreview.activeMaskPosition - y
                    const feather = Math.max(18, root.height * editorPreview.activeMaskFeather)
                    ctx.fillRect(-x, position - feather / 2, root.width, Math.min(feather, root.height))
                    ctx.beginPath()
                    ctx.moveTo(-x, position)
                    ctx.lineTo(root.width - x, position)
                    ctx.stroke()
                }
            }
        }

        Repeater {
            model: root.enablePolygonHandles &&
                   editorPreview.activeMaskKind === "polygon" &&
                   editorPreview.activeMaskPolygonPointCount >= 3
                   ? (editorPreview.activeMaskPolygonPoints || [])
                   : []

            delegate: Rectangle {
                required property var modelData
                required property int index
                property int edgeIndex: index
                readonly property var allPoints: editorPreview.activeMaskPolygonPoints || []
                readonly property var nextPoint: allPoints.length > 0
                                                 ? allPoints[(edgeIndex + 1) % allPoints.length]
                                                 : modelData
                readonly property real normalizedX: (Number(modelData.x || 0) + Number(nextPoint.x || 0)) / 2
                readonly property real normalizedY: (Number(modelData.y || 0) + Number(nextPoint.y || 0)) / 2
                readonly property var viewPoint: root.viewPointForImage(normalizedX, normalizedY)

                objectName: "polygonEdgeInsertHandle" + edgeIndex
                readonly property string handleReadabilityContract: root.handleReadabilityContract
                visible: !maskCanvasOverlay.activeBrushPainting
                x: viewPoint.x - width / 2
                y: viewPoint.y - height / 2
                width: 14
                height: 14
                radius: 3
                rotation: 45
                color: "transparent"
                border.width: 0
                z: 4

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
                    color: theme.alpha(root.activeOperationColor, 0.86)
                }

                MouseArea {
                    objectName: "polygonEdgeInsertInputHandler" + parent.edgeIndex
                    readonly property string inputHandlerRole: "polygon-edit"
                    readonly property string pointerGrabPolicy: "exclusive-polygon-edit"
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    preventStealing: true
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
            model: root.enablePolygonHandles && editorPreview.activeMaskKind === "polygon"
                   ? (editorPreview.activeMaskPolygonPoints || [])
                   : []

            delegate: Rectangle {
                required property var modelData
                required property int index
                property int pointIndex: index
                property real grabOffsetX: width / 2
                property real grabOffsetY: height / 2
                readonly property var viewPoint: root.viewPointForImage(Number(modelData.x || 0), Number(modelData.y || 0))

                objectName: "polygonVertexHandle" + pointIndex
                readonly property string handleReadabilityContract: root.handleReadabilityContract
                visible: !maskCanvasOverlay.activeBrushPainting
                x: viewPoint.x - width / 2
                y: viewPoint.y - height / 2
                width: pointIndex === 0 && editorPreview.activeMaskPolygonPointCount >= 3 ? 18 : 14
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 0
                z: 5

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
                    color: pointIndex === 0 && editorPreview.activeMaskPolygonPointCount >= 3
                           ? root.handleWarningFillColor()
                           : theme.alpha(root.activeOperationColor, 0.88)
                }

                MouseArea {
                    objectName: "polygonVertexDragInputHandler" + parent.pointIndex
                    readonly property string inputHandlerRole: "polygon-edit"
                    readonly property string pointerGrabPolicy: "exclusive-polygon-edit"
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    preventStealing: true
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
                        const localX = parent.x + mouse.x - parent.grabOffsetX + parent.width / 2
                        const localY = parent.y + mouse.y - parent.grabOffsetY + parent.height / 2
                        const point = root.imagePointFromLocal(localX, localY)
                        if (point.valid)
                            editorPreview.moveActivePolygonPoint(
                                        parent.pointIndex,
                                        root.clamp(point.x, 0, 1),
                                        root.clamp(point.y, 0, 1))
                        mouse.accepted = true
                    }
                }
            }
        }

        Rectangle {
            objectName: "polygonSnapCloseTarget"
            readonly property string handleReadabilityContract: root.handleReadabilityContract
            readonly property var snapPoints: editorPreview.activeMaskPolygonPoints || []
            readonly property var firstPoint: snapPoints.length > 0 ? snapPoints[0] : ({ "x": 0.5, "y": 0.5 })
            readonly property var viewPoint: root.viewPointForImage(Number(firstPoint.x || 0), Number(firstPoint.y || 0))
            visible: root.enablePolygonHandles &&
                     editorPreview.activeMaskKind === "polygon" &&
                     editorPreview.activeMaskPolygonPointCount >= 3 &&
                     !editorPreview.activeMaskPolygonSelfIntersecting &&
                     !parent.activeBrushPainting
            x: viewPoint.x - width / 2
            y: viewPoint.y - height / 2
            width: 32
            height: 32
            radius: 16
            color: theme.alpha(theme.warning, 0.14)
            border.width: 3
            border.color: root.handleInnerHaloColor()
            z: 3

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

    Canvas {
        id: activeSelectionOutline
        objectName: root.overlayObjectName("SharedActiveSelectionOutline", "sharedActiveSelectionOutline")
        x: root.visibleCanvasWindow.x
        y: root.visibleCanvasWindow.y
        width: root.visibleCanvasWindow.width
        height: root.visibleCanvasWindow.height
        antialiasing: true
        readonly property string outlineRenderContract: root.outlineRenderContract
        readonly property string outlineAnimationContract: root.outlineAnimationContract
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
            root.masked &&
            root.hasActiveMask &&
            !root.previewTemporarilyHidden() &&
            editorPreview.maskPreviewMode !== "none" &&
            !root.outlineActivationDeferred &&
            !root.compareMode
            ? editorPreview.activeMaskOutlinePaths
            : []
        readonly property var normalizedOutlinePathsCache: root.normalizedOutlinePaths(outlinePaths)
        visible: root.masked &&
                 root.hasActiveMask &&
                 !root.previewTemporarilyHidden() &&
                 editorPreview.maskPreviewMode !== "none" &&
                 !root.compareMode &&
                 !root.activeBrushPainting &&
                 normalizedOutlinePathsCache.length > 0
        z: 6

        onVisibleChanged: root.scheduleActiveSelectionOutlinePaint()
        onWidthChanged: root.scheduleActiveSelectionOutlinePaint()
        onHeightChanged: root.scheduleActiveSelectionOutlinePaint()

        Connections {
            target: editorPreview
            function onControlsChanged() { root.scheduleActiveSelectionOutlinePaint() }
            function onMaskOverlayChanged() { root.scheduleActiveSelectionOutlinePaint() }
        }

        Connections {
            target: root
            function onOutlinePhaseChanged() {
                if (root.outlineAnimationEnabled)
                    root.scheduleActiveSelectionOutlinePaint()
            }
            function onOutlineAnimationEnabledChanged() {
                root.scheduleActiveSelectionOutlinePaint()
            }
        }

        onPaint: {
            const outlinePaths = activeSelectionOutline.normalizedOutlinePathsCache
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            if (!outlinePaths || outlinePaths.length === 0)
                return
            if (Math.abs(x) < 0.001 && Math.abs(y) < 0.001) {
                root.fillOutlinePaths(ctx, outlinePaths)
                root.strokeOutlinePaths(ctx, outlinePaths)
            } else {
                root.fillOutlinePathsWithOffset(ctx, outlinePaths, x, y)
                root.strokeOutlinePathsWithOffset(ctx, outlinePaths, x, y)
            }
        }
    }

    Repeater {
        id: agentGroundingCandidateRepeater
        model: root.externalCandidateRegions || []

        delegate: Item {
            id: groundingCandidate
            required property var modelData
            required property int index
            readonly property var region: modelData.region || ({})
            readonly property var leftPoint: root.viewPointForImage(
                                                      Number(region.centerX || 0.5) - Number(region.radiusX || 0.1),
                                                      Number(region.centerY || 0.5))
            readonly property var rightPoint: root.viewPointForImage(
                                                       Number(region.centerX || 0.5) + Number(region.radiusX || 0.1),
                                                       Number(region.centerY || 0.5))
            readonly property var topPoint: root.viewPointForImage(
                                                     Number(region.centerX || 0.5),
                                                     Number(region.centerY || 0.5) - Number(region.radiusY || 0.1))
            readonly property var bottomPoint: root.viewPointForImage(
                                                        Number(region.centerX || 0.5),
                                                        Number(region.centerY || 0.5) + Number(region.radiusY || 0.1))
            objectName: root.overlayObjectName(
                            "AgentGroundingCandidate" + String(index + 1),
                            "agentGroundingCandidate" + String(index + 1))
            x: Math.min(leftPoint.x, rightPoint.x)
            y: Math.min(topPoint.y, bottomPoint.y)
            width: Math.max(30, Math.abs(rightPoint.x - leftPoint.x))
            height: Math.max(30, Math.abs(bottomPoint.y - topPoint.y))
            visible: root.externalCandidateRegions.length > 1
            z: 7

            Rectangle {
                anchors.fill: parent
                radius: Math.min(width, height) / 2
                color: theme.alpha(root.accentColor, 0.08)
                border.width: root.stableOutlineWidth(6)
                border.color: theme.alpha(theme.ink950, 0.84)
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: root.stableOutlineWidth(4)
                radius: Math.min(width, height) / 2
                color: "transparent"
                border.width: root.stableOutlineWidth(2.5)
                border.color: root.accentColor
            }

            Rectangle {
                objectName: root.overlayObjectName(
                                "AgentGroundingCandidateBadge" + String(groundingCandidate.index + 1),
                                "agentGroundingCandidateBadge" + String(groundingCandidate.index + 1))
                width: 25
                height: 25
                radius: 13
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: -6
                anchors.topMargin: -6
                color: root.accentColor
                border.width: 2
                border.color: theme.ink950

                Text {
                    anchors.centerIn: parent
                    text: String(groundingCandidate.index + 1)
                    color: theme.ink950
                    font.family: theme.fontMono
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }
    }

    Item {
        id: agentFindingFocusPulse
        objectName: root.overlayObjectName("AgentFindingFocusPulse", "agentFindingFocusPulse")
        readonly property var focusCenter: root.viewPointForImage(
                                                    root.hasExternalFocusRegion
                                                    ? Number(root.externalFocusRegion.centerX)
                                                    : Number(editorPreview.activeMaskCenterX || 0.5),
                                                    root.hasExternalFocusRegion
                                                    ? Number(root.externalFocusRegion.centerY)
                                                    : Number(editorPreview.activeMaskCenterY || 0.5))
        width: Math.max(28, root.width *
                        (root.hasExternalFocusRegion
                         ? Number(root.externalFocusRegion.radiusX)
                         : Number(editorPreview.activeMaskRadiusX || 0.1)) * 2)
        height: Math.max(28, root.height *
                         (root.hasExternalFocusRegion
                          ? Number(root.externalFocusRegion.radiusY)
                          : Number(editorPreview.activeMaskRadiusY || 0.1)) * 2)
        x: focusCenter.x - width / 2
        y: focusCenter.y - height / 2
        opacity: 0
        scale: 0.72
        visible: opacity > 0.01 && (root.hasExternalFocusRegion ||
                 (root.hasActiveMask &&
                  (editorPreview.activeMaskKind === "radial" ||
                  editorPreview.activeMaskKind === "ellipse" ||
                  editorPreview.activeMaskKind === "rectangle")))
        z: 8

        Canvas {
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                ctx.beginPath()
                ctx.ellipse(4, 4, Math.max(1, width - 8), Math.max(1, height - 8))
                ctx.lineWidth = root.stableOutlineWidth(6)
                ctx.strokeStyle = theme.alpha(theme.ink950, 0.86)
                ctx.stroke()
                ctx.beginPath()
                ctx.ellipse(4, 4, Math.max(1, width - 8), Math.max(1, height - 8))
                ctx.lineWidth = root.stableOutlineWidth(2.5)
                ctx.strokeStyle = theme.alpha(root.accentColor, 0.98)
                ctx.stroke()
            }
        }

        ParallelAnimation {
            id: agentFindingFocusAnimation
            NumberAnimation {
                target: agentFindingFocusPulse
                property: "scale"
                from: 0.72
                to: 1.18
                duration: 720
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: agentFindingFocusPulse
                property: "opacity"
                from: 0.94
                to: 0
                duration: 720
                easing.type: Easing.OutCubic
            }
        }
    }

    MouseArea {
        objectName: root.overlayObjectName("SharedActiveOutlineHitTarget", "sharedActiveOutlineHitTarget")
        readonly property string selectionSyncContract: "canvas-outline-hit-test|activeOutlineClicked|selectMaskSetAndLayer:v1"
        anchors.fill: parent
        enabled: root.enableOutlineSelection && activeSelectionOutline.visible && !root.activeBrushPainting &&
                 !root.agentPointTargetingEnabled
        acceptedButtons: Qt.LeftButton
        hoverEnabled: false
        preventStealing: false
        cursorShape: Qt.PointingHandCursor
        z: 7

        onPressed: function(mouse) {
            mouse.accepted = root.outlinePathsHitTest(mouse.x, mouse.y,
                activeSelectionOutline.normalizedOutlinePathsCache, 12)
        }

        onClicked: function(mouse) {
            if (!root.selectActiveOutlineFromCanvas(mouse.x, mouse.y))
                mouse.accepted = false
        }
    }

    Rectangle {
        objectName: root.overlayObjectName("AgentPointTargetMarker",
                                          "agentPointTargetMarker")
        readonly property var region: root.agentPointTargetRegion || ({})
        readonly property real centerX: Number(root.agentPointTargetRegion.centerX || 0.5)
        readonly property real centerY: Number(root.agentPointTargetRegion.centerY || 0.5)
        readonly property real radiusX: Math.max(0.02, Number(region.radiusX || 0.12))
        readonly property real radiusY: Math.max(0.02, Number(region.radiusY || 0.12))
        readonly property var leftPoint: root.viewPointForImage(centerX - radiusX, centerY)
        readonly property var rightPoint: root.viewPointForImage(centerX + radiusX, centerY)
        readonly property var topPoint: root.viewPointForImage(centerX, centerY - radiusY)
        readonly property var bottomPoint: root.viewPointForImage(centerX, centerY + radiusY)
        visible: !root.agentPointTargetingEnabled &&
                 Number.isFinite(Number(root.agentPointTargetRegion.centerX)) &&
                 Number.isFinite(Number(root.agentPointTargetRegion.centerY))
        x: Math.min(leftPoint.x, rightPoint.x)
        y: Math.min(topPoint.y, bottomPoint.y)
        width: Math.max(42, Math.abs(rightPoint.x - leftPoint.x))
        height: Math.max(42, Math.abs(bottomPoint.y - topPoint.y))
        radius: Math.min(width, height) / 2
        color: theme.alpha(theme.selectionAccent, 0.10)
        border.width: 2
        border.color: theme.selectionAccent
        z: 11

        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: theme.selectionAccent
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: 6
            text: "TARGET SEED"
            color: theme.selectionAccent
            font.family: theme.fontMono
            font.pixelSize: 9
            font.weight: Font.Bold
        }
    }

    MouseArea {
        objectName: root.overlayObjectName("AgentPointTargetInput",
                                          "agentPointTargetInput")
        readonly property string spatialConversationContract:
            "agent-point-grounded-correction-v2|coordinates:image-normalized|content-snap:confidence-gated|fallback:visible-radial|outside:fail-closed"
        anchors.fill: parent
        enabled: root.agentPointTargetingEnabled && !root.activeBrushPainting
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        cursorShape: Qt.CrossCursor
        z: 12

        onClicked: function(mouse) {
            const point = root.imagePointFromLocal(mouse.x, mouse.y)
            if (!point.valid) {
                mouse.accepted = false
                return
            }
            root.agentPointTargeted(root.clamp(point.x, 0, 1),
                                    root.clamp(point.y, 0, 1))
        }
    }
}
