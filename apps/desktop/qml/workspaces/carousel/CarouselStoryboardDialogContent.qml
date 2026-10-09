pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQml
import "../../components"

ColumnLayout {
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselStoryboardDialog
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: "Storyboard overview"
            color: theme.textStrong
            font.family: theme.fontDisplay
            font.pixelSize: 20
            font.weight: Font.DemiBold
            elide: Text.ElideNone
        }
        StatusChip {
            text: carouselDocument.selectedSlideCount + " selected"
            compact: true
        }
        StatusChip {
            text: carouselDocument.boardSections.length + " sections"
            compact: true
        }
    }

    RowLayout {
        objectName: "carouselCreateBoardSectionControls"
        Layout.fillWidth: true
        spacing: 6

        ToneTextField {
            id: storyboardSectionNameField
            objectName: "carouselStoryboardSectionNameField"
            Layout.fillWidth: true
            placeholderText: "Section name for selected consecutive boards"
            text: root.storyboardSectionName
            onTextEdited: root.storyboardSectionName = text
            onAccepted: {
                if (carouselDocument.createBoardSection(root.storyboardSectionName)) {
                    root.storyboardSectionName = ""
                    text = ""
                }
            }
        }

        ActionButton {
            objectName: "carouselCreateBoardSectionButton"
            compact: true
            tone: "primary"
            text: "Create section"
            enabled: carouselDocument.selectedSlideCount >= 2 &&
                     root.storyboardSectionName.trim().length > 0
            disabledReason: "Select at least two consecutive boards and enter a name."
            onClicked: {
                if (carouselDocument.createBoardSection(root.storyboardSectionName)) {
                    root.storyboardSectionName = ""
                    storyboardSectionNameField.text = ""
                }
            }
        }
    }

    ScrollView {
        objectName: "carouselBoardSectionsOverview"
        Layout.fillWidth: true
        Layout.preferredHeight: carouselDocument.boardSections.length > 0 ? 104 : 0
        visible: carouselDocument.boardSections.length > 0
        clip: true
        contentHeight: availableHeight

        Row {
            height: parent.height
            spacing: 8

            Repeater {
                model: carouselDocument.boardSections

                delegate: Rectangle {
                    required property var modelData
                    width: 286
                    height: 96
                    radius: theme.radius8
                    color: theme.alpha(theme.ink950, 0.66)
                    border.width: 1
                    border.color: theme.alpha(theme.accentWarm400, 0.34)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            ToneTextField {
                                objectName: "carouselBoardSectionRenameField"
                                Layout.fillWidth: true
                                text: String(modelData.name || "Section")
                                onEditingFinished: {
                                    if (text.trim().length > 0 && text.trim() !== String(modelData.name || ""))
                                        carouselDocument.renameBoardSection(String(modelData.sectionId || ""), text)
                                }
                            }
                            StatusChip {
                                text: Number(modelData.span || 0) + " boards"
                                compact: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            ActionButton {
                                compact: true
                                text: "Select"
                                onClicked: carouselDocument.selectBoardSection(String(modelData.sectionId || ""))
                            }
                            ActionButton {
                                objectName: "carouselBoardSectionCollapseButton"
                                compact: true
                                text: Boolean(modelData.collapsed) ? "Expand" : "Collapse"
                                onClicked: carouselDocument.setBoardSectionCollapsed(
                                               String(modelData.sectionId || ""),
                                               !Boolean(modelData.collapsed))
                            }
                            ActionButton {
                                compact: true
                                showText: false
                                iconName: "chevron-left"
                                tooltipText: "Move complete section earlier"
                                enabled: Boolean(modelData.canMoveEarlier)
                                onClicked: carouselDocument.moveBoardSection(String(modelData.sectionId || ""), -1)
                            }
                            ActionButton {
                                compact: true
                                showText: false
                                iconName: "chevron-right"
                                tooltipText: "Move complete section later"
                                enabled: Boolean(modelData.canMoveLater)
                                onClicked: carouselDocument.moveBoardSection(String(modelData.sectionId || ""), 1)
                            }
                            Item { Layout.fillWidth: true }
                            ActionButton {
                                objectName: "carouselBoardSectionUngroupButton"
                                compact: true
                                tone: "danger"
                                text: "Ungroup"
                                onClicked: carouselDocument.removeBoardSection(String(modelData.sectionId || ""))
                            }
                        }
                    }
                }
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: root.boardRoleButtons
            delegate: FilterChip {
                required property var modelData
                text: modelData.label
                onClicked: carouselDocument.setSelectedSlidesRole(modelData.value)
            }
        }
    }

    GridView {
        id: carouselStoryboardGrid
        Layout.fillWidth: true
        Layout.fillHeight: true
        cellWidth: 176
        cellHeight: 224
        clip: true
        model: carouselDocument.storyboardEntries

        delegate: Item {
            required property int index
            required property var modelData
            readonly property int boardIndex: Number(modelData.index || 0)
            width: carouselStoryboardGrid.cellWidth
            height: carouselStoryboardGrid.cellHeight

            CarouselBoardThumb {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                height: 184
                index: parent.boardIndex
                modelData: parent.modelData
                onSelectedRequested: function(boardIndex, modifiers) {
                    if (Boolean(parent.modelData.sectionCollapsed))
                        carouselDocument.selectBoardSection(String(parent.modelData.sectionId || ""))
                    else if (root.hasRangeSelectModifier(modifiers))
                        carouselDocument.selectSlideRange(boardIndex)
                    else if (root.hasMultiSelectModifier(modifiers))
                        carouselDocument.toggleSlideSelection(boardIndex)
                    else
                        carouselDocument.selectSlide(boardIndex)
                }
                onDuplicateRequested: function(boardIndex) {
                    if (Boolean(parent.modelData.sectionCollapsed)) {
                        carouselDocument.selectBoardSection(String(parent.modelData.sectionId || ""))
                        carouselDocument.duplicateSelectedSlides()
                    } else {
                        carouselDocument.selectSlide(boardIndex)
                        carouselDocument.duplicateCurrentSlide()
                    }
                }
                onReorderRequested: function(fromIndex, toIndex) {
                    if (String(parent.modelData.sectionId || "").length > 0)
                        carouselDocument.moveBoardSection(
                                    String(parent.modelData.sectionId || ""),
                                    toIndex < parent.boardIndex ? -1 : 1)
                    else
                        carouselDocument.moveSlide(
                                    fromIndex,
                                    Math.min(carouselDocument.slideCount - 1, toIndex))
                }
                onMediaDropRequested: function(boardIndex, drop) {
                    root.applyMediaDrop(drop, boardIndex, "thumbnail", "primary", 0.5, 0.5)
                }
            }

            StatusChip {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 12
                anchors.topMargin: 12
                visible: Boolean(parent.modelData.sectionFirst)
                text: String(parent.modelData.sectionName || "Section")
                tone: "accent"
                compact: true
                z: 20
            }

            Rectangle {
                objectName: "carouselCollapsedBoardSectionSummary"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 42
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                height: 30
                radius: theme.radius6
                visible: Boolean(parent.modelData.sectionCollapsed)
                color: theme.alpha(theme.ink950, 0.88)
                border.width: 1
                border.color: theme.alpha(theme.accentWarm400, 0.5)
                z: 20

                Label {
                    anchors.centerIn: parent
                    text: Number(parent.parent.modelData.sectionSpan || 0) +
                          " boards collapsed · click to select section"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 9
                }

                TapHandler {
                    onTapped: carouselDocument.selectBoardSection(
                                  String(parent.parent.modelData.sectionId || ""))
                }
            }

            ActionButton {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
                compact: true
                text: "Open in composer"
                onClicked: {
                    carouselDocument.selectSlide(parent.boardIndex)
                    carouselStoryboardDialog.close()
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ActionButton {
            text: "Duplicate selected"
            enabled: carouselDocument.canAddSlide
            onClicked: carouselDocument.duplicateSelectedSlides()
        }
        ActionButton {
            text: "Delete selected"
            tone: "danger"
            enabled: carouselDocument.slideCount > carouselDocument.selectedSlideCount
            onClicked: carouselDocument.removeSelectedSlides()
        }
        Item { Layout.fillWidth: true }
        ActionButton {
            text: "Return to composer"
            tone: "primary"
            onClicked: carouselStoryboardDialog.close()
        }
    }
}
