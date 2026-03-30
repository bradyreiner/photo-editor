pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml
import "../components"

Rectangle {
    id: root
    color: "transparent"

    Theme { id: theme }

    readonly property var selection: carouselDocument.selectionDetails
    property var mediaPickerAssets: []
    property string mediaPickerProjectName: ""
    property string mediaPickerScope: "all"
    property string mediaPickerIntent: "slot"
    readonly property bool compactWorkspace: width < 1380
    readonly property bool condensedHeader: width < 1180
    readonly property real stripScale: carouselDocument.documentPixelHeight > 0 ? stripSurface.height / carouselDocument.documentPixelHeight : 1
    readonly property var templateButtons: [
        { label: "Single", value: "single" },
        { label: "Split Pair", value: "split-pair" },
        { label: "Linked 2", value: "linked-horizontal-pair" },
        { label: "Stacked 3", value: "stacked-triptych" }
    ]

    function slideWidthOnStrip() {
        return carouselDocument.slideCount > 0 ? stripSurface.width / carouselDocument.slideCount : stripSurface.width
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
        return catalogAssets.selectedFilePath.length > 0 && catalogAssets.selectedMediaType === "photo"
    }

    function assignSelectedGalleryMediaAsLayer() {
        if (!galleryLayerSelectionSupported())
            return
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
        mediaPickerIntent = intent || "slot"
        mediaPickerScope = hasProjectScopedMediaPicker() ? "project" : "all"
        refreshMediaPickerAssets()
        mediaPickerDialog.openDialog()
    }

    function applyPickedMedia() {
        if (!mediaPickerDialog.hasSelection || !mediaPickerDialog.selectedOnline)
            return

        if (mediaPickerIntent === "layer") {
            if (mediaPickerDialog.selectedMediaType === "photo") {
                carouselDocument.addPhotoItem(
                            mediaPickerDialog.selectedFilePath,
                            mediaPickerDialog.selectedDisplayLabel,
                            String(mediaPickerDialog.selectedAsset.recipeJson || ""))
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

    function librarySelectionLabel() {
        if (catalogAssets.selectedFilePath.length === 0)
            return "No library asset selected"
        const parts = catalogAssets.selectedFilePath.split("/")
        return parts.length > 0 ? parts[parts.length - 1] : catalogAssets.selectedFilePath
    }

    Connections {
        target: catalogAssets

        function onAssetCountChanged() {
            if (mediaPickerDialog.visible)
                root.refreshMediaPickerAssets()
        }

        function onTotalAssetCountChanged() {
            if (mediaPickerDialog.visible)
                root.refreshMediaPickerAssets()
        }

        function onProjectStatsRevisionChanged() {
            if (mediaPickerDialog.visible)
                root.refreshMediaPickerAssets()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: theme.space12

        Rectangle {
            Layout.preferredWidth: root.compactWorkspace ? 250 : 276
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
                        title: "Canvas"
                        subtitle: documentPixelLabel() + "  •  " + carouselDocument.slideCount + " boards"
                        accent: theme.warning

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            FilterChip {
                                Layout.fillWidth: true
                                text: "4x5"
                                active: carouselDocument.documentFormat === "4x5"
                                onClicked: carouselDocument.documentFormat = "4x5"
                            }

                            FilterChip {
                                Layout.fillWidth: true
                                text: "5x4"
                                active: carouselDocument.documentFormat === "5x4"
                                onClicked: carouselDocument.documentFormat = "5x4"
                            }
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Add Canvas"
                            tone: "primary"
                            enabled: carouselDocument.canAddSlide
                            onClicked: carouselDocument.addSlide()
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Earlier"
                                enabled: carouselDocument.slideCount > 1
                                onClicked: carouselDocument.moveCurrentBackward()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Later"
                                enabled: carouselDocument.slideCount > 1
                                onClicked: carouselDocument.moveCurrentForward()
                            }
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Remove Current"
                            enabled: carouselDocument.slideCount > 1
                            onClicked: carouselDocument.removeCurrentSlide()
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "New canvases are appended flush against the previous one, so anything that crosses a seam exports as separate cut slides automatically."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        title: "Template"
                        subtitle: "Per-canvas layout for slot-based media."
                        accent: theme.accentWarm400

                        Flow {
                            Layout.fillWidth: true
                            width: parent.width
                            spacing: theme.space8

                            Repeater {
                                model: root.templateButtons

                                delegate: FilterChip {
                                    required property var modelData
                                    text: modelData.label
                                    active: selection.templateKind === modelData.value || selection.groupKind === modelData.value
                                    onClicked: carouselDocument.applyTemplateToCurrent(modelData.value)
                                }
                            }
                        }
                    }

                    InspectorSection {
                        width: parent.width
                        title: "Layers"
                        subtitle: currentCanvasLabel() + "  •  " + selection.layerCount + " total"
                        accent: theme.accentWarm400

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Add Photo"
                            tone: "primary"
                            onClicked: carouselDocument.pickPhotoItem()
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Open Gallery As Layer"
                            onClicked: openMediaPicker("layer")
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Use Library As Layer"
                            enabled: galleryLayerSelectionSupported()
                            onClicked: assignSelectedGalleryMediaAsLayer()
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Add Text"
                            onClicked: carouselDocument.addTextItem("New Text")
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: theme.radiusControl
                            color: theme.alpha(theme.ink850, 0.88)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.7)
                            implicitHeight: librarySelectionText.implicitHeight + 18

                            Label {
                                id: librarySelectionText
                                anchors.fill: parent
                                anchors.margins: 10
                                text: "Library: " + librarySelectionLabel()
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
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
                        label: "Selection"
                        value: selection.hasSelection ? selection.displayLabel : "None"
                        accent: theme.accentWarm400
                    }

                    InfoPill {
                        label: "Slot"
                        value: slotLabel()
                        accent: theme.accentCool400
                    }

                    InfoPill {
                        visible: !root.condensedHeader
                        label: "Layers"
                        value: String(selection.layerCount)
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
                    Layout.fillHeight: true
                    radius: 18
                    color: theme.alpha(theme.ink950, 0.72)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.56)

                    gradient: Gradient {
                        GradientStop { position: 0.0; color: theme.alpha(theme.ink700, 0.14) }
                        GradientStop { position: 0.24; color: theme.alpha(theme.ink900, 0.92) }
                        GradientStop { position: 1.0; color: theme.ink950 }
                    }

                    Flickable {
                        id: artboardFlick
                        anchors.fill: parent
                        anchors.margins: theme.panelPadding
                        contentWidth: Math.max(width, stripSurface.width + 40)
                        contentHeight: Math.max(height, stripSurface.height + 40)
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        ScrollBar.horizontal: ScrollBar { }
                        ScrollBar.vertical: ScrollBar { }

                        Rectangle {
                            id: stripSurface
                            x: Math.max(20, (artboardFlick.width - width) / 2)
                            y: Math.max(20, (artboardFlick.height - height) / 2)
                            width: Math.max(200, carouselDocument.documentPixelWidth * root.stripScale)
                            height: Math.max(260, Math.min(artboardFlick.height - 44, root.compactWorkspace ? 420 : 520))
                            radius: 18
                            color: "#fbf7f1"
                            border.width: 2
                            border.color: theme.alpha("#c5b6a6", 0.92)
                            clip: true

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.CrossCursor

                                onPressed: function(mouse) {
                                    const slideWidth = root.slideWidthOnStrip()
                                    const targetIndex = Math.max(0, Math.min(carouselDocument.slideCount - 1, Math.floor(mouse.x / Math.max(1, slideWidth))))
                                    const localX = mouse.x - (targetIndex * slideWidth)
                                    carouselDocument.selectSlide(targetIndex)
                                    chooseSlotForSlide(slideData(targetIndex), localX, mouse.y, slideWidth, stripSurface.height)
                                    carouselDocument.clearSelection()
                                }
                            }

                            Repeater {
                                model: carouselDocument.slides

                                delegate: Item {
                                    required property int index
                                    required property var modelData

                                    x: index * root.slideWidthOnStrip()
                                    y: 0
                                    width: root.slideWidthOnStrip()
                                    height: stripSurface.height

                                    Rectangle {
                                        anchors.fill: parent
                                        color: index % 2 === 0 ? "#fffdf8" : "#fff9f2"
                                        border.width: index < carouselDocument.slideCount - 1 ? 1 : 0
                                        border.color: theme.alpha("#cdb9a4", 0.84)
                                    }

                                    Image {
                                        anchors.fill: parent
                                        source: modelData.previewSource
                                        fillMode: Image.PreserveAspectCrop
                                        cache: false
                                        asynchronous: false
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        color: "transparent"
                                        border.width: modelData.selected ? 2 : 0
                                        border.color: theme.alpha(theme.accentWarm500, 0.96)
                                    }

                                    Rectangle {
                                        visible: modelData.selected && modelData.templateKind === "split-pair" && selection.activeSlot === "primary"
                                        width: parent.width / 2
                                        height: parent.height
                                        color: "transparent"
                                        border.width: 2
                                        border.color: theme.alpha(theme.accentWarm500, 0.96)
                                    }

                                    Rectangle {
                                        visible: modelData.selected && modelData.templateKind === "split-pair" && selection.activeSlot === "secondary"
                                        x: parent.width / 2
                                        width: parent.width / 2
                                        height: parent.height
                                        color: "transparent"
                                        border.width: 2
                                        border.color: theme.alpha(theme.accentWarm500, 0.96)
                                    }

                                    Rectangle {
                                        visible: modelData.selected && modelData.templateKind === "stacked-triptych" && selection.activeSlot === "primary"
                                        width: parent.width
                                        height: parent.height / 3
                                        color: "transparent"
                                        border.width: 2
                                        border.color: theme.alpha(theme.accentWarm500, 0.96)
                                    }

                                    Rectangle {
                                        visible: modelData.selected && modelData.templateKind === "stacked-triptych" && selection.activeSlot === "secondary"
                                        y: parent.height / 3
                                        width: parent.width
                                        height: parent.height / 3
                                        color: "transparent"
                                        border.width: 2
                                        border.color: theme.alpha(theme.accentWarm500, 0.96)
                                    }

                                    Rectangle {
                                        visible: modelData.selected && modelData.templateKind === "stacked-triptych" && selection.activeSlot === "tertiary"
                                        y: parent.height * 2 / 3
                                        width: parent.width
                                        height: parent.height / 3
                                        color: "transparent"
                                        border.width: 2
                                        border.color: theme.alpha(theme.accentWarm500, 0.96)
                                    }

                                    Label {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.margins: 12
                                        text: "Canvas " + (index + 1)
                                        color: modelData.selected ? theme.textStrong : "#7d6858"
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
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
                                    property real resizeStartWidth: 0
                                    property real resizeStartHeight: 0
                                    property real resizeStartX: 0
                                    property real resizeStartY: 0
                                    property real rotateCenterX: 0
                                    property real rotateCenterY: 0
                                    property bool selected: !!modelData.selected

                                    x: Number(modelData.x || 0) * root.stripScale
                                    y: Number(modelData.y || 0) * root.stripScale
                                    width: Math.max(10, Number(modelData.width || 0) * root.stripScale)
                                    height: Math.max(10, Number(modelData.height || 0) * root.stripScale)
                                    z: layerItem.index + 10

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
                                            border.width: layerItem.selected ? 2 : 0
                                            border.color: theme.alpha(theme.accentWarm500, 0.96)
                                            clip: true

                                            Image {
                                                anchors.fill: parent
                                                visible: modelData.type === "image"
                                                source: modelData.sourceUrl
                                                fillMode: modelData.fitMode === "contain" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                                                cache: false
                                                asynchronous: true
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
                                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                                            onPressed: function(mouse) {
                                                carouselDocument.selectItem(modelData.itemId)
                                                const p = layerBody.mapToItem(stripSurface, mouse.x, mouse.y)
                                                layerItem.dragLastX = p.x
                                                layerItem.dragLastY = p.y
                                            }

                                            onPositionChanged: function(mouse) {
                                                if (!pressed)
                                                    return
                                                const p = layerBody.mapToItem(stripSurface, mouse.x, mouse.y)
                                                carouselDocument.moveSelectedItemByPixels((p.x - layerItem.dragLastX) / root.stripScale,
                                                                                         (p.y - layerItem.dragLastY) / root.stripScale)
                                                layerItem.dragLastX = p.x
                                                layerItem.dragLastY = p.y
                                            }
                                        }

                                        Rectangle {
                                            id: rotateHandle
                                            visible: layerItem.selected
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

                                                onPressed: function(mouse) {
                                                    carouselDocument.selectItem(modelData.itemId)
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
                                            }
                                        }

                                        Rectangle {
                                            id: resizeHandle
                                            visible: layerItem.selected
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

                                                onPressed: function(mouse) {
                                                    carouselDocument.selectItem(modelData.itemId)
                                                    layerItem.resizeStartWidth = Number(modelData.width || 0)
                                                    layerItem.resizeStartHeight = Number(modelData.height || 0)
                                                    const p = resizeHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    layerItem.resizeStartX = p.x
                                                    layerItem.resizeStartY = p.y
                                                }

                                                onPositionChanged: function(mouse) {
                                                    if (!pressed)
                                                        return
                                                    const p = resizeHandle.mapToItem(stripSurface, mouse.x, mouse.y)
                                                    carouselDocument.setSelectedItemSize(layerItem.resizeStartWidth + ((p.x - layerItem.resizeStartX) / root.stripScale),
                                                                                        layerItem.resizeStartHeight + ((p.y - layerItem.resizeStartY) / root.stripScale))
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
        }

        Rectangle {
            Layout.preferredWidth: root.compactWorkspace ? 280 : 308
            Layout.minimumWidth: 248
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
                        title: "Media"
                        subtitle: currentCanvasLabel() + "  •  " + currentTemplateLabel()
                        accent: theme.accentWarm400

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
                                      : "The active slot is empty."
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Open Gallery For Slot"
                            tone: "primary"
                            onClicked: openMediaPicker("slot")
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Use Library Selection"
                            enabled: gallerySelectionSupported()
                            onClicked: assignSelectedGalleryMedia()
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Pick Image"
                                onClicked: {
                                    if (selection.canUseTertiary && selection.activeSlot === "tertiary")
                                        carouselDocument.pickSecondaryImage()
                                    else if (selection.canUseSecondary && selection.activeSlot === "secondary")
                                        carouselDocument.pickSecondaryImage()
                                    else
                                        carouselDocument.pickPrimaryImage()
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Pick Video"
                                enabled: canPlaceVideo()
                                onClicked: carouselDocument.pickPrimaryVideo()
                            }
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
                        title: "Selected Layer"
                        subtitle: selection.hasSelection ? selection.displayLabel : "Nothing selected"
                        accent: theme.accentCool400

                        Label {
                            Layout.fillWidth: true
                            text: selection.hasSelection
                                  ? "Drag the layer directly on the strip. Use the top handle to rotate and the lower-right handle to resize."
                                  : "Click a photo or text layer to edit it."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
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

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Remove Selected"
                            enabled: selection.hasSelection
                            onClicked: carouselDocument.removeSelectedItem()
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
                                      ? "x " + Math.round(selection.x) + "  y " + Math.round(selection.y)
                                        + "\n" + Math.round(selection.width) + " x " + Math.round(selection.height)
                                        + "\nrotation " + Math.round(selection.rotation) + "°"
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
                        visible: selection.isImage
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
                        visible: selection.isText
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
        selectionHintText: mediaPickerIntent === "layer" ? "Choose a photo layer" : "Choose media for the active slot"
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
                    text: mediaPickerIntent === "layer" ? "Add As Layer" : "Add To Slot"
                    tone: "primary"
                    enabled: mediaPickerDialog.hasSelection
                             && mediaPickerDialog.selectedOnline
                             && (mediaPickerIntent === "layer"
                                 ? mediaPickerDialog.selectedMediaType === "photo"
                                 : (mediaPickerDialog.selectedMediaType === "photo"
                                    || (mediaPickerDialog.selectedMediaType === "video" && root.canPlaceVideo())))
                    onClicked: root.applyPickedMedia()
                }
            }
        }
    }
}
