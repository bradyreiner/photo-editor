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
    property bool snappingEnabled: true
    property bool guidesEnabled: true
    property bool thirdsGuidesEnabled: false
    property bool safeGuidesEnabled: false
    property real snapThresholdPixels: 12
    property var activeSnapGuides: []
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

    function setSnapGuides(guides) {
        const filtered = []
        for (let index = 0; index < guides.length; ++index) {
            if (guides[index])
                filtered.push(guides[index])
        }
        activeSnapGuides = filtered
    }

    function clearSnapGuides() {
        activeSnapGuides = []
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

    Component.onCompleted: refreshMediaPickerAssets()

    Shortcut {
        sequence: StandardKey.Delete
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

    RowLayout {
        anchors.fill: parent
        spacing: theme.space12

        Rectangle {
            Layout.preferredWidth: 84
            Layout.minimumWidth: 76
            Layout.fillHeight: true
            radius: theme.radius12
            color: theme.alpha(theme.ink900, 0.94)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.58)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Tools"
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }

                Repeater {
                    model: root.toolButtons

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        radius: 12
                        color: root.activeTool === modelData.value
                               ? theme.alpha(theme.accentWarm500, 0.22)
                               : theme.alpha(theme.ink950, 0.55)
                        border.width: 1
                        border.color: root.activeTool === modelData.value
                                      ? theme.accentWarm300
                                      : theme.alpha(theme.lineSoft, 0.42)

                        Column {
                            anchors.centerIn: parent
                            spacing: 2

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.value === "select" ? "V"
                                                                   : modelData.value === "hand" ? "H"
                                                                   : modelData.value === "text" ? "T" : "M"
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setActiveTool(modelData.value)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: theme.alpha(theme.lineSoft, 0.32)
                }

                ActionButton {
                    Layout.fillWidth: true
                    compact: true
                    text: "Fit"
                    onClicked: root.resetZoomToFit()
                }

                FilterChip {
                    Layout.fillWidth: true
                    text: "Snap"
                    active: root.snappingEnabled
                    onClicked: root.snappingEnabled = !root.snappingEnabled
                }

                FilterChip {
                    Layout.fillWidth: true
                    text: "Guides"
                    active: root.guidesEnabled
                    onClicked: root.guidesEnabled = !root.guidesEnabled
                }

                Item {
                    Layout.fillHeight: true
                }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.activeTool === "select"
                          ? "Move, resize, and rotate layers."
                          : root.activeTool === "hand"
                            ? "Pan the workspace."
                            : root.activeTool === "text"
                              ? "Click the canvas to add text."
                              : "Click the canvas to place media."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
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
                    spacing: theme.space10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: "Composer"
                            color: theme.textStrong
                            font.family: theme.fontDisplay
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            text: currentCanvasLabel() + "  •  " + currentTemplateLabel() + "  •  " + documentPixelLabel()
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    InfoPill {
                        label: "Tool"
                        value: root.activeTool === "hand" ? "Pan" : root.activeTool === "text" ? "Text" : root.activeTool === "media" ? "Media" : "Select"
                        accent: theme.accentWarm400
                    }

                    InfoPill {
                        label: "Selection"
                        value: selection.hasSelection ? selection.displayLabel : "None"
                        accent: theme.accentCool400
                    }

                    InfoPill {
                        visible: !root.condensedHeader
                        label: "Slot"
                        value: slotLabel()
                        accent: theme.accentCool400
                    }

                    ActionButton {
                        text: "Export"
                        tone: "primary"
                        enabled: !carouselDocument.exportBusy
                        onClicked: carouselDocument.exportCarousel()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: theme.radiusControl
                    color: theme.alpha(theme.ink950, 0.5)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.48)
                    implicitHeight: 116

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            Label {
                                text: "Canvas"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }

                            FilterChip {
                                text: "4x5"
                                active: carouselDocument.documentFormat === "4x5"
                                onClicked: carouselDocument.documentFormat = "4x5"
                            }

                            FilterChip {
                                text: "5x4"
                                active: carouselDocument.documentFormat === "5x4"
                                onClicked: carouselDocument.documentFormat = "5x4"
                            }

                            Rectangle {
                                Layout.preferredWidth: 1
                                Layout.preferredHeight: 18
                                color: theme.alpha(theme.lineSoft, 0.36)
                            }

                            Repeater {
                                model: root.templateButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    text: modelData.label
                                    active: selection.templateKind === modelData.value || selection.groupKind === modelData.value
                                    onClicked: carouselDocument.applyTemplateToCurrent(modelData.value)
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            ActionButton {
                                compact: true
                                text: "Add Text"
                                onClicked: carouselDocument.addTextItem("New Text")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            FilterChip {
                                text: "Snap"
                                active: root.snappingEnabled
                                onClicked: root.snappingEnabled = !root.snappingEnabled
                            }

                            FilterChip {
                                text: "Guides"
                                active: root.guidesEnabled
                                onClicked: root.guidesEnabled = !root.guidesEnabled
                            }

                            FilterChip {
                                text: "Thirds"
                                active: root.thirdsGuidesEnabled
                                onClicked: root.thirdsGuidesEnabled = !root.thirdsGuidesEnabled
                            }

                            FilterChip {
                                text: "Safe"
                                active: root.safeGuidesEnabled
                                onClicked: root.safeGuidesEnabled = !root.safeGuidesEnabled
                            }

                            Item {
                                Layout.preferredWidth: 12
                            }

                            ActionButton {
                                text: "-"
                                compact: true
                                onClicked: root.zoomOut()
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Zoom"
                                valueText: Math.round(root.composerZoom * 100) + "%"
                                from: 0.5
                                to: 3.0
                                stepSize: 0.05
                                value: root.composerZoom
                                accent: theme.accentCool400
                                onValueDragged: function(value) {
                                    root.composerZoom = root.clampZoom(value)
                                }
                            }

                            ActionButton {
                                text: "+"
                                compact: true
                                onClicked: root.zoomIn()
                            }

                            ActionButton {
                                text: "Fit"
                                compact: true
                                onClicked: root.resetZoomToFit()
                            }
                        }
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
                                        cache: false
                                        asynchronous: false
                                        smooth: true
                                        mipmap: true
                                        sourceSize.width: Math.max(1, Math.round(width))
                                        sourceSize.height: Math.max(1, Math.round(height))
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

                                    Label {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.margins: 12
                                        text: modelData.selected ? "Current • Canvas " + (index + 1) : "Canvas " + (index + 1)
                                        color: modelData.selected ? theme.textStrong : "#7d6858"
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
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
                                    property bool selected: !!modelData.selected
                                    property bool primarySelected: !!modelData.primarySelected

                                    x: Number(modelData.x || 0) * root.stripScale
                                    y: Number(modelData.y || 0) * root.stripScale
                                    width: Math.max(10, Number(modelData.width || 0) * root.stripScale)
                                    height: Math.max(10, Number(modelData.height || 0) * root.stripScale)
                                    z: layerItem.index + 10
                                    visible: !Boolean(modelData.hidden)
                                    opacity: Boolean(modelData.locked) ? 0.82 : 1.0

                                    transform: Rotation {
                                        origin.x: layerBody.width / 2
                                        origin.y: layerBody.height / 2
                                        angle: Number(modelData.rotation || 0)
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
                                                cache: false
                                                asynchronous: false
                                                smooth: true
                                                mipmap: true
                                                sourceSize.width: Math.max(1, Math.round(parent.width))
                                                sourceSize.height: Math.max(1, Math.round(parent.height))
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
                                                layerItem.dragCenterX = Number(modelData.centerX || 0)
                                                layerItem.dragCenterY = Number(modelData.centerY || 0)
                                                layerItem.dragArmed = true
                                            }

                                            onPositionChanged: function(mouse) {
                                                if (!pressed || !layerItem.dragArmed)
                                                    return
                                                const p = layerBody.mapToItem(stripSurface, mouse.x, mouse.y)
                                                const deltaX = (p.x - layerItem.dragLastX) / root.stripScale
                                                const deltaY = (p.y - layerItem.dragLastY) / root.stripScale
                                                const snapped = root.snapDragPosition(
                                                            modelData,
                                                            layerItem.dragCenterX + deltaX,
                                                            layerItem.dragCenterY + deltaY)
                                                carouselDocument.setSelectedItemCenter(snapped.centerX, snapped.centerY)
                                                root.setSnapGuides(snapped.guides)
                                                layerItem.dragCenterX = snapped.centerX
                                                layerItem.dragCenterY = snapped.centerY
                                                layerItem.dragLastX = p.x
                                                layerItem.dragLastY = p.y
                                            }

                                            onReleased: {
                                                layerItem.dragArmed = false
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
                                                }

                                                onPositionChanged: function(mouse) {
                                                    if (!pressed)
                                                        return
                                                    const p = rotateHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    const angle = Math.atan2(p.y - layerItem.rotateCenterY, p.x - layerItem.rotateCenterX) * 180 / Math.PI + 90
                                                    carouselDocument.setSelectedItemRotation(angle)
                                                }

                                                onReleased: root.clearSnapGuides()
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
                                                }

                                                onPositionChanged: function(mouse) {
                                                    if (!pressed)
                                                        return
                                                    const p = resizeHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    const snapped = root.snapResize(
                                                                modelData,
                                                                layerItem.resizeCenterX,
                                                                layerItem.resizeCenterY,
                                                                layerItem.resizeStartWidth + ((p.x - layerItem.resizeStartX) / root.stripScale),
                                                                layerItem.resizeStartHeight + ((p.y - layerItem.resizeStartY) / root.stripScale))
                                                    carouselDocument.setSelectedItemSize(snapped.width, snapped.height)
                                                    root.setSnapGuides(snapped.guides)
                                                }

                                                onReleased: root.clearSnapGuides()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: root.compactWorkspace ? 248 : 272
            Layout.minimumWidth: 228
            Layout.fillHeight: true
            radius: theme.radius12
            color: theme.alpha(theme.ink900, 0.94)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.58)

            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth

                Column {
                    x: theme.panelPadding
                    y: theme.panelPadding
                    width: Math.max(parent.width - theme.panelPadding * 2, 0)
                    spacing: theme.space10

                    InspectorSection {
                        width: parent.width
                        visible: !selection.hasSelection
                        title: "Canvas"
                        subtitle: currentCanvasLabel() + "  •  " + carouselDocument.slideCount + " boards"
                        accent: theme.accentWarm400

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Add Board"
                                tone: "primary"
                                enabled: carouselDocument.canAddSlide
                                onClicked: carouselDocument.addSlide()
                            }

                            ConfirmActionButton {
                                Layout.fillWidth: true
                                text: "Remove"
                                enabled: carouselDocument.slideCount > 1
                                helperText: "Remove the current board"
                                onConfirmed: carouselDocument.removeCurrentSlide()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Move Left"
                                enabled: carouselDocument.slideCount > 1
                                onClicked: carouselDocument.moveCurrentBackward()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Move Right"
                                enabled: carouselDocument.slideCount > 1
                                onClicked: carouselDocument.moveCurrentForward()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: theme.radiusControl
                            color: theme.alpha(theme.ink850, 0.88)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.7)
                            implicitHeight: mediaSummary.implicitHeight + 20

                            Label {
                                id: mediaSummary
                                anchors.fill: parent
                                anchors.margins: 10
                                text: selection.hasMedia
                                      ? selection.slotDisplayLabel + "\n" + slotLabel() + " slot"
                                      : "Choose media for the active slot."
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Choose Media"
                            tone: "primary"
                            onClicked: openMediaPicker("slot")
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: selection.isLinkedGroup ? "Clear Group" : "Clear Slot"
                            enabled: selection.isLinkedGroup || selection.hasMedia
                            onClicked: {
                                if (selection.isLinkedGroup)
                                    carouselDocument.clearCurrentGroup()
                                else
                                    carouselDocument.clearCurrentSlot()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

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
                            spacing: theme.space8

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

                        Label {
                            Layout.fillWidth: true
                            text: selection.hasMedia
                                  ? "Drag inside the active slot to reposition the image. Drag the corner handle on the canvas to scale it."
                                  : "Click a slot on the canvas to target it, then choose media."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
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
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Offset Y"
                            valueText: Math.round(selection.offsetY * 100) + "%"
                            from: -1.0
                            to: 1.0
                            stepSize: 0.01
                            value: selection.offsetY
                            accent: theme.success
                            onValueDragged: function(value) {
                                carouselDocument.setCurrentOffsetY(value)
                            }
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        title: "Layers"
                        subtitle: currentCanvasLabel() + "  •  " + currentSlideLayers.length + " layers"
                        accent: theme.accentCool400

                        Rectangle {
                            Layout.fillWidth: true
                            radius: theme.radiusControl
                            color: theme.alpha(theme.ink850, 0.88)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.7)
                            implicitHeight: currentSlideLayers.length === 0 ? 72 : Math.min(260, Math.max(84, currentSlideLayers.length * 62 + 12))

                            ListView {
                                visible: currentSlideLayers.length > 0
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                spacing: 6
                                model: root.currentSlideLayers

                                delegate: Rectangle {
                                    id: layerRow
                                    required property var modelData

                                    width: parent ? parent.width : 200
                                    height: 56
                                    radius: 10
                                    color: Boolean(modelData.selected)
                                           ? theme.alpha(theme.accentCool400, 0.18)
                                           : theme.alpha(theme.ink900, Boolean(modelData.hidden) ? 0.46 : 0.72)
                                    border.width: 1
                                    border.color: Boolean(modelData.primarySelected)
                                                  ? theme.accentCool400
                                                  : Boolean(modelData.selected)
                                                    ? theme.alpha(theme.accentCool400, 0.7)
                                                  : theme.alpha(theme.lineSoft, 0.26)

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 8

                                        Rectangle {
                                            Layout.preferredWidth: 40
                                            Layout.preferredHeight: 40
                                            radius: 8
                                            color: modelData.type === "text" ? theme.alpha(theme.success, 0.2)
                                                                             : modelData.type === "video" ? theme.alpha(theme.accentCool400, 0.2)
                                                                                                           : theme.alpha(theme.warning, 0.18)

                                            Label {
                                                anchors.centerIn: parent
                                                text: modelData.type === "text" ? "T" : modelData.type === "video" ? "V" : "P"
                                                color: theme.textStrong
                                                font.family: theme.fontMono
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2

                                            Label {
                                                Layout.fillWidth: true
                                                text: String(modelData.displayLabel || "Layer")
                                                color: theme.textStrong
                                                font.family: theme.fontUi
                                                font.pixelSize: 12
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: "x " + Math.round(Number(modelData.x || 0)) + "  y " + Math.round(Number(modelData.y || 0))
                                                color: theme.textMuted
                                                font.family: theme.fontMono
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                            }
                                        }

                                        ActionButton {
                                            compact: true
                                            text: Boolean(modelData.hidden) ? "Show" : "Hide"
                                            onClicked: {
                                                root.focusLayerItem(String(modelData.itemId || ""), Boolean(modelData.selected))
                                                carouselDocument.setSelectedItemHidden(!Boolean(modelData.hidden))
                                            }
                                        }

                                        ActionButton {
                                            compact: true
                                            text: Boolean(modelData.locked) ? "Unlock" : "Lock"
                                            onClicked: {
                                                root.focusLayerItem(String(modelData.itemId || ""), Boolean(modelData.selected))
                                                carouselDocument.setSelectedItemLocked(!Boolean(modelData.locked))
                                            }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        z: -1
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
                                text: "No layers yet"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Duplicate"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.duplicateSelectedItem()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Replace"
                                enabled: selection.selectedCount === 1 && !selection.isText
                                onClicked: root.replaceSelectedLayerFromGallery()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Back"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.sendSelectedBackward()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Forward"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.bringSelectedForward()
                            }
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: selection.hasSelection
                        title: "Properties"
                        subtitle: selection.hasMultipleSelection ? selection.selectedCount + " layers selected" : selection.displayLabel
                        accent: theme.accentCool400

                        Label {
                            Layout.fillWidth: true
                            text: "Cmd/Ctrl-click adds or removes layers from the selection. Transform handles stay on the primary selected layer."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        ConfirmActionButton {
                            Layout.fillWidth: true
                            text: "Remove Selected"
                            enabled: selection.hasSelection
                            helperText: selection.hasMultipleSelection
                                        ? "Remove the selected layers"
                                        : "Remove the selected layer"
                            onConfirmed: carouselDocument.removeSelectedItem()
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: selection.hidden ? "Show Layer" : "Hide Layer"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.setSelectedItemHidden(!selection.hidden)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: selection.locked ? "Unlock Layer" : "Lock Layer"
                                enabled: selection.hasSelection
                                onClicked: carouselDocument.setSelectedItemLocked(!selection.locked)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Left"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionLeft()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Center X"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionHorizontalCenter()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Right"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionRight()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Top"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionTop()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Center Y"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionVerticalCenter()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Bottom"
                                enabled: selection.hasSelection && selection.hasUnlockedSelection
                                onClicked: carouselDocument.alignSelectionBottom()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Space X"
                                enabled: selection.editableSelectionCount > 2
                                onClicked: carouselDocument.distributeSelectionHorizontally()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Space Y"
                                enabled: selection.editableSelectionCount > 2
                                onClicked: carouselDocument.distributeSelectionVertically()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: theme.radiusControl
                            color: theme.alpha(theme.ink850, 0.88)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.7)
                            implicitHeight: selectionSummary.implicitHeight + 20

                            Label {
                                id: selectionSummary
                                anchors.fill: parent
                                anchors.margins: 10
                                text: selection.hasSelection
                                      ? (selection.selectedCount > 1
                                         ? selection.selectedCount + " layers selected"
                                           + "\nprimary x " + Math.round(selection.x) + "  y " + Math.round(selection.y)
                                           + "\nprimary " + Math.round(selection.width) + " x " + Math.round(selection.height)
                                         : "x " + Math.round(selection.x) + "  y " + Math.round(selection.y)
                                           + "\n" + Math.round(selection.width) + " x " + Math.round(selection.height)
                                           + "\nrotation " + Math.round(selection.rotation) + "°")
                                      : "Select a layer to inspect its size and position."
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        visible: selection.isImage && !selection.hasMultipleSelection
                        title: "Photo"
                        subtitle: "How the image fits inside its frame."
                        accent: theme.accentWarm400

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

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
                        subtitle: "Edit content, color, and size."
                        accent: theme.success

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
                                border.color: theme.alpha(theme.lineSoft, 0.72)
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

                        TextField {
                            id: textColorField
                            Layout.fillWidth: true
                            color: theme.textStrong
                            placeholderText: "#1e1a17"
                            background: Rectangle {
                                radius: theme.radiusControl
                                color: theme.alpha(theme.ink850, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.72)
                            }
                            onEditingFinished: {
                                if (selection.isText)
                                    carouselDocument.setSelectedTextColor(text)
                            }
                            onTextChanged: {
                                if (activeFocus && selection.isText)
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
                            label: "Text Size"
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
