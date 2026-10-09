import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as Controls
import QtQuick.Layouts
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
    property int boardDragIndex: -1
    property int boardDragStart: -1
    property int boardDragSpan: 0
    property int boardDragInsertion: -1
    property string boardDragDocumentId: ""
    property string boardDragOrder: ""
    property string boardDragLabel: ""
    property point boardDragPoint: Qt.point(0, 0)
    readonly property bool boardDragActive: boardDragIndex >= 0

    function boardOrderKey() {
        const ids = []
        for (let i = 0; i < boardStageModel.length; ++i) ids.push(String(boardStageModel[i].boardId))
        return ids.join("|")
    }

    function boardBoundaryX(index) {
        const width = viewport.boardUnitWidth * viewport.zoom
        const origin = viewport.imageFrameX - viewport.boardOffset(viewport.imageSpanStart, width, viewport.zoom)
        if (index === viewport.boardStageCount)
            return origin + viewport.boardOffset(index - 1, width, viewport.zoom) + width
        const gap = index > 0 && !viewport.boardStageModel[index].joinedPrevious ? viewport.boardStageGap * viewport.zoom : 0
        return origin + viewport.boardOffset(index, width, viewport.zoom) - gap / 2
    }

    function cancelBoardDrag() {
        boardDragIndex = -1
        boardDragInsertion = -1
    }

    function beginBoardDrag(index, point) {
        if (!boardStageActive || boardFocusMode || workspaceContext.editBoardSwitchBusy || workspaceContext.editBoardExportBusy) return false
        const board = boardStageModel[index] || ({})
        boardDragStart = Number(board.moveSpanStart)
        boardDragSpan = Number(board.moveSpanCount)
        if (!(boardDragStart >= 0 && boardDragSpan > 0 && boardDragSpan < boardStageCount)) return false
        boardDragDocumentId = workspaceContext.editBoardDocumentId
        boardDragOrder = boardOrderKey()
        boardDragLabel = String(board.boardName || qsTr("Board %1").arg(index + 1)) +
            (boardDragSpan > 1 ? qsTr(" · %1 boards together").arg(boardDragSpan) : "")
        boardDragIndex = index
        viewport.forceActiveFocus(Qt.MouseFocusReason)
        updateBoardDrag(point)
        return true
    }

    function updateBoardDrag(point) {
        boardDragPoint = point
        boardDragInsertion = -1
        if (!boardDragActive) return
        if (boardDragDocumentId !== workspaceContext.editBoardDocumentId || boardDragOrder !== boardOrderKey() ||
            workspaceContext.editBoardSwitchBusy || workspaceContext.editBoardExportBusy || boardFocusMode) {
            cancelBoardDrag()
            return
        }
        if (point.x < 0 || point.x > width || point.y < contentTopMargin || point.y > height - themeContext.scaled(32)) return
        let distance = Infinity
        for (let index = 0; index <= boardStageCount; ++index) {
            if (index < boardStageCount && Number(boardStageModel[index].moveSpanStart) !== index) continue
            const next = Math.abs(point.x - boardBoundaryX(index))
            if (next < distance) {
                distance = next
                boardDragInsertion = index
            }
        }
        if (boardDragInsertion >= boardDragStart && boardDragInsertion <= boardDragStart + boardDragSpan)
            boardDragInsertion = -1
    }

    function finishBoardDrag() {
        if (!boardDragActive) return
        updateBoardDrag(boardDragPoint)
        const index = boardDragIndex
        const insertion = boardDragInsertion
        cancelBoardDrag()
        if (index >= 0 && insertion >= 0) workspaceContext.editBoardMoveRequested(index, insertion)
    }

    Keys.onShortcutOverride: function(event) {
        if (viewport.boardDragActive && event.key === Qt.Key_Escape) event.accepted = true
    }
    Keys.onEscapePressed: function(event) {
        if (viewport.boardDragActive) {
            viewport.cancelBoardDrag()
            event.accepted = true
        } else event.accepted = false
    }

    Timer {
        interval: 16
        repeat: true
        running: viewport.boardDragActive
        onTriggered: {
            const edge = themeContext.scaled(40)
            const point = viewport.boardDragPoint
            if (point.x >= 0 && point.x < edge && viewport.boardBoundaryX(0) < 0)
                viewport.panX += Math.min(themeContext.scaled(10), -viewport.boardBoundaryX(0))
            else if (point.x <= viewport.width && point.x > viewport.width - edge && viewport.boardBoundaryX(viewport.boardStageCount) > viewport.width)
                viewport.panX -= Math.min(themeContext.scaled(10), viewport.boardBoundaryX(viewport.boardStageCount) - viewport.width)
            viewport.updateBoardDrag(point)
        }
    }
    property int pendingImageDropIndex: -1
    property string pendingImageDropUrl: ""
    property string pendingImageDropBoardId: ""
    property string pendingImageDropDocumentId: ""
    function acceptsImageDrop(drag) {
        return viewport.boardStageActive && !workspaceContext.editBoardSwitchBusy && !workspaceContext.editBoardExportBusy &&
            drag.hasUrls && drag.urls.length === 1 &&
            /^file:\/\//i.test(String(drag.urls[0])) && /\.(png|jpe?g|tiff?|webp|heic|heif|avif|bmp|gif)$/i.test(String(drag.urls[0])) &&
            (drag.supportedActions & Qt.CopyAction)
    }
    function offerImageDrop(index, url, point) {
        const board = viewport.boardStageModel[index] || ({})
        if (!String(board.boardId || "").length || workspaceContext.editBoardSwitchBusy) return false
        viewport.pendingImageDropIndex = index
        viewport.pendingImageDropUrl = String(url)
        viewport.pendingImageDropBoardId = String(board.boardId)
        viewport.pendingImageDropDocumentId = workspaceContext.editBoardDocumentId
        boardImageDropMenu.x = Math.max(12, Math.min(viewport.width - boardImageDropMenu.width - 12, point.x))
        boardImageDropMenu.y = Math.max(viewport.contentTopMargin, Math.min(viewport.height - boardImageDropMenu.height - 12, point.y))
        boardImageDropMenu.open()
        return true
    }
    function applyImageDrop(operation) {
        const board = viewport.boardStageModel[viewport.pendingImageDropIndex] || ({})
        if (workspaceContext.editBoardSwitchBusy || workspaceContext.editBoardExportBusy ||
            viewport.pendingImageDropDocumentId !== workspaceContext.editBoardDocumentId ||
            String(board.boardId || "") !== viewport.pendingImageDropBoardId) {
            workspaceContext.showEditActionFeedback(qsTr("The board changed. Drop the image again."), true)
            boardImageDropMenu.close()
            return
        }
        const index = viewport.pendingImageDropIndex
        const url = viewport.pendingImageDropUrl
        boardImageDropMenu.close()
        workspaceContext.editBoardImageDropRequested(index, url, operation)
    }
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
    property bool colorMixerDragGestureActive: false
    property bool colorMixerSamplePending: false
    property real colorMixerSamplePointerX: 0
    property real colorMixerSamplePointerY: 0
    property string maskGestureId: ""
    property string maskGestureKind: ""
    property point smartBoxStart: Qt.point(0, 0)
    property point smartBoxEnd: Qt.point(0, 0)

    Rectangle {
        objectName: viewport.objectName === "editImageViewport" ? "selectionObjectBoxPreview" : viewport.objectName + "SelectionBoxPreview"
        visible: viewport.maskGestureKind === "smart-box" && workspaceContext.objectPickArmed && workspaceContext.objectPickBoxMode
        x: viewport.paintedContentX + Math.min(viewport.smartBoxStart.x, viewport.smartBoxEnd.x) * viewport.paintedContentWidth
        y: viewport.paintedContentY + Math.min(viewport.smartBoxStart.y, viewport.smartBoxEnd.y) * viewport.paintedContentHeight
        width: Math.abs(viewport.smartBoxEnd.x - viewport.smartBoxStart.x) * viewport.paintedContentWidth
        height: Math.abs(viewport.smartBoxEnd.y - viewport.smartBoxStart.y) * viewport.paintedContentHeight
        color: "transparent"
        border.width: 1
        border.color: viewport.workspaceContext.mint
        z: 40
    }
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
    readonly property bool editBoardSurface: objectName === "editImageViewport" && workspaceContext.page === "edit"
    readonly property bool boardStageActive: editBoardSurface && workspaceContext.editBoardMode
    property bool boardFocusMode: false
    readonly property var boardStageModel: boardStageActive ? workspaceContext.editBoardModel : []
    readonly property int boardStageCount: Math.max(1, boardStageModel.length)
    readonly property int boardStageIndex: boardStageActive ? workspaceContext.editBoardIndex : 0
    readonly property real boardStageGap: themeContext.scaled(Math.max(6, 24 - Math.max(0, boardStageCount - 3) * 2))
    function boardOffset(index, boardWidth, gapScale) {
        let offset = index * boardWidth
        for (let board = 1; board <= index && board < boardStageModel.length; ++board)
            if (!boardStageModel[board].joinedPrevious) offset += boardStageGap * gapScale
        return offset
    }
    readonly property real boardStageFitGaps: boardFocusMode ? 0 : boardStageTotalGaps
    readonly property real boardStageTotalGaps: boardOffset(boardStageCount - 1, 0, 1)
    readonly property real boardAddSpace: editBoardSurface && previewContext.hasAsset ? themeContext.scaled(56) : 0
    readonly property real previewFitWidth: Math.max(1, (viewportContentWidth - boardAddSpace -
        boardStageFitGaps) / (boardFocusMode ? 1 : boardStageCount))
    readonly property real boardHeaderSpace: boardStageActive ? themeContext.scaled(32) : 0
    readonly property real previewFitHeight: Math.max(1, viewportContentHeight -
        (boardStageActive ? boardHeaderSpace + themeContext.scaled(40) : 0))
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
    readonly property var activeBoardData: boardStageModel[boardStageIndex] || ({})
    readonly property int imageSpanStart: boardStageActive ? Number(activeBoardData.imageSpanStart || 0) : 0
    readonly property int imageSpanCount: boardStageActive ? Math.max(1, Number(activeBoardData.imageSpanCount || 1)) : 1
    readonly property real boardAspect: Number(activeBoardData.boardPixelWidth || expectedOutputPixelWidth) /
        Math.max(1, Number(activeBoardData.boardPixelHeight || expectedOutputPixelHeight))
    readonly property real boardUnitWidth: Math.min(previewFitWidth, previewFitHeight * boardAspect)
    readonly property real boardUnitHeight: boardUnitWidth / boardAspect
    readonly property real baseImageFrameX: contentMargin + (boardFocusMode
        ? (viewportContentWidth - boardAddSpace - boardUnitWidth) / 2 - (boardStageIndex - imageSpanStart) * boardUnitWidth
        : (viewportContentWidth - boardAddSpace - boardStageCount * boardUnitWidth - boardStageTotalGaps) / 2
          + boardOffset(imageSpanStart, boardUnitWidth, 1))
    readonly property real baseImageFrameY: contentTopMargin + boardHeaderSpace + (previewFitHeight - boardUnitHeight) / 2
    readonly property real imageFrameWidth: boardUnitWidth * imageSpanCount
    readonly property real imageFrameX: baseImageFrameX + panX - imageFrameWidth * (zoom - 1) / 2
    readonly property real imageFrameY: baseImageFrameY + panY - boardUnitHeight * (zoom - 1) / 2
    readonly property real sourceAspectWidth: nativePreviewActive ? nativeSourceWidth : expectedOutputPixelWidth
    readonly property real sourceAspectHeight: nativePreviewActive ? nativeSourceHeight : expectedOutputPixelHeight
    readonly property real nativeFitScale: boardStageActive
        ? (activeBoardData.imageFitMode === "contain"
           ? Math.min(imageFrameWidth / sourceAspectWidth, boardUnitHeight / sourceAspectHeight)
           : Math.max(imageFrameWidth / sourceAspectWidth, boardUnitHeight / sourceAspectHeight)) * Number(activeBoardData.imageScale || 1)
        : nativePreviewActive
          ? (fitMode === "fill"
             ? Math.max(previewFitWidth / nativeSourceWidth, previewFitHeight / nativeSourceHeight)
             : Math.min(previewFitWidth / nativeSourceWidth, previewFitHeight / nativeSourceHeight)) : 1
    readonly property real pixelGridThreshold: 8.0
    readonly property real basePaintedContentWidth: boardStageActive ? sourceAspectWidth * nativeFitScale
        : nativePreviewActive ? nativeSourceWidth * nativeFitScale : previewImage.paintedWidth
    readonly property real basePaintedContentHeight: boardStageActive ? sourceAspectHeight * nativeFitScale
        : nativePreviewActive ? nativeSourceHeight * nativeFitScale : previewImage.paintedHeight
    function imagePosition(frameExtent, drawExtent, manualOffset, focal) {
        const free = frameExtent - drawExtent
        if (activeBoardData.imageManualCrop) return free * (Number(manualOffset || 0) + 1) / 2
        return Math.max(Math.min(0, free), Math.min(Math.max(0, free), frameExtent / 2 - drawExtent * Number(focal === undefined ? 0.5 : focal)))
    }
    readonly property real basePaintedContentX: boardStageActive
        ? baseImageFrameX + imagePosition(imageFrameWidth, basePaintedContentWidth, activeBoardData.imageOffsetX, activeBoardData.imageFocalX)
        : contentMargin + (viewportContentWidth - boardAddSpace - basePaintedContentWidth) / 2
    readonly property real basePaintedContentY: boardStageActive
        ? baseImageFrameY + imagePosition(boardUnitHeight, basePaintedContentHeight, activeBoardData.imageOffsetY, activeBoardData.imageFocalY)
        : contentTopMargin + (previewFitHeight - basePaintedContentHeight) / 2
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
    readonly property real paintedContentX: boardStageActive
        ? imageFrameX + (basePaintedContentX - baseImageFrameX) * zoom
        : basePaintedContentX + panX - basePaintedContentWidth * (zoom - 1) / 2
    readonly property real paintedContentY: boardStageActive
        ? imageFrameY + (basePaintedContentY - baseImageFrameY) * zoom
        : basePaintedContentY + panY - basePaintedContentHeight * (zoom - 1) / 2
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
        boardFocusMode = false
        zoom = 1.0
        panX = 0
        panY = 0
    }

    function setBoardFocus(focused) {
        if (!boardStageActive) return
        boardFocusMode = focused
        fitMode = "fit"
        zoom = 1
        panX = 0
        panY = 0
        schedulePublishedPreviewViewportUpdate(true)
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
        if (workspaceContext.colorMixerImageDragActive) {
            previewContext.beginCoalescedEdit()
            colorMixerSamplePending = true
            colorMixerSamplePointerX = mouse.x
            colorMixerSamplePointerY = mouse.y
            workspaceContext.colorMixerDragStartX = mouse.x
            workspaceContext.colorMixerDragStartY = mouse.y
            if (!previewContext.selectColorMixerChannelAt(point.x, point.y))
                finishColorMixerSample(false)
            return true
        }
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

    function finishColorMixerSample(success) {
        if (!colorMixerSamplePending)
            return
        colorMixerSamplePending = false
        if (!success) {
            previewContext.endCoalescedEdit()
            return
        }
        colorMixerDragGestureActive = true
        workspaceContext.colorMixerDragStartHue = workspaceContext.primaryColorMixerChannelValue("hue", previewContext.hueAdjustment)
        workspaceContext.colorMixerDragStartSaturation = workspaceContext.primaryColorMixerChannelValue("saturation", previewContext.saturationAdjustment)
        handleSamplingPointerMoved({x: colorMixerSamplePointerX, y: colorMixerSamplePointerY})
    }

    function handleSamplingPointerMoved(mouse) {
        if (colorMixerSamplePending) {
            colorMixerSamplePointerX = mouse.x
            colorMixerSamplePointerY = mouse.y
            return true
        }
        if (colorMixerDragGestureActive) {
            const hue = Math.max(-100, Math.min(100, workspaceContext.colorMixerDragStartHue +
                                                 (mouse.x - workspaceContext.colorMixerDragStartX) * 200 / Math.max(1, width)))
            const saturation = Math.max(-100, Math.min(100, workspaceContext.colorMixerDragStartSaturation -
                                                        (mouse.y - workspaceContext.colorMixerDragStartY) * 200 / Math.max(1, height)))
            workspaceContext.setPrimaryAdjustmentValue("hueAdjustment", hue)
            workspaceContext.setPrimaryAdjustmentValue("saturationAdjustment", saturation)
            return true
        }
        if (!workspaceContext.maskLuminanceRangeSampling || previewContext.activeMaskKind !== "luminance")
            return false
        const point = normalizedPointForViewport(mouse.x, mouse.y)
        if (!point.valid)
            return false
        return previewContext.updateActiveLuminanceRangeSample(point.x, point.y)
    }

    function handleSamplingPointerReleased(mouse) {
        if (colorMixerDragGestureActive || colorMixerSamplePending) {
            if (colorMixerSamplePending)
                previewContext.cancelColorMixerSample()
            colorMixerSamplePending = false
            colorMixerDragGestureActive = false
            previewContext.endCoalescedEdit()
            return true
        }
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

    function handleSamplingPointerCanceled() {
        if (!colorMixerDragGestureActive && !colorMixerSamplePending)
            return false
        if (colorMixerSamplePending)
            previewContext.cancelColorMixerSample()
        colorMixerSamplePending = false
        colorMixerDragGestureActive = false
        previewContext.endCoalescedEdit()
        return true
    }

    function currentMaskIdentity() {
        const mask = previewContext.maskStack[previewContext.activeMaskIndex]
        return mask ? String(mask.id || "") : ""
    }

    function maskPointerActive() {
        return (viewport.masked || workspaceContext.objectPickArmed || workspaceContext.pixelExactToolReady()) &&
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

        maskGestureId = currentMaskIdentity()
        maskGestureKind = ""
        if (workspaceContext.objectPickArmed) {
            workspaceContext.applyObjectPickModeForModifierState(mouse.modifiers)
            if (workspaceContext.objectPickBoxMode) {
                previewContext.clearSmartSelectPreview()
                smartBoxStart = Qt.point(point.x, point.y)
                smartBoxEnd = smartBoxStart
                maskGestureKind = "smart-box"
                return true
            }
            maskGestureKind = "smart"
            return previewContext.beginSmartSelectStroke(point.x, point.y, paintedContentWidth, paintedContentHeight)
        }
        if (workspaceContext.pixelExactToolReady() && pixelGridVisible) {
            maskGestureKind = "pixel"
            const operation = workspaceContext.applyMaskOperationForModifierState(mouse.modifiers)
            previewContext.applyActiveSelectionPixel(point.x, point.y, operation)
            maskGestureId = currentMaskIdentity()
            return true
        }

        if (workspaceContext.brushToolReady()) {
            workspaceContext.applyMaskOperationForModifierState(mouse.modifiers)
            maskGestureKind = "brush"
            previewContext.beginActiveBrushStroke()
            previewContext.addBrushPointWithPressure(point.x, point.y, 1.0)
            return true
        }

        const kind = String(previewContext.activeMaskKind || "")
        if ((kind === "lasso" && workspaceContext.lassoDrawArmed) ||
                (kind === "polygon" && workspaceContext.polygonDrawArmed)) {
            maskGestureKind = kind
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

    function handleMaskPointerMoved(mouse, pointerPressed) {
        if (!pointerPressed || !maskPointerActive() ||
                maskGestureId !== currentMaskIdentity())
            return false
        const point = normalizedPointForViewport(mouse.x, mouse.y)
        if (!point.valid)
            return false

        if (maskGestureKind === "smart-box") {
            smartBoxEnd = Qt.point(point.x, point.y)
            return true
        }
        if (maskGestureKind === "smart")
            return previewContext.updateSmartSelectStroke(point.x, point.y, paintedContentWidth, paintedContentHeight)

        if (workspaceContext.brushToolReady() && previewContext.brushStrokeActive) {
            previewContext.addBrushPointWithPressure(point.x, point.y, 1.0)
            return true
        }

        if (workspaceContext.pixelExactToolReady() && pixelGridVisible && maskGestureKind === "pixel") {
            const operation = workspaceContext.applyMaskOperationForModifierState(mouse.modifiers)
            return previewContext.applyActiveSelectionPixel(point.x, point.y, operation)
        }

        const kind = String(previewContext.activeMaskKind || "")
        if (kind === "lasso" && maskGestureKind === "lasso" && workspaceContext.lassoDrawArmed) {
            previewContext.appendActivePolygonPoint(point.x, point.y)
            return true
        }

        return false
    }

    function handleMaskPointerReleased() {
        const gestureKind = maskGestureKind
        maskGestureKind = ""
        if (maskGestureId !== currentMaskIdentity())
            return
        if (gestureKind === "smart-box") {
            if (Math.abs(smartBoxEnd.x - smartBoxStart.x) * paintedContentWidth >= 4 &&
                    Math.abs(smartBoxEnd.y - smartBoxStart.y) * paintedContentHeight >= 4) {
                previewContext.scheduleSmartSelectPreviewPath([{
                    type: "box", left: Math.min(smartBoxStart.x, smartBoxEnd.x),
                    top: Math.min(smartBoxStart.y, smartBoxEnd.y),
                    right: Math.max(smartBoxStart.x, smartBoxEnd.x),
                    bottom: Math.max(smartBoxStart.y, smartBoxEnd.y)
                }])
            }
            return
        }
        if (gestureKind === "smart") {
            workspaceContext.handleMaskPickOutcome(previewContext.commitSmartSelectStroke(
                workspaceContext.objectPickMode, workspaceContext.objectPickCreateNew))
            return
        }
        if (previewContext.brushStrokeActive)
            previewContext.endActiveBrushStroke()
        if (gestureKind === "lasso" && workspaceContext.lassoDrawArmed && previewContext.activeMaskKind === "lasso") {
            if (previewContext.activeMaskPolygonPointCount >= 3)
                previewContext.closeActiveLassoPath()
            else {
                previewContext.clearActivePolygonPoints()
                previewContext.removeActiveMask()
            }
            workspaceContext.clearTransientMaskTools()
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
        function onColorMixerSampleCompleted(success) { viewport.finishColorMixerSample(success) }
        function onSourceSamplingBusyChanged() {
            if (!previewContext.sourceSamplingBusy && viewport.colorMixerSamplePending)
                viewport.finishColorMixerSample(false)
        }
        function onAssetChanged() {
            viewport.handleSamplingPointerCanceled()
            viewport.schedulePublishedPreviewViewportUpdate(true)
        }
        function onCompareModeChanged() { viewport.schedulePublishedPreviewViewportUpdate(true) }
        function onPreviewChanged() { viewport.schedulePreviewChangedViewportUpdate() }
        function onDisplayPreviewNativeViewportFullResolutionChanged() {
            if (viewport.pixelInspectionRequested &&
                    !previewContext.displayPreviewNativeViewportFullResolution) {
                viewport.updatePublishedPreviewViewport(true, true)
            }
        }
    }

    Item {
        id: activeImageFrame
        x: viewport.boardStageActive ? viewport.imageFrameX + (viewport.boardFocusMode ? (viewport.boardStageIndex - viewport.imageSpanStart) * viewport.boardUnitWidth * viewport.zoom : 0) : 0
        y: viewport.boardStageActive ? viewport.imageFrameY : 0
        width: viewport.boardStageActive ? viewport.boardUnitWidth * (viewport.boardFocusMode ? 1 : viewport.imageSpanCount) * viewport.zoom : viewport.width
        height: viewport.boardStageActive ? viewport.boardUnitHeight * viewport.zoom : viewport.height
        clip: viewport.boardStageActive
        Image {
            objectName: viewport.objectName + "TransparencyGrid"
            x: viewport.boardStageActive ? 0 : viewport.paintedContentX
            y: viewport.boardStageActive ? 0 : viewport.paintedContentY
            width: viewport.boardStageActive ? activeImageFrame.width : viewport.paintedContentWidth
            height: viewport.boardStageActive ? activeImageFrame.height : viewport.paintedContentHeight
            visible: viewport.previewContentVisible && viewport.previewContext.hasAsset
            fillMode: Image.Tile
            source: "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><path fill="#34383d" d="M0 0h16v16H0z"/><path fill="#3c4046" d="M0 0h8v8H0zM8 8h8v8H8z"/></svg>')
            smooth: false
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
            x: viewport.boardStageActive ? viewport.paintedContentX - activeImageFrame.x : viewport.basePaintedContentX - (width - paintedWidth) / 2
            y: viewport.boardStageActive ? viewport.paintedContentY - activeImageFrame.y : viewport.contentTopMargin + viewport.boardHeaderSpace
            width: viewport.boardStageActive ? viewport.paintedContentWidth : viewport.previewFitWidth
            height: viewport.boardStageActive ? viewport.paintedContentHeight : viewport.previewFitHeight
            source: viewport.visible ? viewport.source : ""
            fillMode: viewport.boardStageActive ? Image.Stretch : viewport.fitMode === "fill" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
            asynchronous: true
            cache: false
            visible: viewport.providerPreviewVisible
            smooth: !viewport.pixelInspectionActive
            mipmap: false
            scale: viewport.boardStageActive ? 1 : viewport.zoom
            transform: Translate {
                x: viewport.boardStageActive ? 0 : viewport.panX
                y: viewport.boardStageActive ? 0 : viewport.panY
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
            x: viewport.boardStageActive ? viewport.paintedContentX - activeImageFrame.x : viewport.basePaintedContentX + viewport.panX
            y: viewport.boardStageActive ? viewport.paintedContentY - activeImageFrame.y : viewport.basePaintedContentY + viewport.panY
            width: viewport.boardStageActive ? viewport.paintedContentWidth : viewport.basePaintedContentWidth
            height: viewport.boardStageActive ? viewport.paintedContentHeight : viewport.basePaintedContentHeight
            scale: viewport.boardStageActive ? 1 : viewport.zoom
            transformOrigin: Item.Center
            visible: viewport.nativePreviewActive && viewport.inspectionDisplayReady
            controller: previewContext
            crispPixels: viewport.pixelInspectionActive
        }

    }

    // Adjacent boards share the regular Edit canvas. Only the selected board
    // owns the native preview and editing overlays; the others are click targets.
    Repeater {
        // Thumbnail publications update the data without destroying focused
        // board controls or an open actions menu.
        model: viewport.boardStageActive ? viewport.boardStageCount : 0
        delegate: Item {
            id: boardSurface
            required property int index
            readonly property var modelData: viewport.boardStageModel[index] || ({})
            readonly property bool selectedBoard: index === viewport.boardStageIndex
            readonly property bool highlightedBoard: index === workspaceContext.selectedEditBoardIndex
            readonly property bool activeImage: index >= viewport.imageSpanStart && index < viewport.imageSpanStart + viewport.imageSpanCount
            readonly property string boardName: String(modelData.boardName || "").trim() || qsTr("Board %1").arg(index + 1)
            x: viewport.imageFrameX + viewport.boardOffset(index, viewport.boardUnitWidth * viewport.zoom, viewport.zoom)
               - viewport.boardOffset(viewport.imageSpanStart, viewport.boardUnitWidth * viewport.zoom, viewport.zoom)
            y: viewport.imageFrameY
            width: viewport.boardUnitWidth * viewport.zoom
            height: viewport.boardUnitHeight * viewport.zoom
            z: 34
            objectName: "editCanvasBoard" + index
            visible: (!viewport.boardFocusMode || selectedBoard) &&
                     x + width > 0 && x < viewport.width && y + height > viewport.contentTopMargin
            activeFocusOnTab: !selectedBoard
            Accessible.role: Accessible.Button
            Accessible.name: boardName + qsTr(", board %1 of %2").arg(index + 1).arg(viewport.boardStageCount) +
                             (selectedBoard ? qsTr(", active") : qsTr(", double-click to edit"))
            Accessible.selected: highlightedBoard
            Accessible.onPressAction: workspaceContext.selectEditBoard(index)
            Keys.onReturnPressed: workspaceContext.editBoardSelected(index)
            Keys.onLeftPressed: workspaceContext.selectEditBoard(Math.max(0, index - 1))
            Keys.onRightPressed: workspaceContext.selectEditBoard(Math.min(viewport.boardStageCount - 1, index + 1))
            Image {
                anchors.fill: parent
                visible: !boardSurface.activeImage
                source: "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><path fill="#34383d" d="M0 0h16v16H0z"/><path fill="#3c4046" d="M0 0h8v8H0zM8 8h8v8H8z"/></svg>')
                fillMode: Image.Tile
                smooth: false
            }
            Image {
                objectName: "editCanvasBoardPreview" + boardSurface.index
                anchors.fill: parent
                visible: !boardSurface.activeImage
                source: visible ? String(boardSurface.modelData.previewSource || "") : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                mipmap: true
            }
            Rectangle {
                anchors.fill: parent
                color: viewport.boardDragActive && boardSurface.index >= viewport.boardDragStart &&
                    boardSurface.index < viewport.boardDragStart + viewport.boardDragSpan ? themeContext.alpha(themeContext.primaryAccent, 0.08) : "transparent"
                border.width: boardSurface.highlightedBoard || boardSurface.activeFocus ? 2 : 1
                border.color: boardSurface.highlightedBoard || boardSurface.activeFocus ? themeContext.accentCool400
                              : boardSelectArea.containsMouse ? themeContext.alpha(themeContext.accentCool400, 0.55) : themeContext.controlEdge
                Behavior on border.color { ColorAnimation { duration: themeContext.durationFast } }
            }
            Rectangle {
                x: 0
                y: -21
                width: 3
                height: 12
                color: themeContext.accentCool400
                visible: boardSurface.selectedBoard
            }
            Text {
                objectName: "editCanvasBoardLabel" + boardSurface.index
                x: boardSurface.selectedBoard ? 9 : 0
                y: -23
                width: Math.max(1, parent.width - x - (boardActions.visible ? boardActions.width + 8 : 0))
                text: boardSurface.width < themeContext.scaled(90) ? String(boardSurface.index + 1)
                      : boardSurface.boardName + (boardSurface.selectedBoard ? qsTr(" · Active") : "")
                color: boardSurface.selectedBoard ? themeContext.textStrong : themeContext.textMuted
                font.family: themeContext.fontUi
                font.pixelSize: themeContext.scaled(12)
                font.weight: boardSurface.selectedBoard ? Font.Medium : Font.Normal
                elide: Text.ElideRight
            }
            MouseArea {
                id: boardHeaderArea
                objectName: "editCanvasBoardHeaderSelect" + boardSurface.index
                x: 0
                y: -30
                width: Math.max(1, parent.width - 36)
                height: 30
                property point pressPoint: Qt.point(0, 0)
                property bool dragged: false
                readonly property bool canDrag: !viewport.boardFocusMode && Number(boardSurface.modelData.moveSpanCount) < viewport.boardStageCount
                enabled: !workspaceContext.editBoardSwitchBusy && !workspaceContext.editBoardExportBusy && !workspaceContext.spacePanActive
                hoverEnabled: true
                preventStealing: true
                cursorShape: dragged && pressed ? Qt.ClosedHandCursor : canDrag ? Qt.OpenHandCursor : Qt.PointingHandCursor
                onPressed: function(mouse) {
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    dragged = false
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const point = mapToItem(viewport, mouse.x, mouse.y)
                    if (!dragged && canDrag && Math.abs(mouse.x - pressPoint.x) >= drag.threshold)
                        dragged = viewport.beginBoardDrag(boardSurface.index, point)
                    if (dragged) viewport.updateBoardDrag(point)
                }
                onReleased: function(mouse) {
                    if (dragged) {
                        viewport.updateBoardDrag(mapToItem(viewport, mouse.x, mouse.y))
                        viewport.finishBoardDrag()
                    }
                }
                onCanceled: viewport.cancelBoardDrag()
                onClicked: {
                    if (!dragged) {
                        boardSurface.forceActiveFocus()
                        workspaceContext.selectEditBoard(boardSurface.index)
                    }
                }
                onDoubleClicked: if (!dragged) workspaceContext.editBoardSelected(boardSurface.index)
                Controls.ToolTip.visible: containsMouse && !pressed && !viewport.boardDragActive
                Controls.ToolTip.delay: 800
                Controls.ToolTip.text: canDrag ? (Number(boardSurface.modelData.moveSpanCount) > 1
                    ? qsTr("Drag to reorder. Connected boards move together.") : qsTr("Drag to reorder this board.")) : qsTr("Click to select. Double-click to edit.")
            }
            MenuButton {
                id: boardActions
                objectName: "editCanvasBoardActions" + boardSurface.index
                x: parent.width - width
                y: -30
                width: height
                visible: boardSurface.selectedBoard || boardSelectArea.containsMouse || boardSurface.activeFocus
                enabled: !workspaceContext.editBoardSwitchBusy && !workspaceContext.editBoardExportBusy
                compact: true
                tone: "quiet"
                iconName: "more"
                showText: false
                primaryOpensMenu: true
                showMenuIndicator: false
                accessibleName: qsTr("Actions for %1").arg(boardSurface.boardName)
                tooltipText: accessibleName
                menuItems: [
                    { key: "rename", label: qsTr("Rename board…"), icon: "text" },
                    { key: "duplicate", label: Number(boardSurface.modelData.imageSpanCount || 1) > 1 ? qsTr("Duplicate spanning image") : qsTr("Duplicate board"), icon: "copy", enabled: workspaceContext.editBoardCanAdd,
                      disabledReason: qsTr("This document has reached its board limit.") },
                    { key: "variant", label: qsTr("Create variant"), icon: "compare", enabled: workspaceContext.editBoardCanAdd,
                      tooltip: qsTr("Create a named, independent copy of this board for comparison."),
                      disabledReason: qsTr("This document has reached its board limit.") },
                    { key: "earlier", label: Number(boardSurface.modelData.imageSpanCount || 1) > 1 ? qsTr("Move image left") : qsTr("Move left"), icon: "chevron-left", separatorBefore: true,
                      enabled: Number(boardSurface.modelData.moveSpanStart) > 0, disabledReason: qsTr("This is the first board or connected image.") },
                    { key: "later", label: Number(boardSurface.modelData.imageSpanCount || 1) > 1 ? qsTr("Move image right") : qsTr("Move right"), icon: "chevron-right", enabled: Number(boardSurface.modelData.moveSpanStart) + Number(boardSurface.modelData.moveSpanCount) < viewport.boardStageCount,
                      disabledReason: qsTr("This is the last board.") },
                    { key: "remove", label: qsTr("Remove board…"), icon: "delete", danger: true, separatorBefore: true,
                      enabled: viewport.boardStageCount > 1, disabledReason: qsTr("Keep at least one board in the document.") }
                ]
                onMenuItemTriggered: function(key) { workspaceContext.editBoardActionRequested(boardSurface.index, key) }
            }
            Rectangle {
                objectName: "editBoardSafeMargins" + boardSurface.index
                x: parent.width * 0.05
                y: parent.height * 0.05
                width: parent.width * 0.9
                height: parent.height * 0.9
                visible: viewport.workspaceContext.layerSafeMarginsEnabled
                color: "transparent"
                border.width: themeContext.hairlineWidth
                border.color: themeContext.alpha(themeContext.primaryAccent, 0.4)
            }
            MouseArea {
                id: boardSelectArea
                objectName: "editCanvasBoardSelect" + boardSurface.index
                anchors.fill: parent
                enabled: !boardSurface.selectedBoard && !workspaceContext.editBoardSwitchBusy && !workspaceContext.spacePanActive
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: boardSurface.activeImage && workspaceContext.editTool !== "layers" ? Qt.NoButton : Qt.LeftButton
                onClicked: {
                    boardSurface.forceActiveFocus()
                    workspaceContext.selectEditBoard(boardSurface.index)
                }
                onDoubleClicked: workspaceContext.editBoardSelected(boardSurface.index)
            }
            DropArea {
                id: boardImageDropArea
                objectName: "editBoardImageDropArea" + boardSurface.index
                anchors.fill: parent
                enabled: !workspaceContext.editBoardSwitchBusy && !workspaceContext.editBoardExportBusy
                onEntered: function(drag) { drag.accepted = viewport.acceptsImageDrop(drag) }
                onDropped: function(drop) {
                    if (!viewport.acceptsImageDrop(drop)) { drop.accepted = false; return }
                    const point = boardSurface.mapToItem(viewport, drop.x, drop.y)
                    if (viewport.offerImageDrop(boardSurface.index, drop.urls[0], point)) drop.accept(Qt.CopyAction)
                    else drop.accepted = false
                }
            }
            Rectangle {
                objectName: "editBoardImageDropHighlight" + boardSurface.index
                anchors.fill: parent
                visible: boardImageDropArea.containsDrag
                color: themeContext.alpha(themeContext.primaryAccent, 0.09)
                border.width: 2
                border.color: themeContext.primaryAccent
                Text {
                    anchors.centerIn: parent
                    width: Math.max(1, parent.width - 24)
                    text: qsTr("Drop image\nChoose how to add it")
                    color: themeContext.textStrong
                    font.family: themeContext.fontUi
                    font.pixelSize: themeContext.typeControl
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Rectangle {
        objectName: "editBoardReorderInsertionMarker"
        visible: viewport.boardDragActive && viewport.boardDragInsertion >= 0
        x: visible ? viewport.boardBoundaryX(viewport.boardDragInsertion) - width / 2 : 0
        y: viewport.imageFrameY - themeContext.scaled(32)
        width: themeContext.scaled(3)
        height: viewport.boardUnitHeight * viewport.zoom + themeContext.scaled(32)
        color: themeContext.primaryAccent
        z: 48
    }

    Rectangle {
        objectName: "editBoardReorderDragLabel"
        visible: viewport.boardDragActive
        x: Math.max(themeContext.scaled(8), Math.min(viewport.width - width - themeContext.scaled(8), viewport.boardDragPoint.x - width / 2))
        y: Math.max(viewport.contentTopMargin, viewport.boardDragPoint.y - height - themeContext.scaled(12))
        width: Math.min(themeContext.scaled(320), boardDragText.implicitWidth + themeContext.scaled(24))
        height: themeContext.scaled(36)
        color: themeContext.surfaceTop
        border.color: themeContext.alpha(themeContext.primaryAccent, 0.6)
        radius: themeContext.radiusSurface
        z: 49
        Text {
            id: boardDragText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: themeContext.scaled(12)
            anchors.rightMargin: themeContext.scaled(12)
            text: viewport.boardDragLabel
            textFormat: Text.PlainText
            color: themeContext.textStrong
            font.family: themeContext.fontUi
            font.pixelSize: themeContext.scaled(12)
            elide: Text.ElideRight
        }
    }

    Controls.Popup {
        id: boardImageDropMenu
        objectName: "editBoardImageDropMenu"
        parent: viewport
        width: Math.min(themeContext.scaled(324), viewport.width - 24)
        height: contentItem.implicitHeight + padding * 2
        padding: 16
        focus: true
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
        readonly property var board: viewport.boardStageModel[viewport.pendingImageDropIndex] || ({})
        onOpened: addDroppedLayerButton.forceActiveFocus(Qt.PopupFocusReason)
        onClosed: workspaceContext.forceActiveFocus(Qt.PopupFocusReason)
        background: Rectangle {
            color: themeContext.ink900
            radius: themeContext.radiusControl
            border.width: themeContext.hairlineWidth
            border.color: themeContext.controlEdge
        }
        contentItem: ColumnLayout {
            spacing: 8
            Controls.Label {
                Layout.fillWidth: true
                text: qsTr("Add image to %1").arg(String(boardImageDropMenu.board.boardName || "") || qsTr("Board %1").arg(viewport.pendingImageDropIndex + 1))
                color: themeContext.textStrong
                font.family: themeContext.fontUi
                font.pixelSize: themeContext.typeControl
                font.weight: Font.Medium
                wrapMode: Text.Wrap
            }
            ToolButton {
                id: addDroppedLayerButton
                objectName: "editBoardDropAddLayerButton"
                Layout.fillWidth: true
                text: qsTr("Add image layer")
                iconName: "image"
                showLabel: true
                onClicked: viewport.applyImageDrop("layer")
            }
            ToolButton {
                objectName: "editBoardDropReplaceBaseButton"
                Layout.fillWidth: true
                text: Number(boardImageDropMenu.board.imageSpanCount || 1) > 1 ? qsTr("Replace spanning base image") : qsTr("Replace base image")
                iconName: "source"
                showLabel: true
                onClicked: viewport.applyImageDrop("replace")
            }
            Controls.Label {
                objectName: "editBoardDropScopeLabel"
                Layout.fillWidth: true
                text: Number(boardImageDropMenu.board.imageSpanCount || 1) > 1
                    ? qsTr("Both choices affect the shared image and layers on boards %1–%2.")
                        .arg(Number(boardImageDropMenu.board.imageSpanStart || 0) + 1)
                        .arg(Number(boardImageDropMenu.board.imageSpanStart || 0) + Number(boardImageDropMenu.board.imageSpanCount || 1))
                    : qsTr("This board keeps its own base image and layers.")
                color: themeContext.textMuted
                font.family: themeContext.fontUi
                font.pixelSize: themeContext.typeCaption
                wrapMode: Text.Wrap
            }
            Controls.Label {
                Layout.fillWidth: true
                text: qsTr("Replacement keeps your content layers and resets erasing on the old base image.")
                color: themeContext.textMuted
                font.family: themeContext.fontUi
                font.pixelSize: themeContext.typeCaption
                wrapMode: Text.Wrap
            }
        }
    }

    Rectangle {
        objectName: "editActionFeedback"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        width: Math.min(parent.width - 32, editActionFeedbackLabel.implicitWidth + 28)
        height: editActionFeedbackLabel.implicitHeight + 20
        z: 50
        enabled: false
        visible: opacity > 0
        opacity: viewport.editBoardSurface && workspaceContext.editActionFeedback.length > 0 ? 1 : 0
        color: themeContext.ink900
        radius: themeContext.radiusControl
        border.width: themeContext.hairlineWidth
        border.color: workspaceContext.editActionFeedbackError ? themeContext.warning : themeContext.controlEdge
        Accessible.role: Accessible.StaticText
        Accessible.name: workspaceContext.editActionFeedback
        Behavior on opacity { NumberAnimation { duration: themeContext.durationFast } }
        Text {
            id: editActionFeedbackLabel
            objectName: "editActionFeedbackLabel"
            anchors.centerIn: parent
            width: Math.max(1, parent.width - 28)
            text: workspaceContext.editActionFeedback
            color: workspaceContext.editActionFeedbackError ? themeContext.warning : themeContext.textStrong
            font.family: themeContext.fontUi
            font.pixelSize: themeContext.typeControl
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    MenuButton {
        objectName: "editCanvasAddBoardButton"
        visible: viewport.editBoardSurface && viewport.previewContentVisible && viewport.previewContext.hasAsset
        x: Math.max(8, Math.min(viewport.width - width - 12,
            (viewport.boardStageActive ? viewport.imageFrameX : viewport.paintedContentX) + viewport.boardOffset(viewport.boardFocusMode ? viewport.boardStageIndex : viewport.boardStageCount - 1,
                                                            viewport.boardUnitWidth * viewport.zoom, viewport.zoom) -
            viewport.boardOffset(viewport.imageSpanStart, viewport.boardUnitWidth * viewport.zoom, viewport.zoom) + viewport.boardUnitWidth * viewport.zoom + 12))
        y: Math.max(viewport.contentTopMargin + 20, Math.min(viewport.height - height - 12,
            viewport.paintedContentY + (viewport.paintedContentHeight - height) / 2))
        z: 40
        iconName: "plus"
        showText: false
        compact: true
        primaryOpensMenu: true
        showMenuIndicator: false
        width: height
        tone: "quiet"
        enabled: !workspaceContext.editBoardSwitchBusy && !previewContext.processing && !previewContext.photoPaintBusy &&
                 !workspaceContext.editBoardExportBusy && (!viewport.boardStageActive || workspaceContext.editBoardCanAdd)
        disabledReason: workspaceContext.editBoardExportBusy ? qsTr("Wait for the current export to finish.")
                        : viewport.boardStageActive && !workspaceContext.editBoardCanAdd ? qsTr("This document has reached its board limit.")
                        : qsTr("Finish the current edit before adding a board.")
        accessibleName: "Add board"
        tooltipText: "Add a blank board or extend this canvas."
        menuItems: [{ key: "blank", label: qsTr("New blank board"), icon: "plus", description: qsTr("An empty board with its own base image and layers.") },
                    { key: "extend", label: qsTr("Extend this canvas"), icon: "chevron-right", description: qsTr("Span this image and its layers across another export tile.") },
                    { key: "photo", label: qsTr("Add photo board…"), icon: "image", description: qsTr("A separate photo with the same board proportions and its own layers.") }]
        onMenuItemTriggered: function(key) {
            if (key === "photo") workspaceContext.editBoardActionRequested(viewport.boardStageIndex, "add-photo")
            else workspaceContext.editBoardAddRequested(key === "extend")
        }
    }

    MiniButton {
        id: boardViewModeButton
        objectName: "editBoardViewModeButton"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        z: 40
        visible: viewport.boardStageActive
        text: viewport.boardFocusMode ? qsTr("All boards") : qsTr("Focus board")
        iconName: viewport.boardFocusMode ? "grid" : "fullscreen"
        role: "ghost"
        enabled: !workspaceContext.editBoardSwitchBusy
        tooltip: viewport.boardFocusMode ? qsTr("Fit the neighboring boards into the canvas.")
                                        : qsTr("Fit the active board for detailed editing.")
        onClicked: viewport.setBoardFocus(!viewport.boardFocusMode)
    }
    Text {
        objectName: "editBoardDocumentStatus"
        x: 12
        y: parent.height - height - 19
        width: Math.max(1, boardViewModeButton.x - x - 16 - (boardSaveRetry.visible ? boardSaveRetry.width + 8 : 0))
        z: 40
        visible: viewport.boardStageActive
        text: workspaceContext.editBoardDocumentName + "  ·  " + String(viewport.activeBoardData.boardPixelWidth || previewContext.sourceImageWidth) + " × " +
              String(viewport.activeBoardData.boardPixelHeight || previewContext.sourceImageHeight) + qsTr(" px  ·  Board %1 of %2  ·  ").arg(viewport.boardStageIndex + 1).arg(viewport.boardStageCount) +
              (workspaceContext.editBoardSaveError.length > 0 ? qsTr("Save failed") : workspaceContext.editBoardSaveState) +
              (viewport.imageSpanCount > 1 ? qsTr("  ·  Shared across %1 boards").arg(viewport.imageSpanCount) : "")
        color: workspaceContext.editBoardSaveError.length > 0 ? themeContext.error : themeContext.textMuted
        font.family: themeContext.fontUi
        font.pixelSize: themeContext.scaled(10)
        elide: Text.ElideMiddle
        Accessible.role: Accessible.StaticText
        Accessible.name: text + (workspaceContext.editBoardSaveError.length > 0 ? ". " + workspaceContext.editBoardSaveError : "")
    }
    MiniButton {
        id: boardSaveRetry
        objectName: "editBoardSaveRetry"
        anchors.right: boardViewModeButton.left
        anchors.rightMargin: 8
        anchors.verticalCenter: boardViewModeButton.verticalCenter
        z: 40
        visible: viewport.boardStageActive && workspaceContext.editBoardSaveError.length > 0
        text: qsTr("Retry save")
        iconName: "retry"
        role: "ghost"
        enabled: !workspaceContext.editBoardSwitchBusy
        tooltip: workspaceContext.editBoardSaveError
        onClicked: workspaceContext.editBoardSaveRequested()
    }

    Rectangle {
        objectName: "editImageSafeMargins"
        visible: viewport.editBoardSurface && !viewport.boardStageActive && viewport.workspaceContext.layerSafeMarginsEnabled
        x: viewport.paintedContentX + viewport.paintedContentWidth * 0.05
        y: viewport.paintedContentY + viewport.paintedContentHeight * 0.05
        width: viewport.paintedContentWidth * 0.9
        height: viewport.paintedContentHeight * 0.9
        color: "transparent"
        border.width: themeContext.hairlineWidth
        border.color: themeContext.alpha(themeContext.primaryAccent, 0.4)
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
        anchorX: viewport.boardStageActive ? viewport.basePaintedContentX : previewImage.x
        anchorY: viewport.boardStageActive ? viewport.basePaintedContentY : previewImage.y
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
        previewAnchorX: viewport.boardStageActive ? viewport.basePaintedContentX : previewImage.x
        previewAnchorY: viewport.boardStageActive ? viewport.basePaintedContentY : previewImage.y
        previewAnchorWidth: viewport.boardStageActive ? viewport.basePaintedContentWidth : previewImage.width
        previewAnchorHeight: viewport.boardStageActive ? viewport.basePaintedContentHeight : previewImage.height
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
        enablePolygonHandles: !workspaceContext.polygonDrawArmed
        enableOutlineSelection: !workspaceContext.brushToolReady() && !workspaceContext.pixelExactToolReady() &&
                                !workspaceContext.lassoDrawArmed && !workspaceContext.polygonDrawArmed &&
                                !workspaceContext.objectPickArmed
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
        activeBrushHardness: previewContext.activeMaskBrushHardness * (1 - previewContext.activeMaskFeather)
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
