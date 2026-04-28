import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PhotoEditor
import "../components"

Rectangle {
    id: root

    color: "transparent"
    focus: true

    Theme { id: theme }

    property real manualZoom: 1.0
    property bool fitToView: true
    property bool inspectorCollapsed: false
    property string transientMaskTool: ""
    property bool optionModifierActive: false
    property bool controlModifierActive: false
    property bool spacePanActive: false
    property bool selectionBrushRestorePending: false
    property bool selectionBrushRestoreSubtract: false
    readonly property bool objectPickArmed: transientMaskTool === "smart" ||
                                            transientMaskTool === "smart-subtract" ||
                                            transientMaskTool === "smart-new" ||
                                            transientMaskTool === "remove" ||
                                            transientMaskTool === "remove-new"
    readonly property bool objectPickCreateNew: transientMaskTool === "smart-new" ||
                                                transientMaskTool === "remove-new"
    readonly property string objectPickMode: transientMaskTool === "smart-subtract" ||
                                             (optionModifierActive &&
                                              transientMaskTool !== "smart-new" &&
                                              transientMaskTool !== "remove" &&
                                              transientMaskTool !== "remove-new" &&
                                              transientMaskTool.indexOf("smart") === 0)
                                             ? "selection-subtract"
                                             : (transientMaskTool === "remove" || transientMaskTool === "remove-new"
                                                ? "remove"
                                                : "selection")
    readonly property bool polygonDrawArmed: transientMaskTool === "polygon"
    readonly property bool lassoDrawArmed: transientMaskTool === "lasso"
    readonly property bool cloneSourcePickArmed: transientMaskTool === "clone-source" ||
                                                 (optionModifierActive &&
                                                  !controlModifierActive &&
                                                  editorPreview.activeMaskKind === "brush" &&
                                                  editorPreview.activeMaskRetouchMode === "clone" &&
                                                  !objectPickArmed &&
                                                  !polygonDrawArmed &&
                                                  !lassoDrawArmed)
    property real selectionOutlinePhase: 0
    property string cropGuideMode: "Thirds"
    property string compareViewMode: "side-by-side"
    property real compareWipePosition: 0.5
    property string editedDisplaySource: ""
    property string referenceDisplaySource: ""
    property string presetDraftName: ""
    property string selectedPresetName: ""
    property bool histogramShowLuminance: true
    property bool histogramShowRgb: true
    property bool histogramLogScale: true
    property var hslChannels: [
        {
            name: "Reds",
            color: "#d45a62"
        },
        {
            name: "Oranges",
            color: "#dd8a39"
        },
        {
            name: "Yellows",
            color: "#d7bd42"
        },
        {
            name: "Greens",
            color: "#61b56f"
        },
        {
            name: "Aquas",
            color: "#49b8c4"
        },
        {
            name: "Blues",
            color: "#4e86d9"
        },
        {
            name: "Purples",
            color: "#8b6be3"
        },
        {
            name: "Magentas",
            color: "#c85ac1"
        }
    ]
    property var adjustmentGroups: [
        { "key": "raw", "label": "RAW" },
        { "key": "tone", "label": "Tone" },
        { "key": "color", "label": "Color" },
        { "key": "crop", "label": "Crop" },
        { "key": "masks", "label": "Masks" },
        { "key": "detail", "label": "Detail" }
    ]
    property var selectedAdjustmentGroups: ["raw", "tone", "color", "crop", "masks", "detail"]
    property string inspectorMode: "adjust"
    property bool batchToolsExpanded: false
    onInspectorModeChanged: {
        if (inspectorMode === "crop" && editorPreview.hasAsset)
            editorPreview.cropEditing = true
        else if (inspectorMode !== "crop" && editorPreview.cropEditing)
            editorPreview.cropEditing = false
    }

    function clampZoom(value) {
        return Math.max(0.25, Math.min(4.0, value));
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function clearTransientTools() {
        transientMaskTool = ""
        editorPreview.clearSmartSelectPreview()
    }

    function activeSelectionBrushCanTemporarilyInvert() {
        return editorPreview.hasActiveMask &&
               editorPreview.activeMaskKind === "brush" &&
               editorPreview.activeMaskToolRole !== "retouch" &&
               !controlModifierActive &&
               !objectPickArmed &&
               !polygonDrawArmed &&
               !lassoDrawArmed
    }

    function manualSelectionSubtractActive() {
        return optionModifierActive && !controlModifierActive
    }

    function syncTemporaryBrushModifierState() {
        if (optionModifierActive && activeSelectionBrushCanTemporarilyInvert()) {
            if (!selectionBrushRestorePending) {
                selectionBrushRestoreSubtract = editorPreview.activeMaskOperation === "subtract"
                selectionBrushRestorePending = true
                editorPreview.startSelectionBrush(!selectionBrushRestoreSubtract)
            }
            return
        }

        if (selectionBrushRestorePending && !editorPreview.brushStrokeActive) {
            editorPreview.startSelectionBrush(selectionBrushRestoreSubtract)
            selectionBrushRestorePending = false
        }
    }

    function deselectMaskEditing() {
        clearTransientTools()
        activeGradePreset = ""
        editorPreview.deselectActiveMask()
    }

    function handleMaskPickOutcome(outcome) {
        if (outcome === "selection-ai" || outcome === "remove-ai")
            transientMaskTool = ""
    }

    function selectTool(mode) {
        clearTransientTools()
        if (mode === "smart" || mode === "smart-subtract" || mode === "smart-new" ||
                mode === "remove" || mode === "remove-new" || mode === "polygon" ||
                mode === "lasso" || mode === "clone-source") {
            transientMaskTool = mode
            inspectorMode = "masks"
        }
    }

    function activeToolLabel() {
        if (transientMaskTool === "lasso")
            return "Lasso"
        if (transientMaskTool === "clone-source")
            return "Pick Clone Source"
        if (transientMaskTool === "polygon")
            return "Polygon"
        if (transientMaskTool === "remove-new")
            return "Pick New Remove Target"
        if (transientMaskTool === "remove")
            return "Pick Remove Target"
        if (transientMaskTool === "smart-subtract")
            return "Subtract Object"
        if (transientMaskTool === "smart-new")
            return "New Object Selection"
        if (transientMaskTool === "smart")
            return "Object Select"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "clone")
            return "Clone Stamp"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "heal")
            return "Heal Brush"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskRetouchMode === "remove")
            return editorPreview.activeMaskCommitted ? "Remove Brush" : "Pending Remove Brush"
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskKind === "object" &&
                editorPreview.activeMaskRetouchMode === "remove")
            return editorPreview.activeMaskCommitted ? "Remove Target" : "Pending Remove Target"
        if (editorPreview.activeMaskKind === "polygon")
            return "Polygon Selection"
        if (editorPreview.activeMaskKind === "object")
            return "Object Selection"
        if (editorPreview.activeMaskKind === "subject")
            return "Main Subject"
        if (editorPreview.activeMaskKind === "brush" &&
                editorPreview.activeMaskName.indexOf("Edge ") === 0 &&
                editorPreview.activeMaskOperation === "subtract")
            return "Edge Erase"
        if (editorPreview.activeMaskKind === "brush" &&
                editorPreview.activeMaskName.indexOf("Edge ") === 0)
            return "Edge Add"
        if (editorPreview.activeMaskKind === "brush" && editorPreview.activeMaskOperation === "subtract")
            return "Brush Erase"
        if (editorPreview.activeMaskKind === "brush")
            return "Brush Add"
        return "Adjust"
    }

    function resetActiveMaskColorGrade() {
        editorPreview.activeMaskTemperature = 0
        editorPreview.activeMaskTint = 0
        editorPreview.activeMaskVibrance = 0
        editorPreview.activeMaskSaturation = 0
        editorPreview.activeMaskHueShift = 0
        editorPreview.activeMaskSaturationShift = 0
        editorPreview.activeMaskColorizeHue = 0
        editorPreview.activeMaskColorizeStrength = 0
    }

    property string activeGradePreset: ""

    function resetAllMaskAdjustments() {
        editorPreview.activeMaskExposure = 0
        editorPreview.activeMaskContrast = 0
        editorPreview.activeMaskHighlights = 0
        editorPreview.activeMaskShadows = 0
        editorPreview.activeMaskWhites = 0
        editorPreview.activeMaskBlacks = 0
        resetActiveMaskColorGrade()
    }

    function applyActiveMaskGradePreset(preset) {
        if (!editorPreview.hasActiveMask || editorPreview.activeMaskToolRole === "retouch")
            return

        if (preset === "reset") {
            resetAllMaskAdjustments()
            activeGradePreset = ""
            return
        }

        resetAllMaskAdjustments()
        activeGradePreset = preset

        if (preset === "warm") {
            editorPreview.activeMaskTemperature = 18
            editorPreview.activeMaskTint = 4
            editorPreview.activeMaskVibrance = 10
            editorPreview.activeMaskSaturation = 6
            return
        }

        if (preset === "cool") {
            editorPreview.activeMaskTemperature = -18
            editorPreview.activeMaskTint = -4
            editorPreview.activeMaskVibrance = 6
            editorPreview.activeMaskSaturation = -4
            return
        }

        if (preset === "pop") {
            editorPreview.activeMaskVibrance = 24
            editorPreview.activeMaskSaturation = 8
            editorPreview.activeMaskContrast = 12
            return
        }

        if (preset === "desat") {
            editorPreview.activeMaskVibrance = -12
            editorPreview.activeMaskSaturation = -28
            return
        }

        if (preset === "tint-warm") {
            editorPreview.activeMaskColorizeHue = 0.09
            editorPreview.activeMaskColorizeStrength = 24
            return
        }

        if (preset === "tint-cool") {
            editorPreview.activeMaskColorizeHue = 0.58
            editorPreview.activeMaskColorizeStrength = 20
            return
        }

        if (preset === "lift") {
            editorPreview.activeMaskExposure = 0.35
            editorPreview.activeMaskWhites = 12
            editorPreview.activeMaskShadows = 10
            return
        }

        if (preset === "burn") {
            editorPreview.activeMaskExposure = -0.35
            editorPreview.activeMaskBlacks = -12
            editorPreview.activeMaskContrast = 10
            return
        }

        if (preset === "soften") {
            editorPreview.activeMaskContrast = -16
            editorPreview.activeMaskHighlights = -10
            editorPreview.activeMaskShadows = 10
            editorPreview.activeMaskSaturation = -8
            return
        }

        if (preset === "teal-pop") {
            editorPreview.activeMaskTemperature = -10
            editorPreview.activeMaskTint = -6
            editorPreview.activeMaskVibrance = 18
            editorPreview.activeMaskColorizeHue = 0.54
            editorPreview.activeMaskColorizeStrength = 14
        }
    }

    function maskWorkspaceAccentColor() {
        if (editorPreview.hasActiveMask &&
                editorPreview.activeMaskIndex >= 0 &&
                editorPreview.activeMaskIndex < editorPreview.maskStack.length)
            return maskAccent(editorPreview.maskStack[editorPreview.activeMaskIndex])
        if (objectPickArmed || polygonDrawArmed || lassoDrawArmed)
            return theme.accentWarm400
        if (cloneSourcePickArmed)
            return theme.accentWarm500
        return theme.accentCool400
    }

    function previewStatusText() {
        if (inspectorMode !== "masks" || !editorPreview.hasAsset)
            return editorPreview.processing ? "" : editorPreview.status

        if (cloneSourcePickArmed)
            return "Masks · " + activeToolLabel() + " · Choose a clean source area"
        if (objectPickArmed && objectPickMode === "remove")
            return "Masks · " + activeToolLabel() + " · Click or drag over the target"
        if (objectPickArmed)
            return "Masks · " + activeToolLabel() + " · Click the image to build the selection"
        if (polygonDrawArmed)
            return "Masks · Polygon · Click points and double-click to finish"
        if (lassoDrawArmed)
            return "Masks · Lasso · Drag directly on the canvas"
        if (editorPreview.hasActiveMask)
            return "Masks · " + activeToolLabel() + " · " + editorPreview.activeMaskName
        return "Masks · Choose a selection source or start painting"
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
               Math.abs(editorPreview.activeMaskColorizeStrength) > 0.001
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

    function maskAccent(mask) {
        if (!mask)
            return "#8ab8d8"
        if (mask.operation === "subtract")
            return "#d36a5e"
        if (mask.operation === "intersect")
            return "#6fa96d"
        if (mask.toolRole === "retouch" && mask.retouchMode === "clone")
            return theme.accentWarm500
        if (mask.toolRole === "retouch" && mask.retouchMode === "heal")
            return "#72cbb2"
        if (mask.toolRole === "retouch" && mask.retouchMode === "remove")
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
            return theme.accentWarm400
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
        editorPreview.activeMaskSelectionExpand = clamp(editorPreview.activeMaskSelectionExpand + delta, -100, 100)
    }

    function nudgeActiveMaskFeather(delta) {
        const minimum = editorPreview.activeMaskKind === "polygon" ? 0.0 : 0.05
        const maximum = editorPreview.activeMaskKind === "polygon" ? 0.2 : 1.0
        editorPreview.activeMaskFeather = clamp(
                    editorPreview.activeMaskFeather + delta,
                    minimum,
                    maximum)
    }

    function maxPanX() {
        return Math.max(0, zoomPreviewContent.width - zoomPreviewFlick.width);
    }

    function sideBySideCompareEnabled() {
        return editorPreview.compareMode &&
               compareViewMode === "side-by-side" &&
               referenceDisplaySource.length > 0
    }

    function wipeCompareEnabled() {
        return editorPreview.compareMode &&
               compareViewMode === "wipe" &&
               referenceDisplaySource.length > 0
    }

    function maxPanY() {
        return Math.max(0, zoomPreviewContent.height - zoomPreviewFlick.height);
    }

    function clampPanPosition() {
        zoomPreviewFlick.contentX = Math.max(0, Math.min(maxPanX(), zoomPreviewFlick.contentX));
        zoomPreviewFlick.contentY = Math.max(0, Math.min(maxPanY(), zoomPreviewFlick.contentY));
    }

    function centerZoomPreview() {
        zoomPreviewFlick.contentX = maxPanX() / 2;
        zoomPreviewFlick.contentY = maxPanY() / 2;
    }

    function updatePreviewTargetEdge() {
        const viewportWidth = Math.max(1, previewSurface.width - 4);
        const viewportHeight = Math.max(1, previewSurface.height - 4);
        const compareWidth = sideBySideCompareEnabled() ? Math.max(1, (viewportWidth - 8) / 2) : viewportWidth;
        const zoomMultiplier = fitToView ? 1.0 : Math.min(2.0, Math.max(1.0, manualZoom));
        editorPreview.previewTargetEdge = Math.round(Math.max(compareWidth, viewportHeight) * Screen.devicePixelRatio * zoomMultiplier);
    }

    function updatePreviewViewport() {
        if (!editorPreview.hasAsset || fitToView || editorPreview.compareMode ||
                !zoomPreviewFlick.visible ||
                !(editedPreviewZoom.visible || editedPreviewZoomNative.visible)) {
            editorPreview.setPreviewViewport(0, 0, 0, 0, 0, false)
            return
        }

        const left = Math.max(editedPreviewZoom.x, zoomPreviewFlick.contentX)
        const top = Math.max(editedPreviewZoom.y, zoomPreviewFlick.contentY)
        const right = Math.min(editedPreviewZoom.x + editedPreviewZoom.width,
                               zoomPreviewFlick.contentX + zoomPreviewFlick.width)
        const bottom = Math.min(editedPreviewZoom.y + editedPreviewZoom.height,
                                zoomPreviewFlick.contentY + zoomPreviewFlick.height)
        const visibleWidth = right - left
        const visibleHeight = bottom - top

        if (visibleWidth <= 1 || visibleHeight <= 1 || editedPreviewZoom.width <= 0 || editedPreviewZoom.height <= 0) {
            editorPreview.setPreviewViewport(0, 0, 0, 0, 0, false)
            return
        }

        const normalizedX = (left - editedPreviewZoom.x) / editedPreviewZoom.width
        const normalizedY = (top - editedPreviewZoom.y) / editedPreviewZoom.height
        const normalizedWidth = visibleWidth / editedPreviewZoom.width
        const normalizedHeight = visibleHeight / editedPreviewZoom.height
        const patchEdge = Math.round(Math.max(visibleWidth, visibleHeight) * Screen.devicePixelRatio)
        editorPreview.setPreviewViewport(
                    normalizedX,
                    normalizedY,
                    normalizedWidth,
                    normalizedHeight,
                    patchEdge,
                    true)
    }

    function imagePixelWidth(image) {
        return Math.max(1, image.implicitWidth > 0 ? image.implicitWidth : image.sourceSize.width);
    }

    function imagePixelHeight(image) {
        return Math.max(1, image.implicitHeight > 0 ? image.implicitHeight : image.sourceSize.height);
    }

    function editedPreviewPixelWidth(image) {
        return editorPreview.displayPreviewNativeAvailable
               ? Math.max(1, editorPreview.displayPreviewNativeSize.width)
               : imagePixelWidth(image)
    }

    function editedPreviewPixelHeight(image) {
        return editorPreview.displayPreviewNativeAvailable
               ? Math.max(1, editorPreview.displayPreviewNativeSize.height)
               : imagePixelHeight(image)
    }

    function preserveAspectFitSize(sourceWidth, sourceHeight, targetWidth, targetHeight) {
        const safeSourceWidth = Math.max(1, sourceWidth)
        const safeSourceHeight = Math.max(1, sourceHeight)
        const scale = Math.min(Math.max(0, targetWidth) / safeSourceWidth,
                               Math.max(0, targetHeight) / safeSourceHeight)
        return Qt.size(safeSourceWidth * scale, safeSourceHeight * scale)
    }

    function fitZoomLevel() {
        if (editedDisplaySource.length === 0)
            return 1.0;

        const imageWidth = editedPreviewPixelWidth(editedPreviewZoom);
        const imageHeight = editedPreviewPixelHeight(editedPreviewZoom);
        const availableWidth = sideBySideCompareEnabled()
                               ? Math.max(1, (fitPreviewRow.width - 8) / 2)
                               : Math.max(1, fitPreviewRow.width);
        const availableHeight = Math.max(1, fitPreviewRow.height);
        return Math.min(availableWidth / imageWidth, availableHeight / imageHeight);
    }

    function currentZoomLevel() {
        return fitToView ? fitZoomLevel() : manualZoom;
    }

    NumberAnimation on selectionOutlinePhase {
        from: 0
        to: 24
        duration: 900
        loops: Animation.Infinite
        running: false
    }

    onManualZoomChanged: {
        updatePreviewTargetEdge()
        updatePreviewViewport()
    }
    onFitToViewChanged: {
        updatePreviewTargetEdge()
        updatePreviewViewport()
    }
    onCompareViewModeChanged: {
        compareWipePosition = 0.5
        updatePreviewTargetEdge()
        updatePreviewViewport()
    }

    Connections {
        target: editorPreview
        function onCompareModeChanged() {
            if (editorPreview.compareMode && editorPreview.whiteBalanceSampling)
                editorPreview.whiteBalanceSampling = false;
            updatePreviewTargetEdge();
            updatePreviewViewport();
        }
    }

    function contentWidthForZoom(zoomLevel) {
        const editedWidth = editedDisplaySource.length > 0 ? editedPreviewPixelWidth(editedPreviewZoom) * zoomLevel : 0;
        const referenceWidth = sideBySideCompareEnabled()
                               ? imagePixelWidth(referencePreviewZoom) * zoomLevel
                               : 0;
        const combinedWidth = sideBySideCompareEnabled() && referenceWidth > 0
                              ? editedWidth + referenceWidth + 16
                              : editedWidth;
        return Math.max(zoomPreviewFlick.width, combinedWidth);
    }

    function contentHeightForZoom(zoomLevel) {
        const editedHeight = editedDisplaySource.length > 0 ? editedPreviewPixelHeight(editedPreviewZoom) * zoomLevel : 0;
        const referenceHeight = sideBySideCompareEnabled()
                                ? imagePixelHeight(referencePreviewZoom) * zoomLevel
                                : 0;
        return Math.max(zoomPreviewFlick.height, Math.max(editedHeight, referenceHeight));
    }

    function setZoomLevel(value, pivotX, pivotY) {
        const nextZoom = clampZoom(value);
        const currentZoom = currentZoomLevel();
        if (Math.abs(nextZoom - currentZoom) < 0.0001)
            return;

        const fitZoom = fitZoomLevel();
        if (nextZoom <= fitZoom * 1.01) {
            fitToView = true;
            zoomPreviewFlick.contentX = 0;
            zoomPreviewFlick.contentY = 0;
            return;
        }

        const viewportPivotX = pivotX === undefined ? zoomPreviewFlick.width / 2 : pivotX;
        const viewportPivotY = pivotY === undefined ? zoomPreviewFlick.height / 2 : pivotY;
        const oldContentWidth = fitToView ? zoomPreviewFlick.width : contentWidthForZoom(currentZoom);
        const oldContentHeight = fitToView ? zoomPreviewFlick.height : contentHeightForZoom(currentZoom);
        const oldFocusX = (fitToView ? 0 : zoomPreviewFlick.contentX) + viewportPivotX;
        const oldFocusY = (fitToView ? 0 : zoomPreviewFlick.contentY) + viewportPivotY;
        const widthRatio = oldFocusX / Math.max(1, oldContentWidth);
        const heightRatio = oldFocusY / Math.max(1, oldContentHeight);

        fitToView = false;
        manualZoom = nextZoom;

        const newContentWidth = contentWidthForZoom(nextZoom);
        const newContentHeight = contentHeightForZoom(nextZoom);
        zoomPreviewFlick.contentX = widthRatio * newContentWidth - viewportPivotX;
        zoomPreviewFlick.contentY = heightRatio * newContentHeight - viewportPivotY;
        clampPanPosition();
    }

    function hslColorForChannel(channelName) {
        for (let index = 0; index < hslChannels.length; ++index) {
            if (hslChannels[index].name === channelName)
                return hslChannels[index].color;
        }

        return "#c99a52";
    }

    function selectedHslColor() {
        return hslColorForChannel(editorPreview.hslChannel);
    }

    function rainbowTrack() {
        return ["#d45a62", "#dd8a39", "#d7bd42", "#61b56f", "#49b8c4", "#4e86d9", "#8b6be3", "#c85ac1", "#d45a62"];
    }

    function gradingPreviewColor(hueValue, saturationValue, luminanceValue) {
        const hue = ((hueValue / 100.0) % 1.0 + 1.0) % 1.0;
        const saturation = Math.max(0.18, Math.min(1.0, Math.abs(saturationValue) / 100.0));
        const lightness = Math.max(0.38, Math.min(0.76, 0.56 + luminanceValue / 280.0));
        return Qt.hsla(hue, saturation, lightness, 1.0);
    }

    function activeAdjustmentGroups() {
        return selectedAdjustmentGroups.slice(0);
    }

    function hasAdjustmentGroup(groupKey) {
        return selectedAdjustmentGroups.indexOf(groupKey) >= 0;
    }

    function toggleAdjustmentGroup(groupKey) {
        let nextGroups = selectedAdjustmentGroups.slice(0);
        const existingIndex = nextGroups.indexOf(groupKey);
        if (existingIndex >= 0) {
            if (nextGroups.length === 1)
                return;
            nextGroups.splice(existingIndex, 1);
        } else {
            nextGroups.push(groupKey);
        }
        selectedAdjustmentGroups = nextGroups;
    }

    function selectedBatchAssetIds() {
        return catalogAssets.selectedAssetIds;
    }

    function copyCurrentAdjustments() {
        editorPreview.copyCurrentSettings();
    }

    function pasteCopiedAdjustments() {
        const groups = activeAdjustmentGroups();
        const targetIds = selectedBatchAssetIds();
        const recipeJson = editorPreview.copiedSettingsRecipeJson();
        if (recipeJson.length === 0)
            return;
        editorPreview.pasteCopiedSettings(groups);
        catalogAssets.applyRecipeJsonToAssetIds(targetIds, recipeJson, groups);
    }

    function syncCurrentAdjustments() {
        const targetIds = selectedBatchAssetIds();
        if (targetIds.length === 0)
            return;
        catalogAssets.applyRecipeJsonToAssetIds(targetIds, editorPreview.currentRecipeJson(), activeAdjustmentGroups());
    }

    function applyPresetToSelection(presetName) {
        if (!presetName || presetName.length === 0)
            return;

        const groups = activeAdjustmentGroups();
        const targetIds = selectedBatchAssetIds();
        const recipeJson = editorPreview.presetRecipeJson(presetName);
        if (recipeJson.length === 0)
            return;

        editorPreview.applyPreset(presetName, groups);
        catalogAssets.applyRecipeJsonToAssetIds(targetIds, recipeJson, groups);
        selectedPresetName = presetName;
    }

    function saveCurrentPreset() {
        const presetName = presetDraftName.trim();
        if (presetName.length === 0)
            return;
        if (editorPreview.savePreset(presetName, true)) {
            presetDraftName = presetName;
            selectedPresetName = presetName;
        }
    }

    function selectAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson) {
        catalogAssets.selectIndex(index);
    }

    function selectRelativeAsset(offset) {
        if (catalogAssets.assetCount === 0)
            return;

        const baseIndex = catalogAssets.selectedIndex >= 0 ? catalogAssets.selectedIndex : 0;
        const nextIndex = Math.max(0, Math.min(catalogAssets.assetCount - 1, baseIndex + offset));
        if (nextIndex === baseIndex)
            return;

        catalogAssets.selectIndex(nextIndex);
    }

    Connections {
        target: editorPreview

        function onPreviewChanged() {
            editedDisplaySource = editorPreview.displayPreviewSource;
            referenceDisplaySource = editorPreview.displayReferencePreviewSource;
            updatePreviewViewport();
        }

        function onActiveBrushStrokeChanged() {
            syncTemporaryBrushModifierState()
        }

        function onCompareModeChanged() {
            if (!fitToView)
                centerZoomPreview();
        }

        function onPresetsChanged() {
            if (selectedPresetName.length > 0 && editorPreview.presetNames.indexOf(selectedPresetName) >= 0)
                return

            selectedPresetName = editorPreview.presetNames.length > 0 ? editorPreview.presetNames[0] : ""
            if (presetDraftName.length === 0 || selectedPresetName.length === 0)
                presetDraftName = selectedPresetName
        }
    }

    Connections {
        target: catalogAssets

        function onSelectedIndexChanged() {
            if (catalogAssets.selectedIndex >= 0)
                filmstripView.positionViewAtIndex(catalogAssets.selectedIndex, ListView.Center);
        }
    }

    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: catalogAssets.selectedIndex > 0
        onActivated: selectRelativeAsset(-1)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: catalogAssets.selectedIndex >= 0 && catalogAssets.selectedIndex < catalogAssets.assetCount - 1
        onActivated: selectRelativeAsset(1)
    }

    Shortcut {
        sequence: StandardKey.Deselect
        context: Qt.WindowShortcut
        enabled: editorPreview.hasActiveMask || editorPreview.smartSelectPreviewActive ||
                 objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed
        onActivated: deselectMaskEditing()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: editorPreview.hasActiveMask || editorPreview.smartSelectPreviewActive ||
                 objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed
        onActivated: deselectMaskEditing()
    }

    Shortcut {
        sequence: "Return"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" &&
                 editorPreview.hasActiveMask &&
                 editorPreview.activeMaskToolRole === "retouch" &&
                 !editorPreview.activeMaskCommitted
        onActivated: editorPreview.applyActiveRetouch()
    }

    Shortcut {
        sequence: "Enter"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" &&
                 editorPreview.hasActiveMask &&
                 editorPreview.activeMaskToolRole === "retouch" &&
                 !editorPreview.activeMaskCommitted
        onActivated: editorPreview.applyActiveRetouch()
    }

    Shortcut {
        sequence: "Backspace"
        context: Qt.WindowShortcut
        enabled: polygonDrawArmed && editorPreview.activeMaskPolygonPointCount > 0
        onActivated: editorPreview.removeLastActivePolygonPoint()
    }

    Shortcut {
        sequence: "Tab"
        context: Qt.WindowShortcut
        enabled: objectPickArmed && editorPreview.smartSelectCanCycleCandidates
        onActivated: editorPreview.cycleSmartSelectCandidate(1)
    }

    Shortcut {
        sequence: "Shift+Tab"
        context: Qt.WindowShortcut
        enabled: objectPickArmed && editorPreview.smartSelectCanCycleCandidates
        onActivated: editorPreview.cycleSmartSelectCandidate(-1)
    }

    Shortcut {
        sequence: "B"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasAsset && editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        onActivated: {
            selectionBrushRestorePending = false
            clearTransientTools()
            editorPreview.startSelectionBrush(false)
        }
    }

    Shortcut {
        sequence: "E"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasAsset && editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        onActivated: {
            selectionBrushRestorePending = false
            clearTransientTools()
            editorPreview.startSelectionBrush(true)
        }
    }

    Shortcut {
        sequence: "["
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.activeMaskKind === "brush"
        onActivated: editorPreview.activeMaskBrushSize = clamp(editorPreview.activeMaskBrushSize - 0.02, 0.01, 0.35)
    }

    Shortcut {
        sequence: "]"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.activeMaskKind === "brush"
        onActivated: editorPreview.activeMaskBrushSize = clamp(editorPreview.activeMaskBrushSize + 0.02, 0.01, 0.35)
    }

    Shortcut {
        sequence: "I"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        onActivated: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
    }

    Shortcut {
        sequence: "V"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasActiveMask
        onActivated: {
            if (editorPreview.maskPreviewMode === "overlay")
                editorPreview.maskPreviewMode = "grayscale"
            else if (editorPreview.maskPreviewMode === "grayscale")
                editorPreview.maskPreviewMode = "black-on-white"
            else if (editorPreview.maskPreviewMode === "black-on-white")
                editorPreview.maskPreviewMode = "white-on-black"
            else
                editorPreview.maskPreviewMode = "overlay"
        }
    }

    Shortcut {
        sequence: "H"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasAsset
        onActivated: {
            clearTransientTools()
            editorPreview.startHealBrush()
        }
    }

    Shortcut {
        sequence: "Delete"
        context: Qt.WindowShortcut
        enabled: inspectorMode === "masks" && editorPreview.hasActiveMask
        onActivated: editorPreview.removeActiveMask()
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Control) {
            controlModifierActive = true
            syncTemporaryBrushModifierState()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Alt) {
            if (!optionModifierActive) {
                optionModifierActive = true
                syncTemporaryBrushModifierState()
            }
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Space) {
            if (!spacePanActive)
                spacePanActive = true
            event.accepted = true
        }
    }

    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Control) {
            controlModifierActive = false
            syncTemporaryBrushModifierState()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Alt) {
            optionModifierActive = false
            syncTemporaryBrushModifierState()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Space) {
            spacePanActive = false
            event.accepted = true
        }
    }

    Component.onCompleted: {
        updatePreviewTargetEdge();
        editedDisplaySource = editorPreview.displayPreviewSource;
        referenceDisplaySource = editorPreview.displayReferencePreviewSource;
        if (editorPreview.presetNames.length > 0) {
            selectedPresetName = editorPreview.presetNames[0];
            presetDraftName = selectedPresetName;
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

            BrandPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                headerVisible: false
                bodyMargin: 2
                bodySpacing: 4
                title: "Editor"

                Rectangle {
                    id: previewSurface
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius12
                    color: theme.ink900
                    border.width: 0
                    border.color: "transparent"
                    clip: true
                    onWidthChanged: {
                        updatePreviewTargetEdge()
                        updatePreviewViewport()
                    }
                    onHeightChanged: {
                        updatePreviewTargetEdge()
                        updatePreviewViewport()
                    }

                    Flickable {
                        id: canvasToolbar
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 8
                        z: 120
                        height: visible ? 50 : 0
                        contentWidth: canvasToolbarRow.implicitWidth
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: contentWidth > width
                        visible: (inspectorMode === "masks" || editorPreview.hasActiveMask || objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed) && editorPreview.hasAsset

                    Row {
                        id: canvasToolbarRow
                        spacing: 8

                        Rectangle {
                            width: selectToolColumn.implicitWidth + 16
                            height: selectToolColumn.implicitHeight + 10
                            radius: theme.radius8
                            color: theme.alpha(theme.ink950, objectPickArmed || lassoDrawArmed || polygonDrawArmed ? 0.96 : 0.86)
                            border.width: 1
                            border.color: theme.alpha(theme.accentWarm400, objectPickArmed || lassoDrawArmed || polygonDrawArmed ? 0.42 : 0.16)

                            Column {
                                id: selectToolColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Select"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        compact: true
                                        text: "Object"
                                        iconName: "smart-select"
                                        tooltipText: "Object Select"
                                        accent: theme.accentWarm400
                                        tone: active ? "primary" : "secondary"
                                        active: objectPickArmed && objectPickMode === "selection" && !objectPickCreateNew
                                        enabled: editorPreview.hasAsset
                                        onClicked: selectTool(objectPickArmed && objectPickMode === "selection" && !objectPickCreateNew ? "" : "smart")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Subtract"
                                        iconName: "smart-select"
                                        tooltipText: "Subtract Object"
                                        accent: theme.accentWarm400
                                        tone: active ? "primary" : "secondary"
                                        active: objectPickArmed && objectPickMode === "selection-subtract"
                                        enabled: editorPreview.hasAsset && editorPreview.hasActiveMask
                                        onClicked: selectTool(objectPickArmed && objectPickMode === "selection-subtract" ? "" : "smart-subtract")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Lasso"
                                        iconName: "lasso"
                                        tooltipText: "Lasso"
                                        accent: "#7ec97a"
                                        tone: active ? "primary" : "secondary"
                                        active: lassoDrawArmed
                                        onClicked: selectTool(lassoDrawArmed ? "" : "lasso")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Polygon"
                                        iconName: "polygon-tool"
                                        tooltipText: "Polygon"
                                        accent: "#7ec97a"
                                        tone: active ? "primary" : "secondary"
                                        active: polygonDrawArmed
                                        onClicked: {
                                            if (!polygonDrawArmed && editorPreview.activeMaskKind !== "polygon")
                                                editorPreview.startPolygonSelection(manualSelectionSubtractActive())
                                            selectTool(polygonDrawArmed ? "" : "polygon")
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: brushToolColumn.implicitWidth + 16
                            height: brushToolColumn.implicitHeight + 10
                            radius: theme.radius8
                            color: theme.alpha(theme.ink950, editorPreview.activeMaskKind === "brush" &&
                                               editorPreview.activeMaskRetouchMode.length === 0 ? 0.96 : 0.86)
                            border.width: 1
                            border.color: theme.alpha(maskWorkspaceAccentColor(),
                                                      editorPreview.activeMaskKind === "brush" &&
                                                      editorPreview.activeMaskRetouchMode.length === 0 ? 0.34 : 0.14)

                            Column {
                                id: brushToolColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Paint"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        compact: true
                                        text: "Add"
                                        iconName: "brush-tool"
                                        tooltipText: "Brush Add"
                                        accent: maskWorkspaceAccentColor()
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskOperation === "add" &&
                                                editorPreview.activeMaskRetouchMode.length === 0
                                        onClicked: {
                                            selectionBrushRestorePending = false
                                            clearTransientTools()
                                            editorPreview.startSelectionBrush(false)
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Erase"
                                        iconName: "brush-subtract"
                                        tooltipText: "Brush Erase"
                                        accent: "#d36a5e"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskOperation === "subtract" &&
                                                editorPreview.activeMaskRetouchMode.length === 0
                                        onClicked: {
                                            selectionBrushRestorePending = false
                                            clearTransientTools()
                                            editorPreview.startSelectionBrush(true)
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            width: retouchToolColumn.implicitWidth + 16
                            height: retouchToolColumn.implicitHeight + 10
                            radius: theme.radius8
                            color: (objectPickArmed && objectPickMode === "remove") ||
                                   (editorPreview.activeMaskToolRole === "retouch")
                                   ? theme.alpha(theme.ink950, 0.96)
                                   : theme.alpha(theme.ink950, 0.86)
                            border.width: 1
                            border.color: theme.alpha("#d98962",
                                                      ((objectPickArmed && objectPickMode === "remove") ||
                                                       (editorPreview.activeMaskToolRole === "retouch")) ? 0.34 : 0.14)

                            Column {
                                id: retouchToolColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Retouch"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        compact: true
                                        text: "Remove Obj"
                                        iconName: "erase-tool"
                                        tooltipText: "Remove Object"
                                        accent: "#d98962"
                                        tone: active ? "primary" : "secondary"
                                        active: objectPickArmed && objectPickMode === "remove" && !objectPickCreateNew
                                        enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                        onClicked: selectTool(objectPickArmed && objectPickMode === "remove" && !objectPickCreateNew ? "" : "remove")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Remove Brush"
                                        iconName: "erase-tool"
                                        tooltipText: "Remove Brush"
                                        accent: "#d98962"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskRetouchMode === "remove"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.startRemoveBrush()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Heal"
                                        iconName: "brush-tool"
                                        tooltipText: "Heal"
                                        accent: "#72cbb2"
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskRetouchMode === "heal"
                                        onClicked: {
                                            clearTransientTools()
                                            editorPreview.startHealBrush()
                                        }
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Clone"
                                        iconName: "brush-tool"
                                        tooltipText: "Clone"
                                        accent: theme.accentWarm500
                                        tone: active ? "primary" : "secondary"
                                        active: editorPreview.activeMaskKind === "brush" &&
                                                editorPreview.activeMaskRetouchMode === "clone"
                                        onClicked: {
                                            clearTransientTools()
                                            if (editorPreview.activeMaskKind === "brush" &&
                                                    editorPreview.activeMaskRetouchMode === "clone") {
                                                selectTool("clone-source")
                                            } else {
                                                editorPreview.startCloneBrush()
                                                selectTool("clone-source")
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: editorPreview.hasActiveMask || objectPickArmed || polygonDrawArmed || lassoDrawArmed || cloneSourcePickArmed
                            width: canvasActionsColumn.implicitWidth + 16
                            height: canvasActionsColumn.implicitHeight + 10
                            radius: theme.radius8
                            color: theme.alpha(theme.ink950, 0.90)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.22)

                            Column {
                                id: canvasActionsColumn
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: "Session"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }

                                Row {
                                    spacing: 4

                                    ActionButton {
                                        compact: true
                                        text: "Deselect"
                                        iconName: "close"
                                        accent: "#f4f7fa"
                                        tone: "secondary"
                                        visible: editorPreview.hasActiveMask
                                        onClicked: deselectMaskEditing()
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "Done"
                                        iconName: "close"
                                        accent: maskWorkspaceAccentColor()
                                        tone: "primary"
                                        onClicked: deselectMaskEditing()
                                    }
                                }
                            }
                        }
                    }
                    }

                    Item {
                        id: fitPreviewRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: canvasToolbar.visible ? canvasToolbar.bottom : parent.top
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 2
                        anchors.rightMargin: 2
                        anchors.topMargin: canvasToolbar.visible ? 6 : 2
                        anchors.bottomMargin: 2
                        visible: fitToView

                    Image {
                        id: editedPreviewFit
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: sideBySideCompareEnabled() ? compareFitDivider.left : parent.right
                        anchors.rightMargin: sideBySideCompareEnabled() ? 8 : 0
                        source: editorPreview.displayPreviewNativeAvailable && !wipeCompareEnabled()
                                ? ""
                                : editedDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                 !editorPreview.displayPreviewNativeAvailable
                    }

                    EditorPreviewTextureItem {
                        id: editedPreviewFitNative
                        readonly property size fittedSize: preserveAspectFitSize(
                                                               editedPreviewPixelWidth(editedPreviewFit),
                                                               editedPreviewPixelHeight(editedPreviewFit),
                                                               editedPreviewFit.width,
                                                               editedPreviewFit.height)
                        x: editedPreviewFit.x + (editedPreviewFit.width - width) / 2
                        y: editedPreviewFit.y + (editedPreviewFit.height - height) / 2
                        width: Math.max(0, fittedSize.width)
                        height: Math.max(0, fittedSize.height)
                        visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                 editorPreview.displayPreviewNativeAvailable
                        controller: editorPreview
                    }

                    Rectangle {
                        id: compareFitDivider
                        visible: sideBySideCompareEnabled() &&
                                 (editedPreviewFit.visible || editedPreviewFitNative.visible)
                        width: 1
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: theme.alpha(theme.lineStrong, 0.42)
                    }

                    Image {
                        id: referencePreviewFit
                        anchors.left: compareFitDivider.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        source: referenceDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        visible: sideBySideCompareEnabled()
                    }

                    Image {
                        id: referencePreviewFitWipe
                        anchors.fill: parent
                        source: referenceDisplaySource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        visible: wipeCompareEnabled()
                    }

                    Item {
                        id: editedPreviewFitWipeClip
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * compareWipePosition
                        clip: true
                        visible: wipeCompareEnabled()

                        Image {
                            id: editedPreviewFitWipe
                            x: 0
                            y: 0
                            width: fitPreviewRow.width
                            height: fitPreviewRow.height
                            source: editedDisplaySource
                            fillMode: Image.PreserveAspectFit
                            asynchronous: false
                            cache: false
                            visible: editedDisplaySource.length > 0
                        }
                    }

                    Rectangle {
                        id: compareFitWipeHandle
                        visible: wipeCompareEnabled() && editedPreviewFitWipe.visible
                        x: Math.round(fitPreviewRow.width * compareWipePosition) - width / 2
                        y: 0
                        width: 2
                        height: parent.height
                        color: theme.alpha(theme.accentWarm400, 0.92)

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 10
                            width: 18
                            height: 24
                            radius: 9
                            color: theme.alpha(theme.ink950, 0.82)
                            border.width: 1
                            border.color: theme.alpha(theme.accentWarm400, 0.5)
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8
                            cursorShape: Qt.SizeHorCursor
                            drag.target: null
                            onPressed: function(mouse) {
                                compareWipePosition = clamp((parent.x + mouse.x) / Math.max(1, fitPreviewRow.width), 0.05, 0.95)
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                compareWipePosition = clamp((parent.x + mouse.x) / Math.max(1, fitPreviewRow.width), 0.05, 0.95)
                            }
                        }
                    }

                    CropOverlay {
                        anchors.fill: parent
                        active: fitToView && editorPreview.cropEditing && !editorPreview.compareMode &&
                                (editedPreviewFit.visible || editedPreviewFitNative.visible)
                        imageX: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x : editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2
                        imageY: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.y : editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2
                        imageWidth: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.width) : Math.max(0, editedPreviewFit.paintedWidth)
                        imageHeight: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.height) : Math.max(0, editedPreviewFit.paintedHeight)
                        cropX: editorPreview.cropRectX
                        cropY: editorPreview.cropRectY
                        cropWidth: editorPreview.cropRectWidth
                        cropHeight: editorPreview.cropRectHeight
                        aspectRatio: editorPreview.cropAspectRatio
                        guideMode: cropGuideMode
                        onRectEdited: function(x, y, width, height) {
                            editorPreview.updateCropRect(x, y, width, height);
                        }
                    }

                    MaskCanvasOverlay {
                        anchors.fill: parent
                        visible: fitToView && !editorPreview.compareMode && !editorPreview.cropEditing &&
                                 (editedPreviewFit.visible || editedPreviewFitNative.visible)
                        editRoot: root
                        imageX: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x : editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2
                        imageY: editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.y : editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2
                        imageWidth: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.width) : Math.max(0, editedPreviewFit.paintedWidth)
                        imageHeight: editorPreview.displayPreviewNativeAvailable ? Math.max(0, editedPreviewFitNative.height) : Math.max(0, editedPreviewFit.paintedHeight)
                    }
                }

                MouseArea {
                    anchors.fill: fitPreviewRow
                    visible: fitToView && editorPreview.whiteBalanceSampling && !editorPreview.compareMode &&
                             !editorPreview.cropEditing &&
                             (editedPreviewFit.visible || editedPreviewFitNative.visible)
                    cursorShape: Qt.CrossCursor
                    onClicked: function (mouse) {
                        const imageX = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.x : editedPreviewFit.x + (editedPreviewFit.width - editedPreviewFit.paintedWidth) / 2;
                        const imageY = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.y : editedPreviewFit.y + (editedPreviewFit.height - editedPreviewFit.paintedHeight) / 2;
                        const imageWidth = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.width : editedPreviewFit.paintedWidth;
                        const imageHeight = editorPreview.displayPreviewNativeAvailable ? editedPreviewFitNative.height : editedPreviewFit.paintedHeight;

                        if (mouse.x < imageX || mouse.x > imageX + imageWidth || mouse.y < imageY || mouse.y > imageY + imageHeight) {
                            editorPreview.whiteBalanceSampling = false;
                            return;
                        }

                        editorPreview.sampleWhiteBalanceAt((mouse.x - imageX) / imageWidth, (mouse.y - imageY) / imageHeight);
                    }
                }

                Flickable {
                    id: zoomPreviewFlick
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: canvasToolbar.visible ? canvasToolbar.bottom : parent.top
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 2
                    anchors.rightMargin: 2
                    anchors.topMargin: canvasToolbar.visible ? 6 : 2
                    anchors.bottomMargin: 2
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    visible: !fitToView
                    contentWidth: zoomPreviewContent.width
                    contentHeight: zoomPreviewContent.height
                    onContentWidthChanged: clampPanPosition()
                    onContentHeightChanged: clampPanPosition()
                    onContentXChanged: updatePreviewViewport()
                    onContentYChanged: updatePreviewViewport()
                    onWidthChanged: updatePreviewViewport()
                    onHeightChanged: updatePreviewViewport()

                    Item {
                        id: zoomPreviewContent
                        width: sideBySideCompareEnabled()
                               ? Math.max(zoomPreviewFlick.width, editedPreviewZoom.width + referencePreviewZoom.width + 16)
                               : Math.max(zoomPreviewFlick.width, editedPreviewZoom.width)
                        height: Math.max(zoomPreviewFlick.height, Math.max(editedPreviewZoom.height, referencePreviewZoom.height))

                        Image {
                            id: editedPreviewZoom
                            x: sideBySideCompareEnabled()
                               ? Math.max(0, (zoomPreviewContent.width - (editedPreviewZoom.width + referencePreviewZoom.width + 16)) / 2)
                               : Math.max(0, (zoomPreviewContent.width - editedPreviewZoom.width) / 2)
                            y: Math.max(0, (zoomPreviewContent.height - editedPreviewZoom.height) / 2)
                            width: editedPreviewPixelWidth(editedPreviewZoom) * manualZoom
                            height: editedPreviewPixelHeight(editedPreviewZoom) * manualZoom
                            source: editorPreview.displayPreviewNativeAvailable && !wipeCompareEnabled()
                                    ? ""
                                    : editedDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                     !editorPreview.displayPreviewNativeAvailable
                        }

                        EditorPreviewTextureItem {
                            id: editedPreviewZoomNative
                            x: editedPreviewZoom.x
                            y: editedPreviewZoom.y
                            width: editedPreviewZoom.width
                            height: editedPreviewZoom.height
                            visible: editedDisplaySource.length > 0 && !wipeCompareEnabled() &&
                                     editorPreview.displayPreviewNativeAvailable
                            controller: editorPreview
                        }

                        Image {
                            readonly property var overlayRect: editorPreview.displayPreviewOverlayRect
                            readonly property bool overlayRectValid: overlayRect !== undefined && overlayRect !== null
                            x: editedPreviewZoom.x + (overlayRectValid ? overlayRect.x : 0) * editedPreviewZoom.width
                            y: editedPreviewZoom.y + (overlayRectValid ? overlayRect.y : 0) * editedPreviewZoom.height
                            width: (overlayRectValid ? overlayRect.width : 0) * editedPreviewZoom.width
                            height: (overlayRectValid ? overlayRect.height : 0) * editedPreviewZoom.height
                            source: editorPreview.displayPreviewOverlaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: !fitToView && (editedPreviewZoom.visible || editedPreviewZoomNative.visible) &&
                                     overlayRectValid &&
                                     editorPreview.displayPreviewOverlaySource.length > 0 &&
                                     width > 1 && height > 1
                        }

                        Image {
                            id: referencePreviewZoom
                            x: sideBySideCompareEnabled() ? editedPreviewZoom.x + editedPreviewZoom.width + 16 : editedPreviewZoom.x
                            y: Math.max(0, (zoomPreviewContent.height - referencePreviewZoom.height) / 2)
                            width: imagePixelWidth(referencePreviewZoom) * manualZoom
                            height: imagePixelHeight(referencePreviewZoom) * manualZoom
                            source: referenceDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: sideBySideCompareEnabled()
                        }

                        Image {
                            id: referencePreviewZoomWipe
                            x: Math.max(0, (zoomPreviewContent.width - referencePreviewZoomWipe.width) / 2)
                            y: Math.max(0, (zoomPreviewContent.height - referencePreviewZoomWipe.height) / 2)
                            width: imagePixelWidth(referencePreviewZoom) * manualZoom
                            height: imagePixelHeight(referencePreviewZoom) * manualZoom
                            source: referenceDisplaySource
                            fillMode: Image.Stretch
                            asynchronous: true
                            cache: false
                            visible: wipeCompareEnabled()
                        }

                        Item {
                            id: editedPreviewZoomWipeClip
                            x: referencePreviewZoomWipe.x
                            y: referencePreviewZoomWipe.y
                            width: referencePreviewZoomWipe.width * compareWipePosition
                            height: referencePreviewZoomWipe.height
                            clip: true
                            visible: wipeCompareEnabled() && editedDisplaySource.length > 0

                            Image {
                                id: editedPreviewZoomWipe
                                x: 0
                                y: 0
                                width: referencePreviewZoomWipe.width
                                height: referencePreviewZoomWipe.height
                                source: editedDisplaySource
                                fillMode: Image.Stretch
                                asynchronous: true
                                cache: false
                            }
                        }

                        Rectangle {
                            id: compareZoomWipeHandle
                            visible: wipeCompareEnabled() && referencePreviewZoomWipe.visible
                            x: editedPreviewZoomWipeClip.x + editedPreviewZoomWipeClip.width - width / 2
                            y: editedPreviewZoomWipeClip.y
                            width: 2
                            height: editedPreviewZoomWipeClip.height
                            color: theme.alpha(theme.accentWarm400, 0.92)

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -8
                                cursorShape: Qt.SizeHorCursor
                                onPressed: function(mouse) {
                                    compareWipePosition = clamp((parent.x + mouse.x - referencePreviewZoomWipe.x) / Math.max(1, referencePreviewZoomWipe.width), 0.05, 0.95)
                                }
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return
                                    compareWipePosition = clamp((parent.x + mouse.x - referencePreviewZoomWipe.x) / Math.max(1, referencePreviewZoomWipe.width), 0.05, 0.95)
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: editorPreview.whiteBalanceSampling && !editorPreview.compareMode &&
                                     !editorPreview.cropEditing &&
                                     (editedPreviewZoom.visible || editedPreviewZoomNative.visible)
                            cursorShape: Qt.CrossCursor
                            onClicked: function (mouse) {
                                if (mouse.x < editedPreviewZoom.x || mouse.x > editedPreviewZoom.x + editedPreviewZoom.width || mouse.y < editedPreviewZoom.y || mouse.y > editedPreviewZoom.y + editedPreviewZoom.height) {
                                    editorPreview.whiteBalanceSampling = false;
                                    return;
                                }

                                editorPreview.sampleWhiteBalanceAt((mouse.x - editedPreviewZoom.x) / editedPreviewZoom.width, (mouse.y - editedPreviewZoom.y) / editedPreviewZoom.height);
                            }
                        }

                        CropOverlay {
                            anchors.fill: parent
                            active: !fitToView && editorPreview.cropEditing && !editorPreview.compareMode &&
                                    (editedPreviewZoom.visible || editedPreviewZoomNative.visible)
                            imageX: editedPreviewZoom.x
                            imageY: editedPreviewZoom.y
                            imageWidth: editedPreviewZoom.width
                            imageHeight: editedPreviewZoom.height
                            cropX: editorPreview.cropRectX
                            cropY: editorPreview.cropRectY
                            cropWidth: editorPreview.cropRectWidth
                            cropHeight: editorPreview.cropRectHeight
                            aspectRatio: editorPreview.cropAspectRatio
                            guideMode: cropGuideMode
                            onRectEdited: function(x, y, width, height) {
                                editorPreview.updateCropRect(x, y, width, height);
                            }
                        }

                        MaskCanvasOverlay {
                            anchors.fill: parent
                            visible: !fitToView && !editorPreview.compareMode && !editorPreview.cropEditing &&
                                     (editedPreviewZoom.visible || editedPreviewZoomNative.visible)
                            editRoot: root
                            imageX: editedPreviewZoom.x
                            imageY: editedPreviewZoom.y
                            imageWidth: editedPreviewZoom.width
                            imageHeight: editedPreviewZoom.height
                        }
                    }
                }

                WheelHandler {
                    target: null
                    enabled: editorPreview.hasAsset
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function (event) {
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y;
                        if (delta === 0)
                            return;

                        const zoomFactor = delta > 0 ? 1.05 : (1 / 1.05);
                        const pivot = event.position || Qt.point(previewSurface.width / 2, previewSurface.height / 2);
                        setZoomLevel(currentZoomLevel() * zoomFactor, pivot.x, pivot.y);
                        event.accepted = true;
                    }
                }

                MouseArea {
                    anchors.fill: zoomPreviewFlick
                    visible: !fitToView && !editorPreview.whiteBalanceSampling && !editorPreview.cropEditing
                             && (spacePanActive ||
                                 (!(inspectorMode === "masks" && editorPreview.hasActiveMask && editorPreview.activeMaskKind === "brush")
                                  && !objectPickArmed && !polygonDrawArmed && !lassoDrawArmed && !cloneSourcePickArmed))
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    cursorShape: maxPanX() > 0 || maxPanY() > 0 || spacePanActive
                                 ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                                 : Qt.ArrowCursor

                    property real lastX: 0
                    property real lastY: 0

                    onPressed: function (mouse) {
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }

                    onPositionChanged: function (mouse) {
                        if (!pressed)
                            return;

                        zoomPreviewFlick.contentX -= mouse.x - lastX;
                        zoomPreviewFlick.contentY -= mouse.y - lastY;
                        clampPanPosition();
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }
                }

                MouseArea {
                    anchors.fill: zoomPreviewFlick
                    visible: !fitToView && !editorPreview.whiteBalanceSampling && !editorPreview.cropEditing
                    acceptedButtons: Qt.MiddleButton | Qt.RightButton
                    preventStealing: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    z: 220

                    property real lastX: 0
                    property real lastY: 0

                    onPressed: function (mouse) {
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }

                    onPositionChanged: function (mouse) {
                        if (!pressed)
                            return;

                        zoomPreviewFlick.contentX -= mouse.x - lastX;
                        zoomPreviewFlick.contentY -= mouse.y - lastY;
                        clampPanPosition();
                        lastX = mouse.x;
                        lastY = mouse.y;
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: theme.radius8
                    visible: editorPreview.displayPreviewSource.length === 0
                    color: theme.ink800

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "Preview unavailable for this asset" : "Select an image in Library"
                            color: "#f2f4f6"
                            font.family: theme.fontUi
                            font.pixelSize: 18
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: catalogAssets.selectedAssetId.length > 0 ? "The source preview could not be decoded for this asset." : "The editor is selection-driven. Pick an image in Library to load the preview and recipe."
                            color: "#8f98a2"
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.preferredWidth: 420
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                radius: theme.radius8
                color: theme.alpha(theme.ink850, 0.9)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.2)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: previewStatusText()
                            visible: text.length > 0
                            color: inspectorMode === "masks" ? theme.alpha(maskWorkspaceAccentColor(), 0.95) : theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            radius: theme.radiusPanel
                            color: theme.alpha(theme.ink900, 0.72)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.14)
                            implicitWidth: zoomModeRow.implicitWidth + 6
                            implicitHeight: 30

                            RowLayout {
                                id: zoomModeRow
                                anchors.fill: parent
                                anchors.margins: 3
                                spacing: 3

                                ActionButton {
                                    text: "Fit"
                                    iconName: "crop"
                                    compact: true
                                    active: fitToView
                                    showText: false
                                    tooltipText: "Fit preview to canvas"
                                    onClicked: {
                                        fitToView = true;
                                        zoomPreviewFlick.contentX = 0;
                                        zoomPreviewFlick.contentY = 0;
                                    }
                                }

                                ActionButton {
                                    text: "1:1"
                                    compact: true
                                    active: !fitToView && Math.abs(manualZoom - 1.0) < 0.01
                                    iconText: "1:1"
                                    showText: false
                                    tooltipText: "View at 1:1"
                                    onClicked: {
                                        setZoomLevel(1.0);
                                        centerZoomPreview();
                                    }
                                }

                                ActionButton {
                                    text: "Compare"
                                    iconName: "compare"
                                    compact: true
                                    active: editorPreview.compareMode
                                    enabled: editorPreview.hasAsset
                                    showText: false
                                    tooltipText: "Toggle compare"
                                    onClicked: editorPreview.compareMode = !editorPreview.compareMode
                                }

                                ActionButton {
                                    text: "Split"
                                    iconName: "split-view"
                                    compact: true
                                    visible: editorPreview.compareMode
                                    active: compareViewMode === "side-by-side"
                                    accent: theme.accentCool500
                                    showText: false
                                    tooltipText: "Split compare"
                                    onClicked: compareViewMode = "side-by-side"
                                }

                                ActionButton {
                                    text: "Wipe"
                                    iconName: "wipe"
                                    compact: true
                                    visible: editorPreview.compareMode
                                    active: compareViewMode === "wipe"
                                    accent: theme.accentCool500
                                    showText: false
                                    tooltipText: "Wipe compare"
                                    onClicked: compareViewMode = "wipe"
                                }
                            }
                        }

                        Label {
                            text: (fitToView ? "Fit" : (Math.round(manualZoom * 100) + "%")) +
                                  (editorPreview.compareMode
                                   ? (compareViewMode === "wipe" ? " · Wipe" : " · Split")
                                   : "")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }
                    }

                    Item {
                        id: filmstripHolder
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        readonly property int thumbWidth: 70
                        readonly property int thumbHeight: 56
                        readonly property int thumbOuterWidth: 74

                        ListView {
                            id: filmstripView
                            width: filmstripHolder.width
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            clip: true
                            orientation: ListView.Horizontal
                            boundsBehavior: Flickable.StopAtBounds
                            spacing: 8
                            model: catalogAssets
                            currentIndex: catalogAssets.selectedIndex

                            delegate: Rectangle {
                                required property int index
                                required property string assetId
                                required property string thumbnailSource

                                readonly property bool isCurrent: catalogAssets.selectedIndex === index
                                readonly property bool isInSelection: catalogAssets.assetSelected(assetId)

                                width: filmstripHolder.thumbOuterWidth
                                height: filmstripView.height
                                radius: theme.radiusChip
                                color: isCurrent
                                       ? theme.alpha(theme.ink700, 0.9)
                                       : (isInSelection ? theme.alpha(theme.ink750, 0.62) : theme.ink900)
                                border.width: isInSelection ? (isCurrent ? 2 : 1) : 0
                                border.color: isCurrent ? theme.alpha(theme.accentWarm400, 0.95) : theme.alpha(theme.accentCool500, 0.56)

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    radius: parent.radius - 1
                                    visible: isCurrent
                                    color: theme.alpha(theme.accentWarm500, 0.1)
                                }

                                Rectangle {
                                    width: filmstripHolder.thumbWidth
                                    height: filmstripHolder.thumbHeight
                                    anchors.centerIn: parent
                                    radius: theme.radius4
                                    color: theme.ink950
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        source: thumbnailSource
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: false
                                        visible: thumbnailSource.length > 0
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        color: theme.ink800
                                        visible: thumbnailSource.length === 0
                                    }
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    anchors.bottomMargin: 2
                                    height: 2
                                    radius: 1
                                    visible: isCurrent
                                    color: theme.accentWarm400
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: function(mouse) {
                                        if (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier))
                                            catalogAssets.toggleSelectionIndex(index)
                                        else
                                            catalogAssets.selectIndex(index)
                                    }
                                }
                            }

                            footer: Item {
                                width: 10
                                height: filmstripView.height
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: catalogAssets.assetCount === 0
                            text: "Filmstrip follows your library selection"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        Item {
            Layout.preferredWidth: inspectorCollapsed ? 24 : 324
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                visible: inspectorCollapsed
                radius: theme.radius8
                color: theme.ink850

                Label {
                    anchors.centerIn: parent
                    text: "<"
                    color: "#d5dbe1"
                    font.family: "Menlo"
                    font.pixelSize: 12
                    rotation: 180
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: inspectorCollapsed = false
                }
            }

            BrandPanel {
                anchors.fill: parent
                visible: !inspectorCollapsed
                headerVisible: false
                bodyMargin: 4
                bodySpacing: 4
                title: "Inspector"

                Rectangle {
                    implicitHeight: inspectorSummary.implicitHeight + 16
                    Layout.fillWidth: true
                    radius: 8
                    color: theme.alpha(theme.ink800, 0.82)

                    ColumnLayout {
                        id: inspectorSummary
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Rectangle {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 7
                                color: theme.ink850

                                Label {
                                    anchors.centerIn: parent
                                    text: "<"
                                    color: "#d5dbe1"
                                    font.family: "Menlo"
                                    font.pixelSize: 12
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: inspectorCollapsed = true
                                }
                            }

                            ActionButton {
                                text: "Adjust"
                                iconName: "adjust"
                                showText: false
                                tooltipText: "Adjust"
                                compact: true
                                active: inspectorMode === "adjust"
                                implicitHeight: 28
                                onClicked: inspectorMode = "adjust"
                            }

                            ActionButton {
                                text: "Masks"
                                iconName: "masks"
                                showText: false
                                tooltipText: "Masks"
                                compact: true
                                active: inspectorMode === "masks"
                                implicitHeight: 28
                                onClicked: inspectorMode = "masks"
                            }

                            ActionButton {
                                text: "Crop"
                                iconName: "crop"
                                showText: false
                                tooltipText: "Crop"
                                compact: true
                                active: inspectorMode === "crop"
                                implicitHeight: 28
                                onClicked: inspectorMode = "crop"
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ActionButton {
                                text: "Reset"
                                compact: true
                                enabled: editorPreview.hasAsset
                                font.pixelSize: 10
                                implicitHeight: 28
                                onClicked: editorPreview.resetAdjustments()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            ActionButton {
                                text: editorPreview.compareMode ? "Compare On" : "Compare"
                                iconName: "compare"
                                active: editorPreview.compareMode
                                compact: true
                                enabled: editorPreview.hasAsset
                                showText: false
                                tooltipText: editorPreview.compareMode ? "Disable compare" : "Enable compare"
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: editorPreview.compareMode = !editorPreview.compareMode
                            }

                            ActionButton {
                                text: compareViewMode === "wipe" ? "Wipe" : "Split"
                                iconName: compareViewMode === "wipe" ? "wipe" : "split-view"
                                compact: true
                                visible: editorPreview.compareMode
                                showText: false
                                tooltipText: compareViewMode === "wipe" ? "Switch to split compare" : "Switch to wipe compare"
                                font.pixelSize: 10
                                implicitHeight: 24
                                onClicked: compareViewMode = compareViewMode === "wipe" ? "side-by-side" : "wipe"
                            }

                            Label {
                                text: "Output"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 9
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 108
                                model: ["sRGB", "Display P3", "Adobe RGB", "ProPhoto RGB", "Rec.709", "Rec.2020"]
                                currentIndex: Math.max(0, model.indexOf(editorPreview.outputColorSpace))
                                onActivated: editorPreview.outputColorSpace = currentText
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: (catalogAssets.selectedRecipeJson.length > 0 ? "Edited" : "Base")
                                      + (editorPreview.maskCount > 0 ? " · Masks" : "")
                                      + " · " + editorPreview.outputColorSpace
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 9
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: inspectorMode === "adjust"
                            radius: 7
                            color: theme.alpha(theme.ink900, 0.72)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.28)
                            implicitHeight: batchToolsColumn.implicitHeight + 14

                            ColumnLayout {
                                id: batchToolsColumn
                                anchors.fill: parent
                                anchors.margins: 7
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    ActionButton {
                                        Layout.fillWidth: true
                                        text: "Copy"
                                        compact: true
                                        enabled: editorPreview.hasAsset
                                        onClicked: copyCurrentAdjustments()
                                    }

                                    ActionButton {
                                        Layout.fillWidth: true
                                        text: "Paste"
                                        compact: true
                                        enabled: editorPreview.hasAsset && editorPreview.hasCopiedSettings
                                        onClicked: pasteCopiedAdjustments()
                                    }

                                    ActionButton {
                                        Layout.fillWidth: true
                                        text: "Sync"
                                        compact: true
                                        enabled: editorPreview.hasAsset && catalogAssets.selectedAssetCount > 0
                                        onClicked: syncCurrentAdjustments()
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 1
                                        Layout.preferredHeight: 20
                                        color: theme.alpha(theme.lineSoft, 0.42)
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 24
                                        radius: 6
                                        color: batchToolsExpanded ? theme.alpha(theme.accentWarm500, 0.14) : "transparent"
                                        border.width: 1
                                        border.color: batchToolsExpanded ? theme.alpha(theme.accentWarm500, 0.3) : theme.alpha(theme.lineSoft, 0.42)

                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "chevron-right"
                                            rotation: batchToolsExpanded ? 90 : 0
                                            color: theme.textFaint
                                            implicitWidth: 9
                                            implicitHeight: 9
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: batchToolsExpanded = !batchToolsExpanded
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: batchToolsExpanded
                                    spacing: 6

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: catalogAssets.selectedAssetCount > 1
                                                  ? catalogAssets.selectedAssetCount + " selected"
                                                  : "Single photo"
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                        }

                                        Item { Layout.fillWidth: true }

                                        ActionButton {
                                            text: "Clear"
                                            compact: true
                                            enabled: catalogAssets.selectedAssetCount > 1
                                            onClicked: catalogAssets.clearSecondarySelection()
                                        }
                                    }

                                    Label {
                                        text: "Copy/paste groups"
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 4

                                        Repeater {
                                            model: adjustmentGroups

                                            delegate: ActionButton {
                                                required property var modelData

                                                text: modelData.label
                                                compact: true
                                                active: hasAdjustmentGroup(modelData.key)
                                                onClicked: toggleAdjustmentGroup(modelData.key)
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: theme.alpha(theme.lineSoft, 0.28)
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        ToneComboBox {
                                            Layout.fillWidth: true
                                            model: editorPreview.presetNames
                                            currentIndex: selectedPresetName.length > 0
                                                          ? editorPreview.presetNames.indexOf(selectedPresetName)
                                                          : (editorPreview.presetNames.length > 0 ? 0 : -1)
                                            onActivated: {
                                                selectedPresetName = currentText
                                                presetDraftName = currentText
                                            }
                                        }

                                        ActionButton {
                                            text: "Apply"
                                            compact: true
                                            tone: "primary"
                                            enabled: selectedPresetName.length > 0 && editorPreview.presetRecipeJson(selectedPresetName).length > 0
                                            onClicked: applyPresetToSelection(selectedPresetName)
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        ToneTextField {
                                            Layout.fillWidth: true
                                            placeholderText: "Preset name"
                                            text: presetDraftName
                                            onTextEdited: presetDraftName = text
                                            onAccepted: saveCurrentPreset()
                                        }

                                        ActionButton {
                                            text: "Save"
                                            compact: true
                                            tone: "primary"
                                            enabled: editorPreview.hasAsset && presetDraftName.trim().length > 0
                                            onClicked: saveCurrentPreset()
                                        }

                                        ConfirmActionButton {
                                            text: "Delete"
                                            compact: true
                                            enabled: selectedPresetName.length > 0 && editorPreview.presetRecipeJson(selectedPresetName).length > 0
                                            helperText: "Delete this saved preset"
                                            onConfirmed: {
                                                if (editorPreview.deletePreset(selectedPresetName)) {
                                                    selectedPresetName = ""
                                                    presetDraftName = ""
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    id: inspectorScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    Column {
                        width: inspectorScroll.availableWidth
                        spacing: 10

                        Rectangle {
                            width: parent.width
                            radius: theme.radius8
                            color: inspectorMode === "masks"
                                   ? theme.alpha(theme.ink900, 0.56)
                                   : theme.alpha(theme.ink900, 0.72)
                            border.width: 1
                            border.color: inspectorMode === "masks"
                                          ? theme.alpha(theme.lineSoft, 0.16)
                                          : theme.alpha(theme.lineSoft, 0.24)
                            implicitHeight: histogramColumn.implicitHeight + (inspectorMode === "masks" ? 10 : 14)

                            ColumnLayout {
                                id: histogramColumn
                                anchors.fill: parent
                                anchors.margins: 7
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        text: "Histogram"
                                        color: theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }

                                    Item { Layout.fillWidth: true }

                                    ActionButton {
                                        text: "RGB"
                                        compact: true
                                        active: histogramShowRgb
                                        onClicked: histogramShowRgb = !histogramShowRgb
                                    }

                                    ActionButton {
                                        text: "Luma"
                                        compact: true
                                        active: histogramShowLuminance
                                        onClicked: histogramShowLuminance = !histogramShowLuminance
                                    }

                                    ActionButton {
                                        text: histogramLogScale ? "Log" : "Lin"
                                        compact: true
                                        active: histogramLogScale
                                        onClicked: histogramLogScale = !histogramLogScale
                                    }
                                }

                                HistogramDisplay {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: inspectorMode === "masks" ? 74 : 88
                                    redData: editorPreview.histogramRed
                                    greenData: editorPreview.histogramGreen
                                    blueData: editorPreview.histogramBlue
                                    luminanceData: editorPreview.histogramLuminance
                                    showRgb: histogramShowRgb
                                    showLuminance: histogramShowLuminance
                                    logScale: histogramLogScale
                                    shadowClipRatio: editorPreview.histogramSampleCount > 0
                                                     ? editorPreview.histogramShadowClipCount / editorPreview.histogramSampleCount
                                                     : 0
                                    highlightClipRatio: editorPreview.histogramSampleCount > 0
                                                        ? editorPreview.histogramHighlightClipCount / editorPreview.histogramSampleCount
                                                        : 0
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label {
                                        text: "Blk " + editorPreview.histogramShadowClipCount
                                        color: editorPreview.histogramShadowClipCount > 0 ? theme.accentWarm400 : theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }

                                    Label {
                                        text: "Wht " + editorPreview.histogramHighlightClipCount
                                        color: editorPreview.histogramHighlightClipCount > 0 ? "#f2bc79" : theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }

                                    Item { Layout.fillWidth: true }

                                    Label {
                                        text: editorPreview.histogramSampleCount > 0
                                              ? Math.round((editorPreview.histogramShadowClipCount / editorPreview.histogramSampleCount) * 100) + "% / " +
                                                Math.round((editorPreview.histogramHighlightClipCount / editorPreview.histogramSampleCount) * 100) + "%"
                                              : "0% / 0%"
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 9
                                    }
                                }
                            }
                        }

                        MaskInspectorSections {
                            width: parent.width
                            visible: inspectorMode === "masks"
                            editRoot: root
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust" && editorPreview.rawControlsAvailable
                            title: "RAW Decode"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Exposure"
                                valueText: editorPreview.rawExposure.toFixed(2) + " EV"
                                from: -4
                                to: 4
                                value: editorPreview.rawExposure
                                onValueDragged: function(value) { editorPreview.rawExposure = value }
                                onResetRequested: editorPreview.rawExposure = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Boost"
                                valueText: editorPreview.rawBoost.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawBoost
                                onValueDragged: function(value) { editorPreview.rawBoost = value }
                                onResetRequested: editorPreview.rawBoost = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadow Boost"
                                valueText: editorPreview.rawShadowBoost.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawShadowBoost
                                onValueDragged: function(value) { editorPreview.rawShadowBoost = value }
                                onResetRequested: editorPreview.rawShadowBoost = 1
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "RAW Detail"
                                valueText: editorPreview.rawDetail.toFixed(2)
                                from: 0
                                to: 2
                                value: editorPreview.rawDetail
                                onValueDragged: function(value) { editorPreview.rawDetail = value }
                                onResetRequested: editorPreview.rawDetail = 0
                            }

                            CheckBox {
                                text: "Highlight Recovery"
                                checked: editorPreview.rawHighlightRecovery
                                onToggled: editorPreview.rawHighlightRecovery = checked
                            }

                            CheckBox {
                                text: "Gamut Mapping"
                                checked: editorPreview.rawGamutMapping
                                onToggled: editorPreview.rawGamutMapping = checked
                            }

                            CheckBox {
                                text: "Lens Correction"
                                checked: editorPreview.rawLensCorrection
                                onToggled: editorPreview.rawLensCorrection = checked
                            }

                            CheckBox {
                                text: "Override White Balance"
                                checked: editorPreview.rawWhiteBalanceOverride
                                onToggled: editorPreview.rawWhiteBalanceOverride = checked
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                enabled: editorPreview.rawWhiteBalanceOverride
                                label: "RAW Temp"
                                valueText: Math.round(editorPreview.rawWhiteBalanceTemperature).toString() + " K"
                                from: 2000
                                to: 15000
                                value: editorPreview.rawWhiteBalanceTemperature
                                onValueDragged: function(value) { editorPreview.rawWhiteBalanceTemperature = value }
                                onResetRequested: editorPreview.rawWhiteBalanceTemperature = 5200
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                enabled: editorPreview.rawWhiteBalanceOverride
                                label: "RAW Tint"
                                valueText: Math.round(editorPreview.rawWhiteBalanceTint).toString()
                                trackColors: ["#4ea56b", "#c8d0d8", "#bb67d6"]
                                from: -150
                                to: 150
                                value: editorPreview.rawWhiteBalanceTint
                                onValueDragged: function(value) { editorPreview.rawWhiteBalanceTint = value }
                                onResetRequested: editorPreview.rawWhiteBalanceTint = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Light"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Exposure"
                                valueText: editorPreview.exposure.toFixed(2) + " EV"
                                from: -2
                                to: 2
                                value: editorPreview.exposure
                                onValueDragged: function(value) { editorPreview.exposure = value }
                                onResetRequested: editorPreview.exposure = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Contrast"
                                valueText: Math.round(editorPreview.contrast).toString()
                                from: -100
                                to: 100
                                value: editorPreview.contrast
                                onValueDragged: function(value) { editorPreview.contrast = value }
                                onResetRequested: editorPreview.contrast = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Highlights"
                                valueText: Math.round(editorPreview.highlights).toString()
                                from: -100
                                to: 100
                                value: editorPreview.highlights
                                onValueDragged: function(value) { editorPreview.highlights = value }
                                onResetRequested: editorPreview.highlights = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadows"
                                valueText: Math.round(editorPreview.shadows).toString()
                                from: -100
                                to: 100
                                value: editorPreview.shadows
                                onValueDragged: function(value) { editorPreview.shadows = value }
                                onResetRequested: editorPreview.shadows = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Whites"
                                valueText: Math.round(editorPreview.whites).toString()
                                from: -100
                                to: 100
                                value: editorPreview.whites
                                onValueDragged: function(value) { editorPreview.whites = value }
                                onResetRequested: editorPreview.whites = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Blacks"
                                valueText: Math.round(editorPreview.blacks).toString()
                                from: -100
                                to: 100
                                value: editorPreview.blacks
                                onValueDragged: function(value) { editorPreview.blacks = value }
                                onResetRequested: editorPreview.blacks = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Tone Curve"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: true

                            ToneCurveEditor {
                                Layout.fillWidth: true
                                accent: editorPreview.curveChannel === "Red"
                                        ? "#d45a62"
                                        : (editorPreview.curveChannel === "Green"
                                           ? "#61b56f"
                                           : (editorPreview.curveChannel === "Blue"
                                              ? "#4e86d9"
                                              : "#c99a52"))
                                selectedChannel: editorPreview.curveChannel
                                pointsModel: editorPreview.activeCurvePoints
                                onChannelSelected: editorPreview.curveChannel = channel
                                onPointsEdited: editorPreview.activeCurvePoints = points
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Color"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    compact: true
                                    iconName: "eyedropper"
                                    showText: false
                                    tooltipText: editorPreview.whiteBalanceSampling ? "Cancel white balance picker" : "Sample white balance from image"
                                    active: editorPreview.whiteBalanceSampling
                                    enabled: editorPreview.hasAsset && !editorPreview.compareMode
                                    onClicked: editorPreview.whiteBalanceSampling = !editorPreview.whiteBalanceSampling
                                }
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Temperature"
                                valueText: Math.round(editorPreview.temperature).toString()
                                trackColors: ["#4c78cb", "#c8d0d8", "#e6a14f"]
                                from: -100
                                to: 100
                                value: editorPreview.temperature
                                onValueDragged: function(value) { editorPreview.temperature = value }
                                onResetRequested: editorPreview.temperature = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Tint"
                                valueText: Math.round(editorPreview.tint).toString()
                                trackColors: ["#4ea56b", "#c8d0d8", "#bb67d6"]
                                from: -100
                                to: 100
                                value: editorPreview.tint
                                onValueDragged: function(value) { editorPreview.tint = value }
                                onResetRequested: editorPreview.tint = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Vibrance"
                                valueText: Math.round(editorPreview.vibrance).toString()
                                from: -100
                                to: 100
                                value: editorPreview.vibrance
                                onValueDragged: function(value) { editorPreview.vibrance = value }
                                onResetRequested: editorPreview.vibrance = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.saturation).toString()
                                from: -100
                                to: 100
                                value: editorPreview.saturation
                                onValueDragged: function(value) { editorPreview.saturation = value }
                                onResetRequested: editorPreview.saturation = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Presence"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: true

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Texture"
                                valueText: Math.round(editorPreview.texture).toString()
                                from: -100
                                to: 100
                                value: editorPreview.texture
                                onValueDragged: function(value) { editorPreview.texture = value }
                                onResetRequested: editorPreview.texture = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Clarity"
                                valueText: Math.round(editorPreview.clarity).toString()
                                from: -100
                                to: 100
                                value: editorPreview.clarity
                                onValueDragged: function(value) { editorPreview.clarity = value }
                                onResetRequested: editorPreview.clarity = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Dehaze"
                                valueText: Math.round(editorPreview.dehaze).toString()
                                from: -100
                                to: 100
                                value: editorPreview.dehaze
                                onValueDragged: function(value) { editorPreview.dehaze = value }
                                onResetRequested: editorPreview.dehaze = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Detail"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: false

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Sharpening"
                                valueText: Math.round(editorPreview.sharpening).toString()
                                from: 0
                                to: 100
                                value: editorPreview.sharpening
                                onValueDragged: function(value) { editorPreview.sharpening = value }
                                onResetRequested: editorPreview.sharpening = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Masking"
                                valueText: Math.round(editorPreview.sharpenMasking).toString()
                                from: 0
                                to: 100
                                value: editorPreview.sharpenMasking
                                onValueDragged: function(value) { editorPreview.sharpenMasking = value }
                                onResetRequested: editorPreview.sharpenMasking = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luma NR"
                                valueText: Math.round(editorPreview.luminanceNr).toString()
                                from: 0
                                to: 100
                                value: editorPreview.luminanceNr
                                onValueDragged: function(value) { editorPreview.luminanceNr = value }
                                onResetRequested: editorPreview.luminanceNr = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Color NR"
                                valueText: Math.round(editorPreview.colorNr).toString()
                                from: 0
                                to: 100
                                value: editorPreview.colorNr
                                onValueDragged: function(value) { editorPreview.colorNr = value }
                                onResetRequested: editorPreview.colorNr = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Optics"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: false

                            Label {
                                visible: editorPreview.lensModel.length > 0
                                text: "Lens: " + editorPreview.lensModel
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Distortion"
                                valueText: Math.round(editorPreview.lensDistortion).toString()
                                trackColors: ["#5d6a77", "#c7d0d7", "#d0a157"]
                                from: -100
                                to: 100
                                value: editorPreview.lensDistortion
                                onValueDragged: function(value) { editorPreview.lensDistortion = value }
                                onResetRequested: editorPreview.lensDistortion = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Edge Light"
                                valueText: Math.round(editorPreview.lensVignette).toString()
                                trackColors: ["#13181d", "#7c8792", "#f1f5f8"]
                                from: -100
                                to: 100
                                value: editorPreview.lensVignette
                                onValueDragged: function(value) { editorPreview.lensVignette = value }
                                onResetRequested: editorPreview.lensVignette = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Defringe"
                                valueText: Math.round(editorPreview.chromaticAberration).toString()
                                trackColors: ["#4d6074", "#d7dde2", "#d0a157"]
                                from: 0
                                to: 100
                                value: editorPreview.chromaticAberration
                                onValueDragged: function(value) { editorPreview.chromaticAberration = value }
                                onResetRequested: editorPreview.chromaticAberration = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Color Mixer"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: true

                            Flow {
                                Layout.fillWidth: true
                                spacing: 6

                                Repeater {
                                    model: hslChannels

                                    delegate: ColorChannelButton {
                                        required property var modelData

                                        label: modelData.name
                                        swatch: modelData.color
                                        selected: editorPreview.hslChannel === modelData.name
                                        onClicked: editorPreview.hslChannel = modelData.name
                                    }
                                }
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.hueAdjustment).toString()
                                trackColors: ["#d54f60", "#d78c38", "#d7bf43", "#61b56f", "#49b8c4", "#4e86d9", "#8b6be3", "#c85ac1"]
                                from: -100
                                to: 100
                                value: editorPreview.hueAdjustment
                                onValueDragged: function(value) { editorPreview.hueAdjustment = value }
                                onResetRequested: editorPreview.hueAdjustment = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.saturationAdjustment).toString()
                                trackColors: ["#505963", selectedHslColor(), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.saturationAdjustment
                                onValueDragged: function(value) { editorPreview.saturationAdjustment = value }
                                onResetRequested: editorPreview.saturationAdjustment = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.luminanceAdjustment).toString()
                                trackColors: ["#101419", selectedHslColor(), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.luminanceAdjustment
                                onValueDragged: function(value) { editorPreview.luminanceAdjustment = value }
                                onResetRequested: editorPreview.luminanceAdjustment = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "adjust"
                            title: "Color Grading"
                            showAccentMarker: false
                            uppercaseTitle: false
                            titleFontFamily: theme.fontDisplay
                            titlePixelSize: 16
                            titleWeight: Font.Bold
                            titleLetterSpacing: 0
                            expandable: true
                            expanded: true

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Shadows"
                                showAccentMarker: false
                                titlePixelSize: 14
                                titleWeight: Font.Bold
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.gradingShadowsHue).toString()
                                trackColors: rainbowTrack()
                                from: -100
                                to: 100
                                value: editorPreview.gradingShadowsHue
                                onValueDragged: function(value) { editorPreview.gradingShadowsHue = value }
                                onResetRequested: editorPreview.gradingShadowsHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.gradingShadowsSaturation).toString()
                                trackColors: ["#505963", gradingPreviewColor(editorPreview.gradingShadowsHue, 100, editorPreview.gradingShadowsLuminance), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingShadowsSaturation
                                onValueDragged: function(value) { editorPreview.gradingShadowsSaturation = value }
                                onResetRequested: editorPreview.gradingShadowsSaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.gradingShadowsLuminance).toString()
                                trackColors: ["#101419", gradingPreviewColor(editorPreview.gradingShadowsHue, editorPreview.gradingShadowsSaturation, 0), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingShadowsLuminance
                                onValueDragged: function(value) { editorPreview.gradingShadowsLuminance = value }
                                onResetRequested: editorPreview.gradingShadowsLuminance = 0
                            }

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Midtones"
                                showAccentMarker: false
                                titlePixelSize: 14
                                titleWeight: Font.Bold
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.gradingMidtonesHue).toString()
                                trackColors: rainbowTrack()
                                from: -100
                                to: 100
                                value: editorPreview.gradingMidtonesHue
                                onValueDragged: function(value) { editorPreview.gradingMidtonesHue = value }
                                onResetRequested: editorPreview.gradingMidtonesHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.gradingMidtonesSaturation).toString()
                                trackColors: ["#505963", gradingPreviewColor(editorPreview.gradingMidtonesHue, 100, editorPreview.gradingMidtonesLuminance), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingMidtonesSaturation
                                onValueDragged: function(value) { editorPreview.gradingMidtonesSaturation = value }
                                onResetRequested: editorPreview.gradingMidtonesSaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.gradingMidtonesLuminance).toString()
                                trackColors: ["#101419", gradingPreviewColor(editorPreview.gradingMidtonesHue, editorPreview.gradingMidtonesSaturation, 0), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingMidtonesLuminance
                                onValueDragged: function(value) { editorPreview.gradingMidtonesLuminance = value }
                                onResetRequested: editorPreview.gradingMidtonesLuminance = 0
                            }

                            SectionHeader {
                                Layout.fillWidth: true
                                title: "Highlights"
                                showAccentMarker: false
                                titlePixelSize: 14
                                titleWeight: Font.Bold
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Hue"
                                valueText: Math.round(editorPreview.gradingHighlightsHue).toString()
                                trackColors: rainbowTrack()
                                from: -100
                                to: 100
                                value: editorPreview.gradingHighlightsHue
                                onValueDragged: function(value) { editorPreview.gradingHighlightsHue = value }
                                onResetRequested: editorPreview.gradingHighlightsHue = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Saturation"
                                valueText: Math.round(editorPreview.gradingHighlightsSaturation).toString()
                                trackColors: ["#505963", gradingPreviewColor(editorPreview.gradingHighlightsHue, 100, editorPreview.gradingHighlightsLuminance), "#ffffff"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingHighlightsSaturation
                                onValueDragged: function(value) { editorPreview.gradingHighlightsSaturation = value }
                                onResetRequested: editorPreview.gradingHighlightsSaturation = 0
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Luminance"
                                valueText: Math.round(editorPreview.gradingHighlightsLuminance).toString()
                                trackColors: ["#101419", gradingPreviewColor(editorPreview.gradingHighlightsHue, editorPreview.gradingHighlightsSaturation, 0), "#eff3f6"]
                                from: -100
                                to: 100
                                value: editorPreview.gradingHighlightsLuminance
                                onValueDragged: function(value) { editorPreview.gradingHighlightsLuminance = value }
                                onResetRequested: editorPreview.gradingHighlightsLuminance = 0
                            }
                        }

                        InspectorSection {
                            width: parent.width
                            visible: inspectorMode === "crop"
                            title: "Crop"
                            subtitle: "Ratio, guides, and crop zoom."
                            expandable: true
                            expanded: true

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                ActionButton {
                                    Layout.fillWidth: true
                                    text: editorPreview.cropEditing ? "Done" : "Edit Crop"
                                    active: editorPreview.cropEditing
                                    onClicked: editorPreview.cropEditing = !editorPreview.cropEditing
                                }

                                ActionButton {
                                    text: "Reset Box"
                                    enabled: editorPreview.hasAsset
                                    onClicked: editorPreview.resetCropRect()
                                }
                            }

                            Label {
                                text: "Ratio"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            ToneComboBox {
                                Layout.fillWidth: true
                                model: ["Original", "Square", "4x5", "3x2", "16x9"]
                                currentIndex: model.indexOf(editorPreview.cropPreset)
                                onActivated: editorPreview.cropPreset = currentText
                            }

                            Label {
                                text: "Guides"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            ToneComboBox {
                                Layout.fillWidth: true
                                model: ["Off", "Thirds", "Center", "Diagonal"]
                                currentIndex: model.indexOf(cropGuideMode)
                                onActivated: cropGuideMode = currentText
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zoom"
                                valueText: editorPreview.cropZoom.toFixed(2) + "x"
                                from: 1
                                to: 3
                                stepSize: 0.01
                                value: editorPreview.cropZoom
                                onValueDragged: function(value) { editorPreview.cropZoom = value }
                                onResetRequested: editorPreview.cropZoom = 1
                            }
                        }
                    }
                }
            }
        }
    }
}
