pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml
import "../components"

Rectangle {
    id: root
    objectName: "carouselWorkspace"
    color: "transparent"
    focus: true

    Theme { id: theme }

    readonly property string carouselVisibilityContract: "carousel-visibility:v1|always:tool-rail-composer-header-artboard-board-strip-inspector|contextual:setup-recent-carousel-media-picker-selection-rating-layer-controls-empty-slot-guides|one-expansion:tool-variants-layer-add-export-actions-object-actions-board-actions|hover-focus:canvas-selection-handles-layer-row-actions-board-thumbs|diagnostic:export-busy-snap-guides-media-offline|preserve:functionality"
    readonly property string carouselSurfaceContract: "carousel-surfaces:v1|setup:create-and-recent|composer:dominant-artboard|tools:rail-with-menus|board-strip:stable|inspector:board-or-selection|layers:object-stack|media-picker:contextual|no-elide:true|no-overlap:true"
    readonly property string carouselPlaceholderLayoutContract: "carousel-placeholder-layout:v1|slot-label-zone:reserved|slot-action-zone:separate|slot-min-gap:8|tiny:icon-only|gallery-empty-card:bounded|no-elide:true|no-overlap:true"
    readonly property string carouselWorkflowPolishContract: "carousel-workflow-polish:v1|first-glance:setup-or-board-canvas-tool-rail-inspector-strip|close-inspection:active-tool-board-selection-media-layer-rating-export|clutter:one-active-selection-context-media-picker-contextual|interaction:create-select-pan-text-media-shape-guide-layer-export|text:no-elide-no-overlap|functionality:preserved"
    readonly property string carouselPageCompletionContract: "page-completion:v1|carousel|rail:select-pan-text-media-shape-guides|content:setup-or-artboard-empty-slot-board-strip|inspector:board-selection-layers-media|toolbar:save-export-board-actions|states:setup-empty-board-selected-media-exporting-error"
    readonly property string carouselSlotSelectorContract: "carousel-slot-selector:v1|split:left-right|stack:top-middle-bottom|full-labels:true|no-abbreviations:true|no-elide:true"

    readonly property var selection: carouselDocument.selectionDetails
    property var mediaPickerAssets: []
    property string mediaPickerProjectName: ""
    property string mediaPickerScope: "all"
    property string mediaPickerIntent: "slot"
    property string activeTool: "select"
    property real composerZoom: 1.0
    readonly property var textColorPresets: [
        "#1e1a17", "#ffffff", "#f5f0e8", "#3c3632",
        "#8b6548", "#c49a6c", "#d45a62", "#dd8a39",
        "#d7bd42", "#61b56f", "#4e86d9", "#8b6be3"
    ]
    readonly property var backgroundColorPresets: [
        "#fbf7f1", "#ffffff", "#f5f0e8", "#101418",
        "#1e1a17", "#36f2a6", "#69cad8", "#f0b35d",
        "#ff453a", "#0a84ff", "#8b6be3", "#d7bd42"
    ]
    readonly property var backgroundGradientButtons: [
        { label: "Solid", value: "solid" },
        { label: "H Grad", value: "linear-horizontal" },
        { label: "V Grad", value: "linear-vertical" }
    ]
    readonly property var shapeFillKindButtons: [
        { label: "Solid", value: "solid" },
        { label: "H Grad", value: "linear-horizontal" },
        { label: "V Grad", value: "linear-vertical" }
    ]
    readonly property var textSizePresets: [
        { label: "S", value: 36 },
        { label: "M", value: 64 },
        { label: "L", value: 96 },
        { label: "XL", value: 144 },
        { label: "2XL", value: 200 }
    ]
    readonly property var textHorizontalAlignmentButtons: [
        { label: "Left", value: "left" },
        { label: "Center", value: "center" },
        { label: "Right", value: "right" }
    ]
    readonly property var textVerticalAlignmentButtons: [
        { label: "Top", value: "top" },
        { label: "Middle", value: "middle" },
        { label: "Bottom", value: "bottom" }
    ]
    readonly property var textWeightButtons: [
        { label: "Regular", value: "normal" },
        { label: "Demi", value: "demibold" },
        { label: "Bold", value: "bold" },
        { label: "Black", value: "black" }
    ]
    readonly property var textFontFamilyButtons: [
        { label: "Display", value: "display" },
        { label: "Serif", value: "serif" },
        { label: "Mono", value: "mono" },
        { label: "System", value: "system" }
    ]
    readonly property var shapeButtons: [
        { label: "Rect", value: "rectangle" },
        { label: "Oval", value: "ellipse" },
        { label: "Line", value: "line" },
        { label: "Arrow", value: "arrow" }
    ]
    readonly property var blendModeButtons: [
        { label: "Normal", value: "normal" },
        { label: "Multiply", value: "multiply" },
        { label: "Screen", value: "screen" },
        { label: "Overlay", value: "overlay" }
    ]
    readonly property var formatButtons: [
        { label: "4:5", value: "4x5", size: "1080 x 1350" },
        { label: "1:1", value: "1x1", size: "1080 x 1080" },
        { label: "9:16", value: "9x16", size: "1080 x 1920" },
        { label: "5:4", value: "5x4", size: "1350 x 1080" }
    ]
    property bool snappingEnabled: true
    property bool guidesEnabled: true
    property bool thirdsGuidesEnabled: false
    property bool safeGuidesEnabled: false
    property real guideOpacity: 0.5
    property color guideColor: "#5fc2d8"
    property real snapThresholdPixels: 12
    property var activeSnapGuides: []
    property var customGuides: []
    property int nextGuideId: 1
    property real globalDragOffsetX: 0
    property real globalDragOffsetY: 0
    property bool anyItemDragging: false
    property bool transformAspectLocked: true
    readonly property int quickLibraryLimit: 8

    property string setupCarouselName: ""
    property string setupFormat: "4x5"
    property int setupSlideCount: 1
    property var recentCarouselRecords: []
    readonly property bool compactWorkspace: width < 1500
    readonly property bool condensedHeader: width < 1180
    readonly property real stripAvailableWidth: Math.max(240, artboardFlick.width - 44)
    readonly property real stripAvailableHeight: Math.max(260, artboardFlick.height - 44)
    readonly property real stripFitScaleWidth: carouselDocument.documentPixelWidth > 0 ? root.stripAvailableWidth / carouselDocument.documentPixelWidth : 1
    readonly property real stripFitScaleHeight: carouselDocument.documentPixelHeight > 0 ? root.stripAvailableHeight / carouselDocument.documentPixelHeight : 1
    readonly property real baseStripScale: Math.max(0.14, Math.min(0.58, root.stripFitScaleWidth, root.stripFitScaleHeight))
    readonly property real stripScale: root.baseStripScale * root.composerZoom
    readonly property var currentSlideLayers: {
        const items = []
        const slide = root.slideGeometry(carouselDocument.currentIndex)
        for (let index = carouselDocument.canvasItems.length - 1; index >= 0; --index) {
            const item = carouselDocument.canvasItems[index]
            if (root.itemIntersectsSlide(item, slide))
                items.push(item)
        }
        return items
    }
    readonly property var templateButtons: [
        { label: "Single", value: "single" },
        { label: "Split Pair", value: "split-pair" },
        { label: "Linked 2", value: "linked-horizontal-pair" },
        { label: "Stacked 3", value: "stacked-triptych" }
    ]
    readonly property var toolButtons: [
        { value: "select", label: "Select", key: "V", cursor: Qt.ArrowCursor },
        { value: "hand", label: "Pan", key: "H", cursor: Qt.OpenHandCursor },
        { value: "text", label: "Text", key: "T", cursor: Qt.IBeamCursor },
        { value: "media", label: "Media", key: "M", cursor: Qt.PointingHandCursor },
        { value: "shape", label: "Shape", key: "U", cursor: Qt.CrossCursor }
    ]

    function slideWidthOnStrip() {
        return carouselDocument.slideCount > 0 ? stripSurface.width / carouselDocument.slideCount : stripSurface.width
    }

    function clampZoom(value) {
        return Math.max(0.5, Math.min(3.0, value))
    }

    function textHorizontalAlignment(value) {
        const normalized = String(value || "center")
        if (normalized === "left")
            return Text.AlignLeft
        if (normalized === "right")
            return Text.AlignRight
        return Text.AlignHCenter
    }

    function textVerticalAlignment(value) {
        const normalized = String(value || "middle")
        if (normalized === "top")
            return Text.AlignTop
        if (normalized === "bottom")
            return Text.AlignBottom
        return Text.AlignVCenter
    }

    function textFontWeight(value) {
        const normalized = String(value || "demibold")
        if (normalized === "normal")
            return Font.Normal
        if (normalized === "bold")
            return Font.Bold
        if (normalized === "black")
            return Font.Black
        return Font.DemiBold
    }

    function textFontFamily(value) {
        const normalized = String(value || "display")
        if (normalized === "serif")
            return "Georgia"
        if (normalized === "mono")
            return theme.fontMono
        if (normalized === "system")
            return theme.fontUi
        return Qt.platform.os === "osx" ? "Avenir Next" : theme.fontDisplay
    }

    function clampedMediaLayerScale(value) {
        return Math.max(0.6, Math.min(3.5, Number(value || 1)))
    }

    function clampedMediaLayerOffset(value) {
        return Math.max(-1, Math.min(1, Number(value || 0)))
    }

    function layerMediaDrawRect(item, frameWidth, frameHeight) {
        const sourceWidth = Math.max(1, Number(item.sourceWidth || frameWidth || 1))
        const sourceHeight = Math.max(1, Number(item.sourceHeight || frameHeight || 1))
        const fitMode = String(item.fitMode || "cover")
        const baseScale = fitMode === "contain"
                ? Math.min(frameWidth / sourceWidth, frameHeight / sourceHeight)
                : Math.max(frameWidth / sourceWidth, frameHeight / sourceHeight)
        const drawWidth = Math.max(1, sourceWidth * baseScale * root.clampedMediaLayerScale(item.mediaScale))
        const drawHeight = Math.max(1, sourceHeight * baseScale * root.clampedMediaLayerScale(item.mediaScale))
        const freeX = frameWidth - drawWidth
        const freeY = frameHeight - drawHeight
        return {
            x: freeX * ((root.clampedMediaLayerOffset(item.mediaOffsetX) + 1) / 2),
            y: freeY * ((root.clampedMediaLayerOffset(item.mediaOffsetY) + 1) / 2),
            width: drawWidth,
            height: drawHeight
        }
    }

    function zoomIn() {
        composerZoom = clampZoom(composerZoom * 1.12)
    }

    function zoomOut() {
        composerZoom = clampZoom(composerZoom / 1.12)
    }

    function resetZoomToFit() {
        composerZoom = 1.0
    }

    function nudgeSelected(dx, dy) {
        if (!selection.hasSelection || !selection.hasUnlockedSelection)
            return
        carouselDocument.moveSelectedItemByPixels(dx, dy)
    }

    function resizeSelectedFromTransformField(axis, value) {
        if (!selection.hasSelection || selection.hasMultipleSelection || !selection.hasUnlockedSelection)
            return
        if (!Number.isFinite(value))
            return

        const currentWidth = Math.max(1, Number(selection.width || 1))
        const currentHeight = Math.max(1, Number(selection.height || 1))
        let nextWidth = currentWidth
        let nextHeight = currentHeight

        if (axis === "width") {
            nextWidth = Math.max(1, value)
            if (transformAspectLocked)
                nextHeight = Math.max(1, nextWidth / (currentWidth / currentHeight))
        } else {
            nextHeight = Math.max(1, value)
            if (transformAspectLocked)
                nextWidth = Math.max(1, nextHeight * (currentWidth / currentHeight))
        }

        carouselDocument.setSelectedItemSize(nextWidth, nextHeight)
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function slidePixelWidth() {
        return carouselDocument.slideCount > 0 ? carouselDocument.documentPixelWidth / carouselDocument.slideCount : carouselDocument.documentPixelWidth
    }

    function slideGeometry(index) {
        const width = slidePixelWidth()
        return {
            x: index * width,
            y: 0,
            width: width,
            height: carouselDocument.documentPixelHeight
        }
    }

    function itemIntersectsSlide(item, slide) {
        if (!item || !slide)
            return false
        const x = Number(item.x || 0)
        const y = Number(item.y || 0)
        const width = Number(item.width || 0)
        const height = Number(item.height || 0)
        return x < slide.x + slide.width
                && x + width > slide.x
                && y < slide.y + slide.height
                && y + height > slide.y
    }

    function hasMultiSelectModifier(modifiers) {
        return Boolean(modifiers & Qt.ControlModifier) || Boolean(modifiers & Qt.MetaModifier)
    }

    function focusLayerItem(itemId, alreadySelected) {
        if (alreadySelected && selection.selectedCount > 1)
            carouselDocument.addItemToSelection(itemId)
        else
            carouselDocument.selectItem(itemId)
    }

    function setActiveTool(toolName) {
        const normalized = toolName === "hand" || toolName === "text" || toolName === "media" || toolName === "shape" ? toolName : "select"
        activeTool = normalized
    }

    function toolCursorShape() {
        if (activeTool === "hand")
            return Qt.OpenHandCursor
        if (activeTool === "text")
            return Qt.IBeamCursor
        if (activeTool === "media")
            return Qt.PointingHandCursor
        if (activeTool === "shape")
            return Qt.CrossCursor
        return Qt.ArrowCursor
    }

    function handleCanvasToolPress(mouse, width, height) {
        const slideWidth = root.slideWidthOnStrip()
        const targetIndex = Math.max(0, Math.min(carouselDocument.slideCount - 1, Math.floor(mouse.x / Math.max(1, slideWidth))))
        const localX = mouse.x - (targetIndex * slideWidth)
        carouselDocument.selectSlide(targetIndex)
        chooseSlotForSlide(slideData(targetIndex), localX, mouse.y, width, height)

        if (activeTool === "text") {
            carouselDocument.addTextItem("New Text")
            carouselDocument.setSelectedItemCenter(mouse.x / root.stripScale, mouse.y / root.stripScale)
            root.setActiveTool("select")
            root.clearSnapGuides()
            return
        }

        if (activeTool === "media") {
            carouselDocument.clearSelection()
            root.clearSnapGuides()
            openMediaPicker("slot")
            root.setActiveTool("select")
            return
        }

        if (activeTool === "shape") {
            carouselDocument.addShapeItem("rectangle")
            carouselDocument.setSelectedItemCenter(mouse.x / root.stripScale, mouse.y / root.stripScale)
            root.setActiveTool("select")
            root.clearSnapGuides()
            return
        }

        carouselDocument.clearSelection()
        root.clearSnapGuides()
    }

    function carouselExportMenuItems() {
        return [
            {
                key: "save",
                label: "Save Document",
                icon: "save",
                enabled: true,
                tooltip: "Save the current carousel document."
            },
            {
                key: "export",
                label: "Export Carousel",
                icon: "export",
                enabled: !carouselDocument.exportBusy,
                disabledReason: "Export is already running.",
                tooltip: carouselDocument.exportBusy ? "Export is already running." : "Render this carousel with the current settings."
            }
        ]
    }

    function handleCarouselExportMenuAction(key) {
        if (key === "save") {
            carouselDocument.saveDocument()
            return
        }
        if (key === "export" && !carouselDocument.exportBusy)
            carouselDocument.exportCarousel()
    }

    function carouselLayerAddMenuItems() {
        return [
            {
                key: "photo",
                label: "Photo Layer",
                icon: "media",
                enabled: true,
                tooltip: "Add a photo layer from the library."
            },
            {
                key: "text",
                label: "Text Layer",
                icon: "menu",
                enabled: true,
                tooltip: "Add editable text to the current board."
            },
            {
                key: "shape",
                label: "Shape Layer",
                icon: "geometry",
                enabled: true,
                tooltip: "Add a rectangle, oval, line, or arrow layer."
            }
        ]
    }

    function handleCarouselLayerAddMenuAction(key) {
        if (key === "photo") {
            root.openMediaPicker("layer")
            return
        }
        if (key === "text") {
            carouselDocument.addTextItem("New Text")
            return
        }
        if (key === "shape")
            carouselDocument.addShapeItem("rectangle")
    }

    function carouselToolMenuItems(toolName) {
        const hasSelection = Boolean(selection.hasSelection)
        const hasUnlockedSelection = Boolean(selection.hasUnlockedSelection)
        const canReplaceMedia = hasSelection && !Boolean(selection.isText)
        const disabledSelectionReason = "Select an unlocked carousel object first."
        if (toolName === "select") {
            return [
                { key: "tool:select", label: "Select Tool", icon: "select", checked: root.activeTool === "select", tooltip: "Select, move, resize, and transform carousel objects." },
                { key: "duplicate", label: "Duplicate Selection", icon: "source", enabled: hasSelection, disabledReason: "Select an object before duplicating.", tooltip: hasSelection ? "Duplicate the selected object or objects." : "Select an object before duplicating." },
                { key: "bring-forward", label: "Bring Forward", icon: "move-forward", separatorBefore: true, enabled: hasUnlockedSelection, disabledReason: disabledSelectionReason, tooltip: hasUnlockedSelection ? "Move the selection one layer forward." : disabledSelectionReason },
                { key: "send-backward", label: "Send Backward", icon: "move-back", enabled: hasUnlockedSelection, disabledReason: disabledSelectionReason, tooltip: hasUnlockedSelection ? "Move the selection one layer backward." : disabledSelectionReason },
                { key: "bring-front", label: "Bring to Front", icon: "move-forward", enabled: hasUnlockedSelection, disabledReason: disabledSelectionReason, tooltip: hasUnlockedSelection ? "Move the selection above other layers." : disabledSelectionReason },
                { key: "send-back", label: "Send to Back", icon: "move-back", enabled: hasUnlockedSelection, disabledReason: disabledSelectionReason, tooltip: hasUnlockedSelection ? "Move the selection behind other layers." : disabledSelectionReason },
                { key: "delete", label: "Delete Selection", icon: "delete", destructive: true, separatorBefore: true, enabled: hasSelection, disabledReason: "Select an object before deleting.", tooltip: hasSelection ? "Delete the selected carousel object." : "Select an object before deleting." }
            ]
        }
        if (toolName === "hand") {
            return [
                { key: "tool:hand", label: "Pan Tool", icon: "menu", checked: root.activeTool === "hand", tooltip: "Drag the board area without editing objects." },
                { key: "zoom-in", label: "Zoom In", icon: "plus", tooltip: "Increase carousel board zoom." },
                { key: "zoom-out", label: "Zoom Out", icon: "minus", tooltip: "Decrease carousel board zoom." },
                { key: "zoom-fit", label: "Fit Board", icon: "fullscreen", checked: Math.abs(root.composerZoom - 1.0) < 0.01, tooltip: "Return the carousel board to fit zoom." }
            ]
        }
        if (toolName === "text") {
            return [
                { key: "tool:text", label: "Text Tool", icon: "edit", checked: root.activeTool === "text", tooltip: "Click the board to place text." },
                { key: "add-text", label: "Add Text Layer", icon: "plus", tooltip: "Add a text layer to the current board." },
                { key: "font-display", label: "Default Font: Display", icon: "menu", enabled: false, disabledReason: "Select a text layer before changing font defaults.", tooltip: "Text font customization is available after selecting a text layer." },
                { key: "font-system", label: "Default Font: System", icon: "menu", enabled: false, disabledReason: "Select a text layer before changing font defaults.", tooltip: "Select a text layer to adjust font family and weight." }
            ]
        }
        if (toolName === "media") {
            return [
                { key: "tool:media", label: "Media Tool", icon: "media", checked: root.activeTool === "media", tooltip: "Click a board slot to choose media." },
                { key: "add-media-layer", label: "Add Media Layer", icon: "plus", tooltip: "Add a photo or video layer from the library." },
                { key: "replace-media", label: "Replace Selected Media", icon: "media", enabled: canReplaceMedia, disabledReason: "Select a photo, video, or shape layer before replacing media.", tooltip: canReplaceMedia ? "Replace the selected photo or video layer." : "Select a photo, video, or shape layer before replacing media." },
                { key: "fit-cover", label: "Fit Mode: Cover", icon: "fullscreen", separatorBefore: true, checked: selection.fitMode === "cover", enabled: hasSelection && !Boolean(selection.isText), disabledReason: "Select a media object before changing fit mode.", tooltip: "Fill the selected frame with the media." },
                { key: "fit-contain", label: "Fit Mode: Contain", icon: "fullscreen", checked: selection.fitMode === "contain", enabled: hasSelection && !Boolean(selection.isText), disabledReason: "Select a media object before changing fit mode.", tooltip: "Fit all selected media inside the frame." }
            ]
        }
        return [
            { key: "tool:shape", label: "Shape Tool", icon: "geometry", checked: root.activeTool === "shape", tooltip: "Click the board to place the default shape." },
            { key: "shape:rectangle", label: "Add Rectangle", icon: "geometry", tooltip: "Add a rectangle shape layer." },
            { key: "shape:ellipse", label: "Add Oval", icon: "geometry", tooltip: "Add an oval shape layer." },
            { key: "shape:line", label: "Add Line", icon: "minus", tooltip: "Add a line shape layer." },
            { key: "shape:arrow", label: "Add Arrow", icon: "move-forward", tooltip: "Add an arrow shape layer." }
        ]
    }

    function handleCarouselToolMenuAction(toolName, key) {
        if (key.indexOf("tool:") === 0) {
            root.setActiveTool(key.slice(5))
            return
        }
        if (key.indexOf("shape:") === 0) {
            carouselDocument.addShapeItem(key.slice(6))
            root.setActiveTool("select")
            return
        }
        if (key === "duplicate") {
            carouselDocument.duplicateSelectedItem()
        } else if (key === "bring-forward") {
            carouselDocument.bringSelectedForward()
        } else if (key === "send-backward") {
            carouselDocument.sendSelectedBackward()
        } else if (key === "bring-front") {
            carouselDocument.bringSelectedToFront()
        } else if (key === "send-back") {
            carouselDocument.sendSelectedToBack()
        } else if (key === "delete") {
            carouselDocument.removeSelectedItem()
        } else if (key === "zoom-in") {
            root.zoomIn()
        } else if (key === "zoom-out") {
            root.zoomOut()
        } else if (key === "zoom-fit") {
            root.resetZoomToFit()
        } else if (key === "add-text") {
            carouselDocument.addTextItem("New Text")
            root.setActiveTool("select")
        } else if (key === "add-media-layer") {
            root.openMediaPicker("layer")
        } else if (key === "replace-media") {
            root.replaceSelectedLayerFromGallery()
        } else if (key === "fit-cover") {
            carouselDocument.setCurrentFitMode("cover")
        } else if (key === "fit-contain") {
            carouselDocument.setCurrentFitMode("contain")
        }
    }

    function slideIndexForDocumentX(xPos) {
        const slideWidth = Math.max(1, slidePixelWidth())
        return clamp(Math.floor(xPos / slideWidth), 0, Math.max(0, carouselDocument.slideCount - 1))
    }

    function slideData(index) {
        if (index < 0 || index >= carouselDocument.slides.length)
            return null
        return carouselDocument.slides[index]
    }

    function currentCanvasLabel() {
        return "Canvas " + (carouselDocument.currentIndex + 1)
    }

    function currentTemplateLabel() {
        if (selection.groupKind === "linked-horizontal-pair")
            return "Linked 2"
        if (selection.templateKind === "split-pair")
            return "Split Pair"
        if (selection.templateKind === "stacked-triptych")
            return "Stacked 3"
        return "Single"
    }

    function slotRegionsForSlide(slide, width, height) {
        if (!slide)
            return []
        if (slide.groupKind === "linked-horizontal-pair") {
            return [{
                    name: "primary",
                    label: "Spread",
                    x: 0,
                    y: 0,
                    width: width,
                    height: height,
                    hasMedia: Boolean(slide.primaryHasMedia)
                }]
        }
        if (slide.templateKind === "split-pair") {
            const leftWidth = width / 2
            return [{
                    name: "primary",
                    label: "Left",
                    x: 0,
                    y: 0,
                    width: leftWidth,
                    height: height,
                    hasMedia: Boolean(slide.primaryHasMedia)
                }, {
                    name: "secondary",
                    label: "Right",
                    x: leftWidth,
                    y: 0,
                    width: width - leftWidth,
                    height: height,
                    hasMedia: Boolean(slide.secondaryHasMedia)
                }]
        }
        if (slide.templateKind === "stacked-triptych") {
            const bandHeight = height / 3
            return [{
                    name: "primary",
                    label: "Top",
                    x: 0,
                    y: 0,
                    width: width,
                    height: bandHeight,
                    hasMedia: Boolean(slide.primaryHasMedia)
                }, {
                    name: "secondary",
                    label: "Middle",
                    x: 0,
                    y: bandHeight,
                    width: width,
                    height: bandHeight,
                    hasMedia: Boolean(slide.secondaryHasMedia)
                }, {
                    name: "tertiary",
                    label: "Bottom",
                    x: 0,
                    y: bandHeight * 2,
                    width: width,
                    height: height - bandHeight * 2,
                    hasMedia: Boolean(slide.tertiaryHasMedia)
                }]
        }
        return [{
                name: "primary",
                label: "Primary",
                x: 0,
                y: 0,
                width: width,
                height: height,
                hasMedia: Boolean(slide.primaryHasMedia)
            }]
    }

    function activeSlotRegionForSlide(slide, width, height) {
        const regions = slotRegionsForSlide(slide, width, height)
        for (let index = 0; index < regions.length; ++index) {
            if (regions[index].name === selection.activeSlot)
                return regions[index]
        }
        return regions.length > 0 ? regions[0] : null
    }

    function slotInteractionGuides(slideIndex, slide, width, height, slotName, offsetX, offsetY) {
        if (!root.guidesEnabled)
            return []
        const region = activeSlotRegionForSlide(slide, width, height)
        if (!region || region.name !== slotName)
            return []
        const guides = []
        const originX = slideIndex * slidePixelWidth()
        if (Math.abs(offsetX) < 0.001)
            guides.push({ orientation: "vertical", position: originX + ((region.x + region.width / 2) / root.stripScale), label: "Center" })
        if (Math.abs(offsetY) < 0.001)
            guides.push({ orientation: "horizontal", position: (region.y + region.height / 2) / root.stripScale, label: "Center" })
        return guides
    }

    function canPlaceVideo() {
        return selection.canPlaceVideo
    }

    function slotLabel() {
        if (selection.templateKind === "split-pair")
            return selection.activeSlot === "secondary" ? "Right" : "Left"
        if (selection.templateKind === "stacked-triptych") {
            if (selection.activeSlot === "tertiary")
                return "Bottom"
            if (selection.activeSlot === "secondary")
                return "Middle"
            return "Top"
        }
        return "Primary"
    }

    function chooseSlotForSlide(slide, xPos, yPos, width, height) {
        if (!slide)
            return
        if (slide.groupKind === "linked-horizontal-pair") {
            carouselDocument.setActiveSlot("primary")
            return
        }
        if (slide.templateKind === "split-pair") {
            carouselDocument.setActiveSlot(xPos < width / 2 ? "primary" : "secondary")
            return
        }
        if (slide.templateKind === "stacked-triptych") {
            const bandHeight = height / 3
            const slotIndex = Math.max(0, Math.min(2, Math.floor(yPos / Math.max(1, bandHeight))))
            carouselDocument.setActiveSlot(slotIndex === 0 ? "primary" : slotIndex === 1 ? "secondary" : "tertiary")
            return
        }
        carouselDocument.setActiveSlot("primary")
    }

    function documentPixelLabel() {
        return carouselDocument.documentPixelWidth + " x " + carouselDocument.documentPixelHeight
    }

    function gallerySelectionSupported() {
        if (catalogAssets.selectedFilePath.length === 0)
            return false
        if (catalogAssets.selectedMediaType === "photo")
            return true
        return catalogAssets.selectedMediaType === "video" && canPlaceVideo()
    }

    function assignSelectedGalleryMedia() {
        if (!gallerySelectionSupported())
            return
        if (catalogAssets.selectedMediaType === "video") {
            carouselDocument.assignPrimaryVideo(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel)
            return
        }

        if (selection.canUseTertiary && selection.activeSlot === "tertiary")
            carouselDocument.assignTertiaryImage(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel, catalogAssets.selectedRecipeJson)
        else if (selection.canUseSecondary && selection.activeSlot === "secondary")
            carouselDocument.assignSecondaryImage(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel, catalogAssets.selectedRecipeJson)
        else
            carouselDocument.assignPrimaryImage(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel, catalogAssets.selectedRecipeJson)
    }

    function galleryLayerSelectionSupported() {
        if (catalogAssets.selectedFilePath.length === 0)
            return false
        return catalogAssets.selectedMediaType === "photo" || catalogAssets.selectedMediaType === "video"
    }

    function assignSelectedGalleryMediaAsLayer() {
        if (!galleryLayerSelectionSupported())
            return
        if (catalogAssets.selectedMediaType === "video")
            carouselDocument.addVideoItem(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel)
        else
            carouselDocument.addPhotoItem(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel, catalogAssets.selectedRecipeJson)
    }

    function placeLibraryAsset(asset, intent) {
        if (!asset || !asset.online)
            return

        const mediaType = String(asset.mediaType || "")
        const filePath = String(asset.filePath || "")
        const displayLabel = String(asset.cameraLabel || asset.assetId || "")
        const recipeJson = String(asset.recipeJson || "")
        if (filePath.length === 0)
            return

        if (intent === "layer") {
            if (mediaType === "video")
                carouselDocument.addVideoItem(filePath, displayLabel)
            else if (mediaType === "photo")
                carouselDocument.addPhotoItem(filePath, displayLabel, recipeJson)
            return
        }

        if (intent === "replace" && selection.hasSelection && !selection.isText) {
            if (mediaType === "video")
                carouselDocument.replaceSelectedVideoItem(filePath, displayLabel)
            else if (mediaType === "photo")
                carouselDocument.replaceSelectedPhotoItem(filePath, displayLabel, recipeJson)
            return
        }

        if (mediaType === "video") {
            if (root.canPlaceVideo())
                carouselDocument.assignPrimaryVideo(filePath, displayLabel)
            return
        }

        if (mediaType !== "photo")
            return
        if (selection.canUseTertiary && selection.activeSlot === "tertiary")
            carouselDocument.assignTertiaryImage(filePath, displayLabel, recipeJson)
        else if (selection.canUseSecondary && selection.activeSlot === "secondary")
            carouselDocument.assignSecondaryImage(filePath, displayLabel, recipeJson)
        else
            carouselDocument.assignPrimaryImage(filePath, displayLabel, recipeJson)
    }

    function preferredMediaPickerProject() {
        if (catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned")
            return catalogAssets.projectFilter
        return ""
    }

    function hasProjectScopedMediaPicker() {
        return preferredMediaPickerProject().length > 0
    }

    function refreshMediaPickerAssets() {
        mediaPickerProjectName = mediaPickerScope === "project" ? preferredMediaPickerProject() : ""
        const assets = catalogAssets.timelinePickerAssets(mediaPickerProjectName)
        const filtered = []
        for (let index = 0; index < assets.length; ++index) {
            const item = assets[index]
            const mediaType = String(item.mediaType || "")
            if (mediaType === "photo" || mediaType === "video")
                filtered.push(item)
        }
        mediaPickerAssets = filtered
    }

    function mediaPickerSubtitle() {
        if (mediaPickerScope === "project" && mediaPickerProjectName.length > 0)
            return "Photos and videos from " + mediaPickerProjectName
        return "Photos and videos from the library"
    }

    function mediaPickerEmptyStateText() {
        if (catalogAssets.loading)
            return "Library is still loading."
        if (mediaPickerScope === "project" && mediaPickerProjectName.length > 0)
            return "No photos or videos are assigned to " + mediaPickerProjectName + "."
        return "No gallery-ready photos or videos are available yet."
    }

    function setMediaPickerScope(scope) {
        if (mediaPickerScope === scope)
            return
        mediaPickerScope = scope
        refreshMediaPickerAssets()
    }

    function openMediaPicker(intent) {
        mediaPickerIntent = intent === "replace" ? "replace" : intent === "layer" ? "layer" : "slot"
        mediaPickerScope = hasProjectScopedMediaPicker() ? "project" : "all"
        refreshMediaPickerAssets()
        mediaPickerDialog.openDialog()
    }

    function applyPickedMedia() {
        if (!mediaPickerDialog.hasSelection || !mediaPickerDialog.selectedOnline)
            return

        if (mediaPickerIntent === "replace") {
            if (mediaPickerDialog.selectedMediaType === "photo") {
                carouselDocument.replaceSelectedPhotoItem(
                            mediaPickerDialog.selectedFilePath,
                            mediaPickerDialog.selectedDisplayLabel,
                            String(mediaPickerDialog.selectedAsset.recipeJson || ""))
            } else if (mediaPickerDialog.selectedMediaType === "video") {
                carouselDocument.replaceSelectedVideoItem(
                            mediaPickerDialog.selectedFilePath,
                            mediaPickerDialog.selectedDisplayLabel)
            }
            mediaPickerDialog.close()
            return
        }

        if (mediaPickerIntent === "layer") {
            if (mediaPickerDialog.selectedMediaType === "photo") {
                carouselDocument.addPhotoItem(
                            mediaPickerDialog.selectedFilePath,
                            mediaPickerDialog.selectedDisplayLabel,
                            String(mediaPickerDialog.selectedAsset.recipeJson || ""))
            } else if (mediaPickerDialog.selectedMediaType === "video") {
                carouselDocument.addVideoItem(
                            mediaPickerDialog.selectedFilePath,
                            mediaPickerDialog.selectedDisplayLabel)
            }
            mediaPickerDialog.close()
            return
        }

        if (mediaPickerDialog.selectedMediaType === "video") {
            carouselDocument.assignPrimaryVideo(mediaPickerDialog.selectedFilePath, mediaPickerDialog.selectedDisplayLabel)
        } else if (selection.canUseTertiary && selection.activeSlot === "tertiary") {
            carouselDocument.assignTertiaryImage(
                        mediaPickerDialog.selectedFilePath,
                        mediaPickerDialog.selectedDisplayLabel,
                        String(mediaPickerDialog.selectedAsset.recipeJson || ""))
        } else if (selection.canUseSecondary && selection.activeSlot === "secondary") {
            carouselDocument.assignSecondaryImage(
                        mediaPickerDialog.selectedFilePath,
                        mediaPickerDialog.selectedDisplayLabel,
                        String(mediaPickerDialog.selectedAsset.recipeJson || ""))
        } else {
            carouselDocument.assignPrimaryImage(
                        mediaPickerDialog.selectedFilePath,
                        mediaPickerDialog.selectedDisplayLabel,
                        String(mediaPickerDialog.selectedAsset.recipeJson || ""))
        }
        mediaPickerDialog.close()
    }

    function replaceSelectedLayerFromGallery() {
        if (!selection.hasSelection || selection.isText)
            return
        openMediaPicker("replace")
    }

    function pushGuide(target, orientation, position, label) {
        if (position < -0.001)
            return
        target.push({
            orientation: orientation,
            position: position,
            label: label
        })
    }

    function guideCandidatesForSlide(index) {
        const rect = slideGeometry(index)
        const slide = slideData(index)
        const guides = []

        pushGuide(guides, "vertical", rect.x, "Left")
        pushGuide(guides, "vertical", rect.x + rect.width / 2, "Center")
        pushGuide(guides, "vertical", rect.x + rect.width, "Right")
        pushGuide(guides, "horizontal", rect.y, "Top")
        pushGuide(guides, "horizontal", rect.y + rect.height / 2, "Middle")
        pushGuide(guides, "horizontal", rect.y + rect.height, "Bottom")

        if (root.thirdsGuidesEnabled) {
            pushGuide(guides, "vertical", rect.x + rect.width / 3, "Third")
            pushGuide(guides, "vertical", rect.x + rect.width * 2 / 3, "Third")
            pushGuide(guides, "horizontal", rect.y + rect.height / 3, "Third")
            pushGuide(guides, "horizontal", rect.y + rect.height * 2 / 3, "Third")
        }

        if (root.safeGuidesEnabled) {
            pushGuide(guides, "vertical", rect.x + rect.width * 0.08, "Safe")
            pushGuide(guides, "vertical", rect.x + rect.width * 0.92, "Safe")
            pushGuide(guides, "horizontal", rect.y + rect.height * 0.08, "Safe")
            pushGuide(guides, "horizontal", rect.y + rect.height * 0.92, "Safe")
        }

        if (slide && slide.templateKind === "split-pair")
            pushGuide(guides, "vertical", rect.x + rect.width / 2, "Slot")
        if (slide && slide.templateKind === "stacked-triptych") {
            pushGuide(guides, "horizontal", rect.y + rect.height / 3, "Slot")
            pushGuide(guides, "horizontal", rect.y + rect.height * 2 / 3, "Slot")
        }

        for (let itemIndex = 0; itemIndex < carouselDocument.canvasItems.length; ++itemIndex) {
            const item = carouselDocument.canvasItems[itemIndex]
            if (String(item.itemId || "") === String(selection.itemId || ""))
                continue
            if (!root.itemIntersectsSlide(item, rect))
                continue
            const x = Number(item.x || 0)
            const y = Number(item.y || 0)
            const width = Number(item.width || 0)
            const height = Number(item.height || 0)
            pushGuide(guides, "vertical", x, "Layer")
            pushGuide(guides, "vertical", x + width / 2, "Layer")
            pushGuide(guides, "vertical", x + width, "Layer")
            pushGuide(guides, "horizontal", y, "Layer")
            pushGuide(guides, "horizontal", y + height / 2, "Layer")
            pushGuide(guides, "horizontal", y + height, "Layer")
        }

        for (let gi = 0; gi < root.customGuides.length; ++gi) {
            const cg = root.customGuides[gi]
            pushGuide(guides, cg.orientation, cg.position, "Custom")
        }

        return guides
    }

    function snapCenterAxis(center, halfSpan, orientation, candidates) {
        const threshold = root.snapThresholdPixels / Math.max(0.01, root.stripScale)
        let best = {
            value: center,
            guide: null,
            distance: threshold + 1
        }
        const anchors = [
            { value: center - halfSpan, offset: halfSpan },
            { value: center, offset: 0 },
            { value: center + halfSpan, offset: -halfSpan }
        ]

        for (let candidateIndex = 0; candidateIndex < candidates.length; ++candidateIndex) {
            const guide = candidates[candidateIndex]
            if (guide.orientation !== orientation)
                continue
            for (let anchorIndex = 0; anchorIndex < anchors.length; ++anchorIndex) {
                const anchor = anchors[anchorIndex]
                const distance = Math.abs(anchor.value - guide.position)
                if (distance < best.distance) {
                    best = {
                        value: guide.position + anchor.offset,
                        guide: guide,
                        distance: distance
                    }
                }
            }
        }

        return best.distance <= threshold ? best : {
            value: center,
            guide: null,
            distance: threshold + 1
        }
    }

    function snapSymmetricAxis(center, halfSpan, orientation, candidates) {
        const threshold = root.snapThresholdPixels / Math.max(0.01, root.stripScale)
        let best = {
            halfSpan: halfSpan,
            guide: null,
            distance: threshold + 1
        }

        for (let candidateIndex = 0; candidateIndex < candidates.length; ++candidateIndex) {
            const guide = candidates[candidateIndex]
            if (guide.orientation !== orientation)
                continue
            const snappedHalfSpan = Math.abs(guide.position - center)
            const distance = Math.abs(snappedHalfSpan - halfSpan)
            if (distance < best.distance) {
                best = {
                    halfSpan: snappedHalfSpan,
                    guide: guide,
                    distance: distance
                }
            }
        }

        return best.distance <= threshold ? best : {
            halfSpan: halfSpan,
            guide: null,
            distance: threshold + 1
        }
    }

    function guidesMatch(a, b) {
        if (a.length !== b.length) return false
        for (let i = 0; i < a.length; ++i) {
            if (a[i].orientation !== b[i].orientation ||
                Math.abs(a[i].position - b[i].position) > 0.01)
                return false
        }
        return true
    }

    function setSnapGuides(guides) {
        const filtered = []
        for (let index = 0; index < guides.length; ++index) {
            if (guides[index])
                filtered.push(guides[index])
        }
        if (!guidesMatch(activeSnapGuides, filtered))
            activeSnapGuides = filtered
    }

    function clearSnapGuides() {
        activeSnapGuides = []
    }

    function addCustomGuide(orientation) {
        const slide = slideGeometry(carouselDocument.currentIndex)
        const position = orientation === "vertical"
            ? slide.x + slide.width / 2
            : slide.y + slide.height / 2
        const id = nextGuideId++
        const updated = customGuides.slice()
        updated.push({ guideId: id, orientation: orientation, position: position })
        customGuides = updated
    }

    function removeCustomGuide(guideId) {
        customGuides = customGuides.filter(function(g) { return g.guideId !== guideId })
    }

    function moveCustomGuide(guideId, newPosition) {
        const updated = customGuides.slice()
        for (let i = 0; i < updated.length; ++i) {
            if (updated[i].guideId === guideId) {
                updated[i] = { guideId: guideId, orientation: updated[i].orientation, position: newPosition }
                break
            }
        }
        customGuides = updated
    }

    function clearAllCustomGuides() {
        customGuides = []
    }

    function beginCarouselSetup() {
        setupCarouselName = catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
            ? catalogAssets.projectFilter + " Carousel"
            : "Untitled Carousel"
        setupFormat = "4x5"
        setupSlideCount = 1
        recentCarouselRecords = carouselDocument.recentDocuments()
    }

    function createCarouselFromSetup() {
        carouselDocument.createCarouselWithSettings({
            "carouselName": setupCarouselName,
            "documentFormat": setupFormat,
            "slideCount": Math.max(1, Math.min(20, setupSlideCount))
        })
    }

    function carouselWorkflowPolishSignature() {
        const mode = carouselDocument.hasActiveCarousel ? "composer" : "setup"
        const selectedKind = selection.hasSelection
                ? (selection.isText ? "text" : (selection.isShape ? "shape" : "media"))
                : "none"
        return "carousel-workflow-polish:v1" +
               "|mode:" + mode +
               "|active-tool:" + root.activeTool +
               "|carousel:" + String(carouselDocument.hasActiveCarousel) +
               "|slide:" + String(carouselDocument.hasActiveCarousel ? carouselDocument.currentIndex + 1 : 0) +
               "/" + String(carouselDocument.slideCount) +
               "|selection:" + selectedKind +
               "|layers:" + String(root.currentSlideLayers.length) +
               "|slot:" + String(selection.activeSlot || "primary") +
               "|media-picker:" + String(mediaPickerDialog.visible) +
               "|media-intent:" + root.mediaPickerIntent +
               "|snapping:" + String(root.snappingEnabled) +
               "|guides:" + String(root.guidesEnabled) +
               "|custom-guides:" + String(root.customGuides.length) +
               "|export-busy:" + String(carouselDocument.exportBusy) +
               "|setup-name:" + String(root.setupCarouselName || "") +
               "|preserve:create-board-select-pan-text-media-shape-guides-layers-transform-fit-fill-clear-save-export"
    }

    function snapDragPosition(itemData, centerX, centerY) {
        if (!root.snappingEnabled)
            return { centerX: centerX, centerY: centerY, guides: [] }

        const slideIndex = slideIndexForDocumentX(centerX)
        const guides = guideCandidatesForSlide(slideIndex)
        const halfWidth = Number(itemData.width || 0) / 2
        const halfHeight = Number(itemData.height || 0) / 2
        const xResult = snapCenterAxis(centerX, halfWidth, "vertical", guides)
        const yResult = snapCenterAxis(centerY, halfHeight, "horizontal", guides)
        return {
            centerX: xResult.value,
            centerY: yResult.value,
            guides: [xResult.guide, yResult.guide]
        }
    }

    function snapResize(itemData, centerX, centerY, width, height) {
        if (!root.snappingEnabled)
            return { width: width, height: height, guides: [] }

        const slideIndex = slideIndexForDocumentX(centerX)
        const guides = guideCandidatesForSlide(slideIndex)
        const widthResult = snapSymmetricAxis(centerX, width / 2, "vertical", guides)
        const heightResult = snapSymmetricAxis(centerY, height / 2, "horizontal", guides)
        return {
            width: Math.max(36, widthResult.halfSpan * 2),
            height: Math.max(36, heightResult.halfSpan * 2),
            guides: [widthResult.guide, heightResult.guide]
        }
    }

    Connections {
        target: catalogAssets

        function onAssetCountChanged() {
            root.refreshMediaPickerAssets()
        }

        function onTotalAssetCountChanged() {
            root.refreshMediaPickerAssets()
        }

        function onProjectStatsRevisionChanged() {
            root.refreshMediaPickerAssets()
        }

        function onLoadingChanged() {
            root.refreshMediaPickerAssets()
        }
    }

    Component.onCompleted: {
        refreshMediaPickerAssets()
        if (!carouselDocument.hasActiveCarousel)
            beginCarouselSetup()
    }

    Connections {
        target: carouselDocument

        function onDocumentStateChanged() {
            if (!carouselDocument.hasActiveCarousel)
                root.beginCarouselSetup()
        }
    }

    Shortcut {
        sequences: [StandardKey.Delete]
        enabled: carouselDocument.hasActiveCarousel
        onActivated: carouselDocument.removeSelectedItem()
    }

    Shortcut {
        sequences: [StandardKey.Copy]
        onActivated: carouselDocument.duplicateSelectedItem()
    }

    Shortcut {
        sequence: "Ctrl+D"
        onActivated: carouselDocument.duplicateSelectedItem()
    }

    Shortcut {
        sequence: "Meta+D"
        onActivated: carouselDocument.duplicateSelectedItem()
    }

    Shortcut {
        sequence: "V"
        onActivated: root.setActiveTool("select")
    }

    Shortcut {
        sequence: "H"
        onActivated: root.setActiveTool("hand")
    }

    Shortcut {
        sequence: "T"
        onActivated: root.setActiveTool("text")
    }

    Shortcut {
        sequence: "M"
        onActivated: root.setActiveTool("media")
    }

    Shortcut {
        sequence: "U"
        onActivated: root.setActiveTool("shape")
    }

    Shortcut {
        sequence: "Shift+T"
        onActivated: carouselDocument.addTextItem("New Text")
    }

    Shortcut {
        sequence: "Shift+U"
        onActivated: carouselDocument.addShapeItem("rectangle")
    }

    Shortcut {
        sequence: "Shift+M"
        onActivated: openMediaPicker("layer")
    }

    Shortcut {
        sequence: "]"
        onActivated: carouselDocument.bringSelectedForward()
    }

    Shortcut {
        sequence: "["
        onActivated: carouselDocument.sendSelectedBackward()
    }

    Shortcut {
        sequence: "="
        onActivated: zoomIn()
    }

    Shortcut {
        sequence: "-"
        onActivated: zoomOut()
    }

    Shortcut {
        sequence: "0"
        onActivated: resetZoomToFit()
    }

    Shortcut {
        sequence: "Left"
        onActivated: root.nudgeSelected(-1, 0)
    }

    Shortcut {
        sequence: "Right"
        onActivated: root.nudgeSelected(1, 0)
    }

    Shortcut {
        sequence: "Up"
        onActivated: root.nudgeSelected(0, -1)
    }

    Shortcut {
        sequence: "Down"
        onActivated: root.nudgeSelected(0, 1)
    }

    Shortcut {
        sequence: "Shift+Left"
        onActivated: root.nudgeSelected(-10, 0)
    }

    Shortcut {
        sequence: "Shift+Right"
        onActivated: root.nudgeSelected(10, 0)
    }

    Shortcut {
        sequence: "Shift+Up"
        onActivated: root.nudgeSelected(0, -10)
    }

    Shortcut {
        sequence: "Shift+Down"
        onActivated: root.nudgeSelected(0, 10)
    }

    Shortcut {
        sequence: "Ctrl+Left"
        onActivated: {
            if (carouselDocument.currentIndex > 0)
                carouselDocument.selectSlide(carouselDocument.currentIndex - 1)
        }
    }

    Shortcut {
        sequence: "Ctrl+Right"
        onActivated: {
            if (carouselDocument.currentIndex < carouselDocument.slideCount - 1)
                carouselDocument.selectSlide(carouselDocument.currentIndex + 1)
        }
    }

    Shortcut {
        sequence: "Ctrl+S"
        onActivated: carouselDocument.saveDocument()
    }

    Shortcut {
        sequence: "Meta+S"
        onActivated: carouselDocument.saveDocument()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            carouselDocument.clearSelection()
            root.clearSnapGuides()
            root.setActiveTool("select")
        }
    }

    ColumnLayout {
        objectName: "carouselSetupSurface"
        readonly property string surfaceContract: "carousel-setup-surface:v1|create-carousel:primary|recent:secondary|format-slide-count-visible:true|no-elide:true|no-overlap:true"
        anchors.fill: parent
        visible: !carouselDocument.hasActiveCarousel
        spacing: 0

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: 940
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 480
                Layout.minimumWidth: 340
                Layout.fillWidth: true
                implicitHeight: newCarouselColumn.implicitHeight + 32
                radius: 12
                color: theme.alpha(theme.ink850, 0.92)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    id: newCarouselColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    SectionHeader {
                        title: "New Carousel"
                        accent: theme.accentWarm400
                    }

                    ToneTextField {
                        Layout.fillWidth: true
                        placeholderText: "Carousel name"
                        text: root.setupCarouselName
                        onTextChanged: root.setupCarouselName = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "Aspect"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        FilterChip {
                            Layout.fillWidth: true
                            text: "4:5"
                            active: root.setupFormat === "4x5"
                            onClicked: root.setupFormat = "4x5"
                        }

                        FilterChip {
                            Layout.fillWidth: true
                            text: "5:4"
                            active: root.setupFormat === "5x4"
                            onClicked: root.setupFormat = "5x4"
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "Slides"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        FilterChip {
                            Layout.fillWidth: true
                            text: "1"
                            active: root.setupSlideCount === 1
                            onClicked: root.setupSlideCount = 1
                        }

                        FilterChip {
                            Layout.fillWidth: true
                            text: "3"
                            active: root.setupSlideCount === 3
                            onClicked: root.setupSlideCount = 3
                        }

                        FilterChip {
                            Layout.fillWidth: true
                            text: "5"
                            active: root.setupSlideCount === 5
                            onClicked: root.setupSlideCount = 5
                        }

                        FilterChip {
                            Layout.fillWidth: true
                            text: "10"
                            active: root.setupSlideCount === 10
                            onClicked: root.setupSlideCount = 10
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            const w = root.setupFormat === "5x4" ? 1350 : 1080
                            const h = root.setupFormat === "5x4" ? 1080 : 1350
                            return root.setupSlideCount + " slide" + (root.setupSlideCount !== 1 ? "s" : "")
                                   + "  •  " + (w * root.setupSlideCount) + " x " + h + " px"
                        }
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 8

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Use Active Project"
                            enabled: catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                            onClicked: root.setupCarouselName = catalogAssets.projectFilter + " Carousel"
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Create Carousel"
                            tone: "primary"
                            onClicked: root.createCarouselFromSetup()
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 340
                Layout.minimumWidth: 260
                Layout.fillWidth: true
                implicitHeight: recentCarouselColumn.implicitHeight + 32
                Layout.alignment: Qt.AlignTop
                radius: 12
                color: theme.alpha(theme.ink850, 0.92)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    id: recentCarouselColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    SectionHeader {
                        title: "Recent Carousels"
                        accent: theme.accentCool400
                    }

                    ListView {
                        id: recentCarouselList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(420, Math.max(48, count * 56))
                        clip: true
                        model: root.recentCarouselRecords
                        spacing: 4

                        delegate: RecentDocumentRow {
                            required property var modelData
                            required property int index

                            width: recentCarouselList.width
                            height: 60
                            iconName: "carousel"
                            title: modelData.name || "Untitled"
                            detail: {
                                const fmt = String(modelData.documentFormat || "4x5").toUpperCase()
                                const slides = Number(modelData.slideCount || 0)
                                const date = String(modelData.updatedAt || modelData.createdAt || "")
                                return fmt + "  •  " + slides + " slide" + (slides !== 1 ? "s" : "")
                                       + (date.length > 0 ? "  •  " + date : "")
                            }
                            accent: theme.accentCool400
                            actionable: String(modelData.documentId || "").length > 0
                            onActivated: {
                                carouselDocument.loadDocument(String(modelData.documentId || ""))
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: recentCarouselList.count === 0
                        text: "No saved carousels yet."
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    RowLayout {
        objectName: "carouselActiveWorkspace"
        readonly property string surfaceContract: "carousel-active-workspace:v1|tool-rail:always|composer:dominant|inspector:always|board-strip:always|selection-contextual:true|no-elide:true|no-overlap:true"
        anchors.fill: parent
        visible: carouselDocument.hasActiveCarousel
        spacing: 6

        Rectangle {
            objectName: "carouselToolRail"
            readonly property string visibilityRole: "always-visible|tool-families-snap-guides-menu-expansion"
            Layout.preferredWidth: 48
            Layout.fillHeight: true
            radius: theme.radius8
            color: theme.alpha(theme.ink900, 0.88)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.36)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                Repeater {
                    model: root.toolButtons

                    delegate: CarouselToolRailButton {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        toolData: modelData
                        activeTool: root.activeTool
                        menuItems: root.carouselToolMenuItems(modelData.value)
                        onToolSelected: function(toolValue) { root.setActiveTool(toolValue) }
                        onMenuItemTriggered: function(toolValue, itemKey) {
                            root.handleCarouselToolMenuAction(toolValue, itemKey)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.topMargin: 2
                    Layout.bottomMargin: 2
                    color: theme.alpha(theme.lineSoft, 0.24)
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    radius: 6
                    color: root.snappingEnabled
                           ? theme.alpha(theme.accentCool500, 0.2)
                           : snapHover.containsMouse ? theme.alpha(theme.ink800, 0.8) : "transparent"
                    border.width: root.snappingEnabled ? 1 : 0
                    border.color: theme.alpha(theme.accentCool400, 0.5)

                    AppIcon {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        name: "snap"
                        color: root.snappingEnabled ? theme.accentCool300 : theme.textMuted
                    }

                    MouseArea {
                        id: snapHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.snappingEnabled = !root.snappingEnabled
                    }

                    ToolTip {
                        visible: snapHover.containsMouse
                        text: root.snappingEnabled ? "Snapping On" : "Snapping Off"
                        delay: 400
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    radius: 6
                    color: root.guidesEnabled
                           ? theme.alpha(theme.accentCool500, 0.2)
                           : guidesHover.containsMouse ? theme.alpha(theme.ink800, 0.8) : "transparent"
                    border.width: root.guidesEnabled ? 1 : 0
                    border.color: theme.alpha(theme.accentCool400, 0.5)

                    AppIcon {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        name: "grid"
                        color: root.guidesEnabled ? theme.accentCool300 : theme.textMuted
                    }

                    MouseArea {
                        id: guidesHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.guidesEnabled = !root.guidesEnabled
                    }

                    ToolTip {
                        visible: guidesHover.containsMouse
                        text: root.guidesEnabled ? "Guides On" : "Guides Off"
                        delay: 400
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        Rectangle {
            objectName: "carouselComposerPanel"
            readonly property string surfaceContract: "carousel-composer-panel:v1|header:always|artboard:dominant|zoom:contextual|board-strip:always|empty-slot:contextual|drag-handles:hover-focus|no-overlap:true"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 420
            radius: theme.radius12
            color: theme.alpha(theme.ink900, 0.9)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.48)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.panelPadding
                spacing: theme.gapPanel

                RowLayout {
                    objectName: "carouselHeaderBar"
                    readonly property string visibilityRole: "always-visible|document-name-format-save-export-status"
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: documentNameField
                        Layout.fillWidth: true
                        Layout.maximumWidth: 280
                        text: carouselDocument.documentName
                        color: theme.textStrong
                        font.family: theme.fontDisplay
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        selectByMouse: true
                        background: Rectangle {
                            radius: 4
                            color: documentNameField.activeFocus
                                   ? theme.alpha(theme.ink850, 0.9)
                                   : "transparent"
                            border.width: documentNameField.activeFocus ? 1 : 0
                            border.color: theme.alpha(theme.accentWarm400, 0.4)
                        }
                        onEditingFinished: {
                            if (text.trim().length > 0)
                                carouselDocument.documentName = text.trim()
                            else
                                text = carouselDocument.documentName
                        }
                    }

                    Label {
                        text: carouselDocument.documentFormat.toUpperCase() + "  " + documentPixelLabel()
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        visible: carouselDocument.exportBusy
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: theme.warning

                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.3; duration: 600 }
                            NumberAnimation { from: 0.3; to: 1.0; duration: 600 }
                        }
                    }

                    ActionButton {
                        compact: true
                        text: "Save"
                        tooltipText: "Save the current carousel document."
                        onClicked: carouselDocument.saveDocument()
                    }

                    MenuButton {
                        compact: true
                        text: "Export"
                        iconName: "export"
                        tone: "primary"
                        enabled: !carouselDocument.exportBusy
                        tooltipText: carouselDocument.exportBusy ? "Export is already running." : "Export this carousel."
                        menuItems: root.carouselExportMenuItems()
                        menuWidth: 190
                        onClicked: carouselDocument.exportCarousel()
                        onMenuItemTriggered: root.handleCarouselExportMenuAction(key)
                    }
                }

                Rectangle {
                    objectName: "carouselArtboardFrame"
                    readonly property string visibilityRole: "always-visible|artboard-canvas-slots-layers-guides-selection-handles"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 18
                    color: theme.alpha(theme.ink950, 0.88)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.42)

                    Flickable {
                        id: artboardFlick
                        objectName: "carouselArtboardViewport"
                        readonly property string visibilityRole: "always-visible|pan-zoom-canvas-tools"
                        anchors.fill: parent
                        anchors.margins: theme.panelPadding
                        contentWidth: Math.max(width, stripSurface.width + 40)
                        contentHeight: Math.max(height, stripSurface.height + 40)
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: root.activeTool === "hand"
                        clip: true

                        ScrollBar.horizontal: ScrollBar { }
                        ScrollBar.vertical: ScrollBar { }

                        WheelHandler {
                            target: null

                            onWheel: function(event) {
                                if (event.modifiers & Qt.ControlModifier) {
                                    if (event.angleDelta.y > 0)
                                        root.zoomIn()
                                    else if (event.angleDelta.y < 0)
                                        root.zoomOut()
                                    event.accepted = true
                                }
                            }
                        }

                        Rectangle {
                            id: stripSurface
                            objectName: "carouselStripSurface"
                            readonly property string visibilityRole: "always-visible|slides-media-objects-selection-guides"
                            x: Math.max(20, (artboardFlick.width - width) / 2)
                            y: Math.max(20, (artboardFlick.height - height) / 2)
                            width: Math.max(200, carouselDocument.documentPixelWidth * root.stripScale)
                            height: Math.max(260, carouselDocument.documentPixelHeight * root.stripScale)
                            radius: 18
                            color: "#fbf7f1"
                            border.width: 2
                            border.color: theme.alpha("#c5b6a6", 0.92)
                            clip: true

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: root.toolCursorShape()

                                onPressed: function(mouse) {
                                    if (root.activeTool === "hand") {
                                        mouse.accepted = false
                                        return
                                    }
                                    root.handleCanvasToolPress(mouse, root.slideWidthOnStrip(), stripSurface.height)
                                }
                            }

                            Repeater {
                                model: carouselDocument.slides

                                delegate: Item {
                                    id: slideDelegate
                                    required property int index
                                    required property var modelData
                                    property var slotRegions: root.slotRegionsForSlide(modelData, width, height)

                                    x: index * root.slideWidthOnStrip()
                                    y: 0
                                    width: root.slideWidthOnStrip()
                                    height: stripSurface.height
                                    z: modelData.selected ? 2 : 1

                                    Rectangle {
                                        anchors.fill: parent
                                        color: String(modelData.backgroundColor || "#fbf7f1")
                                        gradient: Gradient {
                                            orientation: String(modelData.backgroundKind || "solid") === "linear-horizontal"
                                                         ? Gradient.Horizontal : Gradient.Vertical
                                            GradientStop {
                                                position: 0
                                                color: String(modelData.backgroundColor || "#fbf7f1")
                                            }
                                            GradientStop {
                                                position: 1
                                                color: String(modelData.backgroundKind || "solid") === "solid"
                                                       ? String(modelData.backgroundColor || "#fbf7f1")
                                                       : String(modelData.backgroundSecondaryColor || "#ffffff")
                                            }
                                        }
                                        border.width: index < carouselDocument.slideCount - 1 ? 1 : 0
                                        border.color: theme.alpha("#cdb9a4", 0.84)
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        color: modelData.selected ? "transparent" : theme.alpha(theme.ink950, 0.08)
                                    }

                                    Image {
                                        anchors.fill: parent
                                        source: modelData.previewSource
                                        fillMode: Image.PreserveAspectCrop
                                        cache: true
                                        asynchronous: true
                                        smooth: true
                                        mipmap: true
                                        sourceSize.width: Math.max(1, root.slidePixelWidth())
                                        sourceSize.height: Math.max(1, carouselDocument.documentPixelHeight)
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        color: "transparent"
                                        border.width: modelData.selected ? 3 : 0
                                        border.color: theme.alpha(theme.accentWarm500, 0.96)
                                    }

                                    Rectangle {
                                        visible: modelData.selected
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        height: 4
                                        color: theme.accentWarm400
                                    }

                                    Repeater {
                                        model: parent.slotRegions

                                        delegate: Item {
                                            id: slotRegion
                                            required property var modelData
                                            property bool isActive: root.selection.activeSlot === modelData.name
                                            property bool dragArmed: false
                                            property real lastX: 0
                                            property real lastY: 0
                                            property real startScale: 1.0
                                            property real handleStartX: 0
                                            property real handleStartY: 0
                                            property bool scaleArmed: false
                                            readonly property real slotLabelReserveHeight: 36

                                            x: Number(modelData.x || 0)
                                            y: Number(modelData.y || 0)
                                            width: Number(modelData.width || 0)
                                            height: Number(modelData.height || 0)
                                            visible: slideDelegate.modelData.selected && !root.selection.hasSelection

                                            Rectangle {
                                                anchors.fill: parent
                                                color: slotRegion.isActive
                                                       ? theme.alpha(theme.accentWarm500, modelData.hasMedia ? 0.05 : 0.08)
                                                       : theme.alpha(theme.ink950, 0.02)
                                                border.width: slotRegion.isActive ? 2 : 1
                                                border.color: slotRegion.isActive
                                                              ? theme.alpha(theme.accentWarm500, 0.96)
                                                              : theme.alpha("#d6c7b6", 0.42)
                                            }

                                            Label {
                                                objectName: "carouselSlotRegionLabel"
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 10
                                                height: Math.max(0, slotRegion.slotLabelReserveHeight - 12)
                                                text: modelData.label
                                                color: slotRegion.isActive ? theme.textStrong : "#8a7360"
                                                font.family: theme.fontMono
                                                font.pixelSize: 10
                                                fontSizeMode: Text.HorizontalFit
                                                minimumPixelSize: 8
                                                verticalAlignment: Text.AlignTop
                                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                maximumLineCount: 2
                                                elide: Text.ElideNone
                                            }

                                            Rectangle {
                                                id: emptySlotPromptChrome
                                                objectName: "carouselEmptySlotPrompt"
                                                readonly property real labelReserveHeight: slotRegion.slotLabelReserveHeight
                                                readonly property real labelActionGap: 8
                                                readonly property real availablePromptWidth: Math.max(0, parent.width - 20)
                                                readonly property real availablePromptHeight: Math.max(0, parent.height - labelReserveHeight - labelActionGap - 8)
                                                readonly property real readablePromptMinWidth: 112
                                                readonly property real readablePromptMinHeight: 34
                                                readonly property bool compactPrompt: availablePromptWidth < readablePromptMinWidth || availablePromptHeight < readablePromptMinHeight
                                                readonly property bool promptHasRoom: compactPrompt
                                                                                    ? availablePromptWidth >= 32 && availablePromptHeight >= 30
                                                                                    : availablePromptWidth >= readablePromptMinWidth && availablePromptHeight >= readablePromptMinHeight
                                                readonly property bool promptInsideSlot: x >= 0 && y >= labelReserveHeight &&
                                                                                         x + width <= parent.width + 1 &&
                                                                                         y + height <= parent.height + 1
                                                readonly property bool promptClearOfSlotLabel: y >= labelReserveHeight + labelActionGap
                                                readonly property string promptAccessibleName: "Add media to " + String(modelData.label || "slot")
                                                readonly property bool labelTextFullyVisible: compactPrompt ||
                                                                                               emptySlotPrompt.contentWidth <= emptySlotPrompt.width + 1
                                                readonly property string promptContract: "carousel-empty-slot:v3|bounded-to-slot:true|label-zone:reserved|action-zone:separate|min-gap:8|tiny-hidden:true|centered-action:true|icon-zone:fixed|full-label-min:112x34|icon-only-when-tight:true|tooltip-full:true|readable-text:true|no-incomplete-text:true|no-elide:true|no-overlap:true"
                                                readonly property string promptGeometrySignature: "x:" + Math.round(x) +
                                                                                                  "|y:" + Math.round(y) +
                                                                                                  "|w:" + Math.round(width) +
                                                                                                  "|h:" + Math.round(height) +
                                                                                                  "|label-reserve:" + Math.round(labelReserveHeight) +
                                                                                                  "|gap:" + Math.round(labelActionGap) +
                                                                                                  "|has-room:" + String(promptHasRoom) +
                                                                                                  "|compact:" + String(compactPrompt) +
                                                                                                  "|label-fits:" + String(labelTextFullyVisible) +
                                                                                                  "|inside:" + String(promptInsideSlot) +
                                                                                                  "|clear-label:" + String(promptClearOfSlotLabel)
                                                x: Math.max(0, (parent.width - width) / 2)
                                                y: labelReserveHeight + labelActionGap + Math.max(0, (availablePromptHeight - height) / 2)
                                                visible: !modelData.hasMedia &&
                                                         promptHasRoom
                                                width: compactPrompt
                                                       ? Math.min(availablePromptWidth, 32)
                                                       : Math.min(availablePromptWidth, Math.max(readablePromptMinWidth, emptySlotPromptContent.implicitWidth + 20))
                                                height: compactPrompt
                                                        ? Math.min(availablePromptHeight, 30)
                                                        : Math.min(availablePromptHeight, Math.max(readablePromptMinHeight, emptySlotPromptContent.implicitHeight + 10))
                                                radius: 8
                                                color: theme.alpha(theme.ink950, 0.08)
                                                border.width: 1
                                                border.color: theme.alpha(theme.accentWarm500, 0.42)
                                                clip: true
                                                Accessible.name: promptAccessibleName
                                                Accessible.description: compactPrompt
                                                                        ? "Compact add media button. Full label is available in the tooltip."
                                                                        : "Add media button with full visible label."
                                                ToolTip.visible: visible && slotHitArea.containsMouse
                                                ToolTip.text: promptAccessibleName
                                                ToolTip.delay: 350
                                                ToolTip.timeout: 3600

                                                RowLayout {
                                                    id: emptySlotPromptContent
                                                    objectName: "carouselEmptySlotPromptContent"
                                                    anchors.fill: parent
                                                    anchors.leftMargin: emptySlotPromptChrome.compactPrompt ? 7 : 10
                                                    anchors.rightMargin: emptySlotPromptChrome.compactPrompt ? 7 : 10
                                                    spacing: emptySlotPromptChrome.compactPrompt ? 0 : 8

                                                    Rectangle {
                                                        objectName: "carouselEmptySlotPromptIconZone"
                                                        Layout.preferredWidth: 18
                                                        Layout.preferredHeight: 18
                                                        Layout.alignment: Qt.AlignVCenter
                                                        radius: 9
                                                        color: theme.alpha(theme.accentWarm500, 0.18)
                                                        border.width: 1
                                                        border.color: theme.alpha(theme.accentWarm500, 0.42)

                                                        AppIcon {
                                                            anchors.centerIn: parent
                                                            width: 10
                                                            height: 10
                                                            name: "plus"
                                                            color: theme.alpha(theme.ink900, 0.9)
                                                        }
                                                    }

                                                    Label {
                                                        id: emptySlotPrompt
                                                        objectName: "carouselEmptySlotPromptLabel"
                                                        Layout.fillWidth: true
                                                        Layout.alignment: Qt.AlignVCenter
                                                        visible: !emptySlotPromptChrome.compactPrompt
                                                        text: "Add media"
                                                        horizontalAlignment: Text.AlignLeft
                                                        verticalAlignment: Text.AlignVCenter
                                                        color: theme.alpha(theme.ink900, 0.86)
                                                        font.family: theme.fontUi
                                                        font.pixelSize: 12
                                                        font.weight: Font.DemiBold
                                                        fontSizeMode: Text.HorizontalFit
                                                        minimumPixelSize: 10
                                                        elide: Text.ElideNone
                                                    }
                                                }
                                            }

                                            Item {
                                                anchors.fill: parent
                                                visible: slotRegion.isActive && root.guidesEnabled

                                                Rectangle {
                                                    x: parent.width / 2
                                                    y: 0
                                                    width: 1
                                                    height: parent.height
                                                    color: theme.alpha("#5fc2d8", 0.42)
                                                }

                                                Rectangle {
                                                    x: 0
                                                    y: parent.height / 2
                                                    width: parent.width
                                                    height: 1
                                                    color: theme.alpha("#5fc2d8", 0.42)
                                                }
                                            }

                                            MouseArea {
                                                id: slotHitArea
                                                anchors.fill: parent
                                                enabled: root.activeTool === "select" || root.activeTool === "media"
                                                hoverEnabled: true
                                                preventStealing: true
                                                cursorShape: root.activeTool === "media"
                                                             ? Qt.PointingHandCursor
                                                             : modelData.hasMedia
                                                               ? pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                                               : Qt.PointingHandCursor

                                                onPressed: function(mouse) {
                                                    carouselDocument.selectSlide(slideDelegate.index)
                                                    carouselDocument.setActiveSlot(modelData.name)
                                                    carouselDocument.clearSelection()
                                                    if (root.activeTool === "media" || !modelData.hasMedia) {
                                                        slotRegion.dragArmed = false
                                                        root.clearSnapGuides()
                                                        return
                                                    }
                                                    const p = slotRegion.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    slotRegion.lastX = p.x
                                                    slotRegion.lastY = p.y
                                                    slotRegion.dragArmed = true
                                                }

                                                onPositionChanged: function(mouse) {
                                                    if (!pressed || !slotRegion.dragArmed)
                                                        return
                                                    const p = slotRegion.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    carouselDocument.moveCurrentMediaByPixels(
                                                                (p.x - slotRegion.lastX) / root.stripScale,
                                                                (p.y - slotRegion.lastY) / root.stripScale)
                                                    slotRegion.lastX = p.x
                                                    slotRegion.lastY = p.y
                                                    root.setSnapGuides(root.slotInteractionGuides(
                                                                           slideDelegate.index,
                                                                           slideDelegate.modelData,
                                                                           slideDelegate.width,
                                                                           slideDelegate.height,
                                                                           modelData.name,
                                                                           Number(selection.offsetX || 0),
                                                                           Number(selection.offsetY || 0)))
                                                }

                                                onReleased: {
                                                    slotRegion.dragArmed = false
                                                    carouselDocument.commitMediaTransformState()
                                                    root.clearSnapGuides()
                                                }

                                                onClicked: function() {
                                                    carouselDocument.selectSlide(slideDelegate.index)
                                                    carouselDocument.setActiveSlot(modelData.name)
                                                    carouselDocument.clearSelection()
                                                    if (root.activeTool === "media" || !modelData.hasMedia) {
                                                        openMediaPicker("slot")
                                                        if (root.activeTool === "media")
                                                            root.setActiveTool("select")
                                                    }
                                                }

                                                onDoubleClicked: {
                                                    carouselDocument.selectSlide(slideDelegate.index)
                                                    carouselDocument.setActiveSlot(modelData.name)
                                                    carouselDocument.clearSelection()
                                                    openMediaPicker("slot")
                                                }
                                            }

                                            Rectangle {
                                                id: slotScaleHandle
                                                visible: slotRegion.isActive && modelData.hasMedia && root.activeTool === "select"
                                                width: 18
                                                height: 18
                                                radius: 9
                                                anchors.right: parent.right
                                                anchors.bottom: parent.bottom
                                                anchors.margins: -9
                                                color: theme.accentWarm500
                                                border.width: 1
                                                border.color: theme.alpha(theme.ink950, 0.35)

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.SizeFDiagCursor
                                                    preventStealing: true

                                                    onPressed: function(mouse) {
                                                        carouselDocument.selectSlide(slideDelegate.index)
                                                        carouselDocument.setActiveSlot(modelData.name)
                                                        carouselDocument.clearSelection()
                                                        slotRegion.scaleArmed = true
                                                        slotRegion.startScale = Number(selection.scale || 1)
                                                        const p = slotScaleHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                        slotRegion.handleStartX = p.x
                                                        slotRegion.handleStartY = p.y
                                                    }

                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed || !slotRegion.scaleArmed)
                                                            return
                                                        const p = slotScaleHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                        const delta = Math.max(
                                                                        p.x - slotRegion.handleStartX,
                                                                        p.y - slotRegion.handleStartY)
                                                        let scale = slotRegion.startScale + (delta / Math.max(120, Math.min(slotRegion.width, slotRegion.height)))
                                                        if (Math.abs(scale - 1.0) < 0.08)
                                                            scale = 1.0
                                                        carouselDocument.setCurrentScale(scale)
                                                    }

                                                    onReleased: {
                                                        slotRegion.scaleArmed = false
                                                        root.clearSnapGuides()
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        anchors.fill: parent
                                        visible: root.guidesEnabled && modelData.selected

                                        Rectangle {
                                            x: parent.width / 2
                                            y: 0
                                            width: 1
                                            height: parent.height
                                            color: theme.alpha("#5fc2d8", 0.55)
                                        }

                                        Rectangle {
                                            x: 0
                                            y: parent.height / 2
                                            width: parent.width
                                            height: 1
                                            color: theme.alpha("#5fc2d8", 0.55)
                                        }

                                        Rectangle {
                                            visible: root.thirdsGuidesEnabled
                                            x: parent.width / 3
                                            y: 0
                                            width: 1
                                            height: parent.height
                                            color: theme.alpha("#f0b35d", 0.45)
                                        }

                                        Rectangle {
                                            visible: root.thirdsGuidesEnabled
                                            x: parent.width * 2 / 3
                                            y: 0
                                            width: 1
                                            height: parent.height
                                            color: theme.alpha("#f0b35d", 0.45)
                                        }

                                        Rectangle {
                                            visible: root.thirdsGuidesEnabled
                                            x: 0
                                            y: parent.height / 3
                                            width: parent.width
                                            height: 1
                                            color: theme.alpha("#f0b35d", 0.45)
                                        }

                                        Rectangle {
                                            visible: root.thirdsGuidesEnabled
                                            x: 0
                                            y: parent.height * 2 / 3
                                            width: parent.width
                                            height: 1
                                            color: theme.alpha("#f0b35d", 0.45)
                                        }

                                        Rectangle {
                                            visible: root.safeGuidesEnabled
                                            anchors.fill: parent
                                            anchors.margins: Math.round(Math.min(parent.width, parent.height) * 0.08)
                                            color: "transparent"
                                            border.width: 1
                                            border.color: theme.alpha("#7bd0bf", 0.7)
                                        }
                                    }

                                }
                            }

                            Repeater {
                                model: root.activeSnapGuides

                                delegate: Item {
                                    required property var modelData

                                    x: modelData.orientation === "vertical" ? Number(modelData.position || 0) * root.stripScale : 0
                                    y: modelData.orientation === "horizontal" ? Number(modelData.position || 0) * root.stripScale : 0
                                    width: modelData.orientation === "vertical" ? 1 : stripSurface.width
                                    height: modelData.orientation === "horizontal" ? 1 : stripSurface.height

                                    Rectangle {
                                        anchors.fill: parent
                                        color: theme.alpha(theme.accentCool400, 0.92)
                                    }

                                    Rectangle {
                                        visible: modelData.label && modelData.label.length > 0
                                        x: modelData.orientation === "vertical" ? 6 : 10
                                        y: modelData.orientation === "vertical" ? 10 : 6
                                        radius: 8
                                        color: theme.alpha(theme.ink950, 0.88)
                                        border.width: 1
                                        border.color: theme.alpha(theme.accentCool400, 0.72)
                                        implicitWidth: snapGuideText.implicitWidth + 14
                                        implicitHeight: snapGuideText.implicitHeight + 8

                                        Label {
                                            id: snapGuideText
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: theme.textStrong
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: root.customGuides

                                delegate: Item {
                                    id: customGuideItem
                                    required property var modelData
                                    required property int index
                                    property bool isDragging: false
                                    property real dragStartPos: 0
                                    property real dragStartMouse: 0

                                    x: modelData.orientation === "vertical" ? Number(modelData.position || 0) * root.stripScale - 3 : 0
                                    y: modelData.orientation === "horizontal" ? Number(modelData.position || 0) * root.stripScale - 3 : 0
                                    width: modelData.orientation === "vertical" ? 7 : stripSurface.width
                                    height: modelData.orientation === "horizontal" ? 7 : stripSurface.height
                                    z: 5

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: modelData.orientation === "vertical" ? 1 : parent.width
                                        height: modelData.orientation === "horizontal" ? 1 : parent.height
                                        color: theme.alpha(root.guideColor, root.guideOpacity)
                                    }

                                    Rectangle {
                                        x: modelData.orientation === "vertical" ? -4 : 8
                                        y: modelData.orientation === "vertical" ? 8 : -4
                                        width: guideHandleLabel.implicitWidth + 10
                                        height: guideHandleLabel.implicitHeight + 6
                                        radius: 6
                                        color: theme.alpha(theme.ink950, 0.82)
                                        border.width: 1
                                        border.color: theme.alpha(root.guideColor, 0.72)
                                        visible: customGuideMouse.containsMouse || customGuideItem.isDragging

                                        Label {
                                            id: guideHandleLabel
                                            anchors.centerIn: parent
                                            text: Math.round(Number(modelData.position || 0)) + " px"
                                            color: theme.textStrong
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                        }
                                    }

                                    MouseArea {
                                        id: customGuideMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        preventStealing: true
                                        cursorShape: modelData.orientation === "vertical" ? Qt.SplitHCursor : Qt.SplitVCursor

                                        onPressed: function(mouse) {
                                            customGuideItem.isDragging = true
                                            customGuideItem.dragStartPos = Number(modelData.position || 0)
                                            if (modelData.orientation === "vertical") {
                                                const p = customGuideItem.mapToItem(stripSurface, mouse.x, mouse.y)
                                                customGuideItem.dragStartMouse = p.x
                                            } else {
                                                const p = customGuideItem.mapToItem(stripSurface, mouse.x, mouse.y)
                                                customGuideItem.dragStartMouse = p.y
                                            }
                                        }

                                        onPositionChanged: function(mouse) {
                                            if (!pressed || !customGuideItem.isDragging)
                                                return
                                            const p = customGuideItem.mapToItem(stripSurface, mouse.x, mouse.y)
                                            const coord = modelData.orientation === "vertical" ? p.x : p.y
                                            const delta = (coord - customGuideItem.dragStartMouse) / root.stripScale
                                            root.moveCustomGuide(modelData.guideId, customGuideItem.dragStartPos + delta)
                                        }

                                        onReleased: {
                                            customGuideItem.isDragging = false
                                        }

                                        onDoubleClicked: {
                                            root.removeCustomGuide(modelData.guideId)
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: carouselDocument.canvasItems

                                delegate: Item {
                                    id: layerItem
                                    required property int index
                                    required property var modelData
                                    property real dragLastX: 0
                                    property real dragLastY: 0
                                    property real dragCenterX: 0
                                    property real dragCenterY: 0
                                    property real resizeStartWidth: 0
                                    property real resizeStartHeight: 0
                                    property real resizeStartX: 0
                                    property real resizeStartY: 0
                                    property real resizeCenterX: 0
                                    property real resizeCenterY: 0
                                    property real rotateCenterX: 0
                                    property real rotateCenterY: 0
                                    property bool dragArmed: false
                                    property var cachedSnapGuides: []
                                    property bool selected: !!modelData.selected
                                    property bool primarySelected: !!modelData.primarySelected
                                    property bool interacting: false
                                    property real localWidth: 0
                                    property real localHeight: 0
                                    property real localRotation: Number(modelData.rotation || 0)
                                    property real dragOriginCX: 0
                                    property real dragOriginCY: 0
                                    property real stableSourceW: Number(modelData.width || 0)
                                    property real stableSourceH: Number(modelData.height || 0)

                                    x: (Number(modelData.x || 0) * root.stripScale) +
                                       (layerItem.selected && !Boolean(modelData.locked) && root.anyItemDragging
                                        ? root.globalDragOffsetX : 0)
                                    y: (Number(modelData.y || 0) * root.stripScale) +
                                       (layerItem.selected && !Boolean(modelData.locked) && root.anyItemDragging
                                        ? root.globalDragOffsetY : 0)
                                    width: Math.max(10, (layerItem.localWidth > 0
                                        ? layerItem.localWidth : Number(modelData.width || 0)) * root.stripScale)
                                    height: Math.max(10, (layerItem.localHeight > 0
                                        ? layerItem.localHeight : Number(modelData.height || 0)) * root.stripScale)
                                    z: layerItem.index + 10
                                    visible: !Boolean(modelData.hidden)
                                    opacity: (Boolean(modelData.locked) ? 0.82 : 1.0) * Number(modelData.opacity || 1.0)

                                    onModelDataChanged: {
                                        if (!layerItem.interacting) {
                                            layerItem.stableSourceW = Number(modelData.width || 0)
                                            layerItem.stableSourceH = Number(modelData.height || 0)
                                            layerItem.localRotation = Number(modelData.rotation || 0)
                                        }
                                    }

                                    transform: Rotation {
                                        origin.x: layerBody.width / 2
                                        origin.y: layerBody.height / 2
                                        angle: layerItem.interacting ? layerItem.localRotation : Number(modelData.rotation || 0)
                                    }

                                    Item {
                                        id: layerBody
                                        anchors.fill: parent
                                        transform: Scale {
                                            origin.x: layerBody.width / 2
                                            origin.y: layerBody.height / 2
                                            xScale: Boolean(modelData.flippedHorizontal) ? -1 : 1
                                            yScale: Boolean(modelData.flippedVertical) ? -1 : 1
                                        }

                                        Rectangle {
                                            x: Number(modelData.shadowOffsetX || 0) * root.stripScale
                                            y: Number(modelData.shadowOffsetY || 0) * root.stripScale
                                            width: layerBody.width
                                            height: layerBody.height
                                            visible: Number(modelData.shadowOpacity || 0) > 0
                                            radius: (modelData.type === "image" || modelData.type === "video")
                                                    && String(modelData.mediaMaskShape || "rectangle") === "ellipse"
                                                    ? Math.min(width, height) / 2
                                                    : (modelData.type === "image" || modelData.type === "video")
                                                    ? Number(modelData.cornerRadius || 0) * root.stripScale
                                                    : 0
                                            color: theme.alpha(String(modelData.shadowColor || "#000000"),
                                                               Math.max(0, Math.min(1, Number(modelData.shadowOpacity || 0))))
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: (modelData.type === "image" || modelData.type === "video")
                                                    && String(modelData.mediaMaskShape || "rectangle") === "ellipse"
                                                    ? Math.min(width, height) / 2
                                                    : (modelData.type === "image" || modelData.type === "video")
                                                    ? Number(modelData.cornerRadius || 0) * root.stripScale
                                                    : 0
                                            color: modelData.type === "image" ? theme.alpha(theme.ink950, 0.05) : "transparent"
                                            border.width: layerItem.primarySelected ? 2 : layerItem.selected ? 1 : 0
                                            border.color: theme.alpha(theme.accentWarm500, 0.96)
                                            clip: true

                                            Image {
                                                property var cropRect: root.layerMediaDrawRect(modelData, parent.width, parent.height)
                                                x: cropRect.x
                                                y: cropRect.y
                                                width: cropRect.width
                                                height: cropRect.height
                                                visible: modelData.type === "image"
                                                source: modelData.sourceUrl
                                                fillMode: Image.Stretch
                                                cache: true
                                                asynchronous: true
                                                smooth: true
                                                mipmap: true
                                                sourceSize.width: Math.max(1, Math.round(width))
                                                sourceSize.height: Math.max(1, Math.round(height))
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                visible: modelData.type === "shape"
                                                         && (String(modelData.shapeKind || "rectangle") === "rectangle"
                                                             || String(modelData.shapeKind || "rectangle") === "ellipse")
                                                radius: String(modelData.shapeKind || "rectangle") === "ellipse"
                                                        ? Math.min(width, height) / 2
                                                        : Number(modelData.cornerRadius || 0) * root.stripScale
                                                color: String(modelData.fillColor || "#ffffff")
                                                gradient: Gradient {
                                                    orientation: String(modelData.fillKind || "solid") === "linear-horizontal"
                                                                 ? Gradient.Horizontal : Gradient.Vertical
                                                    GradientStop { position: 0.0; color: String(modelData.fillColor || "#ffffff") }
                                                    GradientStop {
                                                        position: 1.0
                                                        color: String(modelData.fillKind || "solid") === "solid"
                                                               ? String(modelData.fillColor || "#ffffff")
                                                               : String(modelData.fillSecondaryColor || "#69cad8")
                                                    }
                                                }
                                                border.width: Math.max(0, Number(modelData.strokeWidth || 0) * root.stripScale)
                                                border.color: String(modelData.strokeColor || "#1e1a17")
                                            }

                                            Canvas {
                                                id: shapeLineCanvas
                                                anchors.fill: parent
                                                visible: modelData.type === "shape"
                                                         && (String(modelData.shapeKind || "") === "line"
                                                             || String(modelData.shapeKind || "") === "arrow")
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.reset()
                                                    const lineWidth = Math.max(1, Number(modelData.strokeWidth || 4) * root.stripScale)
                                                    const y = height / 2
                                                    const startX = lineWidth / 2
                                                    const endX = Math.max(startX, width - lineWidth / 2)
                                                    ctx.strokeStyle = String(modelData.strokeColor || "#36f2a6")
                                                    ctx.fillStyle = String(modelData.strokeColor || "#36f2a6")
                                                    ctx.lineWidth = lineWidth
                                                    ctx.lineCap = "round"
                                                    ctx.lineJoin = "round"
                                                    ctx.beginPath()
                                                    ctx.moveTo(startX, y)
                                                    ctx.lineTo(endX, y)
                                                    ctx.stroke()
                                                    if (String(modelData.shapeKind || "") === "arrow") {
                                                        const head = Math.max(8, Math.min(width, height) * 0.38)
                                                        ctx.beginPath()
                                                        ctx.moveTo(endX, y)
                                                        ctx.lineTo(endX - head, y - head * 0.48)
                                                        ctx.lineTo(endX - head, y + head * 0.48)
                                                        ctx.closePath()
                                                        ctx.fill()
                                                    }
                                                }
                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                                Connections {
                                                    target: carouselDocument
                                                    function onItemsChanged() { shapeLineCanvas.requestPaint() }
                                                }
                                            }

                                            Rectangle {
                                                property var cropRect: root.layerMediaDrawRect(modelData, parent.width, parent.height)
                                                x: cropRect.x
                                                y: cropRect.y
                                                width: cropRect.width
                                                height: cropRect.height
                                                visible: modelData.type === "video"
                                                color: "#0f1820"
                                                border.width: 2
                                                border.color: theme.alpha("#69cad8", 0.88)
                                                radius: 8

                                                Rectangle {
                                                    anchors.centerIn: parent
                                                    width: Math.min(parent.width, parent.height) * 0.28
                                                    height: width
                                                    radius: width / 2
                                                    color: theme.alpha("#69cad8", 0.16)
                                                    border.width: 1
                                                    border.color: theme.alpha("#69cad8", 0.72)

                                                    Canvas {
                                                        anchors.fill: parent
                                                        onPaint: {
                                                            const ctx = getContext("2d")
                                                            ctx.reset()
                                                            ctx.fillStyle = "#69cad8"
                                                            ctx.beginPath()
                                                            ctx.moveTo(width * 0.4, height * 0.28)
                                                            ctx.lineTo(width * 0.4, height * 0.72)
                                                            ctx.lineTo(width * 0.72, height * 0.5)
                                                            ctx.closePath()
                                                            ctx.fill()
                                                        }
                                                    }
                                                }

                                                Label {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.margins: 10
                                                    text: modelData.displayLabel
                                                    color: "#eef4f6"
                                                    font.family: theme.fontMono
                                                    font.pixelSize: Math.max(10, Math.round(11 * root.stripScale / 0.2))
                                                    horizontalAlignment: Text.AlignHCenter
                                                    fontSizeMode: Text.HorizontalFit
                                                    minimumPixelSize: 8
                                                    elide: Text.ElideNone
                                                }
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                visible: modelData.type === "text"
                                                         && Number(modelData.textBackgroundOpacity || 0) > 0
                                                color: theme.alpha(String(modelData.textBackgroundColor || "#ffffff"),
                                                                   Math.max(0, Math.min(1, Number(modelData.textBackgroundOpacity || 0))))
                                            }

                                            Text {
                                                anchors.fill: parent
                                                visible: modelData.type === "text"
                                                text: modelData.text
                                                color: modelData.textColor
                                                font.family: root.textFontFamily(modelData.textFontFamily)
                                                font.pixelSize: Math.max(10, Number(modelData.textPointSize || 0) * root.stripScale)
                                                font.weight: root.textFontWeight(modelData.textWeight)
                                                font.italic: Boolean(modelData.textItalic)
                                                font.letterSpacing: Math.max(0, Number(modelData.textLetterSpacing || 0) * root.stripScale)
                                                wrapMode: Text.WordWrap
                                                horizontalAlignment: root.textHorizontalAlignment(modelData.textHorizontalAlignment)
                                                verticalAlignment: root.textVerticalAlignment(modelData.textVerticalAlignment)
                                                style: Number(modelData.textStrokeWidth || 0) > 0 ? Text.Outline : Text.Normal
                                                styleColor: String(modelData.textStrokeColor || "#000000")
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                visible: (modelData.type === "image" || modelData.type === "video")
                                                         && Number(modelData.frameWidth || 0) > 0
                                                radius: String(modelData.mediaMaskShape || "rectangle") === "ellipse"
                                                        ? Math.min(width, height) / 2
                                                        : Number(modelData.cornerRadius || 0) * root.stripScale
                                                color: "transparent"
                                                border.width: Math.max(1, Number(modelData.frameWidth || 0) * root.stripScale)
                                                border.color: String(modelData.frameColor || "#ffffff")
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: root.activeTool === "select"
                                            preventStealing: true
                                            cursorShape: Boolean(modelData.locked) ? Qt.ForbiddenCursor
                                                                                  : pressed ? Qt.ClosedHandCursor : Qt.SizeAllCursor

                                            onPressed: function(mouse) {
                                                if (root.hasMultiSelectModifier(mouse.modifiers)) {
                                                    carouselDocument.toggleItemSelection(modelData.itemId)
                                                    layerItem.dragArmed = false
                                                    root.clearSnapGuides()
                                                    return
                                                }
                                                if (Boolean(modelData.locked)) {
                                                    root.focusLayerItem(modelData.itemId, layerItem.selected)
                                                    layerItem.dragArmed = false
                                                    root.clearSnapGuides()
                                                    return
                                                }
                                                root.focusLayerItem(modelData.itemId, layerItem.selected)
                                                const p = layerBody.mapToItem(stripSurface, mouse.x, mouse.y)
                                                layerItem.dragLastX = p.x
                                                layerItem.dragLastY = p.y
                                                layerItem.dragOriginCX = Number(modelData.centerX || 0)
                                                layerItem.dragCenterX = layerItem.dragOriginCX
                                                layerItem.dragOriginCY = Number(modelData.centerY || 0)
                                                layerItem.dragCenterY = layerItem.dragOriginCY
                                                layerItem.cachedSnapGuides = root.guideCandidatesForSlide(root.slideIndexForDocumentX(layerItem.dragCenterX))
                                                layerItem.interacting = true
                                                layerItem.dragArmed = true
                                                root.anyItemDragging = true
                                                root.globalDragOffsetX = 0
                                                root.globalDragOffsetY = 0
                                            }

                                            onPositionChanged: function(mouse) {
                                                if (!pressed || !layerItem.dragArmed)
                                                    return
                                                const p = layerBody.mapToItem(stripSurface, mouse.x, mouse.y)
                                                const deltaX = (p.x - layerItem.dragLastX) / root.stripScale
                                                const deltaY = (p.y - layerItem.dragLastY) / root.stripScale
                                                const newCX = layerItem.dragCenterX + deltaX
                                                const newCY = layerItem.dragCenterY + deltaY
                                                let snappedCX = newCX
                                                let snappedCY = newCY
                                                if (root.snappingEnabled) {
                                                    const halfW = Number(modelData.width || 0) / 2
                                                    const halfH = Number(modelData.height || 0) / 2
                                                    const xSnap = root.snapCenterAxis(newCX, halfW, "vertical", layerItem.cachedSnapGuides)
                                                    const ySnap = root.snapCenterAxis(newCY, halfH, "horizontal", layerItem.cachedSnapGuides)
                                                    root.setSnapGuides([xSnap.guide, ySnap.guide])
                                                    snappedCX = xSnap.value
                                                    snappedCY = ySnap.value
                                                }
                                                root.globalDragOffsetX = (snappedCX - layerItem.dragOriginCX) * root.stripScale
                                                root.globalDragOffsetY = (snappedCY - layerItem.dragOriginCY) * root.stripScale
                                                carouselDocument.setSelectedItemCenterSilent(snappedCX, snappedCY)
                                                layerItem.dragCenterX = snappedCX
                                                layerItem.dragCenterY = snappedCY
                                                layerItem.dragLastX = p.x
                                                layerItem.dragLastY = p.y
                                            }

                                            onReleased: {
                                                root.anyItemDragging = false
                                                root.globalDragOffsetX = 0
                                                root.globalDragOffsetY = 0
                                                layerItem.interacting = false
                                                layerItem.dragArmed = false
                                                carouselDocument.commitTransformState()
                                                root.clearSnapGuides()
                                            }
                                        }

                                        Rectangle {
                                            id: rotateHandle
                                            visible: root.activeTool === "select" && layerItem.primarySelected && !Boolean(modelData.locked)
                                            width: 18
                                            height: 18
                                            radius: 9
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.bottom: parent.top
                                            anchors.bottomMargin: 12
                                            color: theme.accentCool400
                                            border.width: 1
                                            border.color: theme.alpha(theme.ink950, 0.35)

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.CrossCursor
                                                preventStealing: true

                                                onPressed: function(mouse) {
                                                    root.focusLayerItem(modelData.itemId, true)
                                                    const center = layerBody.mapToItem(stripSurface, layerBody.width / 2, layerBody.height / 2)
                                                    layerItem.rotateCenterX = center.x
                                                    layerItem.rotateCenterY = center.y
                                                    layerItem.interacting = true
                                                }

                                                onPositionChanged: function(mouse) {
                                                    if (!pressed)
                                                        return
                                                    const p = rotateHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    const angle = Math.atan2(p.y - layerItem.rotateCenterY, p.x - layerItem.rotateCenterX) * 180 / Math.PI + 90
                                                    layerItem.localRotation = angle
                                                    carouselDocument.setSelectedItemRotationSilent(angle)
                                                }

                                                onReleased: {
                                                    layerItem.interacting = false
                                                    carouselDocument.commitTransformState()
                                                    root.clearSnapGuides()
                                                }
                                            }
                                        }

                                        Rectangle {
                                            id: resizeHandle
                                            visible: root.activeTool === "select" && layerItem.primarySelected && !Boolean(modelData.locked)
                                            width: 18
                                            height: 18
                                            radius: 9
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: -9
                                            color: theme.accentWarm500
                                            border.width: 1
                                            border.color: theme.alpha(theme.ink950, 0.35)

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.SizeFDiagCursor
                                                preventStealing: true

                                                onPressed: function(mouse) {
                                                    root.focusLayerItem(modelData.itemId, true)
                                                    layerItem.resizeStartWidth = Number(modelData.width || 0)
                                                    layerItem.resizeStartHeight = Number(modelData.height || 0)
                                                    layerItem.resizeCenterX = Number(modelData.centerX || 0)
                                                    layerItem.resizeCenterY = Number(modelData.centerY || 0)
                                                    const p = resizeHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    layerItem.resizeStartX = p.x
                                                    layerItem.resizeStartY = p.y
                                                    layerItem.cachedSnapGuides = root.guideCandidatesForSlide(root.slideIndexForDocumentX(layerItem.resizeCenterX))
                                                    layerItem.interacting = true
                                                }

                                                onPositionChanged: function(mouse) {
                                                    if (!pressed)
                                                        return
                                                    const p = resizeHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    const newW = layerItem.resizeStartWidth + ((p.x - layerItem.resizeStartX) / root.stripScale)
                                                    const newH = layerItem.resizeStartHeight + ((p.y - layerItem.resizeStartY) / root.stripScale)
                                                    let finalW = Math.max(36, newW)
                                                    let finalH = Math.max(36, newH)
                                                    if (root.snappingEnabled) {
                                                        const wSnap = root.snapSymmetricAxis(layerItem.resizeCenterX, newW / 2, "vertical", layerItem.cachedSnapGuides)
                                                        const hSnap = root.snapSymmetricAxis(layerItem.resizeCenterY, newH / 2, "horizontal", layerItem.cachedSnapGuides)
                                                        finalW = Math.max(36, wSnap.halfSpan * 2)
                                                        finalH = Math.max(36, hSnap.halfSpan * 2)
                                                        root.setSnapGuides([wSnap.guide, hSnap.guide])
                                                    }
                                                    layerItem.localWidth = finalW
                                                    layerItem.localHeight = finalH
                                                    carouselDocument.setSelectedItemSizeSilent(finalW, finalH)
                                                }

                                                onReleased: {
                                                    layerItem.localWidth = 0
                                                    layerItem.localHeight = 0
                                                    layerItem.interacting = false
                                                    carouselDocument.commitTransformState()
                                                    root.clearSnapGuides()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: carouselDocument.slideCount

                            delegate: Rectangle {
                                required property int index
                                x: stripSurface.x + index * root.slideWidthOnStrip() + root.slideWidthOnStrip() / 2 - width / 2
                                y: stripSurface.y - height - 4
                                width: canvasNumLabel.implicitWidth + 12
                                height: canvasNumLabel.implicitHeight + 4
                                radius: 4
                                color: index === carouselDocument.currentIndex
                                       ? theme.alpha(theme.accentWarm500, 0.18)
                                       : "transparent"

                                Label {
                                    id: canvasNumLabel
                                    anchors.centerIn: parent
                                    text: (index + 1).toString()
                                    color: index === carouselDocument.currentIndex ? theme.textStrong : theme.textFaint
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    font.weight: index === carouselDocument.currentIndex ? Font.DemiBold : Font.Normal
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.alignment: Qt.AlignRight
                    Layout.topMargin: -42
                    Layout.rightMargin: 10
                    width: zoomRow.implicitWidth + 16
                    height: 28
                    radius: 14
                    color: theme.alpha(theme.ink950, 0.72)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.28)
                    z: 10

                    RowLayout {
                        id: zoomRow
                        anchors.centerIn: parent
                        spacing: 6

                        Label {
                            text: Math.round(root.composerZoom * 100) + "%"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        Rectangle {
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 12
                            color: theme.alpha(theme.lineSoft, 0.3)
                        }

                        Label {
                            text: "Fit"
                            color: zoomFitHover.containsMouse ? theme.textStrong : theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10

                            MouseArea {
                                id: zoomFitHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.resetZoomToFit()
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "carouselBoardStrip"
                    readonly property string visibilityRole: "always-visible|board-thumbs-add-select-duplicate"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    radius: theme.radius8
                    color: theme.alpha(theme.ink950, 0.68)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.32)
                    clip: true

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        ColumnLayout {
                            Layout.preferredWidth: 64
                            Layout.fillHeight: true
                            spacing: 4

                            Label {
                                Layout.fillWidth: true
                                text: "Boards"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: carouselDocument.slideCount + " total"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "+"
                                enabled: carouselDocument.canAddSlide
                                onClicked: carouselDocument.addSlide()
                            }
                        }

                        ListView {
                            id: boardFilmstrip
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            orientation: ListView.Horizontal
                            spacing: 8
                            clip: true
                            model: carouselDocument.slides
                            boundsBehavior: Flickable.StopAtBounds

                            delegate: CarouselBoardThumb {
                                height: boardFilmstrip.height
                                onSelectedRequested: function(index) { carouselDocument.selectSlide(index) }
                                onDuplicateRequested: function(index) { carouselDocument.duplicateCurrentSlide() }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "carouselInspectorPanel"
            readonly property string surfaceContract: "carousel-inspector:v1|board-settings:when-no-object|selection-settings:contextual|layers:always|media:one-expansion|actions:menus|no-elide:true|no-overlap:true"
            Layout.preferredWidth: root.compactWorkspace ? 248 : 268
            Layout.minimumWidth: 228
            Layout.fillHeight: true
            radius: theme.radius8
            color: theme.alpha(theme.ink900, 0.92)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.36)

            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth

                Column {
                    x: theme.panelPadding
                    y: theme.panelPadding
                    width: Math.max(parent.width - theme.panelPadding * 2, 0)
                    spacing: 8

                    InspectorSection {
                        width: parent.width
                        visible: !selection.hasSelection
                        title: "Boards"
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: true

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "+"
                                enabled: carouselDocument.canAddSlide
                                onClicked: carouselDocument.addSlide()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Duplicate"
                                enabled: carouselDocument.canAddSlide
                                onClicked: carouselDocument.duplicateCurrentSlide()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "chevron-left"
                                showText: false
                                enabled: carouselDocument.slideCount > 1
                                onClicked: carouselDocument.moveCurrentBackward()
                            }

                            Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: (carouselDocument.currentIndex + 1) + " / " + carouselDocument.slideCount
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 11
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "chevron-right"
                                showText: false
                                enabled: carouselDocument.slideCount > 1
                                onClicked: carouselDocument.moveCurrentForward()
                            }

                            ConfirmActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "x"
                                enabled: carouselDocument.slideCount > 1
                                helperText: "Remove board"
                                onConfirmed: carouselDocument.removeCurrentSlide()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: root.formatButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: carouselDocument.documentFormat === modelData.value
                                    onClicked: carouselDocument.documentFormat = modelData.value
                                    ToolTip.visible: formatHover.containsMouse
                                    ToolTip.text: modelData.size
                                    ToolTip.delay: 400

                                    MouseArea {
                                        id: formatHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        acceptedButtons: Qt.NoButton
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: root.templateButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.templateKind === modelData.value || selection.groupKind === modelData.value
                                    onClicked: carouselDocument.applyTemplateToCurrent(modelData.value)
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Background"
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.backgroundColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.backgroundColor === modelData
                                    accent: theme.accentWarm400
                                    label: "Background " + modelData
                                    onPicked: function(swatchColor) {
                                        if (selection.backgroundKind === "solid") {
                                            carouselDocument.setCurrentSlideBackgroundColor(swatchColor)
                                        } else {
                                            carouselDocument.setCurrentSlideBackgroundGradient(
                                                        selection.backgroundKind,
                                                        swatchColor,
                                                        selection.backgroundSecondaryColor)
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.backgroundGradientButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.backgroundKind === modelData.value
                                    onClicked: {
                                        if (modelData.value === "solid") {
                                            carouselDocument.setCurrentSlideBackgroundColor(selection.backgroundColor)
                                        } else {
                                            carouselDocument.setCurrentSlideBackgroundGradient(
                                                        modelData.value,
                                                        selection.backgroundColor,
                                                        selection.backgroundSecondaryColor)
                                        }
                                    }
                                }
                            }
                        }

                        TextField {
                            id: boardBackgroundField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.textStrong
                            font.pixelSize: 11
                            font.family: theme.fontMono
                            placeholderText: "#fbf7f1"
                            background: Rectangle {
                                radius: 4
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.4)
                            }
                            onEditingFinished: {
                                if (selection.backgroundKind === "solid") {
                                    carouselDocument.setCurrentSlideBackgroundColor(text)
                                } else {
                                    carouselDocument.setCurrentSlideBackgroundGradient(
                                                selection.backgroundKind,
                                                text,
                                                selection.backgroundSecondaryColor)
                                }
                            }
                        }

                        Binding {
                            target: boardBackgroundField
                            property: "text"
                            value: selection.backgroundColor
                            when: !selection.hasSelection && !boardBackgroundField.activeFocus
                        }

                        TextField {
                            id: boardBackgroundSecondaryField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            visible: selection.backgroundKind !== "solid"
                            color: theme.textStrong
                            font.pixelSize: 11
                            font.family: theme.fontMono
                            placeholderText: "#ffffff"
                            background: Rectangle {
                                radius: 4
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.4)
                            }
                            onEditingFinished: {
                                carouselDocument.setCurrentSlideBackgroundGradient(
                                            selection.backgroundKind,
                                            selection.backgroundColor,
                                            text)
                            }
                        }

                        Binding {
                            target: boardBackgroundSecondaryField
                            property: "text"
                            value: selection.backgroundSecondaryColor
                            when: !selection.hasSelection && !boardBackgroundSecondaryField.activeFocus
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: !selection.hasSelection
                        title: "Media"
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: true

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            FilterChip {
                                Layout.fillWidth: true
                                text: selection.templateKind === "split-pair" ? "Left" : "Top"
                                active: selection.activeSlot === "primary"
                                onClicked: carouselDocument.setActiveSlot("primary")
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                visible: selection.canUseSecondary
                                text: selection.templateKind === "split-pair" ? "Right" : "Middle"
                                active: selection.activeSlot === "secondary"
                                onClicked: carouselDocument.setActiveSlot("secondary")
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                visible: selection.canUseTertiary
                                text: "Bottom"
                                active: selection.activeSlot === "tertiary"
                                onClicked: carouselDocument.setActiveSlot("tertiary")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Choose"
                                tone: "primary"
                                onClicked: openMediaPicker("slot")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Clear"
                                enabled: selection.isLinkedGroup || selection.hasMedia
                                onClicked: {
                                    if (selection.isLinkedGroup)
                                        carouselDocument.clearCurrentGroup()
                                    else
                                        carouselDocument.clearCurrentSlot()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            visible: root.mediaPickerAssets.length > 0

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    Layout.fillWidth: true
                                    text: "Edited Library"
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }

                                ActionButton {
                                    compact: true
                                    text: "More"
                                    onClicked: root.openMediaPicker("slot")
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 78
                                orientation: ListView.Horizontal
                                spacing: 6
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: root.mediaPickerAssets.slice(0, root.quickLibraryLimit)

                                delegate: CarouselMediaThumb {
                                    required property var modelData

                                    asset: modelData
                                    onActivated: function(asset, modifiers) {
                                        root.placeLibraryAsset(asset, root.hasMultiSelectModifier(modifiers) ? "layer" : "slot")
                                    }
                                    onDoubleActivated: function(asset) {
                                        root.placeLibraryAsset(asset, "layer")
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Fill"
                                active: selection.mediaFitMode === "cover"
                                onClicked: carouselDocument.setCurrentFitMode("cover")
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Fit"
                                active: selection.mediaFitMode === "contain"
                                onClicked: carouselDocument.setCurrentFitMode("contain")
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Scale"
                            valueText: selection.scale.toFixed(2) + "x"
                            from: 0.6
                            to: 3.5
                            stepSize: 0.01
                            value: selection.scale
                            accent: theme.accentWarm400
                            onValueDragged: function(value) {
                                carouselDocument.setCurrentScale(value)
                            }
                            onResetRequested: carouselDocument.setCurrentScale(1.0)
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Offset X"
                            valueText: Math.round(selection.offsetX * 100) + "%"
                            from: -1.0
                            to: 1.0
                            stepSize: 0.01
                            value: selection.offsetX
                            accent: theme.accentCool400
                            onValueDragged: function(value) {
                                carouselDocument.setCurrentOffsetX(value)
                            }
                            onResetRequested: carouselDocument.setCurrentOffsetX(0)
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Offset Y"
                            valueText: Math.round(selection.offsetY * 100) + "%"
                            from: -1.0
                            to: 1.0
                            stepSize: 0.01
                            value: selection.offsetY
                            accent: theme.accentCool400
                            onValueDragged: function(value) {
                                carouselDocument.setCurrentOffsetY(value)
                            }
                            onResetRequested: carouselDocument.setCurrentOffsetY(0)
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: !selection.hasSelection
                        title: "Guides"
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: false

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Thirds"
                                active: root.thirdsGuidesEnabled
                                onClicked: root.thirdsGuidesEnabled = !root.thirdsGuidesEnabled
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Safe"
                                active: root.safeGuidesEnabled
                                onClicked: root.safeGuidesEnabled = !root.safeGuidesEnabled
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Opacity"
                            valueText: Math.round(root.guideOpacity * 100) + "%"
                            from: 0.1
                            to: 1.0
                            stepSize: 0.05
                            value: root.guideOpacity
                            accent: theme.accentCool400
                            onValueDragged: function(value) {
                                root.guideOpacity = value
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "V Guide"
                                onClicked: root.addCustomGuide("vertical")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "H Guide"
                                onClicked: root.addCustomGuide("horizontal")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Clear"
                                enabled: root.customGuides.length > 0
                                onClicked: root.clearAllCustomGuides()
                            }
                        }

                        Label {
                            visible: root.customGuides.length > 0
                            Layout.fillWidth: true
                            text: root.customGuides.length + " custom guide" + (root.customGuides.length !== 1 ? "s" : "") + "  •  double-click to remove"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        title: "Layers"
                        subtitle: currentSlideLayers.length + " on board " + (carouselDocument.currentIndex + 1)
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: true

                        Rectangle {
                            Layout.fillWidth: true
                            radius: theme.radiusControl
                            color: theme.alpha(theme.ink850, 0.88)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.5)
                            implicitHeight: currentSlideLayers.length === 0 ? 48 : Math.min(220, Math.max(72, currentSlideLayers.length * 46 + 10))

                            ListView {
                                visible: currentSlideLayers.length > 0
                                anchors.fill: parent
                                anchors.margins: 5
                                clip: true
                                spacing: 3
                                model: root.currentSlideLayers

                                delegate: CarouselLayerRow {
                                    required property var modelData

                                    width: parent ? parent.width : 200
                                    height: 40
                                    objectName: "carouselLayerRow"
                                    itemId: String(modelData.itemId || "")
                                    label: String(modelData.displayLabel || "Layer")
                                    layerType: String(modelData.type || "media")
                                    selected: Boolean(modelData.selected)
                                    primarySelected: Boolean(modelData.primarySelected)
                                    hidden: Boolean(modelData.hidden)
                                    locked: Boolean(modelData.locked)
                                    accent: theme.accentCool400
                                    onActivated: function(modifiers) {
                                        if (root.hasMultiSelectModifier(modifiers))
                                            carouselDocument.toggleItemSelection(String(modelData.itemId || ""))
                                        else
                                            root.focusLayerItem(String(modelData.itemId || ""), Boolean(modelData.selected))
                                    }
                                }
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: currentSlideLayers.length === 0
                                text: "No layers"
                                color: theme.textFaint
                                font.family: theme.fontUi
                                font.pixelSize: 10
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            MenuButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Photo"
                                iconName: "media"
                                tone: "primary"
                                tooltipText: "Add a photo layer. Open the menu for text and shape layers."
                                menuItems: root.carouselLayerAddMenuItems()
                                menuWidth: 176
                                onClicked: root.openMediaPicker("layer")
                                onMenuItemTriggered: root.handleCarouselLayerAddMenuAction(key)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Text"
                                onClicked: carouselDocument.addTextItem("New Text")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Shape"
                                onClicked: carouselDocument.addShapeItem("rectangle")
                            }

                            ConfirmActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Del"
                                enabled: selection.hasSelection
                                helperText: "Remove layer"
                                onConfirmed: carouselDocument.removeSelectedItem()
                            }
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: selection.hasSelection
                        title: "Transform"
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: true

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: "X"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            TextField {
                                id: transformXField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 26
                                enabled: selection.hasSelection && !selection.hasMultipleSelection && selection.hasUnlockedSelection
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                                selectByMouse: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                validator: DoubleValidator { decimals: 1 }
                                background: Rectangle {
                                    radius: 4
                                    color: theme.alpha(theme.ink850, 0.9)
                                    border.width: 1
                                    border.color: transformXField.activeFocus ? theme.accentCool400 : theme.alpha(theme.lineSoft, 0.4)
                                }
                                onEditingFinished: {
                                    const value = Number(text)
                                    if (Number.isFinite(value))
                                        carouselDocument.setSelectedItemCenter(value + Number(selection.width || 0) / 2, Number(selection.centerY || 0))
                                }
                            }

                            Label {
                                text: "Y"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            TextField {
                                id: transformYField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 26
                                enabled: selection.hasSelection && !selection.hasMultipleSelection && selection.hasUnlockedSelection
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                                selectByMouse: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                validator: DoubleValidator { decimals: 1 }
                                background: Rectangle {
                                    radius: 4
                                    color: theme.alpha(theme.ink850, 0.9)
                                    border.width: 1
                                    border.color: transformYField.activeFocus ? theme.accentCool400 : theme.alpha(theme.lineSoft, 0.4)
                                }
                                onEditingFinished: {
                                    const value = Number(text)
                                    if (Number.isFinite(value))
                                        carouselDocument.setSelectedItemCenter(Number(selection.centerX || 0), value + Number(selection.height || 0) / 2)
                                }
                            }

                            Label {
                                text: "R"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            TextField {
                                id: transformRotationField
                                Layout.preferredWidth: 54
                                Layout.preferredHeight: 26
                                enabled: selection.hasSelection && !selection.hasMultipleSelection && selection.hasUnlockedSelection
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                                selectByMouse: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                validator: DoubleValidator { decimals: 1 }
                                background: Rectangle {
                                    radius: 4
                                    color: theme.alpha(theme.ink850, 0.9)
                                    border.width: 1
                                    border.color: transformRotationField.activeFocus ? theme.accentCool400 : theme.alpha(theme.lineSoft, 0.4)
                                }
                                onEditingFinished: {
                                    const value = Number(text)
                                    if (Number.isFinite(value))
                                        carouselDocument.setSelectedItemRotation(value)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: "W"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            TextField {
                                id: transformWidthField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 26
                                enabled: selection.hasSelection && !selection.hasMultipleSelection && selection.hasUnlockedSelection
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                                selectByMouse: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                validator: DoubleValidator { bottom: 1; decimals: 1 }
                                background: Rectangle {
                                    radius: 4
                                    color: theme.alpha(theme.ink850, 0.9)
                                    border.width: 1
                                    border.color: transformWidthField.activeFocus ? theme.accentCool400 : theme.alpha(theme.lineSoft, 0.4)
                                }
                                onEditingFinished: {
                                    const value = Number(text)
                                    root.resizeSelectedFromTransformField("width", value)
                                }
                            }

                            ActionButton {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 26
                                compact: true
                                iconName: "link"
                                showText: false
                                active: root.transformAspectLocked
                                enabled: selection.hasSelection && !selection.hasMultipleSelection && selection.hasUnlockedSelection
                                tooltipText: root.transformAspectLocked ? "Aspect ratio locked" : "Aspect ratio unlocked"
                                onClicked: root.transformAspectLocked = !root.transformAspectLocked
                            }

                            Label {
                                text: "H"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            TextField {
                                id: transformHeightField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 26
                                enabled: selection.hasSelection && !selection.hasMultipleSelection && selection.hasUnlockedSelection
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignRight
                                selectByMouse: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                validator: DoubleValidator { bottom: 1; decimals: 1 }
                                background: Rectangle {
                                    radius: 4
                                    color: theme.alpha(theme.ink850, 0.9)
                                    border.width: 1
                                    border.color: transformHeightField.activeFocus ? theme.accentCool400 : theme.alpha(theme.lineSoft, 0.4)
                                }
                                onEditingFinished: {
                                    const value = Number(text)
                                    root.resizeSelectedFromTransformField("height", value)
                                }
                            }
                        }

                        Binding {
                            target: transformXField
                            property: "text"
                            value: Math.round(selection.x).toString()
                            when: selection.hasSelection && !transformXField.activeFocus
                        }

                        Binding {
                            target: transformYField
                            property: "text"
                            value: Math.round(selection.y).toString()
                            when: selection.hasSelection && !transformYField.activeFocus
                        }

                        Binding {
                            target: transformRotationField
                            property: "text"
                            value: Math.round(selection.rotation).toString()
                            when: selection.hasSelection && !transformRotationField.activeFocus
                        }

                        Binding {
                            target: transformWidthField
                            property: "text"
                            value: Math.round(selection.width).toString()
                            when: selection.hasSelection && !transformWidthField.activeFocus
                        }

                        Binding {
                            target: transformHeightField
                            property: "text"
                            value: Math.round(selection.height).toString()
                            when: selection.hasSelection && !transformHeightField.activeFocus
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Flip H"
                                active: Boolean(selection.flippedHorizontal)
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.setSelectedItemFlippedHorizontal(!Boolean(selection.flippedHorizontal))
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Flip V"
                                active: Boolean(selection.flippedVertical)
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.setSelectedItemFlippedVertical(!Boolean(selection.flippedVertical))
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: selection.isImage || selection.isVideo

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Fill"
                                active: selection.itemFitMode === "cover"
                                enabled: selection.hasUnlockedSelection
                                onClicked: carouselDocument.setCurrentFitMode("cover")
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Fit"
                                active: selection.itemFitMode === "contain"
                                enabled: selection.hasUnlockedSelection
                                onClicked: carouselDocument.setCurrentFitMode("contain")
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: selection.isImage || selection.isVideo
                            label: "Radius"
                            valueText: Math.round(selection.cornerRadius) + " px"
                            from: 0
                            to: 360
                            stepSize: 1
                            value: selection.cornerRadius
                            accent: theme.accentCool400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedItemCornerRadius(value)
                            }
                            onResetRequested: carouselDocument.setSelectedItemCornerRadius(0)
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: selection.isImage || selection.isVideo
                            label: "Frame"
                            valueText: Math.round(selection.frameWidth) + " px"
                            from: 0
                            to: 160
                            stepSize: 1
                            value: selection.frameWidth
                            accent: theme.accentWarm400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedItemFrameWidth(value)
                            }
                            onResetRequested: carouselDocument.setSelectedItemFrameWidth(0)
                        }

                        Flow {
                            Layout.fillWidth: true
                            visible: selection.isImage || selection.isVideo
                            spacing: 3

                            Repeater {
                                model: root.textColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.frameColor === modelData
                                    accent: theme.accentWarm400
                                    label: "Frame " + modelData
                                    onPicked: function(swatchColor) { carouselDocument.setSelectedItemFrameColor(swatchColor) }
                                }
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Shadow"
                            valueText: Math.round(selection.shadowOpacity * 100) + "%"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: selection.shadowOpacity
                            accent: theme.accentCool400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedItemShadowOpacity(value)
                            }
                            onResetRequested: carouselDocument.setSelectedItemShadowOpacity(0)
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadow X"
                                valueText: Math.round(selection.shadowOffsetX) + " px"
                                from: -160
                                to: 160
                                stepSize: 1
                                value: selection.shadowOffsetX
                                accent: theme.accentWarm400
                                onValueDragged: function(value) {
                                    carouselDocument.setSelectedItemShadowOffset(value, selection.shadowOffsetY)
                                }
                                onResetRequested: carouselDocument.setSelectedItemShadowOffset(32, selection.shadowOffsetY)
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Shadow Y"
                                valueText: Math.round(selection.shadowOffsetY) + " px"
                                from: -160
                                to: 160
                                stepSize: 1
                                value: selection.shadowOffsetY
                                accent: theme.accentWarm400
                                onValueDragged: function(value) {
                                    carouselDocument.setSelectedItemShadowOffset(selection.shadowOffsetX, value)
                                }
                                onResetRequested: carouselDocument.setSelectedItemShadowOffset(selection.shadowOffsetX, 32)
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.shadowColor === modelData
                                    accent: theme.accentCool400
                                    label: "Shadow " + modelData
                                    onPicked: function(swatchColor) { carouselDocument.setSelectedItemShadowColor(swatchColor) }
                                }
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Opacity"
                            valueText: Math.round(selection.opacity * 100) + "%"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: selection.opacity
                            accent: theme.accentCool400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedItemOpacity(value)
                            }
                            onResetRequested: carouselDocument.setSelectedItemOpacity(1.0)
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: root.blendModeButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.blendMode === modelData.value
                                    enabled: selection.hasSelection && selection.hasUnlockedSelection
                                    onClicked: carouselDocument.setSelectedItemBlendMode(modelData.value)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "chevron-left"
                                showText: false
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionLeft()
                                ToolTip.visible: leftHover.containsMouse
                                ToolTip.text: "Align Left"
                                ToolTip.delay: 400
                                MouseArea { id: leftHover; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "split-view"
                                showText: false
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionHorizontalCenter()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "chevron-right"
                                showText: false
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionRight()
                            }

                            Rectangle {
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 18
                                color: theme.alpha(theme.lineSoft, 0.24)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Duplicate"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.duplicateSelectedItem()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Top"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionTop()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Middle"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionVerticalCenter()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Bottom"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionBottom()
                            }

                            Rectangle {
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 18
                                color: theme.alpha(theme.lineSoft, 0.24)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Dist H"
                                enabled: selection.selectedCount > 2 && selection.hasUnlockedSelection
                                onClicked: carouselDocument.distributeSelectionHorizontally()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Dist V"
                                enabled: selection.selectedCount > 2 && selection.hasUnlockedSelection
                                onClicked: carouselDocument.distributeSelectionVertically()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            visible: selection.hasSelection && selection.hasUnlockedSelection

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconText: "B"
                                showText: false
                                tooltipText: "Send to Back"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.sendSelectedToBack()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "move-back"
                                showText: false
                                tooltipText: "Send Backward"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.sendSelectedBackward()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "move-forward"
                                showText: false
                                tooltipText: "Bring Forward"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.bringSelectedForward()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconText: "F"
                                showText: false
                                tooltipText: "Bring to Front"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.bringSelectedToFront()
                            }

                            Rectangle {
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 18
                                color: theme.alpha(theme.lineSoft, 0.24)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: selection.hidden ? "Show" : "Hide"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.setSelectedItemHidden(!selection.hidden)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: selection.locked ? "Unlock" : "Lock"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.setSelectedItemLocked(!selection.locked)
                            }
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: selection.isShape && !selection.hasMultipleSelection
                        title: "Shape"
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: true

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: root.shapeButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.shapeKind === modelData.value
                                    onClicked: carouselDocument.setSelectedShapeKind(modelData.value)
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Fill"
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: root.shapeFillKindButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: String(selection.fillKind || "solid") === modelData.value
                                    onClicked: carouselDocument.setSelectedShapeFillKind(modelData.value)
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.fillColor === modelData
                                    accent: theme.accentWarm400
                                    label: "Shape fill " + modelData
                                    onPicked: function(swatchColor) { carouselDocument.setSelectedShapeFillColor(swatchColor) }
                                }
                            }
                        }

                        TextField {
                            id: shapeFillField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.textStrong
                            font.pixelSize: 11
                            font.family: theme.fontMono
                            placeholderText: "#ffffff"
                            background: Rectangle {
                                radius: 4
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.4)
                            }
                            onEditingFinished: {
                                if (selection.isShape)
                                    carouselDocument.setSelectedShapeFillColor(text)
                            }
                        }

                        Binding {
                            target: shapeFillField
                            property: "text"
                            value: selection.fillColor
                            when: selection.isShape && !shapeFillField.activeFocus
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: String(selection.fillKind || "solid") !== "solid"
                            text: "Second"
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        Flow {
                            Layout.fillWidth: true
                            visible: String(selection.fillKind || "solid") !== "solid"
                            spacing: 3

                            Repeater {
                                model: root.textColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.fillSecondaryColor === modelData
                                    accent: theme.accentCool400
                                    label: "Shape secondary fill " + modelData
                                    onPicked: function(swatchColor) { carouselDocument.setSelectedShapeSecondaryFillColor(swatchColor) }
                                }
                            }
                        }

                        TextField {
                            id: shapeSecondaryFillField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            visible: String(selection.fillKind || "solid") !== "solid"
                            color: theme.textStrong
                            font.pixelSize: 11
                            font.family: theme.fontMono
                            placeholderText: "#69cad8"
                            background: Rectangle {
                                radius: 4
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.4)
                            }
                            onEditingFinished: {
                                if (selection.isShape)
                                    carouselDocument.setSelectedShapeSecondaryFillColor(text)
                            }
                        }

                        Binding {
                            target: shapeSecondaryFillField
                            property: "text"
                            value: selection.fillSecondaryColor
                            when: selection.isShape && !shapeSecondaryFillField.activeFocus
                        }

                        TextField {
                            id: shapeStrokeField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.textStrong
                            font.pixelSize: 11
                            font.family: theme.fontMono
                            placeholderText: "#1e1a17"
                            background: Rectangle {
                                radius: 4
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.4)
                            }
                            onEditingFinished: {
                                if (selection.isShape)
                                    carouselDocument.setSelectedShapeStrokeColor(text)
                            }
                        }

                        Binding {
                            target: shapeStrokeField
                            property: "text"
                            value: selection.strokeColor
                            when: selection.isShape && !shapeStrokeField.activeFocus
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Stroke"
                            valueText: Math.round(selection.strokeWidth) + " px"
                            from: 0
                            to: 96
                            stepSize: 1
                            value: selection.strokeWidth
                            accent: theme.accentWarm400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedShapeStrokeWidth(value)
                            }
                            onResetRequested: carouselDocument.setSelectedShapeStrokeWidth(0)
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            visible: selection.shapeKind === "rectangle"
                            label: "Radius"
                            valueText: Math.round(selection.cornerRadius) + " px"
                            from: 0
                            to: 240
                            stepSize: 1
                            value: selection.cornerRadius
                            accent: theme.success
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedShapeCornerRadius(value)
                            }
                            onResetRequested: carouselDocument.setSelectedShapeCornerRadius(0)
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: (selection.isImage || selection.isVideo) && !selection.hasMultipleSelection
                        title: selection.isVideo ? "Video" : "Photo"
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: true

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Fill"
                                active: selection.fitMode === "cover"
                                onClicked: carouselDocument.setCurrentFitMode("cover")
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Fit"
                                active: selection.fitMode === "contain"
                                onClicked: carouselDocument.setCurrentFitMode("contain")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Rect"
                                active: String(selection.mediaMaskShape || "rectangle") === "rectangle"
                                onClicked: carouselDocument.setSelectedItemMediaMaskShape("rectangle")
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Oval"
                                active: String(selection.mediaMaskShape || "rectangle") === "ellipse"
                                onClicked: carouselDocument.setSelectedItemMediaMaskShape("ellipse")
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Crop"
                            valueText: Number(selection.mediaScale || 1).toFixed(2) + "x"
                            from: 0.6
                            to: 3.5
                            stepSize: 0.01
                            value: selection.mediaScale
                            accent: theme.accentWarm400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedItemMediaScale(value)
                            }
                            onResetRequested: carouselDocument.resetSelectedItemMediaCrop()
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Pan X"
                                valueText: Math.round(Number(selection.mediaOffsetX || 0) * 100) + "%"
                                from: -1
                                to: 1
                                stepSize: 0.01
                                value: selection.mediaOffsetX
                                accent: theme.accentCool400
                                onValueDragged: function(value) {
                                    carouselDocument.setSelectedItemMediaOffset(value, selection.mediaOffsetY)
                                }
                                onResetRequested: carouselDocument.setSelectedItemMediaOffset(0, selection.mediaOffsetY)
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Pan Y"
                                valueText: Math.round(Number(selection.mediaOffsetY || 0) * 100) + "%"
                                from: -1
                                to: 1
                                stepSize: 0.01
                                value: selection.mediaOffsetY
                                accent: theme.accentCool400
                                onValueDragged: function(value) {
                                    carouselDocument.setSelectedItemMediaOffset(selection.mediaOffsetX, value)
                                }
                                onResetRequested: carouselDocument.setSelectedItemMediaOffset(selection.mediaOffsetX, 0)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Replace"
                                tone: "primary"
                                enabled: selection.hasSelection && !selection.isText
                                onClicked: root.openMediaPicker("replace")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Reset Fit"
                                enabled: selection.hasSelection && !selection.isText
                                onClicked: {
                                    carouselDocument.setCurrentFitMode("cover")
                                    carouselDocument.resetSelectedItemMediaCrop()
                                }
                            }
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: selection.isText && !selection.hasMultipleSelection
                        title: "Text"
                        showAccentMarker: false
                        uppercaseTitle: false
                        titleFontFamily: theme.fontDisplay
                        titlePixelSize: 15
                        titleWeight: Font.Bold
                        titleLetterSpacing: 0
                        expandable: true
                        expanded: true

                        TextArea {
                            id: textEditor
                            Layout.fillWidth: true
                            wrapMode: TextEdit.Wrap
                            color: theme.textStrong
                            placeholderText: "Enter text"
                            selectByMouse: true
                            background: Rectangle {
                                radius: theme.radiusControl
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.5)
                            }
                            onActiveFocusChanged: {
                                if (!activeFocus && selection.isText)
                                    carouselDocument.setSelectedText(text)
                            }
                            onTextChanged: {
                                if (activeFocus && selection.isText)
                                    carouselDocument.setSelectedText(text)
                            }
                        }

                        Binding {
                            target: textEditor
                            property: "text"
                            value: selection.text
                            when: selection.isText && !textEditor.activeFocus
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.textColor === modelData
                                    accent: theme.accentWarm400
                                    label: "Text " + modelData
                                    onPicked: function(swatchColor) { carouselDocument.setSelectedTextColor(swatchColor) }
                                }
                            }
                        }

                        TextField {
                            id: textColorField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: theme.textStrong
                            font.pixelSize: 11
                            font.family: theme.fontMono
                            placeholderText: "#1e1a17"
                            background: Rectangle {
                                radius: 4
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.4)
                            }
                            onEditingFinished: {
                                if (selection.isText)
                                    carouselDocument.setSelectedTextColor(text)
                            }
                        }

                        Binding {
                            target: textColorField
                            property: "text"
                            value: selection.textColor
                            when: selection.isText && !textColorField.activeFocus
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Box"
                            valueText: Math.round(selection.textBackgroundOpacity * 100) + "%"
                            from: 0
                            to: 1
                            stepSize: 0.01
                            value: selection.textBackgroundOpacity
                            accent: theme.accentWarm400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedTextBackgroundOpacity(value)
                            }
                            onResetRequested: carouselDocument.setSelectedTextBackgroundOpacity(0)
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.textBackgroundColor === modelData
                                    accent: theme.accentWarm400
                                    label: "Text box " + modelData
                                    onPicked: function(swatchColor) { carouselDocument.setSelectedTextBackgroundColor(swatchColor) }
                                }
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Size"
                            valueText: Math.round(selection.textPointSize) + " pt"
                            from: 16
                            to: 240
                            stepSize: 1
                            value: selection.textPointSize
                            accent: theme.success
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedTextPointSize(value)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textSizePresets

                                delegate: ActionButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    compact: true
                                    active: Math.abs(selection.textPointSize - modelData.value) < 2
                                    onClicked: carouselDocument.setSelectedTextPointSize(modelData.value)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textFontFamilyButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.textFontFamily === modelData.value
                                    onClicked: carouselDocument.setSelectedTextFontFamily(modelData.value)
                                }
                            }
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Tracking"
                            valueText: Math.round(selection.textLetterSpacing) + " px"
                            from: 0
                            to: 80
                            stepSize: 1
                            value: selection.textLetterSpacing
                            accent: theme.accentCool400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedTextLetterSpacing(value)
                            }
                            onResetRequested: carouselDocument.setSelectedTextLetterSpacing(0)
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Stroke"
                            valueText: Math.round(selection.textStrokeWidth) + " px"
                            from: 0
                            to: 32
                            stepSize: 1
                            value: selection.textStrokeWidth
                            accent: theme.accentWarm400
                            onValueDragged: function(value) {
                                carouselDocument.setSelectedTextStrokeWidth(value)
                            }
                            onResetRequested: carouselDocument.setSelectedTextStrokeWidth(0)
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textColorPresets

                                delegate: ColorSwatchButton {
                                    required property string modelData

                                    swatchColor: modelData
                                    selected: selection.textStrokeColor === modelData
                                    accent: theme.accentCool400
                                    label: "Text stroke " + modelData
                                    onPicked: function(swatchColor) { carouselDocument.setSelectedTextStrokeColor(swatchColor) }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textWeightButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.textWeight === modelData.value
                                    onClicked: carouselDocument.setSelectedTextWeight(modelData.value)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            FilterChip {
                                Layout.fillWidth: true
                                text: "Italic"
                                active: selection.textItalic
                                onClicked: carouselDocument.setSelectedTextItalic(!selection.textItalic)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textHorizontalAlignmentButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.textHorizontalAlignment === modelData.value
                                    onClicked: carouselDocument.setSelectedTextHorizontalAlignment(modelData.value)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Repeater {
                                model: root.textVerticalAlignmentButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    active: selection.textVerticalAlignment === modelData.value
                                    onClicked: carouselDocument.setSelectedTextVerticalAlignment(modelData.value)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    GalleryPickerDialog {
        id: mediaPickerDialog
        theme: theme
        dialogTitle: "Gallery"
        subtitle: root.mediaPickerSubtitle()
        emptyStateText: root.mediaPickerEmptyStateText()
        selectionHintText: mediaPickerIntent === "replace"
                           ? "Choose media to replace the selected layer"
                           : mediaPickerIntent === "layer" ? "Choose a photo or video layer" : "Choose media for the active slot"
        showProjectScopeToggle: root.hasProjectScopedMediaPicker()
        scope: root.mediaPickerScope
        assets: root.mediaPickerAssets
        onProjectScopeRequested: root.setMediaPickerScope("project")
        onAllMediaScopeRequested: root.setMediaPickerScope("all")
        onAssetActivated: root.applyPickedMedia()
        sidePanelFooter: Component {
            ColumnLayout {
                width: parent ? parent.width : undefined
                spacing: 8

                ActionButton {
                    Layout.fillWidth: true
                    text: mediaPickerIntent === "replace" ? "Replace Layer" : mediaPickerIntent === "layer" ? "Add As Layer" : "Add To Slot"
                    tone: "primary"
                    enabled: mediaPickerDialog.hasSelection
                             && mediaPickerDialog.selectedOnline
                             && (mediaPickerIntent === "replace"
                                 ? (mediaPickerDialog.selectedMediaType === "photo"
                                    || mediaPickerDialog.selectedMediaType === "video")
                                 : mediaPickerIntent === "layer"
                                 ? (mediaPickerDialog.selectedMediaType === "photo"
                                    || mediaPickerDialog.selectedMediaType === "video")
                                 : (mediaPickerDialog.selectedMediaType === "photo"
                                    || (mediaPickerDialog.selectedMediaType === "video" && root.canPlaceVideo())))
                    onClicked: root.applyPickedMedia()
                }
            }
        }
    }
}
