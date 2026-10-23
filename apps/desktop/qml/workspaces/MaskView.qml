pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PhotoEditor
import "../components"

Rectangle {
    id: root
    objectName: "maskWorkspace"

    Theme { id: theme }
    MaskRowActions {
        id: maskRowActions
        previewController: editorPreview
        nameEditor: activeMaskNameField
    }
    MaskSearchModel {
        id: maskSearchModel
        query: root.maskSearchQuery
        maskSets: editorPreview.maskSets || []
        toolItems: root.maskSearchToolItems()
        adjustmentItems: root.maskSearchAdjustmentItems()
    }

    color: "transparent"
    readonly property string maskVisibilityContract: "mask-visibility:v1|always:canvas-inspector-active-context-source-controls-overlay-state|contextual:brush-cursor-lasso-polygon-handles-alpha-import-refine-toolbar-pending-apply-cancel|one-expansion:mask-source-variants-context-menu-refine-options-adjustment-groups|hover-focus:mask-source-actions-reorder-visibility-solo-lock|diagnostic:generated-unsupported-preview-failure-runtime-detail|preserve:functionality"
    readonly property string maskSurfaceContract: "mask-surfaces:v1|canvas:dominant-overlay|sources:generated-geometric-brush-alpha|inspector:active-mask-stack-adjustments|overlays:brush-lasso-polygon-outline|pending:apply-cancel-retry-visible|context-menu:one-expansion|no-elide:true|no-overlap:true"
    readonly property string maskWorkflowPolishContract: "mask-workflow-polish:v1|first-glance:canvas-active-selection-inspector-source-tools|close-inspection:context-source-operation-preview-refine-pending-actions|clutter:provider-details-diagnostic-only-one-active-context|interaction:create-select-refine-combine-preview-rename-reorder-delete|text:no-elide-no-overlap|functionality:preserved"
    readonly property string maskPageCompletionContract: "page-completion:v1|masks|sidebar:source-variant-controls-search|content:canvas-overlay-brush-lasso-polygon-handles|inspector:active-context-mask-stack-refine-adjustments|toolbar:combine-preview-import-retry-apply-cancel|states:no-mask-active-mask-generating-locked-empty-error"
    readonly property string maskLayerParameterTerminologyContract: "mask-layer-parameter-terminology:v1|layer-opacity:composite-layer-result|mask-density:alpha-strength-before-gating|mask-opacity:mask-source-contribution|brush-flow:per-stamp-paint-rate|brush-opacity:pressure-or-source-opacity|overlay-opacity:visualization-only"
    readonly property string pointerCoordinateMappingContract: "mask-view-pointer-mapper:v1|CanvasCoordinateMapper|crop-rotation-aware|all-mask-inputs"
    property bool objectPickArmed: false
    property bool objectPickCreateNew: false
    property string objectPickMode: "selection"
    property bool polygonDrawArmed: false
    property bool lassoDrawArmed: false
    property bool cloneSourcePickArmed: false
    property bool createMaskChooserOpen: false
    property string maskSearchQuery: ""
    property bool tabletBrushStrokeActive: false
    property real brushHoverX: 0
    property real brushHoverY: 0
    property bool brushHoverActive: false
    property bool brushCursorPositionKnown: false
    property real selectionOutlinePhase
    readonly property color activeMaskAccent: root.maskAccent({
        "kind": editorPreview.activeMaskKind,
        "operation": editorPreview.activeMaskOperation,
        "toolRole": editorPreview.activeMaskToolRole,
        "retouchMode": editorPreview.activeMaskRetouchMode
    })
    readonly property int selectionOutlineAnimationPointBudget: 1800
    readonly property string selectionOutlineAnimationContract: "visibility-gated|motion-budgeted|point-budgeted:v1"
    readonly property bool selectionOutlineAnimationEnabled: selectionOutlineAnimationShouldRun()
    readonly property int stableInspectorWidth: 410
    readonly property int minimumCanvasWidth: 360
    readonly property int minimumCanvasHeight: 320
    readonly property string outlineRenderContract: "anti-aliased|zoom-stable|pixel-aligned:v1"
    readonly property string handleReadabilityContract: "dual-contrast-halo|large-hit-target|color-independent:v1"

    function stableOutlineWidth(logicalPixels) {
        return Math.max(0.75, Number(logicalPixels || 1.0))
    }

    function stableOutlineDash(logicalPixels) {
        return Math.max(1.0, Number(logicalPixels || 1.0))
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

    FileDialog {
        id: alphaMaskFileDialog
        title: "Import Alpha Mask"
        nameFilters: ["Image masks (*.png *.tif *.tiff *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: {
            root.clearTransientTools()
            editorPreview.beginAlphaImageMaskImport(selectedFile)
        }
    }

    CanvasCoordinateMapper {
        id: maskCoordinateMapper
        contentX: 0
        contentY: 0
        contentWidth: previewItem.width
        contentHeight: previewItem.height
        cropX: editorPreview.cropRectX
        cropY: editorPreview.cropRectY
        cropWidth: editorPreview.cropRectWidth
        cropHeight: editorPreview.cropRectHeight
        rotationDegrees: editorPreview.transformRotation
        fitMode: "fit"
        devicePixelRatio: Math.max(1.0, Number(Screen.devicePixelRatio || 1.0))
    }

    Rectangle {
        objectName: "alphaMaskImportPreviewPanel"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        width: 280
        height: alphaImportColumn.implicitHeight + 20
        radius: 4
        color: theme.alpha(theme.ink950, 0.92)
        border.width: theme.chromeBorderWidth
        border.color: theme.alpha(theme.polar, 0.28)
        visible: editorPreview.alphaMaskImportPreviewActive
        z: 50

        ColumnLayout {
            id: alphaImportColumn
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Label {
                objectName: "alphaMaskImportPreviewSummary"
                Layout.fillWidth: true
                text: "Alpha " + editorPreview.alphaMaskImportPreview.width + "x" +
                      editorPreview.alphaMaskImportPreview.height + "  " +
                      editorPreview.alphaMaskImportPreview.selectedPixelCount + " px"
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 10
                font.letterSpacing: 0
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 8
                elide: Text.ElideNone
            }

            ToolOptionToggle {
                objectName: "alphaMaskImportInvertCheck"
                Layout.fillWidth: true
                label: "Invert"
                helperText: "Invert the imported alpha mask."
                checked: editorPreview.alphaMaskImportInvert
                onToggled: checked => editorPreview.alphaMaskImportInvert = checked
            }

            ToolValueSlider {
                objectName: "alphaMaskImportThresholdSlider"
                Layout.fillWidth: true
                label: "Threshold"
                valueText: Math.round(value * 100) + "%"
                tooltipText: "Alpha mask threshold"
                from: 0
                to: 1
                stepSize: 0.01
                value: editorPreview.alphaMaskImportThreshold
                onMoved: editorPreview.alphaMaskImportThreshold = value
            }

            ToneComboBox {
                objectName: "alphaMaskImportResampleModeCombo"
                Layout.fillWidth: true
                accent: theme.focusRing
                model: ["Original", "Match Source"]
                currentIndex: editorPreview.alphaMaskImportResampleMode === "Match Source" ? 1 : 0
                onActivated: editorPreview.alphaMaskImportResampleMode = currentText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ActionButton {
                    objectName: "alphaMaskImportCancelButton"
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: editorPreview.cancelAlphaImageMaskImport()
                }

                ActionButton {
                    objectName: "alphaMaskImportCommitButton"
                    Layout.fillWidth: true
                    text: "Commit"
                    onClicked: editorPreview.commitAlphaImageMaskImport()
                }
            }
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

    function smartSelectionRuntimeLabel() {
        return String(editorPreview.smartSelectionRuntimeLabel || "Local Smart Select")
    }

    function clearTransientTools() {
        objectPickArmed = false
        objectPickCreateNew = false
        objectPickMode = "selection"
        polygonDrawArmed = false
        lassoDrawArmed = false
        cloneSourcePickArmed = false
        editorPreview.resetSmartSelectStroke()
    }

    function imagePointFromItem(item, itemX, itemY) {
        if (!item)
            return { valid: false, x: 0, y: 0 }
        const mapped = item.mapToItem(previewItem, Number(itemX || 0), Number(itemY || 0))
        const point = maskCoordinateMapper.viewToImagePoint(mapped.x, mapped.y)
        return { valid: point.valid, x: root.clamp(point.x, 0.0, 1.0), y: root.clamp(point.y, 0.0, 1.0) }
    }

    function pointerMappingDiagnosticSignature(item, itemX, itemY) {
        if (!item)
            return "mask-view-pointer:v1|invalid-item"
        const mapped = item.mapToItem(previewItem, Number(itemX || 0), Number(itemY || 0))
        return "mask-view-pointer:v1|" +
               maskCoordinateMapper.pointerMappingDiagnosticSignature(mapped.x, mapped.y)
    }

    function normalizedStrokePoint(item, itemX, itemY) {
        return imagePointFromItem(item, itemX, itemY)
    }

    function viewPointForImage(normalizedX, normalizedY) {
        const point = maskCoordinateMapper.imageToViewPoint(normalizedX, normalizedY)
        return { valid: point.valid, x: point.x, y: point.y }
    }

    function viewDiameterForImageSpan(centerX, centerY, normalizedDiameter) {
        const radius = Math.max(0.0, Number(normalizedDiameter || 0)) / 2.0
        const left = root.viewPointForImage(root.clamp(centerX - radius, 0.0, 1.0), centerY)
        const right = root.viewPointForImage(root.clamp(centerX + radius, 0.0, 1.0), centerY)
        const top = root.viewPointForImage(centerX, root.clamp(centerY - radius, 0.0, 1.0))
        const bottom = root.viewPointForImage(centerX, root.clamp(centerY + radius, 0.0, 1.0))
        const horizontal = Math.sqrt(Math.pow(right.x - left.x, 2) + Math.pow(right.y - left.y, 2))
        const vertical = Math.sqrt(Math.pow(bottom.x - top.x, 2) + Math.pow(bottom.y - top.y, 2))
        return Math.max(1, horizontal, vertical)
    }

    function beginSmartSelectStroke(mouseX, mouseY) {
        const point = normalizedStrokePoint(previewItem, mouseX, mouseY)
        editorPreview.beginSmartSelectStroke(
                    point.x,
                    point.y,
                    previewItem.width,
                    previewItem.height)
    }

    function updateSmartSelectStroke(mouseX, mouseY) {
        const point = normalizedStrokePoint(previewItem, mouseX, mouseY)
        editorPreview.updateSmartSelectStroke(
                    point.x,
                    point.y,
                    previewItem.width,
                    previewItem.height)
    }

    function commitSmartSelectStroke() {
        editorPreview.commitSmartSelectStroke(objectPickMode, objectPickCreateNew)
        objectPickCreateNew = false
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

    function closeCreateMaskChooser() {
        root.createMaskChooserOpen = false
    }

    function chooseCanvasCreateTool(mode) {
        root.closeCreateMaskChooser()
        if (mode === "polygon" && editorPreview.activeMaskKind !== "polygon")
            editorPreview.startPolygonSelection()
        root.selectTool(mode)
    }

    function chooseMaskSource(kind) {
        root.closeCreateMaskChooser()
        root.clearTransientTools()
        editorPreview.addMask(kind)
    }

    function chooseBrushMaskSource(subtract) {
        root.closeCreateMaskChooser()
        root.clearTransientTools()
        editorPreview.startSelectionBrush(subtract)
    }

    function chooseAlphaMaskSource() {
        root.closeCreateMaskChooser()
        root.clearTransientTools()
        alphaMaskFileDialog.open()
    }

    function openCreateMaskContext() {
        root.clearTransientTools()
        root.createMaskChooserOpen = !root.createMaskChooserOpen
    }

    function activeMaskEditingContext() {
        if (root.createMaskChooserOpen)
            return "create"
        if (root.objectPickArmed || editorPreview.smartSelectStrokeActive || editorPreview.smartSelectPreviewActive)
            return "smart-select"
        if (root.polygonDrawArmed || root.lassoDrawArmed ||
                editorPreview.activeMaskKind === "polygon" ||
                editorPreview.activeMaskKind === "lasso")
            return "polygon"
        if (editorPreview.activeMaskToolRole === "retouch")
            return "retouch"
        if (editorPreview.activeMaskKind === "brush")
            return "brush"
        if (editorPreview.hasActiveMask)
            return "refine"
        return "create"
    }

    function maskContextVisible(contextName) {
        return root.activeMaskEditingContext() === contextName
    }

    function maskActiveControlsContextVisible() {
        return editorPreview.hasActiveMask &&
               !root.maskContextVisible("create") &&
               !root.maskContextVisible("smart-select")
    }

    function activeMaskEditingContextLabel() {
        const context = root.activeMaskEditingContext()
        if (context === "create")
            return "Create"
        if (context === "smart-select")
            return "Smart Select"
        if (context === "polygon")
            return root.lassoDrawArmed || editorPreview.activeMaskKind === "lasso" ? "Lasso / Polygon" : "Polygon"
        if (context === "retouch")
            return "Retouch"
        if (context === "brush")
            return "Brush"
        return "Refine"
    }

    function activeMaskEditingContextSignature() {
        const context = root.activeMaskEditingContext()
        return "context:" + context +
               "|create:" + (context === "create").toString() +
               "|refine:" + (context === "refine").toString() +
               "|brush:" + (context === "brush").toString() +
               "|polygon:" + (context === "polygon").toString() +
               "|smart-select:" + (context === "smart-select").toString() +
               "|retouch:" + (context === "retouch").toString()
    }

    function maskWorkflowPolishSignature() {
        const context = root.activeMaskEditingContext()
        const pending = root.objectPickArmed ||
                        root.polygonDrawArmed ||
                        root.lassoDrawArmed ||
                        root.cloneSourcePickArmed ||
                        editorPreview.smartSelectStrokeActive ||
                        editorPreview.smartSelectPreviewActive ||
                        editorPreview.brushStrokeActive ||
                        editorPreview.alphaMaskImportPreviewActive
        return "mask-workflow-polish:v1" +
               "|asset:" + String(editorPreview.hasAsset) +
               "|context:" + context +
               "|active:" + String(editorPreview.hasActiveMask) +
               "|sets:" + String(editorPreview.maskSetCount) +
               "|sources:" + String(editorPreview.maskCount) +
               "|operation:" + String(editorPreview.hasActiveMask ? editorPreview.activeMaskOperation : root.maskCreationOperation) +
               "|preview:" + String(editorPreview.maskPreviewDisplayMode || "overlay") +
               "|create-open:" + String(root.createMaskChooserOpen) +
               "|search:" + String(maskSearchModel.searchText().length > 0) +
               "|pending:" + String(pending) +
               "|inspector:true|canvas:true|source-list:true" +
               "|preserve:object-subject-sky-background-brush-lasso-polygon-gradient-range-alpha-import-combine-preview-invert-refine-rename-reorder-duplicate-delete"
    }

    function percentText(value) {
        return Math.round(root.clamp(value, 0.0, 1.0) * 100).toString() + "%"
    }

    function brushCursorReadout() {
        const sizeText = editorPreview.activeMaskBrushSizeSummary || "Size --"
        return sizeText +
               "  Hard " + root.percentText(editorPreview.activeMaskBrushHardness) +
               "  Flow " + root.percentText(editorPreview.activeMaskBrushFlow) +
               "  Density " + root.percentText(editorPreview.activeMaskDensity)
    }

    function activeMaskHeaderKind() {
        if (editorPreview.activeMaskToolRole === "retouch" && editorPreview.activeMaskRetouchMode.length > 0)
            return editorPreview.activeMaskRetouchMode
        return editorPreview.activeMaskKind.length > 0 ? editorPreview.activeMaskKind : "selection"
    }

    function activeMaskHeaderTarget() {
        if (editorPreview.activeMaskToolRole === "retouch")
            return "Retouch Stack"
        return editorPreview.activeMaskSetName.length > 0
               ? editorPreview.activeMaskSetName
               : "Mask Set"
    }

    function activeMaskCompactHeaderSignature() {
        return "name:" + editorPreview.activeMaskName +
               "|kind:" + root.activeMaskHeaderKind() +
               "|operation:" + editorPreview.activeMaskOperation +
               "|target:" + root.activeMaskHeaderTarget() +
               "|enabled:" + editorPreview.activeMaskEnabled.toString() +
               "|solo:" + editorPreview.activeMaskSolo.toString() +
               "|locked:" + editorPreview.activeMaskLocked.toString() +
               "|opacity:" + root.percentText(editorPreview.activeMaskOpacity)
    }

    function activeToolLabel() {
        if (lassoDrawArmed)
            return "Lasso"
        if (cloneSourcePickArmed)
            return "Pick Clone Source"
        if (polygonDrawArmed)
            return "Polygon"
        if (objectPickArmed && objectPickCreateNew)
            return "New " + root.smartSelectionRuntimeLabel()
        if (objectPickArmed && objectPickMode === "selection-subtract")
            return "Subtract " + root.smartSelectionRuntimeLabel()
        if (objectPickArmed)
            return root.smartSelectionRuntimeLabel()
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
            return "Skin Cleanup (Heuristic)"
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

    function maskCompareGroupActive(groupKey) {
        return editorPreview.maskCompareAdjustmentGroups.indexOf(groupKey) >= 0
    }

    function toggleMaskCompareGroup(groupKey) {
        const groups = editorPreview.maskCompareAdjustmentGroups.slice(0)
        const existingIndex = groups.indexOf(groupKey)
        if (existingIndex >= 0)
            groups.splice(existingIndex, 1)
        else
            groups.push(groupKey)
        editorPreview.maskCompareAdjustmentGroups = groups
    }

    function maskSearchToolItems() {
        return [
            { category: "Tool", label: "Smart Select", detail: "Create or refine with object selection", keywords: "smart select object subject ai local runtime", action: "smart" },
            { category: "Tool", label: "Brush Add", detail: "Paint an additive brush component", keywords: "brush add paint selection", action: "brush-add" },
            { category: "Tool", label: "Brush Subtract", detail: "Paint an erasing brush component", keywords: "brush subtract erase remove paint", action: "brush-subtract" },
            { category: "Tool", label: "Lasso", detail: "Draw a freeform selection", keywords: "lasso freeform manual selection", action: "lasso" },
            { category: "Tool", label: "Polygon", detail: "Click editable polygon points", keywords: "polygon manual points selection", action: "polygon" },
            { category: "Tool", label: "Linear", detail: "Create a linear gradient selection", keywords: "linear gradient mask", action: "linear" },
            { category: "Tool", label: "Radial", detail: "Create a radial selection", keywords: "radial ellipse circular selection", action: "radial" },
            { category: "Tool", label: "Luma Range", detail: "Select by luminance", keywords: "luma luminance range brightness", action: "luminance" },
            { category: "Tool", label: "Color Range", detail: "Select by sampled color", keywords: "color range hue saturation sample eyedropper", action: "color" },
            { category: "Tool", label: "Depth Range", detail: "Select by depth metadata", keywords: "depth range portrait", action: "depth" },
            { category: "Tool", label: "Alpha Image", detail: "Import an image as a matte", keywords: "alpha image import matte file", action: "alpha" },
            { category: "Tool", label: "Subject", detail: "Create a subject selection", keywords: "subject foreground smart select generated", action: "subject" }
        ]
    }

    function maskSearchAdjustmentItems() {
        return [
            { category: "Control", label: "Exposure", detail: "Tone adjustment", keywords: "exposure tone ev light local adjustment" },
            { category: "Control", label: "Warmth", detail: "Color adjustment", keywords: "warmth temperature color white balance local adjustment" },
            { category: "Control", label: "Hue Shift", detail: "Color adjustment", keywords: "hue shift color local adjustment" },
            { category: "Control", label: "Saturation", detail: "Presence adjustment", keywords: "saturation chroma presence local adjustment" },
            { category: "Control", label: "Tint Hue", detail: "Presence adjustment", keywords: "tint hue colorize local adjustment" },
            { category: "Control", label: "Tint Amount", detail: "Presence adjustment", keywords: "tint amount colorize strength local adjustment" },
            { category: "Control", label: "Remove", detail: "Effects adjustment", keywords: "remove strength effects retouch local adjustment" },
            { category: "Control", label: "Heal Texture", detail: "Detail adjustment", keywords: "heal texture detail retouch local adjustment" },
            { category: "Control", label: "Heal Color", detail: "Detail adjustment", keywords: "heal color detail retouch local adjustment" }
        ]
    }

    function activateMaskSearchResult(result) {
        if (result.flatIndex !== undefined && result.flatIndex >= 0) {
            editorPreview.activeMaskIndex = result.flatIndex
            return
        }
        if (result.action === "smart") {
            root.chooseCanvasCreateTool("smart-new")
        } else if (result.action === "brush-add") {
            root.chooseBrushMaskSource(false)
        } else if (result.action === "brush-subtract") {
            root.chooseBrushMaskSource(true)
        } else if (result.action === "lasso" || result.action === "polygon") {
            root.chooseCanvasCreateTool(result.action)
        } else if (result.action === "alpha") {
            root.chooseAlphaMaskSource()
        } else if (result.action === "subject") {
            root.closeCreateMaskChooser()
            root.clearTransientTools()
            editorPreview.addAIMask("subject")
        } else if (result.action && result.action.length > 0) {
            root.chooseMaskSource(result.action)
        }
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

    function activeRefineEffectCount() {
        const featherDefault = activeMaskResetFeather()
        let count = editorPreview.activeMaskSelectionInverted ? 1 : 0
        if (Math.abs(editorPreview.activeMaskSelectionExpand) > 1)
            ++count
        if (Math.abs(editorPreview.activeMaskFeather - featherDefault) > 0.01)
            ++count
        if (editorPreview.activeMaskSelectionSmooth > 1)
            ++count
        if (editorPreview.activeMaskSelectionFineEdge > 1)
            ++count
        if (editorPreview.activeMaskSelectionEdgeContrast > 1)
            ++count
        if (editorPreview.activeMaskSelectionDecontaminate > 1)
            ++count
        return count
    }

    function isAiMaskKind(kind) {
        return kind === "subject" || kind === "sky" || kind === "background" ||
               kind === "people" || kind === "face" || kind === "hair" ||
               kind === "clothing" || kind === "object"
    }

    function aiMaskLabel(kind) {
        if (kind === "sky")
            return "Sky (Heuristic)"
        if (kind === "background")
            return "Background (Subject Invert)"
        if (kind === "people")
            return "People (Unsupported)"
        if (kind === "face")
            return "Face (Unsupported)"
        if (kind === "hair")
            return "Hair (Unsupported)"
        if (kind === "clothing")
            return "Clothing (Unsupported)"
        if (kind === "facial-hair")
            return "Facial Hair (Unsupported)"
        if (kind === "lips")
            return "Lips (Unsupported)"
        if (kind === "teeth")
            return "Teeth (Unsupported)"
        if (kind === "eyes")
            return "Eyes (Unsupported)"
        if (kind === "skin")
            return "Skin (Unsupported)"
        if (kind === "water")
            return "Water (Unsupported)"
        if (kind === "vegetation")
            return "Vegetation (Unsupported)"
        if (kind === "mountains")
            return "Mountains (Unsupported)"
        if (kind === "ground")
            return "Ground (Unsupported)"
        if (kind === "architecture")
            return "Architecture (Unsupported)"
        if (kind === "snow")
            return "Snow (Unsupported)"
        if (kind === "object")
            return "Object"
        return "Subject"
    }

    function generatedMaskTooltip(kind) {
        if (kind === "sky")
            return "Uses a color and position heuristic, not semantic sky segmentation."
        if (kind === "background")
            return "Inverts the subject foreground matte; it is not a separate background model."
        if (kind === "people" || kind === "face" || kind === "hair" || kind === "clothing" ||
                kind === "facial-hair" || kind === "lips" || kind === "teeth" ||
                kind === "eyes" || kind === "skin")
            return "Hidden until the runtime can return this person-part matte."
        if (kind === "water" || kind === "vegetation" || kind === "mountains" ||
                kind === "ground" || kind === "architecture" || kind === "snow")
            return "Hidden until the runtime can return a real landscape semantic matte for this target."
        return "Uses the available foreground selection runtime."
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
        if (mask.toolRole !== "retouch" &&
                (mask.operation === "add" || mask.operation === "subtract" ||
                 mask.operation === "intersect"))
            return selectionOperationOverlayColor(mask.operation)
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

    function resolvedMaskOverlayColor(mask) {
        if (mask && mask.toolRole === "retouch") {
            if (mask.retouchMode === "clone")
                return "#7cb5ff"
            if (mask.retouchMode === "heal")
                return "#72cbb2"
            return "#d98962"
        }
        const customColor = String(editorPreview.maskPreviewOverlayColor || "")
        return customColor.length > 0 ? customColor : defaultMaskOverlayColor()
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

    function outlinePointCount(outlines) {
        if (!outlines || outlines.length === undefined)
            return 0
        let total = 0
        for (let i = 0; i < outlines.length; ++i) {
            const entry = outlines[i]
            if (!entry)
                continue
            if (entry.x !== undefined && entry.y !== undefined) {
                total += 1
            } else if (entry.length !== undefined) {
                total += entry.length
            }
        }
        return total
    }

    function activeSelectionOutlinePointCount() {
        const pathPointCount = root.outlinePointCount(editorPreview.activeMaskOutlinePaths)
        if (pathPointCount > 0)
            return pathPointCount
        return root.outlinePointCount(editorPreview.activeMaskOutlinePoints)
    }

    function smartSelectionPreviewOutlinePointCount() {
        return root.outlinePointCount(editorPreview.smartSelectPreviewOutlinePoints)
    }

    function selectionOutlineAnimationPointCount() {
        const activePointCount = root.activeSelectionOutlinePointCount()
        if (activePointCount >= 2)
            return activePointCount
        if (objectPickArmed && editorPreview.smartSelectPreviewActive)
            return root.smartSelectionPreviewOutlinePointCount()
        return activePointCount
    }

    function selectionOutlineAnimationShouldRun() {
        if (editorPreview.maskPreviewMode !== "marching-ants")
            return false
        if (!root.visible || (!editorPreview.hasActiveMask &&
                !(objectPickArmed && editorPreview.smartSelectPreviewActive)))
            return false
        if (editorPreview.maskPreviewTemporarilyHidden === true || editorPreview.compareMode ||
                editorPreview.cropEditing)
            return false
        if (editorPreview.brushStrokeActive || editorPreview.smartSelectStrokeActive ||
                root.tabletBrushStrokeActive)
            return false
        const pointCount = root.selectionOutlineAnimationPointCount()
        return pointCount >= 2 && pointCount <= root.selectionOutlineAnimationPointBudget
    }

    NumberAnimation on selectionOutlinePhase {
        from: 0
        to: 14
        duration: 900
        loops: Animation.Infinite
        running: root.selectionOutlineAnimationEnabled
    }

    RowLayout {
        objectName: "maskWorkspaceStableSplit"
        anchors.fill: parent
        spacing: 14

        BrandPanel {
            objectName: "maskCanvasPane"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: root.minimumCanvasWidth
            Layout.minimumHeight: root.minimumCanvasHeight
            title: "Masks"
            subtitle: "Build each mask from ordered Components, then refine its local edit."

            Rectangle {
                objectName: "maskCanvasViewport"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: root.minimumCanvasWidth
                Layout.minimumHeight: root.minimumCanvasHeight
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

                    SharedMaskOverlay {
                        anchors.fill: parent
                        objectNamePrefix: "maskView"
                        masked: maskPreview.visible && (editorPreview.maskSetCount > 0 || editorPreview.hasActiveMask)
                        hasActiveMask: editorPreview.hasActiveMask
                        compareMode: false
                        activeBrushPainting: root.tabletBrushStrokeActive || editorPreview.brushStrokeActive
                        accentColor: "#19E0B0"
                        outlinePhase: root.selectionOutlinePhase
                        outlineAnimationEnabled: root.selectionOutlineAnimationEnabled
                        cropX: editorPreview.cropRectX
                        cropY: editorPreview.cropRectY
                        cropWidth: editorPreview.cropRectWidth
                        cropHeight: editorPreview.cropRectHeight
                        transformRotation: editorPreview.transformRotation
                        enablePolygonHandles: false
                        z: 0
                    }

                    Repeater {
                        model: editorPreview.maskStack

                        delegate: Item {
                            required property int index
                            required property var modelData

                            readonly property var mask: modelData
                            readonly property bool active: index === editorPreview.activeMaskIndex
                            readonly property color accent: root.maskAccent(mask)
                            readonly property color overlayAccent: root.resolvedMaskOverlayColor(mask)
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
                                        GradientStop { position: 0.0; color: Qt.alpha(overlayAccent, 0.34) }
                                        GradientStop {
                                            position: Math.max(0.0, linearOverlay.positionValue - linearOverlay.featherValue * 0.5)
                                            color: Qt.alpha(overlayAccent, 0.14)
                                        }
                                        GradientStop { position: linearOverlay.positionValue; color: Qt.rgba(0.95, 0.97, 0.98, 0.72) }
                                        GradientStop {
                                            position: Math.min(1.0, linearOverlay.positionValue + linearOverlay.featherValue * 0.5)
                                            color: Qt.alpha(overlayAccent, 0.06)
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
                                    opacity: 0.9
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: active ? Qt.CrossCursor : Qt.PointingHandCursor
                                    onPressed: function(mouse) {
                                        editorPreview.activeMaskIndex = index
                                        if (active) {
                                            const point = root.imagePointFromItem(linearOverlay, mouse.x, mouse.y)
                                            if (point.valid)
                                                editorPreview.activeMaskPosition = root.clamp(point.y, 0, 1)
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: active
                                    x: linearOverlay.width / 2 - width / 2
                                    y: linearOverlay.height * linearOverlay.positionValue - height / 2
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
                                            const point = root.imagePointFromItem(
                                                        linearOverlay,
                                                        parent.x + mouse.x,
                                                        parent.y + mouse.y + parent.height / 2)
                                            if (point.valid)
                                                editorPreview.activeMaskPosition = root.clamp(point.y, 0, 1)
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
                                        const innerScale = Math.max(0.0, 1.0 - radialOverlay.featherValue)
                                        ctx.reset()
                                        ctx.strokeStyle = "#f4f7fa"
                                        ctx.lineWidth = active ? 1.8 : 1.2
                                        ctx.fillStyle = Qt.alpha(overlayAccent, active ? 0.18 : 0.10)
                                        ctx.beginPath()
                                        ctx.ellipse(1, 1, Math.max(1, width - 2), Math.max(1, height - 2))
                                        ctx.fill()
                                        ctx.stroke()
                                        ctx.strokeStyle = "rgba(244,247,250,0.45)"
                                        ctx.beginPath()
                                        ctx.ellipse(width / 2 - Math.max(1, (width / 2 - 1) * innerScale),
                                                    height / 2 - Math.max(1, (height / 2 - 1) * innerScale),
                                                    Math.max(1, (width / 2 - 1) * innerScale) * 2,
                                                    Math.max(1, (height / 2 - 1) * innerScale) * 2)
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
                                            const point = root.imagePointFromItem(
                                                        radialOverlay,
                                                        parent.x + mouse.x,
                                                        parent.y + mouse.y)
                                            if (point.valid) {
                                                editorPreview.activeMaskCenterX = root.clamp(point.x, 0, 1)
                                                editorPreview.activeMaskCenterY = root.clamp(point.y, 0, 1)
                                            }
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
                                            const point = root.imagePointFromItem(
                                                        radialOverlay,
                                                        parent.x + mouse.x,
                                                        parent.y + mouse.y)
                                            if (point.valid)
                                                editorPreview.activeMaskRadiusX = root.clamp(Math.abs(point.x - radialOverlay.centerXValue), 0.05, 1.0)
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
                                            const point = root.imagePointFromItem(
                                                        radialOverlay,
                                                        parent.x + mouse.x,
                                                        parent.y + mouse.y)
                                            if (point.valid)
                                                editorPreview.activeMaskRadiusY = root.clamp(Math.abs(point.y - radialOverlay.centerYValue), 0.05, 1.0)
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
                                                     overlayAccent,
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

                                Rectangle {
                                    property var sourcePoint: root.viewPointForImage(mask.cloneSourceX || 0.5, mask.cloneSourceY || 0.5)
                                    property var targetPoint: root.viewPointForImage(
                                        Math.max(0, Math.min(1, (mask.cloneSourceX || 0.5) - (mask.cloneOffsetX || 0))),
                                        Math.max(0, Math.min(1, (mask.cloneSourceY || 0.5) - (mask.cloneOffsetY || 0))))
                                    property real sourceX: sourcePoint.x
                                    property real sourceY: sourcePoint.y
                                    property real targetX: targetPoint.x
                                    property real targetY: targetPoint.y
                                    property real offsetLength: Math.sqrt(Math.pow(targetX - sourceX, 2) + Math.pow(targetY - sourceY, 2))
                                    visible: active && mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
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
                                    property real targetX: Math.max(0, Math.min(1, (mask.cloneSourceX || 0.5) - (mask.cloneOffsetX || 0)))
                                    property real targetY: Math.max(0, Math.min(1, (mask.cloneSourceY || 0.5) - (mask.cloneOffsetY || 0)))
                                    property var targetPoint: root.viewPointForImage(targetX, targetY)
                                    visible: active && mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
                                    x: targetPoint.x - width / 2
                                    y: targetPoint.y - height / 2
                                    width: Math.max(18, Math.min(58, root.viewDiameterForImageSpan(
                                                             targetX,
                                                             targetY,
                                                             (mask.brushSize || 0.07) * (mask.cloneScale || 1.0))))
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
                                    property var sourcePoint: root.viewPointForImage(mask.cloneSourceX || 0.5, mask.cloneSourceY || 0.5)
                                    visible: active && mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
                                    x: sourcePoint.x - width / 2
                                    y: sourcePoint.y - height / 2
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
                                        root.brushHoverX = x
                                        root.brushHoverY = y
                                        root.brushHoverActive = true
                                        root.brushCursorPositionKnown = true
                                        const point = root.imagePointFromItem(brushOverlay, x, y)
                                        if (!point.valid)
                                            return
                                        if (mask.retouchMode === "clone" &&
                                                (root.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                            editorPreview.setActiveMaskCloneSource(point.x, point.y)
                                            root.cloneSourcePickArmed = false
                                            root.tabletBrushStrokeActive = false
                                            return
                                        }
                                        editorPreview.beginActiveBrushStroke()
                                        root.tabletBrushStrokeActive = true
                                        editorPreview.addBrushPointWithTabletEraser(
                                                    point.x,
                                                    point.y,
                                                    pressure,
                                                    tiltX,
                                                    tiltY,
                                                    eraser)
                                    }

                                    onTabletMoved: function(x, y, pressure, tiltX, tiltY, eraser) {
                                        root.brushHoverX = x
                                        root.brushHoverY = y
                                        root.brushHoverActive = true
                                        root.brushCursorPositionKnown = true
                                        if (!root.tabletBrushStrokeActive || root.cloneSourcePickArmed ||
                                                (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                            return
                                        const point = root.imagePointFromItem(brushOverlay, x, y)
                                        if (!point.valid)
                                            return
                                        editorPreview.addBrushPointWithTabletEraser(
                                                    point.x,
                                                    point.y,
                                                    pressure,
                                                    tiltX,
                                                    tiltY,
                                                    eraser)
                                    }

                                    onTabletReleased: function(x, y, pressure, tiltX, tiltY, eraser) {
                                        root.brushHoverX = x
                                        root.brushHoverY = y
                                        root.brushCursorPositionKnown = true
                                        if (root.tabletBrushStrokeActive)
                                            editorPreview.endActiveBrushStroke()
                                        root.tabletBrushStrokeActive = false
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    enabled: active
                                    hoverEnabled: true
                                    cursorShape: Qt.CrossCursor
                                    onEntered: root.brushHoverActive = true
                                    onExited: root.brushHoverActive = false
                                    onPressed: function(mouse) {
                                        editorPreview.activeMaskIndex = index
                                        root.brushHoverX = mouse.x
                                        root.brushHoverY = mouse.y
                                        root.brushHoverActive = true
                                        root.brushCursorPositionKnown = true
                                        const point = root.imagePointFromItem(brushOverlay, mouse.x, mouse.y)
                                        if (!point.valid)
                                            return
                                        if (mask.retouchMode === "clone" &&
                                                (root.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                            editorPreview.setActiveMaskCloneSource(point.x, point.y)
                                            root.cloneSourcePickArmed = false
                                            return
                                        }
                                        editorPreview.addBrushPointWithTablet(point.x, point.y, root.pointerPressure(mouse), root.pointerTilt(mouse, "xTilt"), root.pointerTilt(mouse, "yTilt"))
                                    }
                                    onPositionChanged: function(mouse) {
                                        root.brushHoverX = mouse.x
                                        root.brushHoverY = mouse.y
                                        root.brushHoverActive = true
                                        root.brushCursorPositionKnown = true
                                        if (!pressed || root.cloneSourcePickArmed ||
                                                (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                            return
                                        const point = root.imagePointFromItem(brushOverlay, mouse.x, mouse.y)
                                        if (point.valid)
                                            editorPreview.addBrushPointWithTablet(point.x, point.y, root.pointerPressure(mouse), root.pointerTilt(mouse, "xTilt"), root.pointerTilt(mouse, "yTilt"))
                                    }
                                }

                                Item {
                                    id: maskViewBrushCursor
                                    objectName: "maskViewBrushCursor"
                                    readonly property real brushDiameter: Math.max(
                                        8,
                                        Math.min(previewItem.width, previewItem.height) *
                                            editorPreview.activeMaskBrushSize * 2)
                                    readonly property real hardnessDiameter: Math.max(
                                        2,
                                        brushDiameter * root.clamp(editorPreview.activeMaskBrushHardness, 0.0, 1.0))
                                    readonly property string brushCursorVisibilityContract: "persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1"
                                    visible: active && (root.brushCursorPositionKnown ||
                                                        root.brushHoverActive ||
                                                        editorPreview.brushStrokeActive)
                                    x: root.brushHoverX - width / 2
                                    y: root.brushHoverY - height / 2
                                    width: brushDiameter
                                    height: brushDiameter
                                    z: 8

                                    Rectangle {
                                        objectName: "maskViewBrushCursorOuterHaloRing"
                                        anchors.fill: parent
                                        radius: width / 2
                                        color: "transparent"
                                        border.width: 3.0
                                        border.color: Qt.alpha("#101419", 0.92)
                                    }

                                    Rectangle {
                                        objectName: "maskViewBrushCursorFeatherRing"
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        radius: width / 2
                                        color: "transparent"
                                        border.width: 1.5
                                        border.color: root.selectionOperationOverlayColor(editorPreview.activeMaskOperation)
                                    }

                                    Rectangle {
                                        objectName: "maskViewBrushCursorHardnessRing"
                                        anchors.centerIn: parent
                                        width: maskViewBrushCursor.hardnessDiameter
                                        height: width
                                        radius: width / 2
                                        color: "transparent"
                                        border.width: 1.5
                                        border.color: Qt.alpha("#f4f7fa", 0.92)
                                    }

                                    Label {
                                        objectName: "maskViewBrushCursorReadout"
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
                                        const firstViewPoint = root.viewPointForImage(points[0].x || 0.5, points[0].y || 0.5)
                                        ctx.moveTo(root.pixelAligned(firstViewPoint.x), root.pixelAligned(firstViewPoint.y))
                                        for (let i = 1; i < count; ++i) {
                                            const viewPoint = root.viewPointForImage(points[i].x || 0.5, points[i].y || 0.5)
                                            ctx.lineTo(root.pixelAligned(viewPoint.x), root.pixelAligned(viewPoint.y))
                                        }
                                        if (count >= 3)
                                            ctx.closePath()

                                        if (count >= 3) {
                                            ctx.fillStyle = Qt.alpha(overlayAccent, active ? 0.16 : 0.08)
                                            ctx.fill()
                                        }

                                        ctx.strokeStyle = Qt.alpha("#f4f7fa", active ? 0.88 : 0.55)
                                        ctx.lineWidth = active ? 1.8 : 1.1
                                        if (ctx.setLineDash)
                                            ctx.setLineDash([8, 6])
                                        ctx.lineDashOffset = root.selectionOutlineAnimationEnabled ? -root.selectionOutlinePhase : 0
                                        ctx.stroke()
                                    }
                                }

                                Repeater {
                                    model: mask.polygonPoints || []

                                    delegate: Rectangle {
                                        required property var modelData

                                        readonly property string handleReadabilityContract: root.handleReadabilityContract
                                        readonly property var viewPoint: root.viewPointForImage(modelData.x || 0.5, modelData.y || 0.5)
                                        width: 14
                                        height: width
                                        radius: width / 2
                                        x: viewPoint.x - width / 2
                                        y: viewPoint.y - height / 2
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
                                            color: Qt.alpha(accent, 0.88)
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                visible: active && (mask.kind === "luminance" || mask.kind === "color" || mask.kind === "depth" || mask.kind === "alpha" || root.isAiMaskKind(mask.kind))
                                anchors.centerIn: parent
                                radius: 10
                                color: Qt.alpha(overlayAccent, 0.18)
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
                                            : root.aiMaskLabel(mask.kind) + " mask active"
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
                        objectName: "maskViewActiveSelectionOutline"
                        antialiasing: true
                        anchors.fill: parent
                        readonly property string outlineRenderContract: root.outlineRenderContract
                        readonly property string outlineAnimationContract: root.selectionOutlineAnimationContract
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

                        Connections {
                            target: root

                            function onSelectionOutlinePhaseChanged() {
                                if (root.selectionOutlineAnimationEnabled)
                                    activeSelectionOutline.requestPaint()
                            }

                            function onSelectionOutlineAnimationEnabledChanged() {
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

                            const overlayAccent = root.resolvedMaskOverlayColor(
                                        editorPreview.maskStack[editorPreview.activeMaskIndex])
                            ctx.beginPath()
                            const firstViewPoint = root.viewPointForImage(points[0].x || 0.5, points[0].y || 0.5)
                            ctx.moveTo(root.pixelAligned(firstViewPoint.x),
                                       root.pixelAligned(firstViewPoint.y))
                            for (let i = 1; i < count; ++i) {
                                const viewPoint = root.viewPointForImage(points[i].x || 0.5, points[i].y || 0.5)
                                ctx.lineTo(root.pixelAligned(viewPoint.x),
                                           root.pixelAligned(viewPoint.y))
                            }
                            if (count >= 3)
                                ctx.closePath()

                            if (count >= 3) {
                                ctx.fillStyle = Qt.alpha(overlayAccent, 0.12)
                                ctx.fill()
                            }

                            const dashOn = root.stableOutlineDash(8)
                            const dashOff = root.stableOutlineDash(6)
                            const phase = root.selectionOutlineAnimationEnabled
                                          ? root.stableOutlineDash(root.selectionOutlinePhase)
                                          : 0
                            ctx.lineWidth = root.stableOutlineWidth(2)
                            if (ctx.setLineDash)
                                ctx.setLineDash([dashOn, dashOff])
                            ctx.lineDashOffset = -phase
                            ctx.strokeStyle = "#f4f7fa"
                            ctx.stroke()
                            if (ctx.setLineDash)
                                ctx.setLineDash([dashOn, dashOff])
                            ctx.lineDashOffset = root.stableOutlineDash(12) - phase
                            ctx.strokeStyle = Qt.alpha("#101419", 0.78)
                            ctx.stroke()
                        }
                    }

                    Item {
                        objectName: "maskViewPointerHandlerLayer"
                        anchors.fill: parent
                        enabled: root.objectPickArmed || root.polygonDrawArmed || root.lassoDrawArmed
                        z: 100
                        property real smartPointerX: 0
                        property real smartPointerY: 0
                        property real lassoPointerX: 0
                        property real lassoPointerY: 0

                        HoverHandler {
                            id: maskViewToolHoverHandler
                            objectName: "maskViewToolHoverHandler"
                            enabled: parent.enabled
                            cursorShape: (root.polygonDrawArmed || root.lassoDrawArmed || root.objectPickArmed)
                                         ? Qt.CrossCursor
                                         : Qt.ArrowCursor

                            onPointChanged: {
                                if (root.objectPickArmed && !editorPreview.smartSelectStrokeActive) {
                                    const imagePoint = root.normalizedStrokePoint(previewItem, point.position.x, point.position.y)
                                    if (imagePoint.valid)
                                        editorPreview.scheduleSmartSelectPreview(imagePoint.x, imagePoint.y)
                                }
                            }
                        }

                        PointHandler {
                            id: maskViewSmartSelectPointHandler
                            objectName: "maskViewSmartSelectPointHandler"
                            readonly property string pointerGrabPolicy: "exclusive-smart-selection-stroke"
                            enabled: root.objectPickArmed
                            acceptedButtons: Qt.LeftButton
                            grabPermissions: PointerHandler.TakeOverForbidden
                            target: null

                            onActiveChanged: {
                                if (active) {
                                    parent.smartPointerX = point.position.x
                                    parent.smartPointerY = point.position.y
                                    root.beginSmartSelectStroke(parent.smartPointerX, parent.smartPointerY)
                                } else {
                                    root.commitSmartSelectStroke()
                                }
                            }

                            onPointChanged: {
                                parent.smartPointerX = point.position.x
                                parent.smartPointerY = point.position.y
                                if (active)
                                    root.updateSmartSelectStroke(parent.smartPointerX, parent.smartPointerY)
                            }
                        }

                        PointHandler {
                            id: maskViewLassoPointHandler
                            objectName: "maskViewLassoPointHandler"
                            readonly property string pointerGrabPolicy: "exclusive-lasso-drag"
                            enabled: root.lassoDrawArmed
                            acceptedButtons: Qt.LeftButton
                            grabPermissions: PointerHandler.TakeOverForbidden
                            target: null

                            onActiveChanged: {
                                if (active) {
                                    parent.lassoPointerX = point.position.x
                                    parent.lassoPointerY = point.position.y
                                    editorPreview.startLassoSelection()
                                    const imagePoint = root.normalizedStrokePoint(
                                                previewItem,
                                                parent.lassoPointerX,
                                                parent.lassoPointerY)
                                    if (imagePoint.valid)
                                        editorPreview.appendActivePolygonPoint(imagePoint.x, imagePoint.y)
                                } else {
                                    root.lassoDrawArmed = false
                                }
                            }

                            onPointChanged: {
                                parent.lassoPointerX = point.position.x
                                parent.lassoPointerY = point.position.y
                                if (active) {
                                    const imagePoint = root.normalizedStrokePoint(
                                                previewItem,
                                                parent.lassoPointerX,
                                                parent.lassoPointerY)
                                    if (imagePoint.valid)
                                        editorPreview.appendActivePolygonPoint(imagePoint.x, imagePoint.y)
                                }
                            }
                        }

                        TapHandler {
                            id: maskViewPolygonTapHandler
                            objectName: "maskViewPolygonTapHandler"
                            readonly property string pointerGrabPolicy: "exclusive-polygon-tap"
                            enabled: root.polygonDrawArmed
                            acceptedButtons: Qt.LeftButton
                            grabPermissions: PointerHandler.TakeOverForbidden

                            onTapped: function(eventPoint, button) {
                                const imagePoint = root.normalizedStrokePoint(
                                            previewItem,
                                            eventPoint.position.x,
                                            eventPoint.position.y)
                                if (imagePoint.valid)
                                    editorPreview.appendActivePolygonPoint(imagePoint.x, imagePoint.y)
                            }

                            onDoubleTapped: function(eventPoint, button) {
                                const imagePoint = root.normalizedStrokePoint(
                                            previewItem,
                                            eventPoint.position.x,
                                            eventPoint.position.y)
                                if (imagePoint.valid)
                                    editorPreview.appendActivePolygonPoint(imagePoint.x, imagePoint.y)
                                root.polygonDrawArmed = false
                            }
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
            objectName: "maskInspectorPane"
            Layout.minimumWidth: root.stableInspectorWidth
            Layout.preferredWidth: root.stableInspectorWidth
            Layout.maximumWidth: root.stableInspectorWidth
            Layout.fillHeight: true
            title: "Selection Inspector"
            subtitle: "Pick a tool from the preview rail, then refine the active selection here."

            ScrollView {
                id: maskInspectorScrollView
                objectName: "maskInspectorScrollView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                contentHeight: maskInspectorColumn.implicitHeight
                clip: true

                ColumnLayout {
                    id: maskInspectorColumn
                    objectName: "maskInspectorColumn"
                    width: maskInspectorScrollView.availableWidth
                    spacing: 12

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Tools"
                        subtitle: "Select, refine, and retouch tools for the canvas."

                        ToneTextField {
                            objectName: "maskSearchField"
                            accessibleLabel: "Search mask tools and controls"
                            Layout.fillWidth: true
                            text: root.maskSearchQuery
                            placeholderText: "Search masks, tools, controls"
                            selectByMouse: true
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                            color: "#f2f4f6"
                            placeholderTextColor: "#6f7882"
                            onTextChanged: root.maskSearchQuery = text
                            background: Rectangle {
                                radius: 8
                                color: "#11161a"
                                border.width: 1
                                border.color: maskSearchModel.searchText().length > 0 ? Qt.alpha("#8ab8d8", 0.52) : "#2a3138"
                            }
                        }

                        Rectangle {
                            objectName: "maskSearchResultsPanel"
                            Layout.fillWidth: true
                            visible: maskSearchModel.searchText().length > 0
                            implicitHeight: visible ? maskSearchResultsColumn.implicitHeight + 12 : 0
                            radius: 8
                            color: "#11161a"
                            border.width: 1
                            border.color: "#2a3138"

                            ColumnLayout {
                                id: maskSearchResultsColumn
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 6

                                Repeater {
                                    model: maskSearchModel.results()

                                    delegate: ActionButton {
                                        required property var modelData
                                        objectName: "maskSearchResultButton"
                                        Layout.fillWidth: true
                                        text: modelData.category + " · " + modelData.label
                                        iconName: modelData.category === "Tool" ? "local-adjust" : (modelData.category === "Mask" ? "masks" : "density")
                                        compact: true
                                        tooltipText: modelData.detail
                                        onClicked: root.activateMaskSearchResult(modelData)
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: maskSearchModel.results().length === 0
                                    text: "No matching mask tools, masks, or local controls."
                                    color: "#8f98a2"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Rectangle {
                            objectName: "maskActiveContextHeader"
                            Layout.fillWidth: true
                            implicitHeight: maskActiveContextRow.implicitHeight + 12
                            radius: 8
                            color: "#11161a"
                            border.width: 1
                            border.color: root.maskContextVisible("create") ? "#5f8fb3" : "#2a3138"

                            RowLayout {
                                id: maskActiveContextRow
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                Label {
                                    objectName: "maskActiveContextLabel"
                                    Layout.fillWidth: true
                                    text: "Context · " + root.activeMaskEditingContextLabel()
                                    color: "#f2f4f6"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    fontSizeMode: Text.HorizontalFit
                                    minimumPixelSize: 8
                                    elide: Text.ElideNone
                                }

                                Label {
                                    objectName: "maskActiveContextSignature"
                                    visible: false
                                    text: root.activeMaskEditingContextSignature()
                                }
                            }
                        }

                        ActionButton {
                            objectName: "createNewMaskButton"
                            Layout.fillWidth: true
                            text: "Create New Mask"
                            iconName: "plus"
                            tone: "primary"
                            active: root.maskContextVisible("create") && root.createMaskChooserOpen
                            enabled: editorPreview.hasAsset
                            tooltipText: "Open a compact chooser for smart, brush, shape, range, and imported mask tools."
                            onClicked: root.openCreateMaskContext()
                        }

                        Rectangle {
                            objectName: "createNewMaskChooser"
                            Layout.fillWidth: true
                            visible: root.maskContextVisible("create") && root.createMaskChooserOpen
                            implicitHeight: visible ? createMaskChooserColumn.implicitHeight + 16 : 0
                            radius: 8
                            color: "#11161a"
                            border.width: 1
                            border.color: "#2a3138"
                            clip: true

                            ColumnLayout {
                                id: createMaskChooserColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                GridLayout {
                                    objectName: "createNewMaskChooserGrid"
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 8
                                    rowSpacing: 8

                                    ActionButton {
                                        objectName: "createMaskSmartButton"
                                        Layout.fillWidth: true
                                        text: "Smart Select"
                                        compact: true
                                        enabled: editorPreview.hasAsset
                                        onClicked: root.chooseCanvasCreateTool("smart-new")
                                    }

                                    ActionButton {
                                        objectName: "createMaskBrushAddButton"
                                        Layout.fillWidth: true
                                        text: "Brush Add"
                                        compact: true
                                        onClicked: root.chooseBrushMaskSource(false)
                                    }

                                    ActionButton {
                                        objectName: "createMaskBrushSubtractButton"
                                        Layout.fillWidth: true
                                        text: "Brush Subtract"
                                        compact: true
                                        onClicked: root.chooseBrushMaskSource(true)
                                    }

                                    ActionButton {
                                        objectName: "createMaskLassoButton"
                                        Layout.fillWidth: true
                                        text: "Lasso"
                                        compact: true
                                        onClicked: root.chooseCanvasCreateTool("lasso")
                                    }

                                    ActionButton {
                                        objectName: "createMaskPolygonButton"
                                        Layout.fillWidth: true
                                        text: "Polygon"
                                        compact: true
                                        onClicked: root.chooseCanvasCreateTool("polygon")
                                    }

                                    ActionButton {
                                        objectName: "createMaskLinearButton"
                                        Layout.fillWidth: true
                                        text: "Linear"
                                        compact: true
                                        onClicked: root.chooseMaskSource("linear")
                                    }

                                    ActionButton {
                                        objectName: "createMaskRadialButton"
                                        Layout.fillWidth: true
                                        text: "Radial"
                                        compact: true
                                        onClicked: root.chooseMaskSource("radial")
                                    }

                                    ActionButton {
                                        objectName: "createMaskLuminanceButton"
                                        Layout.fillWidth: true
                                        text: "Luma Range"
                                        compact: true
                                        onClicked: root.chooseMaskSource("luminance")
                                    }

                                    ActionButton {
                                        objectName: "createMaskColorButton"
                                        Layout.fillWidth: true
                                        text: "Color Range"
                                        compact: true
                                        onClicked: root.chooseMaskSource("color")
                                    }

                                    ActionButton {
                                        objectName: "createMaskDepthButton"
                                        Layout.fillWidth: true
                                        text: "Depth Range"
                                        compact: true
                                        onClicked: root.chooseMaskSource("depth")
                                    }

                                    ActionButton {
                                        objectName: "createMaskAlphaImageButton"
                                        Layout.fillWidth: true
                                        text: "Alpha Image"
                                        compact: true
                                        onClicked: root.chooseAlphaMaskSource()
                                    }

                                    ActionButton {
                                        objectName: "createMaskSubjectButton"
                                        Layout.fillWidth: true
                                        text: "Subject"
                                        compact: true
                                        enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                        onClicked: {
                                            root.closeCreateMaskChooser()
                                            root.clearTransientTools()
                                            editorPreview.addAIMask("subject")
                                        }
                                    }
                                }
                            }
                        }

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
                            text: "Vision Object Select requires macOS 14 or later. " +
                                  root.smartSelectionRuntimeLabel() + " remains available for rough local selections."
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Selections"
                        subtitle: "Shape and range selections for local adjustments."
                        visible: root.maskContextVisible("create")

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
                                objectName: "maskGeneratedSubjectButton"
                                Layout.fillWidth: true
                                text: "Subject"
                                tooltipText: root.generatedMaskTooltip("subject")
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("subject")
                                }
                            }

                            ActionButton {
                                objectName: "maskGeneratedSkyHeuristicButton"
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("sky")
                                tooltipText: root.generatedMaskTooltip("sky")
                                enabled: editorPreview.hasAsset
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("sky")
                                }
                            }

                            ActionButton {
                                objectName: "maskGeneratedBackgroundDerivedButton"
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("background")
                                tooltipText: root.generatedMaskTooltip("background")
                                enabled: editorPreview.hasAsset && editorPreview.subjectMaskAvailable
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("background")
                                }
                            }

                            ActionButton {
                                objectName: "maskGeneratedPeopleUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("people")
                                tooltipText: root.generatedMaskTooltip("people")
                                enabled: false
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("people")
                                }
                            }

                            ActionButton {
                                objectName: "maskGeneratedFaceUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("face")
                                tooltipText: root.generatedMaskTooltip("face")
                                enabled: false
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("face")
                                }
                            }

                            ActionButton {
                                objectName: "maskGeneratedHairUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("hair")
                                tooltipText: root.generatedMaskTooltip("hair")
                                enabled: false
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("hair")
                                }
                            }

                            ActionButton {
                                objectName: "maskGeneratedClothingUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("clothing")
                                tooltipText: root.generatedMaskTooltip("clothing")
                                enabled: false
                                onClicked: {
                                    root.clearTransientTools()
                                    editorPreview.addAIMask("clothing")
                                }
                            }

                            ActionButton {
                                objectName: "maskGeneratedFacialHairUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("facial-hair")
                                tooltipText: root.generatedMaskTooltip("facial-hair")
                                enabled: false
                                onClicked: editorPreview.addAIMask("facial-hair")
                            }

                            ActionButton {
                                objectName: "maskGeneratedLipsUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("lips")
                                tooltipText: root.generatedMaskTooltip("lips")
                                enabled: false
                                onClicked: editorPreview.addAIMask("lips")
                            }

                            ActionButton {
                                objectName: "maskGeneratedTeethUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("teeth")
                                tooltipText: root.generatedMaskTooltip("teeth")
                                enabled: false
                                onClicked: editorPreview.addAIMask("teeth")
                            }

                            ActionButton {
                                objectName: "maskGeneratedEyesUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("eyes")
                                tooltipText: root.generatedMaskTooltip("eyes")
                                enabled: false
                                onClicked: editorPreview.addAIMask("eyes")
                            }

                            ActionButton {
                                objectName: "maskGeneratedSkinUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("skin")
                                tooltipText: root.generatedMaskTooltip("skin")
                                enabled: false
                                onClicked: editorPreview.addAIMask("skin")
                            }

                            ActionButton {
                                objectName: "maskGeneratedWaterUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("water")
                                tooltipText: root.generatedMaskTooltip("water")
                                enabled: false
                                onClicked: editorPreview.addAIMask("water")
                            }

                            ActionButton {
                                objectName: "maskGeneratedVegetationUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("vegetation")
                                tooltipText: root.generatedMaskTooltip("vegetation")
                                enabled: false
                                onClicked: editorPreview.addAIMask("vegetation")
                            }

                            ActionButton {
                                objectName: "maskGeneratedMountainsUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("mountains")
                                tooltipText: root.generatedMaskTooltip("mountains")
                                enabled: false
                                onClicked: editorPreview.addAIMask("mountains")
                            }

                            ActionButton {
                                objectName: "maskGeneratedGroundUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("ground")
                                tooltipText: root.generatedMaskTooltip("ground")
                                enabled: false
                                onClicked: editorPreview.addAIMask("ground")
                            }

                            ActionButton {
                                objectName: "maskGeneratedArchitectureUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("architecture")
                                tooltipText: root.generatedMaskTooltip("architecture")
                                enabled: false
                                onClicked: editorPreview.addAIMask("architecture")
                            }

                            ActionButton {
                                objectName: "maskGeneratedSnowUnsupportedButton"
                                visible: false
                                Layout.fillWidth: true
                                text: root.aiMaskLabel("snow")
                                tooltipText: root.generatedMaskTooltip("snow")
                                enabled: false
                                onClicked: editorPreview.addAIMask("snow")
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Masks"

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: editorPreview.maskSets

                                delegate: Rectangle {
                                    id: maskSetDelegate
                                    objectName: "maskSetFocusRow"
                                    required property int index
                                    required property var modelData

                                    Layout.fillWidth: true
                                    visible: maskSearchModel.setMatches(modelData)
                                    radius: 10
                                    color: "#171b1f"
                                    border.width: 1
                                    border.color: activeFocus ? "#8ab8d8" : "#2d3339"
                                    implicitHeight: setColumn.implicitHeight + 20
                                    activeFocusOnTab: true
                                    Keys.onReturnPressed: if (modelData.sources.length > 0) editorPreview.activeMaskIndex = modelData.sources[0].flatIndex
                                    Keys.onEnterPressed: if (modelData.sources.length > 0) editorPreview.activeMaskIndex = modelData.sources[0].flatIndex
                                    Keys.onSpacePressed: if (modelData.sources.length > 0) editorPreview.activeMaskIndex = modelData.sources[0].flatIndex

                                    ColumnLayout {
                                        id: setColumn
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 8

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Rectangle {
                                                objectName: "maskSetThumbnail"
                                                Layout.preferredWidth: 44
                                                Layout.preferredHeight: 30
                                                radius: 5
                                                color: "#090c0f"
                                                border.width: 1
                                                border.color: Qt.alpha(modelData.sources.length > 0
                                                                       ? root.maskAccent(modelData.sources[0])
                                                                       : "#8ab8d8", 0.44)
                                                clip: true

                                                Image {
                                                    anchors.fill: parent
                                                    anchors.margins: 1
                                                    source: modelData.thumbnailSource || ""
                                                    fillMode: Image.PreserveAspectCrop
                                                    asynchronous: true
                                                    cache: false
                                                    visible: source.toString().length > 0
                                                }

                                                Rectangle {
                                                    anchors.left: parent.left
                                                    anchors.bottom: parent.bottom
                                                    anchors.leftMargin: 4
                                                    anchors.bottomMargin: 4
                                                    width: 8
                                                    height: 8
                                                    radius: 4
                                                    color: modelData.sources.length > 0
                                                           ? root.maskAccent(modelData.sources[0])
                                                           : "#8ab8d8"
                                                    border.width: 1
                                                    border.color: "#0c1014"
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: modelData.name
                                                    color: "#f2f4f6"
                                                    font.family: "Avenir Next"
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                    maximumLineCount: 2
                                                    elide: Text.ElideNone
                                                }

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: modelData.sourceCount === 1
                                                          ? "1 component"
                                                          : modelData.sourceCount.toString() + " components"
                                                    color: "#8f98a2"
                                                    font.family: "Avenir Next"
                                                    font.pixelSize: 11
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 8
                                                    elide: Text.ElideNone
                                                }
                                            }
                                        }

                                        Label {
                                            objectName: "maskComponentsHeaderLabel"
                                            Layout.fillWidth: true
                                            text: "Components"
                                            color: "#aeb7c1"
                                            font.family: "Avenir Next"
                                            font.pixelSize: 10
                                            font.weight: Font.DemiBold
                                            fontSizeMode: Text.HorizontalFit
                                            minimumPixelSize: 8
                                            elide: Text.ElideNone
                                            visible: modelData.sourceCount > 0
                                        }

                                        Repeater {
                                            model: (modelData.sources || []).filter(maskSearchModel.sourceMatches)

                                            delegate: Rectangle {
                                                id: maskSourceDelegate
                                                objectName: "maskSourceFocusRow"
                                                required property var modelData

                                                Layout.fillWidth: true
                                                radius: 8
                                                color: modelData.flatIndex === editorPreview.activeMaskIndex
                                                       ? "#21272d"
                                                       : "#14181c"
                                                border.width: 1
                                                border.color: activeFocus
                                                              ? "#8ab8d8"
                                                              : (modelData.flatIndex === editorPreview.activeMaskIndex
                                                                 ? root.maskAccent(modelData)
                                                                 : "#242b31")
                                                implicitHeight: 44
                                                activeFocusOnTab: true
                                                Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                                Keys.onEnterPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                                Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                                Drag.active: maskSourceDragHandler.active
                                                Drag.keys: ["mask-source"]
                                                Drag.supportedActions: Qt.MoveAction
                                                Drag.mimeData: {
                                                    "application/x-photo-editor-mask-source-index": String(modelData.flatIndex)
                                                }

                                                DragHandler {
                                                    id: maskSourceDragHandler
                                                    objectName: "maskSourceDragHandler"
                                                    target: null
                                                    onActiveChanged: {
                                                        if (active)
                                                            editorPreview.activeMaskIndex = modelData.flatIndex
                                                    }
                                                }

                                                DropArea {
                                                    objectName: "maskSourceReorderDropArea"
                                                    anchors.fill: parent
                                                    keys: ["mask-source"]
                                                    onDropped: function(drop) {
                                                        const sourceIndex = Number(drop.getDataAsString("application/x-photo-editor-mask-source-index"))
                                                        if (Number.isFinite(sourceIndex) &&
                                                                editorPreview.moveMaskSource(sourceIndex, maskSetDelegate.modelData.id, index))
                                                            drop.acceptProposedAction()
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    onClicked: function(mouse) {
                                                        if (mouse.button === Qt.RightButton) {
                                                            editorPreview.activeMaskIndex = modelData.flatIndex
                                                            maskSourceContextMenu.popup()
                                                            return
                                                        }
                                                        editorPreview.activeMaskIndex =
                                                                (editorPreview.activeMaskIndex === modelData.flatIndex
                                                                 ? -1
                                                                 : modelData.flatIndex)
                                                    }
                                                }

                                                AppMenu {
                                                    id: maskSourceContextMenu
                                                    objectName: "maskSourceContextMenu"
                                                    accent: root.maskAccent(modelData)

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextSelectItem"
                                                        text: "Select"
                                                        iconName: "select"
                                                        accent: maskSourceContextMenu.accent
                                                        onTriggered: editorPreview.activeMaskIndex = modelData.flatIndex
                                                    }

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextRenameItem"
                                                        text: "Rename"
                                                        iconName: "edit"
                                                        accent: maskSourceContextMenu.accent
                                                        onTriggered: maskRowActions.rename(modelData.flatIndex)
                                                    }

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextEnableItem"
                                                        text: modelData.enabled ? "Disable" : "Enable"
                                                        iconName: modelData.enabled ? "hidden" : "visible"
                                                        accent: maskSourceContextMenu.accent
                                                        enabled: !modelData.locked
                                                        onTriggered: maskRowActions.toggleEnabled(modelData.flatIndex, modelData.enabled)
                                                    }

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextSoloItem"
                                                        text: modelData.solo ? "Clear Solo" : "Solo"
                                                        iconName: "star"
                                                        accent: maskSourceContextMenu.accent
                                                        enabled: !modelData.locked
                                                        onTriggered: maskRowActions.toggleSolo(modelData.flatIndex, modelData.solo)
                                                    }

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextLockItem"
                                                        text: modelData.locked ? "Unlock" : "Lock"
                                                        iconName: "lock"
                                                        accent: maskSourceContextMenu.accent
                                                        onTriggered: maskRowActions.toggleLocked(modelData.flatIndex, modelData.locked)
                                                    }

                                                    MenuSeparator {}

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextMoveUpItem"
                                                        text: "Move Up"
                                                        iconName: "move-up"
                                                        accent: maskSourceContextMenu.accent
                                                        enabled: modelData.flatIndex > 0
                                                        onTriggered: maskRowActions.move(modelData.flatIndex, -1)
                                                    }

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextMoveDownItem"
                                                        text: "Move Down"
                                                        iconName: "move-down"
                                                        accent: maskSourceContextMenu.accent
                                                        enabled: modelData.flatIndex < editorPreview.maskCount - 1
                                                        onTriggered: maskRowActions.move(modelData.flatIndex, 1)
                                                    }

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextDuplicateItem"
                                                        text: "Duplicate"
                                                        iconName: "plus"
                                                        accent: maskSourceContextMenu.accent
                                                        onTriggered: maskRowActions.duplicate(modelData.flatIndex)
                                                    }

                                                    AppMenuItem {
                                                        objectName: "maskSourceContextDeleteItem"
                                                        text: "Delete"
                                                        iconName: "delete"
                                                        danger: true
                                                        accent: maskSourceContextMenu.accent
                                                        onTriggered: maskRowActions.remove(modelData.flatIndex)
                                                    }
                                                }

                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    spacing: 8

                                                    Rectangle {
                                                        objectName: "maskSourceThumbnail"
                                                        Layout.preferredWidth: 34
                                                        Layout.preferredHeight: 24
                                                        radius: 5
                                                        color: "#090c0f"
                                                        border.width: 1
                                                        border.color: Qt.alpha(root.maskAccent(modelData), 0.42)
                                                        clip: true

                                                        Image {
                                                            anchors.fill: parent
                                                            anchors.margins: 1
                                                            source: modelData.thumbnailSource || ""
                                                            fillMode: Image.PreserveAspectCrop
                                                            asynchronous: true
                                                            cache: false
                                                            visible: source.toString().length > 0
                                                        }

                                                        Rectangle {
                                                            anchors.left: parent.left
                                                            anchors.bottom: parent.bottom
                                                            anchors.leftMargin: 3
                                                            anchors.bottomMargin: 3
                                                            width: 7
                                                            height: 7
                                                            radius: 4
                                                            color: root.maskAccent(modelData)
                                                            border.width: 1
                                                            border.color: "#0c1014"
                                                        }
                                                    }

                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 1

                                                        Label {
                                                            Layout.fillWidth: true
                                                            text: modelData.name
                                                            color: "#f2f4f6"
                                                            font.family: "Avenir Next"
                                                            font.pixelSize: 12
                                                            font.weight: Font.Medium
                                                            fontSizeMode: Text.HorizontalFit
                                                            minimumPixelSize: 8
                                                            elide: Text.ElideNone
                                                        }

                                                        Label {
                                                            Layout.fillWidth: true
                                                            text: modelData.kind + " · " + modelData.operation
                                                            color: "#8f98a2"
                                                            font.family: "Avenir Next"
                                                            font.pixelSize: 10
                                                            fontSizeMode: Text.HorizontalFit
                                                            minimumPixelSize: 7
                                                            elide: Text.ElideNone
                                                        }
                                                    }

                                                    ActionButton {
                                                        text: modelData.enabled ? "On" : "Off"
                                                        active: modelData.enabled
                                                        onClicked: maskRowActions.toggleEnabled(modelData.flatIndex, modelData.enabled)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                objectName: "maskEmptyStateGuidance"
                                visible: editorPreview.maskSetCount === 0
                                text: "No masks yet. Create a mask with Smart Select, Brush, Lasso, Polygon, a range control, or Alpha Image."
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
                                objectName: "activeMaskDuplicateButton"
                                Layout.fillWidth: true
                                text: "Duplicate"
                                onClicked: editorPreview.duplicateActiveMask()
                            }

                            ConfirmActionButton {
                                objectName: "activeMaskDeleteButton"
                                Layout.fillWidth: true
                                text: "Delete"
                                helperText: "Remove this selection recipe entry; the original file is unchanged."
                                onConfirmed: editorPreview.removeActiveMask()
                            }
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Retouch"
                        subtitle: "Heal, clone, skin, red-eye, dust, and remove live outside selection sets."

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: editorPreview.retouchStack

                                delegate: Rectangle {
                                    objectName: "retouchFocusRow"
                                    required property var modelData

                                    Layout.fillWidth: true
                                    radius: 10
                                    color: modelData.flatIndex === editorPreview.activeMaskIndex ? "#21272d" : "#171b1f"
                                    border.width: 1
                                    border.color: activeFocus
                                                  ? "#8ab8d8"
                                                  : (modelData.flatIndex === editorPreview.activeMaskIndex
                                                     ? root.maskAccent(modelData)
                                                     : "#2d3339")
                                    implicitHeight: 50
                                    activeFocusOnTab: true
                                    Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                    Keys.onEnterPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                    Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                editorPreview.activeMaskIndex = modelData.flatIndex
                                                retouchRowContextMenu.popup()
                                                return
                                            }
                                            editorPreview.activeMaskIndex =
                                                    (editorPreview.activeMaskIndex === modelData.flatIndex
                                                     ? -1
                                                     : modelData.flatIndex)
                                        }
                                    }

                                    AppMenu {
                                        id: retouchRowContextMenu
                                        objectName: "retouchRowContextMenu"
                                        accent: root.maskAccent(modelData)

                                        AppMenuItem {
                                            objectName: "retouchRowContextSelectItem"
                                            text: "Select"
                                            iconName: "select"
                                            accent: retouchRowContextMenu.accent
                                            onTriggered: editorPreview.activeMaskIndex = modelData.flatIndex
                                        }

                                        AppMenuItem {
                                            objectName: "retouchRowContextApplyItem"
                                            text: "Apply"
                                            iconName: "check"
                                            accent: retouchRowContextMenu.accent
                                            visible: !modelData.committed
                                            onTriggered: maskRowActions.applyRetouch(modelData.flatIndex)
                                        }

                                        AppMenuItem {
                                            objectName: "retouchRowContextCancelItem"
                                            text: "Cancel"
                                            iconName: "close"
                                            accent: retouchRowContextMenu.accent
                                            visible: !modelData.committed
                                            onTriggered: maskRowActions.cancelRetouch(modelData.flatIndex)
                                        }

                                        AppMenuItem {
                                            objectName: "retouchRowContextRenameItem"
                                            text: "Rename"
                                            iconName: "edit"
                                            accent: retouchRowContextMenu.accent
                                            onTriggered: maskRowActions.rename(modelData.flatIndex)
                                        }

                                        AppMenuItem {
                                            objectName: "retouchRowContextEnableItem"
                                            text: modelData.enabled ? "Disable" : "Enable"
                                            iconName: modelData.enabled ? "hidden" : "visible"
                                            accent: retouchRowContextMenu.accent
                                            onTriggered: maskRowActions.toggleEnabled(modelData.flatIndex, modelData.enabled)
                                        }

                                        MenuSeparator {}

                                        AppMenuItem {
                                            objectName: "retouchRowContextMoveUpItem"
                                            text: "Move Up"
                                            iconName: "move-up"
                                            accent: retouchRowContextMenu.accent
                                            enabled: modelData.flatIndex > 0
                                            onTriggered: maskRowActions.move(modelData.flatIndex, -1)
                                        }

                                        AppMenuItem {
                                            objectName: "retouchRowContextMoveDownItem"
                                            text: "Move Down"
                                            iconName: "move-down"
                                            accent: retouchRowContextMenu.accent
                                            enabled: modelData.flatIndex < editorPreview.maskCount - 1
                                            onTriggered: maskRowActions.move(modelData.flatIndex, 1)
                                        }

                                        AppMenuItem {
                                            objectName: "retouchRowContextDeleteItem"
                                            text: "Delete"
                                            iconName: "delete"
                                            danger: true
                                            accent: retouchRowContextMenu.accent
                                            onTriggered: maskRowActions.remove(modelData.flatIndex)
                                        }
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 8

                                        Rectangle {
                                            Layout.preferredWidth: 10
                                            Layout.preferredHeight: 10
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
                                                onClicked: maskRowActions.rename(modelData.flatIndex)
                                            }

                                            ActionButton {
                                                text: modelData.enabled ? "Off" : "On"
                                                compact: true
                                                accent: modelData.enabled ? "#d98962" : root.maskAccent(modelData)
                                                onClicked: maskRowActions.toggleEnabled(modelData.flatIndex, modelData.enabled)
                                            }

                                            ActionButton {
                                                text: "Up"
                                                compact: true
                                                enabled: modelData.flatIndex > 0
                                                onClicked: maskRowActions.move(modelData.flatIndex, -1)
                                            }

                                            ActionButton {
                                                text: "Down"
                                                compact: true
                                                enabled: modelData.flatIndex < editorPreview.maskCount - 1
                                                onClicked: maskRowActions.move(modelData.flatIndex, 1)
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

                            RetouchBeforeAfterControls {
                                Layout.fillWidth: true
                                previewController: editorPreview
                            }
                        }
                    }

                    InspectorSection {
                        objectName: "activeMaskControlsSection"
                        Layout.fillWidth: true
                        title: editorPreview.activeMaskToolRole === "retouch" ? "Active Retouch" : "Active Selection"
                        visible: root.maskActiveControlsContextVisible()

                        Rectangle {
                            objectName: "activeMaskCompactHeader"
                            Layout.fillWidth: true
                            implicitHeight: activeMaskCompactHeaderColumn.implicitHeight + 16
                            radius: 8
                            color: "#11161a"
                            border.width: 1
                            border.color: root.activeMaskAccent

                            ColumnLayout {
                                id: activeMaskCompactHeaderColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Rectangle {
                                        Layout.preferredWidth: 10
                                        Layout.preferredHeight: 10
                                        radius: 5
                                        color: root.activeMaskAccent
                                    }

                                    Label {
                                        objectName: "activeMaskHeaderNameLabel"
                                        Layout.fillWidth: true
                                        text: editorPreview.activeMaskName
                                        color: "#f2f4f6"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        maximumLineCount: 2
                                        elide: Text.ElideNone
                                    }

                                    Label {
                                        objectName: "activeMaskHeaderOpacityLabel"
                                        text: "Opacity " + root.percentText(editorPreview.activeMaskOpacity)
                                        color: "#aeb7c1"
                                        font.family: "Menlo"
                                        font.pixelSize: 10
                                        font.letterSpacing: 0
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 8
                                    rowSpacing: 6

                                    Label {
                                        objectName: "activeMaskHeaderKindLabel"
                                        Layout.fillWidth: true
                                        text: "Kind · " + root.activeMaskHeaderKind()
                                        color: "#8f98a2"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 10
                                        fontSizeMode: Text.HorizontalFit
                                        minimumPixelSize: 8
                                        elide: Text.ElideNone
                                    }

                                    Label {
                                        objectName: "activeMaskHeaderOperationLabel"
                                        Layout.fillWidth: true
                                        text: "Operation · " + editorPreview.activeMaskOperation
                                        color: "#8f98a2"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 10
                                        fontSizeMode: Text.HorizontalFit
                                        minimumPixelSize: 8
                                        elide: Text.ElideNone
                                    }

                                    Label {
                                        objectName: "activeMaskHeaderTargetLabel"
                                        Layout.fillWidth: true
                                        text: "Target · " + root.activeMaskHeaderTarget()
                                        color: "#8f98a2"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 10
                                        fontSizeMode: Text.HorizontalFit
                                        minimumPixelSize: 8
                                        elide: Text.ElideNone
                                    }

                                    Label {
                                        objectName: "activeMaskHeaderSignature"
                                        visible: false
                                        text: root.activeMaskCompactHeaderSignature()
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    ActionButton {
                                        objectName: "activeMaskHeaderEnabledButton"
                                        Layout.fillWidth: true
                                        text: editorPreview.activeMaskEnabled ? "Enabled" : "Disabled"
                                        compact: true
                                        active: editorPreview.activeMaskEnabled
                                        enabled: !editorPreview.activeMaskLocked
                                        onClicked: editorPreview.activeMaskEnabled = !editorPreview.activeMaskEnabled
                                    }

                                    ActionButton {
                                        objectName: "activeMaskHeaderSoloButton"
                                        Layout.fillWidth: true
                                        text: editorPreview.activeMaskSolo ? "Soloed" : "Solo"
                                        compact: true
                                        active: editorPreview.activeMaskSolo
                                        enabled: !editorPreview.activeMaskLocked
                                        onClicked: editorPreview.activeMaskSolo = !editorPreview.activeMaskSolo
                                    }

                                    ActionButton {
                                        objectName: "activeMaskHeaderLockButton"
                                        Layout.fillWidth: true
                                        text: editorPreview.activeMaskLocked ? "Locked" : "Unlocked"
                                        compact: true
                                        active: editorPreview.activeMaskLocked
                                        accent: editorPreview.activeMaskLocked ? "#d98962" : root.activeMaskAccent
                                        onClicked: editorPreview.activeMaskLocked = !editorPreview.activeMaskLocked
                                    }
                                }
                            }
                        }

                        Label {
                            objectName: "activeMaskDensityOpacityHelp"
                            Layout.fillWidth: true
                            text: "Density sets maximum mask strength. Opacity controls blend amount."
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }

                        InspectorSlider {
                            objectName: "activeMaskOpacitySlider"
                            Layout.fillWidth: true
                            readonly property string parameterRole: "mask-opacity"
                            label: "Mask Opacity"
                            valueText: root.percentText(editorPreview.activeMaskOpacity)
                            from: 0
                            to: 100
                            stepSize: 1
                            defaultValue: 100
                            value: Math.round(editorPreview.activeMaskOpacity * 100)
                            accent: root.activeMaskAccent
                            onValueDragged: function(value) { editorPreview.activeMaskOpacity = value / 100 }
                            onResetRequested: editorPreview.activeMaskOpacity = 1.0
                        }

                        InspectorSlider {
                            objectName: "activeMaskDensitySlider"
                            Layout.fillWidth: true
                            readonly property string parameterRole: "mask-density"
                            label: "Mask Density"
                            valueText: root.percentText(editorPreview.activeMaskDensity)
                            from: 0
                            to: 100
                            stepSize: 1
                            defaultValue: 100
                            value: Math.round(editorPreview.activeMaskDensity * 100)
                            accent: root.activeMaskAccent
                            onValueDragged: function(value) { editorPreview.activeMaskDensity = value / 100 }
                            onResetRequested: editorPreview.activeMaskDensity = 1.0
                        }

                        ToneTextField {
                            id: activeMaskNameField
                            objectName: "activeMaskRenameField"
                            Layout.fillWidth: true
                            text: editorPreview.activeMaskName
                            placeholderText: editorPreview.activeMaskToolRole === "retouch" ? "Retouch name" : "Selection name"
                            onEditingFinished: editorPreview.activeMaskName = text
                        }

                        ToneTextField {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskToolRole === "retouch" &&
                                     (editorPreview.activeMaskRetouchMode === "remove" ||
                                      editorPreview.activeMaskRetouchMode === "heal" ||
                                      editorPreview.activeMaskRetouchMode === "patch")
                            text: editorPreview.inpaintPrompt
                            placeholderText: "Texture hint (smooth or textured); fills from nearby pixels"
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
                                tooltipText: "Regenerate the retouch patch candidate in the recipe; the original file is unchanged."
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
                                     editorPreview.inpaintPatchDiagnosticsAvailable &&
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
                                objectName: "activeMaskOperationAddButton"
                                Layout.fillWidth: true
                                text: "Add"
                                active: editorPreview.activeMaskOperation === "add"
                                onClicked: editorPreview.activeMaskOperation = "add"
                            }

                            ActionButton {
                                objectName: "activeMaskOperationSubtractButton"
                                Layout.fillWidth: true
                                text: "Subtract"
                                active: editorPreview.activeMaskOperation === "subtract"
                                onClicked: editorPreview.activeMaskOperation = "subtract"
                            }

                            ActionButton {
                                objectName: "activeMaskOperationIntersectButton"
                                Layout.fillWidth: true
                                text: "Intersect"
                                active: editorPreview.activeMaskOperation === "intersect"
                                onClicked: editorPreview.activeMaskOperation = "intersect"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: editorPreview.activeMaskToolRole !== "retouch" &&
                                     editorPreview.activeMaskKind !== "brush"

                            ActionButton {
                                objectName: "activeMaskOperationExcludeButton"
                                Layout.fillWidth: true
                                text: "Exclude"
                                active: editorPreview.activeMaskOperation === "exclude"
                                onClicked: editorPreview.activeMaskOperation = "exclude"
                            }

                            ActionButton {
                                objectName: "activeMaskOperationReplaceButton"
                                Layout.fillWidth: true
                                text: "Replace"
                                active: editorPreview.activeMaskOperation === "replace"
                                onClicked: editorPreview.activeMaskOperation = "replace"
                            }

                            ActionButton {
                                objectName: "activeMaskOperationUnionButton"
                                Layout.fillWidth: true
                                text: "Union"
                                active: editorPreview.activeMaskOperation === "union"
                                onClicked: editorPreview.activeMaskOperation = "union"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                objectName: "activeMaskHideButton"
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskEnabled ? "Hide" : "Show"
                                active: editorPreview.activeMaskEnabled
                                enabled: !editorPreview.activeMaskLocked
                                onClicked: editorPreview.activeMaskEnabled = !editorPreview.activeMaskEnabled
                            }

                            ActionButton {
                                objectName: "activeMaskLockButton"
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskLocked ? "Unlock" : "Lock"
                                active: editorPreview.activeMaskLocked
                                accent: editorPreview.activeMaskLocked ? "#d98962" : root.activeMaskAccent
                                onClicked: editorPreview.activeMaskLocked = !editorPreview.activeMaskLocked
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
                        visible: root.maskContextVisible("refine") && root.activeMaskSupportsSelectionRefine()

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8

                            ActionButton {
                                objectName: "activeMaskInvertButton"
                                Layout.fillWidth: true
                                text: "Invert"
                                iconName: "invert"
                                active: editorPreview.activeMaskSelectionInverted
                                onClicked: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Reset Edge"
                                tooltipText: "Reset matte refinement while preserving the source geometry and local adjustments."
                                onClicked: editorPreview.resetActiveMaskRefinement()
                            }

                            ActionButton {
                                objectName: "maskViewShowOriginalRefinementButton"
                                Layout.fillWidth: true
                                text: "Show Original"
                                iconName: "compare"
                                active: editorPreview.maskRefinementPreviewMode === "original"
                                tooltipText: "Preview this selection before edge refinement without changing the recipe."
                                onClicked: editorPreview.maskRefinementPreviewMode = "original"
                            }

                            ActionButton {
                                objectName: "maskViewShowRefinedRefinementButton"
                                Layout.fillWidth: true
                                text: "Show Refined"
                                iconName: "target"
                                active: editorPreview.maskRefinementPreviewMode !== "original"
                                tooltipText: "Preview this selection with current edge refinement."
                                onClicked: editorPreview.maskRefinementPreviewMode = "refined"
                            }

                            ActionButton {
                                objectName: "maskViewHighQualityRefinementPreviewButton"
                                Layout.fillWidth: true
                                text: "High Quality"
                                iconName: "sparkles"
                                active: editorPreview.maskRefinementHighQualityPreview
                                tooltipText: "Use a higher-resolution selection refinement preview for expensive edge checks."
                                onClicked: editorPreview.maskRefinementHighQualityPreview = !editorPreview.maskRefinementHighQualityPreview
                            }

                            ActionButton {
                                objectName: "maskViewRefinementEdgePreviewButton"
                                visible: false
                                Layout.fillWidth: true
                                text: "Refine Edge"
                                iconName: "focus-frame"
                                active: false
                                enabled: false
                                tooltipText: "Show only the matte pixels changed by current refinement controls."
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
                                  : "Pick with " + root.smartSelectionRuntimeLabel() + " or draw a shape, then use Refine +/- or quick edge actions before touching adjustments."
                            wrapMode: Text.WordWrap
                            color: "#8f98a2"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: root.activeRefineEffectCount() > 0
                               ? "Fine Tune · " + root.activeRefineEffectCount().toString() + " active"
                               : "Fine Tune"
                        subtitle: root.activeRefineEffectCount() > 0
                                  ? root.activeRefineEffectCount().toString() + " advanced refine changes active."
                                  : "Advanced refinement is collapsed until needed."
                        visible: root.maskContextVisible("refine") && root.activeMaskSupportsSelectionRefine()
                        expandable: true
                        expanded: false

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Shift Edge"
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
                            label: "Contrast Edge"
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
                        objectName: "activeMaskAdjustmentSection"
                        Layout.fillWidth: true
                        title: "Adjustment"
                        subtitle: root.activeMaskHasAdjustments()
                                  ? "The active selection is modifying tone, color, or removal."
                                  : "Collapsed until the active selection actually needs image adjustments."
                        expandable: true
                        expanded: root.activeMaskHasAdjustments()
                        visible: root.maskContextVisible("refine") && editorPreview.hasActiveMask

                        RowLayout {
                            objectName: "maskAdjustToneGroup"
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: "Tone"
                                color: "#aeb7c1"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            ActionButton {
                                objectName: "maskAdjustToneBeforeAfterButton"
                                compact: true
                                showText: false
                                iconName: root.maskCompareGroupActive("tone") ? "hidden" : "visible"
                                active: root.maskCompareGroupActive("tone")
                                enabled: editorPreview.hasActiveMask
                                tooltipText: root.maskCompareGroupActive("tone")
                                             ? "Restore local Tone in before/after."
                                             : "Compare before and after for local Tone without changing values."
                                onClicked: root.toggleMaskCompareGroup("tone")
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Exposure"
                            valueText: editorPreview.activeMaskExposure.toFixed(2) + " EV"
                            from: -5
                            to: 5
                            precisionResponse: true
                            stepSize: 0.01
                            value: editorPreview.activeMaskExposure
                            onInteractionStarted: editorPreview.beginCoalescedEdit()
                            onInteractionFinished: editorPreview.endCoalescedEdit()
                            onValueDragged: function(value) { editorPreview.activeMaskExposure = value }
                            onResetRequested: editorPreview.activeMaskExposure = 0
                        }

                        RowLayout {
                            objectName: "maskAdjustColorGroup"
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: "Color"
                                color: "#aeb7c1"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            ActionButton {
                                objectName: "maskAdjustColorBeforeAfterButton"
                                compact: true
                                showText: false
                                iconName: root.maskCompareGroupActive("color") ? "hidden" : "visible"
                                active: root.maskCompareGroupActive("color")
                                enabled: editorPreview.hasActiveMask
                                tooltipText: root.maskCompareGroupActive("color")
                                             ? "Restore local Color in before/after."
                                             : "Compare before and after for local Color without changing values."
                                onClicked: root.toggleMaskCompareGroup("color")
                            }
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

                        RowLayout {
                            objectName: "maskAdjustPresenceGroup"
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: "Presence"
                                color: "#aeb7c1"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            ActionButton {
                                objectName: "maskAdjustPresenceBeforeAfterButton"
                                compact: true
                                showText: false
                                iconName: root.maskCompareGroupActive("presence") ? "hidden" : "visible"
                                active: root.maskCompareGroupActive("presence")
                                enabled: editorPreview.hasActiveMask
                                tooltipText: root.maskCompareGroupActive("presence")
                                             ? "Restore local Presence in before/after."
                                             : "Compare before and after for local Presence without changing values."
                                onClicked: root.toggleMaskCompareGroup("presence")
                            }
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

                        RowLayout {
                            objectName: "maskAdjustEffectsGroup"
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: "Effects"
                                color: "#aeb7c1"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            ActionButton {
                                objectName: "maskAdjustEffectsBeforeAfterButton"
                                compact: true
                                showText: false
                                iconName: root.maskCompareGroupActive("effects") ? "hidden" : "visible"
                                active: root.maskCompareGroupActive("effects")
                                enabled: editorPreview.hasActiveMask
                                tooltipText: root.maskCompareGroupActive("effects")
                                             ? "Restore local Effects in before/after."
                                             : "Compare before and after for local Effects without changing values."
                                onClicked: root.toggleMaskCompareGroup("effects")
                            }
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

                        RowLayout {
                            objectName: "maskAdjustDetailGroup"
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskRetouchMode === "heal"
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: "Detail"
                                color: "#aeb7c1"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            ActionButton {
                                objectName: "maskAdjustDetailBeforeAfterButton"
                                compact: true
                                showText: false
                                iconName: root.maskCompareGroupActive("detail") ? "hidden" : "visible"
                                active: root.maskCompareGroupActive("detail")
                                enabled: editorPreview.hasActiveMask
                                tooltipText: root.maskCompareGroupActive("detail")
                                             ? "Restore local Detail in before/after."
                                             : "Compare before and after for local Detail without changing values."
                                onClicked: root.toggleMaskCompareGroup("detail")
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskRetouchMode === "heal"
                            label: "Heal Texture"
                            valueText: Math.round(editorPreview.activeMaskHealTextureBlend * 100).toString() + "%"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskHealTextureBlend
                            onValueDragged: function(value) { editorPreview.activeMaskHealTextureBlend = value }
                            onResetRequested: editorPreview.activeMaskHealTextureBlend = 0.68
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskRetouchMode === "heal"
                            label: "Heal Color"
                            valueText: Math.round(editorPreview.activeMaskHealColorBlend * 100).toString() + "%"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: editorPreview.activeMaskHealColorBlend
                            onValueDragged: function(value) { editorPreview.activeMaskHealColorBlend = value }
                            onResetRequested: editorPreview.activeMaskHealColorBlend = 0.46
                        }
                    }

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Shape"
                        visible: root.maskContextVisible("refine") &&
                                 editorPreview.hasActiveMask &&
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
                        visible: root.maskContextVisible("brush")

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
                            columns: 3
                            columnSpacing: 8
                            rowSpacing: 8
                            visible: editorPreview.activeMaskKind === "brush"

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Save Custom"
                                compact: true
                                onClicked: editorPreview.saveActiveMaskBrushPreset("Custom Brush", true)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Delete Custom"
                                compact: true
                                enabled: editorPreview.isCustomBrushPreset(editorPreview.activeMaskBrushPreset)
                                onClicked: editorPreview.deleteCustomBrushPreset(editorPreview.activeMaskBrushPreset)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Reset Custom"
                                compact: true
                                onClicked: editorPreview.resetCustomBrushPresets()
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
                                text: editorPreview.activeMaskCloneAligned ? "Aligned" : "Non-aligned"
                                iconName: "compare"
                                active: editorPreview.activeMaskCloneAligned
                                onClicked: editorPreview.activeMaskCloneAligned = !editorPreview.activeMaskCloneAligned
                            }

                            Label {
                                Layout.fillWidth: true
                                text: editorPreview.activeMaskCloneScale.toFixed(2) + "x / " +
                                      Math.round(editorPreview.activeMaskCloneRotation).toString() + "°"
                                horizontalAlignment: Text.AlignRight
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                            }

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
                            readonly property string parameterRole: "brush-flow"
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
                            valueText: Math.round(editorPreview.activeMaskBrushSpacing * 100).toString() + "% of diameter"
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

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Pressure Size"
                            valueText: Math.round(editorPreview.activeMaskBrushPressureSize * 100).toString() + "%"
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushPressureSize
                            onValueDragged: function(value) { editorPreview.activeMaskBrushPressureSize = value }
                            onResetRequested: editorPreview.activeMaskBrushPressureSize = 0.65
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Pressure Flow"
                            valueText: Math.round(editorPreview.activeMaskBrushPressureFlow * 100).toString() + "%"
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushPressureFlow
                            onValueDragged: function(value) { editorPreview.activeMaskBrushPressureFlow = value }
                            onResetRequested: editorPreview.activeMaskBrushPressureFlow = 1.0
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Pressure Opacity"
                            valueText: Math.round(editorPreview.activeMaskBrushPressureOpacity * 100).toString() + "%"
                            from: 0.0
                            to: 1.0
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushPressureOpacity
                            onValueDragged: function(value) { editorPreview.activeMaskBrushPressureOpacity = value }
                            onResetRequested: editorPreview.activeMaskBrushPressureOpacity = 0.0
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
                            text: editorPreview.activeMaskBrushAutoMask ? "Edge Aware" : "Freehand"
                            active: editorPreview.activeMaskBrushAutoMask
                            compact: true
                            onClicked: editorPreview.activeMaskBrushAutoMask = !editorPreview.activeMaskBrushAutoMask
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskBrushAutoMask
                            label: "Edge-Aware Range"
                            valueText: Math.round(editorPreview.activeMaskBrushAutoMaskTolerance * 100).toString() + "%"
                            from: 0.02
                            to: 0.75
                            stepSize: 0.01
                            value: editorPreview.activeMaskBrushAutoMaskTolerance
                            onValueDragged: function(value) { editorPreview.activeMaskBrushAutoMaskTolerance = value }
                            onResetRequested: editorPreview.activeMaskBrushAutoMaskTolerance = 0.18
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskBrushAutoMask
                            text: editorPreview.activeMaskBrushAutoMaskContinuousSampling ? "Sample Each Dab" : "Sample First Dab"
                            active: editorPreview.activeMaskBrushAutoMaskContinuousSampling
                            compact: true
                            tooltipText: editorPreview.activeMaskBrushAutoMaskContinuousSampling
                                         ? "Update the protected color from every dab center during the stroke."
                                         : "Lock the protected color to the first dab in the stroke."
                            onClicked: editorPreview.activeMaskBrushAutoMaskContinuousSampling =
                                       !editorPreview.activeMaskBrushAutoMaskContinuousSampling
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            visible: editorPreview.activeMaskBrushAutoMask
                            text: "Protect Edges"
                            active: editorPreview.activeMaskBrushProtectEdges
                            compact: true
                            tooltipText: "Tighten edge gating for hair, sky, and building boundaries."
                            onClicked: editorPreview.activeMaskBrushProtectEdges =
                                       !editorPreview.activeMaskBrushProtectEdges
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
                        visible: root.maskContextVisible("polygon")

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
                        visible: root.maskContextVisible("refine") && editorPreview.activeMaskKind === "luminance"

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
                        visible: root.maskContextVisible("refine") && editorPreview.activeMaskKind === "color"

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
                        title: "Generated Refine"
                        visible: root.maskContextVisible("refine") && root.isAiMaskKind(editorPreview.activeMaskKind)

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
                                  ? "Object masks are created with " + root.smartSelectionRuntimeLabel() + " in the preview."
                                  : root.aiMaskLabel(editorPreview.activeMaskKind) + " masks are generated from the source image and stay editable as part of the mask stack."
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
