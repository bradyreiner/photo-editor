import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    color: "transparent"

    Theme { id: theme }

    property var selection: carouselDocument.selectionDetails
    property real dragStartOffsetX: 0
    property real dragStartOffsetY: 0
    property real dragPressX: 0
    property real dragPressY: 0

    function slideAt(index) {
        if (index < 0 || index >= carouselDocument.slides.length)
            return null
        return carouselDocument.slides[index]
    }

    function currentSlide() {
        return slideAt(carouselDocument.currentIndex)
    }

    function activeScale() {
        return selection.scale !== undefined ? selection.scale : 1.0
    }

    function activeOffsetX() {
        return selection.offsetX !== undefined ? selection.offsetX : 0.0
    }

    function activeOffsetY() {
        return selection.offsetY !== undefined ? selection.offsetY : 0.0
    }

    function chooseSlotForPosition(xPos) {
        const slide = currentSlide()
        if (!slide || slide.templateKind !== "split-pair")
            return

        carouselDocument.setActiveSlot(xPos < previewViewport.width / 2 ? "primary" : "secondary")
    }

    function gallerySelectionSupported() {
        if (catalogAssets.selectedFilePath.length === 0)
            return false

        if (catalogAssets.selectedMediaType === "photo")
            return true

        return !selection.isLinkedGroup && !selection.canUseSecondary
    }

    function assignSelectedGalleryMedia() {
        if (catalogAssets.selectedFilePath.length === 0)
            return

        if (catalogAssets.selectedMediaType === "video" || catalogAssets.selectedMediaType === "timeline") {
            carouselDocument.assignPrimaryVideo(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel)
            return
        }

        if (selection.canUseSecondary && selection.activeSlot === "secondary")
            carouselDocument.assignSecondaryImage(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel, catalogAssets.selectedRecipeJson)
        else
            carouselDocument.assignPrimaryImage(catalogAssets.selectedFilePath, catalogAssets.selectedCameraLabel, catalogAssets.selectedRecipeJson)
    }

    function resetTransform() {
        carouselDocument.setCurrentScale(1.0)
        carouselDocument.setCurrentOffsetX(0.0)
        carouselDocument.setCurrentOffsetY(0.0)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "Carousel Composer"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }

            Rectangle {
                width: 1
                height: 18
                color: theme.lineSoft
            }

            Label {
                text: currentSlide() ? "Slide " + currentSlide().slideNumber : "Slide"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

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

            ActionButton {
                text: "Export"
                tone: "primary"
                enabled: !carouselDocument.exportBusy
                onClicked: carouselDocument.exportCarousel()
            }
        }

        Label {
            Layout.fillWidth: true
            text: carouselDocument.status
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 18
            color: theme.alpha(theme.ink900, 0.8)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.65)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: [
                            { label: "Single", value: "single" },
                            { label: "Split Pair", value: "split-pair" },
                            { label: "Linked 2", value: "linked-horizontal-pair" },
                            { label: "Stacked 3", value: "linked-vertical-triptych" }
                        ]

                        delegate: FilterChip {
                            required property var modelData
                            text: modelData.label
                            active: selection.templateKind === modelData.value
                            onClicked: carouselDocument.applyTemplateToCurrent(modelData.value)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    FilterChip {
                        text: "Fill"
                        active: selection.fitMode === "cover"
                        onClicked: carouselDocument.setCurrentFitMode("cover")
                    }

                    FilterChip {
                        text: "Fit"
                        active: selection.fitMode === "contain"
                        onClicked: carouselDocument.setCurrentFitMode("contain")
                    }

                    ActionButton {
                        text: "Reset"
                        enabled: selection.hasMedia
                        onClicked: resetTransform()
                    }

                    ActionButton {
                        text: "Remove"
                        onClicked: carouselDocument.removeCurrentSlide()
                    }
                }

                Rectangle {
                    id: previewViewport
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 20
                    color: "#0b1014"
                    border.width: 0
                    clip: true

                    Image {
                        id: heroPreview
                        anchors.fill: parent
                        anchors.margins: 10
                        source: currentSlide() ? currentSlide().previewSource : ""
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: false
                    }

                    Rectangle {
                        visible: currentSlide() && currentSlide().templateKind === "split-pair"
                        x: previewViewport.width / 2 - width / 2
                        width: 2
                        height: previewViewport.height
                        color: theme.alpha(theme.accentWarm500, 0.9)
                    }

                    Rectangle {
                        visible: selection.canUseSecondary && selection.activeSlot === "primary"
                        width: previewViewport.width / 2
                        height: previewViewport.height
                        color: "transparent"
                        border.width: 2
                        border.color: theme.alpha(theme.accentWarm500, 0.9)
                    }

                    Rectangle {
                        visible: selection.canUseSecondary && selection.activeSlot === "secondary"
                        x: previewViewport.width / 2
                        width: previewViewport.width / 2
                        height: previewViewport.height
                        color: "transparent"
                        border.width: 2
                        border.color: theme.alpha(theme.accentWarm500, 0.9)
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: selection.hasMedia
                        cursorShape: selection.hasMedia ? Qt.OpenHandCursor : Qt.ArrowCursor

                        onPressed: function(mouse) {
                            chooseSlotForPosition(mouse.x)
                            dragPressX = mouse.x
                            dragPressY = mouse.y
                            dragStartOffsetX = activeOffsetX()
                            dragStartOffsetY = activeOffsetY()
                        }

                        onPositionChanged: function(mouse) {
                            if (!pressed)
                                return

                            carouselDocument.setCurrentOffsetX(dragStartOffsetX + ((mouse.x - dragPressX) / previewViewport.width) * 2.0)
                            carouselDocument.setCurrentOffsetY(dragStartOffsetY + ((mouse.y - dragPressY) / previewViewport.height) * 2.0)
                        }
                    }

                    WheelHandler {
                        target: null
                        enabled: selection.hasMedia

                        onWheel: function(event) {
                            const step = event.angleDelta.y > 0 ? 0.08 : -0.08
                            carouselDocument.setCurrentScale(activeScale() + step)
                            event.accepted = true
                        }
                    }

                    Rectangle {
                        visible: !selection.hasMedia
                        anchors.centerIn: parent
                        radius: 18
                        color: theme.alpha(theme.ink950, 0.82)
                        border.width: 1
                        border.color: theme.alpha(theme.lineStrong, 0.55)
                        implicitWidth: emptyColumn.implicitWidth + 36
                        implicitHeight: emptyColumn.implicitHeight + 32

                        ColumnLayout {
                            id: emptyColumn
                            anchors.centerIn: parent
                            spacing: 10

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: selection.isLinkedGroup ? "Add a gallery image to the linked stack" : "Add a gallery image to the canvas"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 14
                                font.weight: Font.Medium
                            }

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: catalogAssets.selectedFilePath.length > 0
                                      ? (gallerySelectionSupported()
                                         ? "Using the current Library selection"
                                         : "Video and timeline assets only work on a single slide")
                                      : "Select a photo, video, or timeline in Library, then place it here"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 8

                                ActionButton {
                                    text: "Add From Gallery"
                                    enabled: gallerySelectionSupported()
                                    onClicked: assignSelectedGalleryMedia()
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        radius: 16
                        color: theme.alpha(theme.ink950, 0.8)
                        border.width: 1
                        border.color: theme.alpha(theme.lineStrong, 0.52)
                        implicitHeight: 72

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            ColumnLayout {
                                Layout.preferredWidth: 190
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: selection.displayLabel && selection.displayLabel.length > 0 ? selection.displayLabel : "No media selected"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: selection.isLinkedGroup
                                          ? "Linked stack  •  drag to pan  •  scroll to zoom"
                                          : selection.canUseSecondary
                                            ? "Split pair  •  click left/right to target a half"
                                            : "Canvas  •  drag to pan  •  scroll to zoom"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            ActionButton {
                                text: "Add From Gallery"
                                enabled: gallerySelectionSupported()
                                onClicked: assignSelectedGalleryMedia()
                            }

                            ActionButton {
                                text: selection.isLinkedGroup ? "Clear Group" : "Clear"
                                onClicked: {
                                    if (selection.isLinkedGroup)
                                        carouselDocument.clearCurrentGroup()
                                    else
                                        carouselDocument.clearCurrentSlot()
                                }
                            }

                            Item { Layout.fillWidth: true }

                            InfoPill {
                                label: "Scale"
                                value: activeScale().toFixed(2) + "x"
                            }

                            InfoPill {
                                label: "Pan"
                                value: Math.round(activeOffsetX() * 100) + ", " + Math.round(activeOffsetY() * 100)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: theme.alpha(theme.ink950, 0.58)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.75)
                    implicitHeight: 136
                    clip: true

                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 12
                        contentWidth: stripRow.width
                        contentHeight: stripRow.height
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        Row {
                            id: stripRow
                            spacing: 12
                            height: parent.height

                            Repeater {
                                model: carouselDocument.slides

                                delegate: Rectangle {
                                    required property var modelData

                                    width: 152
                                    height: 110
                                    radius: 16
                                    color: modelData.selected ? theme.alpha(theme.ink800, 0.96) : theme.alpha(theme.ink850, 0.94)
                                    border.width: modelData.selected ? 2 : 1
                                    border.color: modelData.selected ? theme.accentWarm500 : theme.alpha(theme.lineSoft, 0.8)

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 6

                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true

                                            Rectangle {
                                                anchors.fill: parent
                                                radius: 10
                                                color: theme.ink900
                                                clip: true
                                            }

                                            Image {
                                                anchors.fill: parent
                                                anchors.margins: 4
                                                source: modelData.previewSource
                                                fillMode: Image.PreserveAspectFit
                                                cache: false
                                                asynchronous: false
                                            }

                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.top: parent.top
                                                anchors.margins: 6
                                                radius: 7
                                                color: theme.alpha(theme.ink950, 0.82)
                                                implicitWidth: numLabel.implicitWidth + 12
                                                implicitHeight: 20

                                                Label {
                                                    id: numLabel
                                                    anchors.centerIn: parent
                                                    text: modelData.title
                                                    color: theme.textStrong
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: carouselDocument.selectSlide(modelData.index)
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.label && modelData.label.length > 0 ? modelData.label : "Add media"
                                            color: theme.textMain
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: 118
                                height: 110
                                radius: 16
                                color: theme.alpha(theme.ink850, 0.85)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.75)

                                ActionButton {
                                    anchors.centerIn: parent
                                    text: "+ Add Slide"
                                    tone: "primary"
                                    enabled: carouselDocument.canAddSlide
                                    onClicked: carouselDocument.addSlide()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
