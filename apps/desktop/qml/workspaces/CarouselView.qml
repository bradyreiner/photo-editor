pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml
import "../components"

Rectangle {
    id: root
    color: "transparent"
    focus: true

    Theme { id: theme }

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
    readonly property var textSizePresets: [
        { label: "S", value: 36 },
        { label: "M", value: 64 },
        { label: "L", value: 96 },
        { label: "XL", value: 144 },
        { label: "2XL", value: 200 }
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
        { value: "media", label: "Media", key: "M", cursor: Qt.PointingHandCursor }
    ]

    function slideWidthOnStrip() {
        return carouselDocument.slideCount > 0 ? stripSurface.width / carouselDocument.slideCount : stripSurface.width
    }

    function clampZoom(value) {
        return Math.max(0.5, Math.min(3.0, value))
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
        const normalized = toolName === "hand" || toolName === "text" || toolName === "media" ? toolName : "select"
        activeTool = normalized
    }

    function toolCursorShape() {
        if (activeTool === "hand")
            return Qt.OpenHandCursor
        if (activeTool === "text")
            return Qt.IBeamCursor
        if (activeTool === "media")
            return Qt.PointingHandCursor
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

        carouselDocument.clearSelection()
        root.clearSnapGuides()
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
        sequence: StandardKey.Delete
        enabled: carouselDocument.hasActiveCarousel
        onActivated: carouselDocument.removeSelectedItem()
    }

    Shortcut {
        sequence: StandardKey.Copy
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
        sequence: "Shift+T"
        onActivated: carouselDocument.addTextItem("New Text")
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

                        delegate: Rectangle {
                            required property var modelData
                            required property int index

                            width: recentCarouselList.width
                            height: 52
                            radius: 8
                            color: recentCarouselHover.containsMouse
                                   ? theme.alpha(theme.accentCool400, 0.16)
                                   : theme.alpha(theme.ink900, 0.62)
                            border.width: 1
                            border.color: recentCarouselHover.containsMouse
                                          ? theme.alpha(theme.accentCool400, 0.42)
                                          : theme.alpha(theme.lineSoft, 0.24)

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                anchors.topMargin: 6
                                anchors.bottomMargin: 6
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name || "Untitled"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        const fmt = String(modelData.documentFormat || "4x5").toUpperCase()
                                        const slides = Number(modelData.slideCount || 0)
                                        const date = String(modelData.updatedAt || modelData.createdAt || "")
                                        return fmt + "  •  " + slides + " slide" + (slides !== 1 ? "s" : "")
                                               + (date.length > 0 ? "  •  " + date : "")
                                    }
                                    color: theme.textFaint
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: recentCarouselHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    carouselDocument.loadDocument(String(modelData.documentId || ""))
                                }
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
        anchors.fill: parent
        visible: carouselDocument.hasActiveCarousel
        spacing: 6

        Rectangle {
            Layout.preferredWidth: 44
            Layout.fillHeight: true
            radius: theme.radius8
            color: theme.alpha(theme.ink900, 0.88)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.36)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 5
                spacing: 4

                Repeater {
                    model: root.toolButtons

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: 8
                        color: root.activeTool === modelData.value
                               ? theme.alpha(theme.accentWarm500, 0.24)
                               : toolHover.containsMouse
                                 ? theme.alpha(theme.ink800, 0.8)
                                 : "transparent"
                        border.width: root.activeTool === modelData.value ? 1 : 0
                        border.color: theme.alpha(theme.accentWarm400, 0.6)

                        AppIcon {
                            visible: modelData.value === "select" || modelData.value === "media"
                            anchors.centerIn: parent
                            width: 16
                            height: 16
                            name: modelData.value === "select" ? "select" : "media"
                            color: root.activeTool === modelData.value ? theme.accentWarm300 : theme.textMain
                        }

                        Label {
                            visible: modelData.value === "hand" || modelData.value === "text"
                            anchors.centerIn: parent
                            text: modelData.value === "hand" ? "H" : "T"
                            color: root.activeTool === modelData.value ? theme.accentWarm300 : theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }

                        MouseArea {
                            id: toolHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setActiveTool(modelData.value)
                        }

                        ToolTip {
                            visible: toolHover.containsMouse
                            text: modelData.label + " (" + modelData.key + ")"
                            delay: 400
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
                        onClicked: carouselDocument.saveDocument()
                    }

                    ActionButton {
                        text: "Export"
                        tone: "primary"
                        compact: true
                        enabled: !carouselDocument.exportBusy
                        onClicked: carouselDocument.exportCarousel()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 18
                    color: theme.alpha(theme.ink950, 0.88)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.42)

                    Flickable {
                        id: artboardFlick
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
                                        color: index % 2 === 0 ? "#fffdf8" : "#fff9f2"
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
                                                anchors.left: parent.left
                                                anchors.top: parent.top
                                                anchors.margins: 10
                                                text: modelData.label
                                                color: slotRegion.isActive ? theme.textStrong : "#8a7360"
                                                font.family: theme.fontMono
                                                font.pixelSize: 10
                                            }

                                            Label {
                                                anchors.centerIn: parent
                                                visible: !modelData.hasMedia
                                                text: "Click to add media"
                                                color: theme.alpha(theme.textMuted, 0.92)
                                                font.family: theme.fontUi
                                                font.pixelSize: 12
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
                                    opacity: Boolean(modelData.locked) ? 0.82 : 1.0

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

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: modelData.type === "image" ? 2 : 0
                                            color: modelData.type === "image" ? theme.alpha(theme.ink950, 0.05) : "transparent"
                                            border.width: layerItem.primarySelected ? 2 : layerItem.selected ? 1 : 0
                                            border.color: theme.alpha(theme.accentWarm500, 0.96)
                                            clip: true

                                            Image {
                                                anchors.fill: parent
                                                visible: modelData.type === "image"
                                                source: modelData.sourceUrl
                                                fillMode: modelData.fitMode === "contain" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                                                cache: true
                                                asynchronous: true
                                                smooth: true
                                                mipmap: true
                                                sourceSize.width: Math.max(1, Math.round(layerItem.stableSourceW))
                                                sourceSize.height: Math.max(1, Math.round(layerItem.stableSourceH))
                                            }

                                            Rectangle {
                                                anchors.fill: parent
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
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Text {
                                                anchors.fill: parent
                                                visible: modelData.type === "text"
                                                text: modelData.text
                                                color: modelData.textColor
                                                font.family: theme.fontDisplay
                                                font.pixelSize: Math.max(10, Number(modelData.textPointSize || 0) * root.stripScale)
                                                font.weight: Font.DemiBold
                                                wrapMode: Text.WordWrap
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
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
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
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
            }
        }

        Rectangle {
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

                            FilterChip {
                                Layout.fillWidth: true
                                text: "4:5"
                                active: carouselDocument.documentFormat === "4x5"
                                onClicked: carouselDocument.documentFormat = "4x5"
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                text: "5:4"
                                active: carouselDocument.documentFormat === "5x4"
                                onClicked: carouselDocument.documentFormat = "5x4"
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
                                text: selection.templateKind === "split-pair" ? "Right" : "Mid"
                                active: selection.activeSlot === "secondary"
                                onClicked: carouselDocument.setActiveSlot("secondary")
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                visible: selection.canUseTertiary
                                text: "Btm"
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

                                delegate: Rectangle {
                                    id: layerRow
                                    required property var modelData

                                    width: parent ? parent.width : 200
                                    height: 40
                                    radius: 6
                                    color: Boolean(modelData.selected)
                                           ? theme.alpha(theme.accentCool400, 0.16)
                                           : Boolean(modelData.hidden) ? theme.alpha(theme.ink900, 0.4) : "transparent"
                                    border.width: Boolean(modelData.primarySelected) ? 1 : 0
                                    border.color: theme.alpha(theme.accentCool400, 0.7)

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 6
                                        spacing: 6

                                        AppIcon {
                                            Layout.preferredWidth: 12
                                            Layout.preferredHeight: 12
                                            name: modelData.type === "text" ? "edit"
                                                  : modelData.type === "video" ? "source" : "media"
                                            color: Boolean(modelData.hidden) ? theme.textFaint : theme.textMuted
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: String(modelData.displayLabel || "Layer")
                                            color: Boolean(modelData.hidden) ? theme.textFaint : theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }

                                        Rectangle {
                                            visible: Boolean(modelData.locked)
                                            Layout.preferredWidth: 14
                                            Layout.preferredHeight: 14
                                            radius: 3
                                            color: theme.alpha(theme.warning, 0.2)

                                            AppIcon {
                                                anchors.centerIn: parent
                                                width: 8
                                                height: 8
                                                name: "link"
                                                color: theme.warning
                                            }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: function(mouse) {
                                            if (root.hasMultiSelectModifier(mouse.modifiers))
                                                carouselDocument.toggleItemSelection(String(modelData.itemId || ""))
                                            else
                                                root.focusLayerItem(String(modelData.itemId || ""), Boolean(modelData.selected))
                                        }
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

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Photo"
                                tone: "primary"
                                onClicked: root.openMediaPicker("layer")
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Text"
                                onClicked: carouselDocument.addTextItem("New Text")
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

                            Label {
                                Layout.fillWidth: true
                                text: Math.round(selection.x).toString()
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 11
                            }

                            Label {
                                text: "Y"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                Layout.fillWidth: true
                                text: Math.round(selection.y).toString()
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 11
                            }

                            Label {
                                text: "R"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                text: Math.round(selection.rotation) + "\u00B0"
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 11
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

                            Label {
                                Layout.fillWidth: true
                                text: Math.round(selection.width).toString()
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 11
                            }

                            Label {
                                text: "H"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            Label {
                                Layout.fillWidth: true
                                text: Math.round(selection.height).toString()
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 11
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
                                text: "Dup"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.duplicateSelectedItem()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            visible: selection.hasSelection && selection.hasUnlockedSelection

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "move-back"
                                showText: false
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.sendSelectedBackward()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                iconName: "move-forward"
                                showText: false
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.bringSelectedForward()
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
                        visible: selection.isImage && !selection.hasMultipleSelection
                        title: "Photo"
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

                                delegate: Rectangle {
                                    required property string modelData
                                    width: 20
                                    height: 20
                                    radius: 4
                                    color: modelData
                                    border.width: selection.textColor === modelData ? 2 : 1
                                    border.color: selection.textColor === modelData
                                                  ? theme.accentWarm400
                                                  : theme.alpha(theme.lineSoft, 0.36)

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: carouselDocument.setSelectedTextColor(modelData)
                                    }
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
