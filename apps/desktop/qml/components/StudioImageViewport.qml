import QtQuick
import QtQuick.Window
import PhotoEditor
import "../workspaces"

Rectangle {
    id: viewport

    required property var workspaceContext
    required property var previewContext
    required property var themeContext
    property string source: workspaceContext.displayImageSource(0)
    property bool masked: false
    property bool framed: true
    property string resetKey: source
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    property string fitMode: "fit"
    property int topChromeInset: 0
    property bool syncViewport: false
    property bool publishPreviewViewport: false
    property bool loupeEnabled: false
    property bool focusCheckOverlayEnabled: false
    property bool focusPeakingOverlayEnabled: false
    property bool clippingOverlayEnabled: false
    property bool falseColorOverlayEnabled: false
    property bool gamutOverlayEnabled: false
    property bool zoneMapOverlayEnabled: false
    property bool applyingSynchronizedView: false
    property real pointerX: width / 2
    property real pointerY: height / 2
    readonly property real loupeSize: 168
    readonly property int contentMargin: framed ? 10 : 0
    readonly property int contentTopMargin: contentMargin + topChromeInset
    readonly property bool editedPreviewSurface: String(source || "") === String(workspaceContext.displayImageSource(0) || "") &&
                                                 String(source || "").length > 0
    readonly property bool nativePreviewActive: editedPreviewSurface &&
                                                previewContext.displayPreviewNativeAvailable
    readonly property bool providerPreviewVisible: String(source || "").length > 0 &&
                                                   (!nativePreviewActive || !inspectionDisplayReady)
    readonly property real viewportContentWidth: Math.max(0, width - contentMargin * 2)
    readonly property real viewportContentHeight: Math.max(0, height - contentTopMargin - contentMargin)
    readonly property real nativeSourceWidth: Math.max(1, Number(previewContext.displayPreviewNativeSize.width || 0))
    readonly property real nativeSourceHeight: Math.max(1, Number(previewContext.displayPreviewNativeSize.height || 0))
    readonly property real expectedOutputPixelWidth: previewContext.cropEditing
        ? Math.max(1, Number(previewContext.sourceImageWidth || 1))
        : Math.max(1, Math.round(Number(previewContext.sourceImageWidth || 1) *
                                Math.max(0.01, Number(previewContext.cropRectWidth || 1))))
    readonly property real expectedOutputPixelHeight: previewContext.cropEditing
        ? Math.max(1, Number(previewContext.sourceImageHeight || 1))
        : Math.max(1, Math.round(Number(previewContext.sourceImageHeight || 1) *
                                Math.max(0.01, Number(previewContext.cropRectHeight || 1))))
    readonly property real inspectionPixelWidth: previewContext.displayPreviewNativeFullResolution
        ? nativeSourceWidth
        : expectedOutputPixelWidth
    readonly property real inspectionPixelHeight: previewContext.displayPreviewNativeFullResolution
        ? nativeSourceHeight
        : expectedOutputPixelHeight
    readonly property real nativeFitScale: nativePreviewActive
        ? (fitMode === "fill"
           ? Math.max(viewportContentWidth / nativeSourceWidth, viewportContentHeight / nativeSourceHeight)
           : Math.min(viewportContentWidth / nativeSourceWidth, viewportContentHeight / nativeSourceHeight))
        : 1
    readonly property real pixelGridThreshold: 8.0
    readonly property real basePaintedContentWidth: nativePreviewActive
        ? nativeSourceWidth * nativeFitScale
        : previewImage.paintedWidth
    readonly property real basePaintedContentHeight: nativePreviewActive
        ? nativeSourceHeight * nativeFitScale
        : previewImage.paintedHeight
    readonly property real basePaintedContentX: contentMargin
        + (viewportContentWidth - basePaintedContentWidth) / 2
    readonly property real basePaintedContentY: contentTopMargin
        + (viewportContentHeight - basePaintedContentHeight) / 2
    readonly property real hardInspectionZoomLimit: 256.0
    readonly property real deepInspectionZoomMultiplier: 4.0
    readonly property real minimumDeepInspectionZoom: 64.0
    readonly property real wheelZoomStepBase: 1.22
    readonly property real sourcePixelInspectionZoom: inspectionPixelWidth > 0 &&
                                                     inspectionPixelHeight > 0 &&
                                                     basePaintedContentWidth > 0 &&
                                                     basePaintedContentHeight > 0
        ? Math.max(
              pixelGridThreshold * inspectionPixelWidth / Math.max(1, basePaintedContentWidth),
              pixelGridThreshold * inspectionPixelHeight / Math.max(1, basePaintedContentHeight))
        : 1.0
    readonly property real maxInspectionZoom: Math.max(
        8.0,
        Math.min(
            hardInspectionZoomLimit,
            Math.ceil(Math.max(minimumDeepInspectionZoom,
                               sourcePixelInspectionZoom * deepInspectionZoomMultiplier))))
    readonly property var inspectionZoomLevels: [1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, maxInspectionZoom]
    readonly property real paintedContentX: basePaintedContentX
        + panX - basePaintedContentWidth * (zoom - 1) / 2
    readonly property real paintedContentY: basePaintedContentY
        + panY - basePaintedContentHeight * (zoom - 1) / 2
    readonly property real paintedContentWidth: basePaintedContentWidth * zoom
    readonly property real paintedContentHeight: basePaintedContentHeight * zoom
    readonly property bool previewContentVisible: nativePreviewActive ||
                                                  providerPreviewVisible

    Rectangle {
        id: luminanceRangeSamplePatch
        objectName: "luminanceRangeSamplePatch"
        readonly property var samplePreview: previewContext.activeMaskLuminanceSamplePreview || ({})
        readonly property var sampleViewRect: viewport.viewRectForImage(
                                                  Number(samplePreview.x || 0),
                                                  Number(samplePreview.y || 0),
                                                  Number(samplePreview.width || 0),
                                                  Number(samplePreview.height || 0))
        visible: Boolean(samplePreview.active) &&
                 previewContext.activeMaskKind === "luminance" &&
                 viewport.previewContentVisible
        x: sampleViewRect.x
        y: sampleViewRect.y
        width: Math.max(8, sampleViewRect.width)
        height: Math.max(8, sampleViewRect.height)
        radius: 2
        color: themeContext.alpha(workspaceContext.mint, 0.12)
        border.width: 2
        border.color: themeContext.alpha(workspaceContext.mintSoft, 0.88)
        z: 18
    }
    readonly property real sourcePixelScreenWidth: inspectionPixelWidth > 0
        ? paintedContentWidth / Math.max(1, inspectionPixelWidth)
        : 0
    readonly property real sourcePixelScreenHeight: inspectionPixelHeight > 0
        ? paintedContentHeight / Math.max(1, inspectionPixelHeight)
        : 0
    readonly property real inspectionDevicePixelRatio: Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
    readonly property real oneToOneZoom: Math.max(
        inspectionPixelWidth / Math.max(1, basePaintedContentWidth * inspectionDevicePixelRatio),
        inspectionPixelHeight / Math.max(1, basePaintedContentHeight * inspectionDevicePixelRatio))
    readonly property bool pixelInspectionRequested: previewContentVisible &&
                                                     sourcePixelScreenWidth * inspectionDevicePixelRatio >= 0.999 &&
                                                     sourcePixelScreenHeight * inspectionDevicePixelRatio >= 0.999
    readonly property bool pixelGridRequested: previewContentVisible &&
                                               sourcePixelScreenWidth >= pixelGridThreshold &&
                                               sourcePixelScreenHeight >= pixelGridThreshold
    readonly property bool pixelGridFullResolutionBacked: nativePreviewActive &&
                                                          previewContext.displayPreviewNativeViewportFullResolution
    readonly property bool pixelInspectionActive: pixelInspectionRequested &&
                                                  pixelGridFullResolutionBacked
    readonly property bool pixelGridVisible: pixelGridRequested &&
                                             pixelGridFullResolutionBacked
    readonly property bool inspectionDisplayReady: !pixelInspectionRequested ||
                                                   pixelGridFullResolutionBacked
    readonly property bool highCostInspectionOverlaysVisible: inspectionDisplayReady
    readonly property string pixelGridRenderContract: "pixel-grid-render:v6|scenegraph-lines:true|viewport-bounded:true|visible-lines-only:true|no-raster-canvas:true|content-aligned:true|source-backed-required:true|visible-region-coverage-required:true|crop-output-coordinates:true"
    readonly property string pixelInspectionPerformanceContract: "pixel-inspection-performance:v5|full-resolution-visible-tile-required:true|proxy-display-while-inspecting:false|viewport-publisher-service:true|signature-dedup:true|event-loop-coalesced:true|source-patch-refresh:immediate"
    readonly property string mediaSurfaceContract: "media-surface:v1|canvas-stage:dominant|image-corners:consistent|empty-loading-failure:same-bounds|chrome:quiet|badges:corner-stacked|toolbar-baseline:single"
    readonly property string previewChromeContract: "preview-chrome:v2|status:global-under-navigation|diagnostics:overlay-menu|top-controls:baseline|failure:global-retry|content-dominant:true"
    readonly property string pixelInspectionContract: "pixel-inspection:v5|one-to-one:one-source-pixel-per-device-pixel|grid-threshold:8-logical-px|full-resolution-from-one-to-one-up:true|full-resolution-visible-coverage-required:true|crop-output-mapped-to-source:true|cursor-anchored:true|crisp-source-pixels:true|proxy-fallback:false|adaptive-source-pixel|max:source-pixel-4x-min-6400|wheel:exponential-delta"
    color: themeContext.mediaCanvas
    border.width: themeContext.chromeBorderWidth
    border.color: themeContext.alpha(workspaceContext.hairline, 0.78)
    radius: themeContext.radius8
    clip: true

    function nextInspectionZoom() {
        let firstZoom = 1.0
        let previousZoom = 0.0
        for (let index = 0; index < inspectionZoomLevels.length; ++index) {
            const candidateZoom = workspaceContext.clamp(Number(inspectionZoomLevels[index] || 1), 1.0, maxInspectionZoom)
            if (candidateZoom <= previousZoom + 0.0001)
                continue
            if (previousZoom <= 0.0001)
                firstZoom = candidateZoom
            previousZoom = candidateZoom
            if (zoom < candidateZoom - 0.0001)
                return candidateZoom
        }
        return firstZoom
    }

    function previousInspectionZoom() {
        let previousZoom = maxInspectionZoom + 1.0
        for (let index = inspectionZoomLevels.length - 1; index >= 0; --index) {
            const candidateZoom = workspaceContext.clamp(Number(inspectionZoomLevels[index] || 1), 1.0, maxInspectionZoom)
            if (candidateZoom >= previousZoom - 0.0001)
                continue
            previousZoom = candidateZoom
            if (zoom > candidateZoom + 0.0001)
                return candidateZoom
        }
        return 1.0
    }

    function wheelZoomFactor(wheel) {
        const angleY = wheel && wheel.angleDelta ? Number(wheel.angleDelta.y || 0) : 0
        const pixelY = wheel && wheel.pixelDelta ? Number(wheel.pixelDelta.y || 0) : 0
        const steps = Math.abs(angleY) > 0.0001 ? angleY / 120.0 : pixelY / 90.0
        if (Math.abs(steps) <= 0.0001)
            return 1.0
        return Math.pow(wheelZoomStepBase, workspaceContext.clamp(steps, -6.0, 6.0))
    }

    function setZoom(nextZoom, anchorX, anchorY) {
        function finiteOr(value, fallback) {
            const numeric = Number(value)
            return isFinite(numeric) ? numeric : fallback
        }

        const requestedZoom = finiteOr(nextZoom, zoom)
        const safeMaxZoom = Math.max(1.0, finiteOr(maxInspectionZoom, 8.0))
        const oldZoom = Math.max(0.01, finiteOr(zoom, 1.0))
        const clampedZoom = workspaceContext.clamp(requestedZoom, 0.25, safeMaxZoom)
        if (Math.abs(clampedZoom - oldZoom) < 0.0001)
            return
        if (clampedZoom <= 1.0) {
            zoom = clampedZoom
            panX = 0
            panY = 0
            return
        }

        const viewportWidth = Math.max(0, finiteOr(width, 0))
        const viewportHeight = Math.max(0, finiteOr(height, 0))
        const pivotX = workspaceContext.clamp(
                    finiteOr(anchorX === undefined ? viewportWidth / 2 : anchorX, viewportWidth / 2),
                    0,
                    viewportWidth)
        const pivotY = workspaceContext.clamp(
                    finiteOr(anchorY === undefined ? viewportHeight / 2 : anchorY, viewportHeight / 2),
                    0,
                    viewportHeight)
        const basePaintedWidth = Math.max(1, finiteOr(paintedContentWidth / oldZoom, basePaintedContentWidth))
        const basePaintedHeight = Math.max(1, finiteOr(paintedContentHeight / oldZoom, basePaintedContentHeight))
        const basePaintedX = finiteOr(paintedContentX, basePaintedContentX) - finiteOr(panX, 0) + basePaintedWidth * (oldZoom - 1) / 2
        const basePaintedY = finiteOr(paintedContentY, basePaintedContentY) - finiteOr(panY, 0) + basePaintedHeight * (oldZoom - 1) / 2
        const currentPaintedContentX = finiteOr(paintedContentX, basePaintedX)
        const currentPaintedContentY = finiteOr(paintedContentY, basePaintedY)
        const currentPaintedContentWidth = Math.max(1, finiteOr(paintedContentWidth, basePaintedWidth))
        const currentPaintedContentHeight = Math.max(1, finiteOr(paintedContentHeight, basePaintedHeight))
        const anchorNormalizedX = workspaceContext.clamp((pivotX - currentPaintedContentX) / Math.max(1, currentPaintedContentWidth), 0, 1)
        const anchorNormalizedY = workspaceContext.clamp((pivotY - currentPaintedContentY) / Math.max(1, currentPaintedContentHeight), 0, 1)
        const nextPaintedWidth = basePaintedWidth * clampedZoom
        const nextPaintedHeight = basePaintedHeight * clampedZoom
        const nextPaintedX = pivotX - anchorNormalizedX * nextPaintedWidth
        const nextPaintedY = pivotY - anchorNormalizedY * nextPaintedHeight

        zoom = clampedZoom
        panX = finiteOr(nextPaintedX - basePaintedX + basePaintedWidth * (clampedZoom - 1) / 2, 0)
        panY = finiteOr(nextPaintedY - basePaintedY + basePaintedHeight * (clampedZoom - 1) / 2, 0)
    }

    function inspectDetailAtOneToOne() {
        if (!previewContext.hasAsset)
            return false
        const targetZoom = workspaceContext.clamp(
            oneToOneZoom,
            0.25,
            maxInspectionZoom)
        setZoom(targetZoom)
        panX = 0
        panY = 0
        workspaceContext.loupeEnabled = true
        workspaceContext.focusPeakingOverlayEnabled = true
        workspaceContext.focusCheckOverlayEnabled = false
        workspaceContext.clippingOverlayEnabled = false
        workspaceContext.falseColorOverlayEnabled = false
        workspaceContext.gamutOverlayEnabled = false
        workspaceContext.zoneMapOverlayEnabled = false
        schedulePublishedPreviewViewportUpdate()
        return true
    }

    onMaxInspectionZoomChanged: {
        if (zoom > maxInspectionZoom)
            setZoom(maxInspectionZoom)
    }

    function setFitMode(nextFitMode) {
        if (nextFitMode !== "fit" && nextFitMode !== "fill")
            return
        fitMode = nextFitMode
        zoom = 1.0
        panX = 0
        panY = 0
    }

    function publishSynchronizedView() {
        if (!syncViewport || !workspaceContext.syncEnabled || applyingSynchronizedView)
            return
        workspaceContext.synchronizedZoom = zoom
        workspaceContext.synchronizedPanX = panX
        workspaceContext.synchronizedPanY = panY
    }

    function applySynchronizedView() {
        if (!syncViewport || !workspaceContext.syncEnabled || applyingSynchronizedView)
            return
        applyingSynchronizedView = true
        zoom = workspaceContext.synchronizedZoom
        panX = workspaceContext.synchronizedPanX
        panY = workspaceContext.synchronizedPanY
        applyingSynchronizedView = false
    }

    function resetView() {
        setFitMode("fit")
    }

    function zoomIn() {
        setZoom(nextInspectionZoom())
    }

    function zoomOut() {
        setZoom(previousInspectionZoom())
    }

    CanvasCoordinateMapper {
        id: viewportCoordinateMapper
        contentX: viewport.paintedContentX
        contentY: viewport.paintedContentY
        contentWidth: viewport.paintedContentWidth
        contentHeight: viewport.paintedContentHeight
        fitMode: viewport.fitMode
        devicePixelRatio: workspaceContext.thumbnailScreenDevicePixelRatio()
    }

    function normalizedPointForViewport(x, y) {
        const point = viewportCoordinateMapper.viewToImagePoint(x, y)
        return { valid: point.valid, x: point.x, y: point.y }
    }

    function pointerMappingDiagnosticSignature(x, y) {
        return "studio-viewport-pointer:v1|" +
               viewportCoordinateMapper.pointerMappingDiagnosticSignature(x, y)
    }

    function viewPointForImage(normalizedX, normalizedY) {
        const point = viewportCoordinateMapper.imageToViewPoint(normalizedX, normalizedY)
        return { valid: point.valid, x: point.x, y: point.y }
    }

    function viewRectForImage(normalizedX, normalizedY, normalizedWidth, normalizedHeight) {
        const rect = viewportCoordinateMapper.imageToViewRect(
                    normalizedX,
                    normalizedY,
                    normalizedWidth,
                    normalizedHeight)
        return { valid: rect.valid, x: rect.x, y: rect.y, width: rect.width, height: rect.height }
    }

    function pixelBrushCursorCellForViewport(x, y) {
        if (!workspaceContext.pixelExactToolReady() || !pixelGridVisible)
            return { valid: false, x: 0, y: 0, width: 0, height: 0, pixelX: -1, pixelY: -1 }
        const point = normalizedPointForViewport(x, y)
        if (!point.valid)
            return { valid: false, x: 0, y: 0, width: 0, height: 0, pixelX: -1, pixelY: -1 }
        const sourceWidth = Math.max(1, Number(previewContext.sourceImageWidth || 1))
        const sourceHeight = Math.max(1, Number(previewContext.sourceImageHeight || 1))
        const pixelX = workspaceContext.clamp(Math.floor(point.x * sourceWidth), 0, sourceWidth - 1)
        const pixelY = workspaceContext.clamp(Math.floor(point.y * sourceHeight), 0, sourceHeight - 1)
        const rect = viewRectForImage(
                    pixelX / sourceWidth,
                    pixelY / sourceHeight,
                    1.0 / sourceWidth,
                    1.0 / sourceHeight)
        return {
            valid: rect.valid,
            x: rect.x,
            y: rect.y,
            width: Math.max(1, rect.width),
            height: Math.max(1, rect.height),
            pixelX: pixelX,
            pixelY: pixelY
        }
    }

    function viewDiameterForImageSpan(centerX, centerY, normalizedDiameter) {
        const radius = Math.max(0.0, Number(normalizedDiameter || 0)) / 2.0
        const left = viewport.viewPointForImage(workspaceContext.clamp(centerX - radius, 0.0, 1.0), centerY)
        const right = viewport.viewPointForImage(workspaceContext.clamp(centerX + radius, 0.0, 1.0), centerY)
        const top = viewport.viewPointForImage(centerX, workspaceContext.clamp(centerY - radius, 0.0, 1.0))
        const bottom = viewport.viewPointForImage(centerX, workspaceContext.clamp(centerY + radius, 0.0, 1.0))
        const horizontal = Math.sqrt(Math.pow(right.x - left.x, 2) + Math.pow(right.y - left.y, 2))
        const vertical = Math.sqrt(Math.pow(bottom.x - top.x, 2) + Math.pow(bottom.y - top.y, 2))
        return Math.max(1, horizontal, vertical)
    }

    function clearPublishedPreviewViewport(immediate, force) {
        if (!publishPreviewViewport)
            return
        previewContext.schedulePreviewViewportPublication(
                    0, 0, 0, 0, 0, 0, 0, 0, 1, false, false,
                    false, immediate === true, force === true)
    }

    function thumbnailScreenDevicePixelRatio() {
        return Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
    }

    function thumbnailSourceSizeForDisplay(displayPixels, backingPixels) {
        return Math.max(0, Math.max(Number(backingPixels || 0),
                                   Math.round(Math.max(1, Number(displayPixels || 1)) *
                                              workspaceContext.thumbnailScreenDevicePixelRatio())))
    }

    function updatePublishedPreviewQuality() {
        if (!publishPreviewViewport || !workspaceContext.workspaceActive ||
                !previewContext.hasAsset || previewContext.compareMode || !visible)
            return
        const settledMaskQuality = workspaceContext.maskWorkActive && workspaceContext.maskPreviewQualityBoostReady
        previewContext.setPreviewPresentationMetrics(
                    width,
                    height,
                    Screen.devicePixelRatio,
                    settledMaskQuality)
    }

    function updatePublishedPreviewViewport(immediate, force) {
        if (!publishPreviewViewport)
            return
        updatePublishedPreviewQuality()
        if (!workspaceContext.workspaceActive || !previewContext.hasAsset || previewContext.compareMode || !visible ||
                previewImage.source.toString().length === 0 ||
                (zoom <= 1.01 && !pixelInspectionRequested) ||
                paintedContentWidth <= 1 || paintedContentHeight <= 1) {
            clearPublishedPreviewViewport(immediate, force)
            return
        }

        const visibleLeft = Math.max(paintedContentX, contentMargin)
        const visibleTop = Math.max(paintedContentY, contentTopMargin)
        const visibleRight = Math.min(paintedContentX + paintedContentWidth, width - contentMargin)
        const visibleBottom = Math.min(paintedContentY + paintedContentHeight, height - contentMargin)
        const visibleWidth = visibleRight - visibleLeft
        const visibleHeight = visibleBottom - visibleTop
        if (visibleWidth <= 1 || visibleHeight <= 1) {
            clearPublishedPreviewViewport(immediate, force)
            return
        }
        const visibleSourcePixelWidth = Math.max(
                    1,
                    inspectionPixelWidth *
                    visibleWidth / Math.max(1, paintedContentWidth))
        const visibleSourcePixelHeight = Math.max(
                    1,
                    inspectionPixelHeight *
                    visibleHeight / Math.max(1, paintedContentHeight))
        const viewportX = (visibleLeft - paintedContentX) / paintedContentWidth
        const viewportY = (visibleTop - paintedContentY) / paintedContentHeight
        const viewportWidth = visibleWidth / paintedContentWidth
        const viewportHeight = visibleHeight / paintedContentHeight
        previewContext.schedulePreviewViewportPublication(
                    viewportX,
                    viewportY,
                    viewportWidth,
                    viewportHeight,
                    visibleSourcePixelWidth,
                    visibleSourcePixelHeight,
                    visibleWidth,
                    visibleHeight,
                    Screen.devicePixelRatio,
                    workspaceContext.maskWorkActive,
                    pixelInspectionRequested,
                    true,
                    immediate === true,
                    force === true)
    }

    function schedulePublishedPreviewViewportUpdate(immediate) {
        if (!publishPreviewViewport)
            return
        updatePublishedPreviewViewport(immediate === true, false)
    }

    function schedulePreviewChangedViewportUpdate() {
        if (pixelInspectionRequested)
            return
        schedulePublishedPreviewViewportUpdate(false)
    }

    function updatePixelInspectorAt(x, y) {
        const point = normalizedPointForViewport(x, y)
        if (point.valid)
            previewContext.updatePixelInspector(point.x, point.y)
        else
            previewContext.clearPixelInspector()
    }

    function handleSamplingPointerPressed(mouse) {
        if (!previewContext.hasAsset || previewContext.compareMode)
            return false
        const point = normalizedPointForViewport(mouse.x, mouse.y)
        if (!point.valid)
            return false
        if (previewContext.whiteBalanceSampling) {
            previewContext.sampleWhiteBalanceAt(point.x, point.y)
            return true
        }
        if (previewContext.colorToolSamplingTarget.length > 0) {
            previewContext.sampleColorToolAt(point.x, point.y)
            return true
        }
        if (workspaceContext.maskColorRangeSampling && previewContext.activeMaskKind === "color") {
            const sampled = previewContext.sampleActiveColorRangeAt(point.x, point.y)
            if (sampled)
                workspaceContext.maskColorRangeSampling = false
            return true
        }
        if (workspaceContext.maskLuminanceRangeSampling && previewContext.activeMaskKind === "luminance")
            return previewContext.beginActiveLuminanceRangeSample(point.x, point.y)
        if (workspaceContext.maskDepthRangeSampling && previewContext.activeMaskKind === "depth") {
            const sampled = previewContext.sampleActiveDepthRangeAt(point.x, point.y)
            if (sampled)
                workspaceContext.maskDepthRangeSampling = false
            return true
        }
        return false
    }

    function handleSamplingPointerMoved(mouse) {
        if (!workspaceContext.maskLuminanceRangeSampling || previewContext.activeMaskKind !== "luminance")
            return false
        const point = normalizedPointForViewport(mouse.x, mouse.y)
        if (!point.valid)
            return false
        return previewContext.updateActiveLuminanceRangeSample(point.x, point.y)
    }

    function handleSamplingPointerReleased(mouse) {
        if (!workspaceContext.maskLuminanceRangeSampling || previewContext.activeMaskKind !== "luminance")
            return false
        const point = normalizedPointForViewport(mouse.x, mouse.y)
        if (!point.valid) {
            previewContext.cancelActiveLuminanceRangeSample()
            return true
        }
        const sampled = previewContext.commitActiveLuminanceRangeSample(point.x, point.y)
        if (sampled)
            workspaceContext.maskLuminanceRangeSampling = false
        return true
    }

    function maskPointerActive() {
        return viewport.masked &&
               viewport.previewContentVisible &&
               !workspaceContext.spacePanActive &&
               !previewContext.compareMode &&
               (workspaceContext.brushToolReady() || workspaceContext.pixelExactToolReady() || workspaceContext.editTool === "masks")
    }

    function handleMaskPointerPressed(mouse) {
        if (!maskPointerActive())
            return false
        const point = normalizedPointForViewport(mouse.x, mouse.y)
        if (!point.valid)
            return false

        if (workspaceContext.pixelExactToolReady() && pixelGridVisible) {
            const operation = workspaceContext.applyMaskOperationForModifierState(mouse.modifiers)
            return previewContext.applyActiveSelectionPixel(point.x, point.y, operation)
        }

        if (workspaceContext.brushToolReady()) {
            workspaceContext.applyMaskOperationForModifierState(mouse.modifiers)
            previewContext.beginActiveBrushStroke()
            previewContext.addBrushPointWithPressure(point.x, point.y, 1.0)
            return true
        }

        const kind = String(previewContext.activeMaskKind || "")
        if (kind === "lasso" || kind === "polygon") {
            const operation = workspaceContext.applyMaskOperationForModifierState(mouse.modifiers)
            if (kind === "polygon" &&
                    workspaceContext.completeActivePolygonIfSnappingToFirst(
                        point.x, point.y, viewport.paintedContentWidth, viewport.paintedContentHeight))
                return true
            if (previewContext.hasActiveMask)
                previewContext.activeMaskOperation = operation
            previewContext.appendActivePolygonPoint(point.x, point.y)
            return true
        }

        if (!previewContext.hasActiveMask || kind === "object") {
            const objectMode = workspaceContext.applyObjectPickModeForModifierState(mouse.modifiers)
            if (objectMode === "selection-subtract")
                previewContext.smartSelectSubtractAt(point.x, point.y, !previewContext.hasActiveMask)
            else
                previewContext.smartSelectAddAt(point.x, point.y, !previewContext.hasActiveMask)
            return true
        }

        return false
    }

    function handleMaskPointerMoved(mouse) {
        if (!maskPointerActive())
            return false
        const point = normalizedPointForViewport(mouse.x, mouse.y)
        if (!point.valid)
            return false

        if (workspaceContext.brushToolReady() && previewContext.brushStrokeActive) {
            previewContext.addBrushPointWithPressure(point.x, point.y, 1.0)
            return true
        }

        if (workspaceContext.pixelExactToolReady() && pixelGridVisible && viewportMouse.pressed) {
            const operation = workspaceContext.applyMaskOperationForModifierState(mouse.modifiers)
            return previewContext.applyActiveSelectionPixel(point.x, point.y, operation)
        }

        const kind = String(previewContext.activeMaskKind || "")
        if ((kind === "lasso" || kind === "polygon") && viewportMouse.pressed) {
            previewContext.appendActivePolygonPoint(point.x, point.y)
            return true
        }

        return false
    }

    function handleMaskPointerReleased() {
        if (previewContext.brushStrokeActive)
            previewContext.endActiveBrushStroke()
        if (previewContext.activeMaskKind === "lasso") {
            if (previewContext.activeMaskPolygonPointCount >= 3)
                previewContext.closeActiveLassoPath()
            else {
                previewContext.clearActivePolygonPoints()
                previewContext.removeActiveMask()
            }
            workspaceContext.editTool = "masks"
        }
    }

    onResetKeyChanged: resetView()
    onSyncViewportChanged: applySynchronizedView()
    onZoomChanged: {
        publishSynchronizedView()
        schedulePublishedPreviewViewportUpdate(false)
    }
    onPanXChanged: {
        publishSynchronizedView()
        schedulePublishedPreviewViewportUpdate(false)
    }
    onPanYChanged: {
        publishSynchronizedView()
        schedulePublishedPreviewViewportUpdate(false)
    }
    onPixelInspectionRequestedChanged: schedulePublishedPreviewViewportUpdate(true)
    onWidthChanged: schedulePublishedPreviewViewportUpdate(true)
    onHeightChanged: schedulePublishedPreviewViewportUpdate(true)
    onVisibleChanged: schedulePublishedPreviewViewportUpdate(true)
    onSourceChanged: schedulePublishedPreviewViewportUpdate(true)
    Component.onCompleted: schedulePublishedPreviewViewportUpdate(true)

    Connections {
        target: workspaceContext
        function onSynchronizedZoomChanged() { viewport.applySynchronizedView() }
        function onSynchronizedPanXChanged() { viewport.applySynchronizedView() }
        function onSynchronizedPanYChanged() { viewport.applySynchronizedView() }
        function onSyncEnabledChanged() { viewport.applySynchronizedView() }
        function onWorkspaceActiveChanged() { viewport.schedulePublishedPreviewViewportUpdate(true) }
    }

    Connections {
        target: previewContext
        function onAssetChanged() { viewport.schedulePublishedPreviewViewportUpdate(true) }
        function onCompareModeChanged() { viewport.schedulePublishedPreviewViewportUpdate(true) }
        function onPreviewChanged() { viewport.schedulePreviewChangedViewportUpdate() }
        function onDisplayPreviewNativeViewportFullResolutionChanged() {
            if (viewport.pixelInspectionRequested &&
                    !previewContext.displayPreviewNativeViewportFullResolution) {
                viewport.updatePublishedPreviewViewport(true, true)
            }
        }
    }

    Image {
        id: previewImage
        property string loadedAssetId: ""
        Component.onCompleted: {
            // Qt 6.5 remains supported; retention is available from Qt 6.8.
            if (typeof previewImage["retainWhileLoading"] !== "undefined") {
                previewImage["retainWhileLoading"] = Qt.binding(function() {
                    return previewImage.loadedAssetId.length > 0 &&
                           previewImage.loadedAssetId === String(viewport.previewContext.assetId || "")
                })
            }
        }
        anchors.fill: parent
        anchors.leftMargin: viewport.contentMargin
        anchors.rightMargin: viewport.contentMargin
        anchors.topMargin: viewport.contentTopMargin
        anchors.bottomMargin: viewport.contentMargin
        source: viewport.source
        fillMode: viewport.fitMode === "fill" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        asynchronous: true
        cache: false
        visible: viewport.providerPreviewVisible
        smooth: !viewport.pixelInspectionActive
        mipmap: false
        scale: viewport.zoom
        transform: Translate {
            x: viewport.panX
            y: viewport.panY
        }
        onPaintedWidthChanged: viewport.schedulePublishedPreviewViewportUpdate(true)
        onPaintedHeightChanged: viewport.schedulePublishedPreviewViewportUpdate(true)
        onStatusChanged: {
            if (status === Image.Ready)
                loadedAssetId = String(viewport.previewContext.assetId || "")
            viewport.schedulePublishedPreviewViewportUpdate(true)
        }
    }

    EditorPreviewTextureItem {
        id: nativePreviewImage
        objectName: viewport.objectName.length > 0 ? viewport.objectName + "NativePreviewImage" : "nativePreviewImage"
        x: viewport.basePaintedContentX + viewport.panX
        y: viewport.basePaintedContentY + viewport.panY
        width: viewport.basePaintedContentWidth
        height: viewport.basePaintedContentHeight
        scale: viewport.zoom
        transformOrigin: Item.Center
        visible: viewport.nativePreviewActive && viewport.inspectionDisplayReady
        controller: previewContext
        crispPixels: viewport.pixelInspectionActive
    }

    StudioPixelGridOverlay {
        anchors.fill: parent
        viewportObjectName: viewport.objectName
        pixelGridVisible: viewport.pixelGridVisible
        inspectionPixelWidth: viewport.inspectionPixelWidth
        inspectionPixelHeight: viewport.inspectionPixelHeight
        sourcePixelScreenWidth: viewport.sourcePixelScreenWidth
        sourcePixelScreenHeight: viewport.sourcePixelScreenHeight
        paintedContentX: viewport.paintedContentX
        paintedContentY: viewport.paintedContentY
        paintedContentWidth: viewport.paintedContentWidth
        paintedContentHeight: viewport.paintedContentHeight
        pixelGridThreshold: viewport.pixelGridThreshold
        gridLineColor: themeContext.alpha(themeContext.polar, 0.22)
    }

    Rectangle {
        id: topChromeBacking
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: viewport.contentTopMargin
        visible: viewport.topChromeInset > 0
        color: viewport.color
        z: 19

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: themeContext.chromeBorderWidth
            color: workspaceContext.hairline
        }
    }

    Rectangle {
        anchors.fill: parent
        color: themeContext.ink850
        visible: !viewport.previewContentVisible
    }

    StudioViewportStatusStack {
        anchors.fill: parent
        viewportObjectName: viewport.objectName
        pixelInspectionRequested: viewport.pixelInspectionRequested
        pixelGridFullResolutionBacked: viewport.pixelGridFullResolutionBacked
        previewProcessing: previewContext.processing
        previewContentVisible: viewport.previewContentVisible
        editedPreviewSurface: viewport.editedPreviewSurface
        previewRenderFailed: workspaceContext.previewRenderFailed()
        previewUpdateBadgeText: workspaceContext.previewUpdateBadgeText()
        previewFailureDetail: previewContext.previewFailureDetail
        maskActivityProgressValue: workspaceContext.maskActivityProgressValue()
        maskActivityChipVisible: workspaceContext.maskActivityChipVisible()
        maskActivityChipText: workspaceContext.maskActivityChipText()
        onRetryRequested: previewContext.retryPreviewRender()
    }

    StudioDiagnosticOverlayStack {
        anchors.fill: parent
        contentX: viewport.basePaintedContentX + viewport.panX
        contentY: viewport.basePaintedContentY + viewport.panY
        contentWidth: viewport.basePaintedContentWidth
        contentHeight: viewport.basePaintedContentHeight
        contentScale: viewport.zoom
        anchorX: previewImage.x
        anchorY: previewImage.y
        previewContentVisible: viewport.previewContentVisible
        highCostOverlaysVisible: viewport.highCostInspectionOverlaysVisible
        focusCheckEnabled: workspaceContext.focusCheckOverlayEnabled && viewport.focusCheckOverlayEnabled
        focusPeakingEnabled: workspaceContext.focusPeakingOverlayEnabled && viewport.focusPeakingOverlayEnabled
        clippingEnabled: workspaceContext.clippingOverlayEnabled && viewport.clippingOverlayEnabled
        falseColorEnabled: workspaceContext.falseColorOverlayEnabled && viewport.falseColorOverlayEnabled
        gamutEnabled: workspaceContext.gamutOverlayEnabled && viewport.gamutOverlayEnabled
        zoneMapEnabled: workspaceContext.zoneMapOverlayEnabled && viewport.zoneMapOverlayEnabled
        colorToolMaskEnabled: previewContext.colorToolMaskPreviewEnabled
        compareMode: previewContext.compareMode
        softProofEnabled: previewContext.softProofEnabled
        softProofProfile: previewContext.softProofProfile
        outputColorSpace: previewContext.outputColorSpace
        accentColor: workspaceContext.mint
        focusCheckSource: previewContext.focusCheckOverlaySource
        focusPeakingSource: previewContext.focusPeakingOverlaySource
        clippingSource: previewContext.clippingOverlaySource
        falseColorSource: previewContext.falseColorOverlaySource
        gamutSource: previewContext.gamutOverlaySource
        zoneMapSource: previewContext.zoneMapOverlaySource
        colorToolMaskSource: previewContext.colorToolMaskPreviewSource
    }

    StudioViewportLoupe {
        previewController: previewContext
        loupeVisible: workspaceContext.loupeEnabled && viewport.loupeEnabled &&
                      viewport.highCostInspectionOverlaysVisible && viewport.previewContentVisible
        loupeX: workspaceContext.loupeX
        loupeY: workspaceContext.loupeY
        loupeZoom: workspaceContext.loupeZoom
        loupeSize: viewport.loupeSize
        viewportWidth: viewport.width
        viewportHeight: viewport.height
        viewportZoom: viewport.zoom
        viewportPanX: viewport.panX
        viewportPanY: viewport.panY
        viewportObjectName: viewport.objectName
        previewSource: viewport.source
        nativePreviewActive: viewport.nativePreviewActive
        pixelInspectionActive: viewport.pixelInspectionActive
        previewAnchorX: previewImage.x
        previewAnchorY: previewImage.y
        previewAnchorWidth: previewImage.width
        previewAnchorHeight: previewImage.height
        previewFillMode: previewImage.fillMode
        baseContentX: viewport.basePaintedContentX
        baseContentY: viewport.basePaintedContentY
        baseContentWidth: viewport.basePaintedContentWidth
        baseContentHeight: viewport.basePaintedContentHeight
        accentColor: workspaceContext.mint
    }

    SharedMaskOverlay {
        x: viewport.paintedContentWidth > 1 ? viewport.paintedContentX : viewport.contentMargin
        y: viewport.paintedContentHeight > 1 ? viewport.paintedContentY : viewport.contentMargin
        width: viewport.paintedContentWidth > 1 ? viewport.paintedContentWidth : Math.max(1, viewport.width - viewport.contentMargin * 2)
        height: viewport.paintedContentHeight > 1 ? viewport.paintedContentHeight : Math.max(1, viewport.height - viewport.contentMargin * 2)
        objectNamePrefix: viewport.objectName
        masked: viewport.masked
        hasActiveMask: previewContext.hasActiveMask
        compareMode: previewContext.compareMode
        activeBrushPainting: workspaceContext.brushToolReady() && previewContext.brushStrokeActive
        accentColor: workspaceContext.mint
        outlinePhase: workspaceContext.selectionOutlinePhase
        outlineAnimationEnabled: workspaceContext.selectionOutlineAnimationEnabled
        outlineVisualScale: viewport.zoom
        outlineActivationDeferred: workspaceContext.maskOutlineActivationDeferred
        externalFocusSerial: workspaceContext.agentFindingFocusSerial
        externalFocusRegion: workspaceContext.agentFindingFocusRegion
        externalCandidateRegions: workspaceContext.agentSelectionCandidateRegions
        agentPointTargetingEnabled: workspaceContext.agentSpatialTargetMode
        agentPointTargetRegion: workspaceContext.agentSpatialTargetRegion
        selectionFillVisible: workspaceContext.editInspectorTab === "layers"
        cropX: previewContext.cropRectX
        cropY: previewContext.cropRectY
        cropWidth: previewContext.cropRectWidth
        cropHeight: previewContext.cropRectHeight
        transformRotation: previewContext.transformRotation
        enablePolygonHandles: true
        onActiveOutlineClicked: workspaceContext.selectMaskSetAndLayer(workspaceContext.activeMaskSetData(), false, true)
        onAgentPointTargeted: function(normalizedX, normalizedY) {
            workspaceContext.chooseAgentSpatialTarget(normalizedX, normalizedY)
        }
        z: 17
    }

    StudioViewportBrushCursors {
        anchors.fill: parent
        viewportObjectName: viewport.objectName
        pixelBrushCell: viewport.pixelBrushCursorCellForViewport(viewport.pointerX, viewport.pointerY)
        pixelBrushVisible: workspaceContext.pixelExactToolReady() && viewport.pixelGridVisible
        pixelBrushColor: workspaceContext.selectionOperationOverlayColor(workspaceContext.maskCreationOperation)
        paintedContentWidth: viewport.paintedContentWidth
        paintedContentHeight: viewport.paintedContentHeight
        paintedContentX: viewport.paintedContentX
        paintedContentY: viewport.paintedContentY
        pointerX: viewport.pointerX
        pointerY: viewport.pointerY
        activeBrushSize: previewContext.activeMaskBrushSize
        activeBrushHardness: previewContext.activeMaskBrushHardness
        activeMaskOperation: previewContext.activeMaskOperation
        brushVisible: workspaceContext.brushToolReady() && viewport.previewContentVisible
        brushOperationColor: workspaceContext.selectionOperationOverlayColor(previewContext.activeMaskOperation)
        brushSizeText: workspaceContext.brushSizeVisibleText()
        brushHardnessText: workspaceContext.brushHardnessValueText()
        brushFlowText: workspaceContext.brushFlowValueText()
        brushDensityText: workspaceContext.brushDensityValueText()
    }

    StudioRetouchOverlayCommandSurface {
        anchors.fill: parent
        workspace: workspaceContext
        viewport: viewport
        previewController: previewContext
    }

    StudioViewportInputCommandSurface {
        anchors.fill: parent
        workspace: workspaceContext
        viewport: viewport
        previewController: previewContext
    }
}
