pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQml
import "../../components"

ColumnLayout {
    id: content
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselExportSheet
    property bool guidanceExpanded: false
    readonly property string jobState: String(carouselExportSheet.progress.state || "idle")
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function focusInitialTarget() {
        pageOutput.forceActiveFocus(Qt.TabFocusReason);
        if (exportScroll.contentItem) exportScroll.contentItem.contentY = 0;
    }
    function revealFocusedControl(item) {
        if (!item || !item.visible || !exportScroll.contentItem) return;
        let ancestor = item;
        while (ancestor && ancestor !== settingsColumn) ancestor = ancestor.parent;
        if (ancestor !== settingsColumn) return;
        const viewport = exportScroll.contentItem as Flickable;
        if (!viewport) return;
        const top = item.mapToItem(viewport.contentItem, 0, 0).y;
        const bottom = top + item.height;
        let position = viewport.contentY;
        if (top < position) position = top - content.theme.space8;
        else if (bottom > position + viewport.height) position = bottom - viewport.height + content.theme.space8;
        viewport.contentY = Math.max(0, Math.min(position, Math.max(0, viewport.contentHeight - viewport.height)));
    }
    function revealActiveFocus() {
        if (content.Window.window) revealFocusedControl(content.Window.window.activeFocusItem);
    }
    Connections {
        target: content.Window.window
        function onActiveFocusItemChanged() {
            content.revealFocusedControl(content.Window.window.activeFocusItem);
        }
    }
    Connections {
        target: content.carouselExportSheet
        function onActivePageChanged() {
            Qt.callLater(function() {
                if (exportScroll.contentItem) exportScroll.contentItem.contentY = 0;
            });
        }
    }
    RowLayout {
        objectName: "carouselExportHeader"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            Layout.fillWidth: true
            text: "Export carousel"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        ActionButton {
            objectName: "carouselExportCloseButton"
            text: "Close"
            iconName: "close"
            compact: true
            onClicked: content.carouselExportSheet.close()
        }
    }
    Label {
        objectName: "carouselExportSummary"
        Layout.fillWidth: true
        text: content.carouselExportSheet.summary.boardCount + " board" + (content.carouselExportSheet.summary.boardCount === 1 ? "" : "s")
              + " · " + content.carouselExportSheet.summary.ratio
              + " · " + content.carouselExportSheet.summary.pixelSize + " px"
              + (String(content.carouselExportSheet.summary.fileTypes || "").indexOf("PNG") >= 0 ||
                 String(content.carouselExportSheet.summary.fileTypes || "").indexOf("JPEG") >= 0 ||
                 String(content.carouselExportSheet.summary.fileTypes || "").indexOf("WEBP") >= 0
                 ? " · 8-bit image" : "")
              + " · " + content.carouselExportSheet.summary.fileTypes
              + " · " + content.carouselExportSheet.summary.colorSpace
        color: content.theme.textMuted
        font.family: content.theme.fontUi
        font.pixelSize: content.theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    Label {
        objectName: "carouselExportEditableProjectGuidance"
        Layout.fillWidth: true
        text: "Image boards are rendered files; video boards use source copies. The editable carousel stays in Library."
        color: content.theme.textMuted
        font.family: content.theme.fontUi
        font.pixelSize: content.theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    Flow {
        objectName: "carouselExportNavigation"
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: content.theme.space6
        FilterChip {
            id: pageOutput
            objectName: "carouselExportSettingsTab"
            text: "Output"
            active: content.carouselExportSheet.activePage === 0
            onClicked: content.carouselExportSheet.activePage = 0
        }
        FilterChip {
            objectName: "carouselExportFilesTab"
            text: "Files"
            active: content.carouselExportSheet.activePage === 2
            onClicked: content.carouselExportSheet.activePage = 2
        }
        FilterChip {
            objectName: "carouselExportChecksTab"
            text: "Checks" + (content.carouselExportSheet.preflightIssues.length > 0 ? "  " + content.carouselExportSheet.preflightIssues.length : "")
            active: content.carouselExportSheet.activePage === 3
            tone: content.carouselExportSheet.summary.blockingIssueCount > 0 ? "warning" : "neutral"
            onClicked: content.carouselExportSheet.activePage = 3
        }
        FilterChip {
            objectName: "carouselExportPresetsTab"
            text: "Presets"
            active: content.carouselExportSheet.activePage === 1
            onClicked: content.carouselExportSheet.activePage = 1
        }
    }
    ScrollView {
        id: exportScroll
        objectName: "carouselExportScroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: content.theme.scaled(440)
        clip: true
        contentWidth: availableWidth
        onHeightChanged: Qt.callLater(content.revealActiveFocus)
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.vertical.active: true
        ScrollBar.vertical.visible: exportScroll.ScrollBar.vertical.size < 1
        ColumnLayout {
            id: settingsColumn
            width: exportScroll.availableWidth
            enabled: !content.carouselDocument.exportBusy
            spacing: content.theme.space18
            onImplicitHeightChanged: Qt.callLater(content.revealActiveFocus)
            ColumnLayout {
                objectName: "carouselExportBoardsSection"
                Layout.fillWidth: true

                visible: content.carouselExportSheet.activePage === 0
                spacing: content.theme.space8

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: "Boards to export"
                        objectName: "carouselExportBoardsHeading"
                        color: content.theme.textMain
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        font.weight: Font.DemiBold
                    }

                    ActionButton {
                        objectName: "carouselExportReviewBoardsButton"
                        compact: true
                        text: content.carouselExportSheet.boardReviewExpanded
                              ? "Hide board review" : "Review board order"
                        onClicked: content.carouselExportSheet.boardReviewExpanded =
                                       !content.carouselExportSheet.boardReviewExpanded
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space5

                    Repeater {
                        model: content.carouselDocument.exportBoardScopeOptions

                        delegate: FilterChip {
                            required property string modelData
                            objectName: "carouselExportScope_" + modelData
                            text: modelData === "current" ? "Current board" :
                                  modelData === "selected" ? "Selected (" + content.carouselDocument.selectedSlideCount + ")" :
                                  "All boards"
                            active: content.carouselDocument.exportBoardScope === modelData
                            onClicked: content.carouselDocument.setExportBoardScope(modelData)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        text: "Exact ordered output · boards " + content.carouselExportSheet.summary.boardOrder
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        Layout.fillWidth: true
                    }

                    Label {
                        text: content.carouselExportSheet.summary.boardCount + " board" +
                              (content.carouselExportSheet.summary.boardCount === 1 ? "" : "s")
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                    }
                }

                ListView {
                    id: boardReviewList
                    objectName: "carouselExportOrderedBoardReview"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? Math.min(content.theme.scaled(230), Math.max(content.theme.scaled(90), exportScroll.height - content.theme.scaled(60))) : 0
                    visible: content.carouselExportSheet.boardReviewExpanded
                    spacing: content.theme.space4
                    clip: true
                    reuseItems: true
                    activeFocusOnTab: true
                    model: content.carouselExportSheet.boardReview
                    property int reviewedBoardIndex: -1
                    Accessible.role: Accessible.List
                    Accessible.name: "Ordered boards and output filenames"
                    function restoreReviewedBoard() {
                        if (!count || reviewedBoardIndex < 0) return;
                        for (let row = 0; row < model.length; ++row) {
                            if (Number(model[row].boardIndex) === reviewedBoardIndex) {
                                currentIndex = row;
                                break;
                            }
                        }
                        positionViewAtIndex(currentIndex, ListView.Contain);
                        if (activeFocus) content.revealFocusedControl(boardReviewList);
                    }
                    onModelChanged: Qt.callLater(restoreReviewedBoard)
                    function focusRow(index) {
                        if (!count) return;
                        currentIndex = Math.max(0, Math.min(index, count - 1));
                        reviewedBoardIndex = Number(model[currentIndex].boardIndex);
                        positionViewAtIndex(currentIndex, ListView.Contain);
                        forceActiveFocus(Qt.TabFocusReason);
                        Qt.callLater(restoreReviewedBoard);
                    }
                    Keys.onDownPressed: focusRow(currentIndex + 1)
                    Keys.onUpPressed: focusRow(currentIndex < 0 ? 0 : currentIndex - 1)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                            focusRow(event.key === Qt.Key_Home ? 0 : count - 1);
                            event.accepted = true;
                        }
                    }
                    highlightResizeDuration: 0
                    highlightMoveDuration: content.theme.durationFast
                    highlight: Rectangle {
                        visible: boardReviewList.activeFocus
                        radius: content.theme.radiusControl
                        color: content.theme.alpha(content.theme.focusFill, 0.14)
                        border.width: content.theme.focusBorderWidth
                        border.color: content.theme.focusRing
                    }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; active: true }
                    delegate: Item {
                        id: boardReviewRow
                        required property var modelData
                        required property int index
                        objectName: "carouselExportBoardReviewRow_" + index
                        width: boardReviewList.width - content.theme.space8
                        implicitHeight: Math.max(content.theme.scaled(64), reviewLabels.implicitHeight) + content.theme.space8 * 2
                        Accessible.role: Accessible.ListItem
                        Accessible.name: "Board " + modelData.boardNumber + ", " + String(modelData.boardName || "") + ", " + String(modelData.resolvedName || "")
                        Accessible.selectable: true
                        Accessible.selected: boardReviewList.currentIndex === index
                        Accessible.onPressAction: boardReviewList.focusRow(index)
                        TapHandler { onTapped: boardReviewList.focusRow(boardReviewRow.index) }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: content.theme.space8
                            spacing: content.theme.space12
                            Rectangle {
                                Layout.preferredWidth: content.theme.scaled(90)
                                Layout.preferredHeight: content.theme.scaled(64)
                                Layout.alignment: Qt.AlignTop
                                radius: content.theme.radiusControl
                                color: String(boardReviewRow.modelData.backgroundColor || "#fbf7f1")
                                clip: true
                                Image {
                                    objectName: "carouselExportBoardPreview_" + boardReviewRow.index
                                    anchors.fill: parent
                                    source: String(boardReviewRow.modelData.previewSource || "")
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                }
                            }
                            ColumnLayout {
                                id: reviewLabels
                                Layout.fillWidth: true
                                spacing: content.theme.space4
                                Label {
                                    Layout.fillWidth: true
                                    text: "Board " + boardReviewRow.modelData.boardNumber + (String(boardReviewRow.modelData.boardName || "").length ? " · " + boardReviewRow.modelData.boardName : "")
                                    color: content.theme.textStrong
                                    font.family: content.theme.fontUi
                                    font.pixelSize: content.theme.typeControl
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                                Label {
                                    objectName: "carouselExportBoardFilename_" + boardReviewRow.index
                                    Layout.fillWidth: true
                                    text: String(boardReviewRow.modelData.resolvedName || "")
                                    color: boardReviewRow.modelData.collision ? content.theme.warning : content.theme.textMuted
                                    font.family: content.theme.fontMono
                                    font.pixelSize: content.theme.typeCaption
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "carouselExportResolutionSection"
                Layout.fillWidth: true

                visible: content.carouselExportSheet.activePage === 0
                spacing: content.theme.space6

                Label {
                    text: "Export resolution"
                    objectName: "carouselExportResolutionHeading"
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    font.weight: Font.DemiBold
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space5

                    Repeater {
                        model: content.carouselDocument.exportSizeModeOptions

                        delegate: FilterChip {
                            required property string modelData
                            objectName: "carouselExportSize_" + modelData
                            text: modelData === "1x" ? "1×" :
                                  modelData === "2x" ? "2×" : "Custom"
                            active: content.carouselDocument.exportSizeSettings.mode === modelData
                            onClicked: content.carouselDocument.setExportSizeMode(modelData)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: content.theme.space8

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        text: "Composer preview · " + content.carouselDocument.exportSizeSettings.previewResolution + " px"
                        color: content.theme.textMuted
                        font.family: content.theme.fontMono
                        font.pixelSize: content.theme.typeCaption
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        text: "Rendered export · " + content.carouselDocument.exportSizeSettings.exportResolution + " px"
                        color: content.theme.textStrong
                        font.family: content.theme.fontMono
                        font.pixelSize: content.theme.typeCaption
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: content.theme.space6
                    visible: content.carouselDocument.exportSizeSettings.mode === "custom"

                    ToneTextField {
                        id: exportCustomWidthField
                        objectName: "carouselExportCustomWidthField"
                        accessibleLabel: "Export width in pixels"
                        Layout.fillWidth: true
                        placeholderText: "Width"
                        selectByMouse: true
                        validator: IntValidator { bottom: 64; top: 16384 }
                        onEditingFinished: content.carouselDocument.setExportCustomPixelSize(
                                               Number(text),
                                               Number(content.carouselDocument.exportSizeSettings.customHeight))
                    }

                    Label {
                        text: "×"
                        color: content.theme.textMuted
                        font.family: content.theme.fontMono
                        font.pixelSize: content.theme.typeCaption
                    }

                    ToneTextField {
                        id: exportCustomHeightField
                        objectName: "carouselExportCustomHeightField"
                        accessibleLabel: "Export height in pixels"
                        Layout.fillWidth: true
                        placeholderText: "Height"
                        selectByMouse: true
                        validator: IntValidator { bottom: 64; top: 16384 }
                        onEditingFinished: content.carouselDocument.setExportCustomPixelSize(
                                               Number(content.carouselDocument.exportSizeSettings.customWidth),
                                               Number(text))
                    }

                    ToneCheckBox {
                        objectName: "carouselExportAspectLock"
                        allowLabelWrap: true
                        text: content.carouselDocument.exportSizeSettings.aspectLocked
                              ? "Aspect locked" : "Aspect unlocked"
                        checked: content.carouselDocument.exportSizeSettings.aspectLocked
                        onClicked: content.carouselDocument.setExportAspectLocked(
                                       !content.carouselDocument.exportSizeSettings.aspectLocked)
                    }
                }

                Binding {
                    target: exportCustomWidthField
                    property: "text"
                    value: String(content.carouselDocument.exportSizeSettings.customWidth)
                    when: !exportCustomWidthField.activeFocus
                }

                Binding {
                    target: exportCustomHeightField
                    property: "text"
                    value: String(content.carouselDocument.exportSizeSettings.customHeight)
                    when: !exportCustomHeightField.activeFocus
                }

                Label {
                    Layout.fillWidth: true
                    visible: String(content.carouselDocument.exportSizeSettings.warning || "").length > 0
                    text: String(content.carouselDocument.exportSizeSettings.warning || "")
                    color: content.theme.warning
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }

            ColumnLayout {
                objectName: "carouselExportImageSection"
                Layout.fillWidth: true

                visible: content.carouselExportSheet.activePage === 0
                spacing: content.theme.space6

                Label {
                    text: "Image output"
                    objectName: "carouselExportImageHeading"
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    font.weight: Font.DemiBold
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space6

                    Repeater {
                        model: content.carouselDocument.exportImageFormatOptions

                        delegate: FilterChip {
                            required property string modelData
                            objectName: "carouselExportFormat_" + modelData
                            text: modelData === "jpeg" ? "JPEG" : modelData.toUpperCase()
                            active: content.carouselDocument.exportImageFormat === modelData
                            onClicked: content.carouselDocument.setExportImageFormat(modelData)
                        }
                    }
                }

                InspectorSlider {
                    objectName: "carouselExportQualitySlider"
                    visible: content.carouselDocument.exportImageFormat !== "png"
                    Layout.fillWidth: true
                    label: "Quality"
                    valueText: content.carouselDocument.exportImageQuality + "%"
                    from: 1
                    to: 100
                    stepSize: 1
                    value: content.carouselDocument.exportImageQuality
                    accent: content.theme.accentCool400
                    onValueDragged: function(value) {
                        content.carouselDocument.setExportImageQuality(Math.round(value))
                    }
                    onResetRequested: content.carouselDocument.setExportImageQuality(92)
                }

                ToneCheckBox {
                    objectName: "carouselExportTransparencyToggle"
                    allowLabelWrap: true
                    Layout.fillWidth: true
                    text: content.carouselDocument.exportImageFormat === "jpeg"
                          ? "Transparency unavailable in JPEG"
                          : "Preserve transparency"
                    checked: content.carouselDocument.exportImageFormat !== "jpeg" &&
                            content.carouselDocument.exportPreserveTransparency
                    enabled: content.carouselDocument.exportImageFormat !== "jpeg"
                    onClicked: content.carouselDocument.setExportPreserveTransparency(
                                   !content.carouselDocument.exportPreserveTransparency)
                }

                Label {
                    Layout.fillWidth: true
                    text: content.carouselExportSheet.summary.formatWarning
                    color: content.carouselDocument.exportImageFormat === "png"
                           ? content.theme.textMuted
                           : content.theme.warning
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }

            ColumnLayout {
                objectName: "carouselExportProfileSection"
                Layout.fillWidth: true

                visible: content.carouselExportSheet.activePage === 0
                spacing: content.theme.space6

                Label {
                    text: "Output color profile"
                    objectName: "carouselExportProfileHeading"
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    font.weight: Font.DemiBold
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space6

                    Repeater {
                        model: content.carouselDocument.exportColorSpaceOptions

                        delegate: FilterChip {
                            required property string modelData
                            objectName: "carouselExportProfile_" + modelData
                            text: modelData
                            active: content.carouselDocument.exportColorSpace === modelData
                            onClicked: content.carouselDocument.setExportColorSpace(modelData)
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "The selected profile is embedded in every image and verified after writing."
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }

            Label {
                objectName: "carouselExportEstimate"
                Layout.fillWidth: true
                visible: content.carouselExportSheet.activePage === 0
                text: "Estimated output · " + content.carouselExportSheet.summary.estimatedOutput
                color: content.theme.textMuted
                font.family: content.theme.fontUi
                font.pixelSize: content.theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }

            ColumnLayout {
                objectName: "carouselExportFilesSection"
                Layout.fillWidth: true

                visible: content.carouselExportSheet.activePage === 2
                spacing: content.theme.space6

                Label {
                    text: "File naming"
                    objectName: "carouselExportFilesHeading"
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    font.weight: Font.DemiBold
                }

                ToneTextField {
                    id: exportFilenameField
                    objectName: "carouselExportFilenameField"
                    accessibleLabel: "Export filename pattern"
                    Layout.fillWidth: true
                    selectByMouse: true
                    color: content.theme.textStrong
                    font.family: content.theme.fontMono

                    onEditingFinished: content.carouselDocument.setExportFilenamePattern(text)
                }

                Binding {
                    target: exportFilenameField
                    property: "text"
                    value: content.carouselDocument.exportFilenamePattern
                    when: !exportFilenameField.activeFocus
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: content.theme.space4

                    Repeater {
                        model: ["{document}", "{board_number}", "{board_role}", "{ratio}", "{version}", "{date}"]

                        delegate: ActionButton {
                            required property string modelData
                            text: modelData
                            objectName: "carouselExportToken_" + modelData
                            compact: true
                            onClicked: {
                                exportFilenameField.insert(exportFilenameField.cursorPosition, modelData)
                                content.carouselDocument.setExportFilenamePattern(exportFilenameField.text)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: content.theme.space6

                    Label {
                        text: "Version"
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                    }

                    ActionButton {
                        objectName: "carouselDecreaseExportVersionButton"
                        text: "Decrease export version"
                        showText: false
                        iconName: "minus"
                        tooltipText: "Decrease export version"
                        compact: true
                        enabled: content.carouselDocument.exportVersion > 1
                        onClicked: content.carouselDocument.setExportVersion(content.carouselDocument.exportVersion - 1)
                    }

                    Label {
                        text: "v" + content.carouselDocument.exportVersion
                        color: content.theme.textStrong
                        font.family: content.theme.fontMono
                        font.pixelSize: content.theme.typeCaption
                    }

                    ActionButton {
                        objectName: "carouselIncreaseExportVersionButton"
                        text: "Increase export version"
                        showText: false
                        iconName: "plus"
                        tooltipText: "Increase export version"
                        compact: true
                        enabled: content.carouselDocument.exportVersion < 999
                        onClicked: content.carouselDocument.setExportVersion(content.carouselDocument.exportVersion + 1)
                    }

                    Item { Layout.fillWidth: true }
                }

                Label {
                    text: "If a file exists"
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space5

                    Repeater {
                        model: content.carouselDocument.exportCollisionPolicyOptions

                        delegate: FilterChip {
                            required property string modelData
                            objectName: "carouselExportCollision_" + modelData
                            text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                            active: content.carouselDocument.exportCollisionPolicy === modelData
                            onClicked: content.carouselDocument.setExportCollisionPolicy(modelData)
                        }
                    }
                }

                ToneCheckBox {
                    id: popupTooltipAnchor1
                    allowLabelWrap: true
                    Layout.fillWidth: true
                    text: "Delivery manifest"
                    objectName: "carouselExportManifestToggle"
                    checked: content.carouselDocument.exportManifestEnabled
                    onClicked: content.carouselDocument.setExportManifestEnabled(
                                   !content.carouselDocument.exportManifestEnabled)
                    AppToolTip {
                        id: sharedTooltip2
                        targetItem: popupTooltipAnchor1
                        visible: popupTooltipAnchor1.visible && (manifestHover.hovered)
                        delay: 350
                        text: "Include ordered output metadata, hashes, profiles, board roles, accessibility text, warnings, and export time."
                    }
                    HoverHandler { id: manifestHover }
                }

                ToneCheckBox {
                    allowLabelWrap: true
                    id: carouselPdfProofToggle
                    objectName: "carouselPdfProofToggle"
                    Layout.fillWidth: true
                    text: "PDF Review Contact Sheet"
                    checked: content.carouselDocument.exportProofSheetEnabled
                    onClicked: content.carouselDocument.setExportProofSheetEnabled(
                                   !content.carouselDocument.exportProofSheetEnabled)
                    AppToolTip {
                        id: sharedTooltip3
                        targetItem: carouselPdfProofToggle
                        visible: carouselPdfProofToggle.visible && (pdfProofHover.hovered)
                        delay: 350
                        text: "Create a separate A4 landscape review PDF with four color-managed board thumbnails per page."
                    }
                    HoverHandler { id: pdfProofHover }
                }

                ToneCheckBox {
                    allowLabelWrap: true
                    id: carouselPdfProofNotesToggle
                    objectName: "carouselPdfProofNotesToggle"
                    Layout.fillWidth: true
                    visible: content.carouselDocument.exportProofSheetEnabled
                    text: "Include Board Notes"
                    checked: content.carouselDocument.exportProofIncludeNotes
                    onClicked: content.carouselDocument.setExportProofIncludeNotes(
                                   !content.carouselDocument.exportProofIncludeNotes)
                }

                Label {
                    objectName: "carouselPdfProofResolvedSettings"
                    Layout.fillWidth: true
                    visible: content.carouselDocument.exportProofSheetEnabled
                    text: "A4 landscape · 4 boards per page · board number, name, role, and resolved file name · " +
                          (content.carouselDocument.exportProofIncludeNotes
                           ? "notes included · " : "notes omitted · ") +
                          content.carouselDocument.exportColorSpace + " thumbnails converted to sRGB proof space · review only"
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: content.theme.space3

                    Repeater {
                        model: content.carouselDocument.exportOutputPreview

                        delegate: RowLayout {
                            id: outputFileRow
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: content.theme.space6

                            Label {
                                objectName: "carouselExportResolvedFilename_" + outputFileRow.index
                                text: String(outputFileRow.modelData.resolvedName || "")
                                color: outputFileRow.modelData.collision ? content.theme.warning : content.theme.textMain
                                font.family: content.theme.fontMono
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                                Layout.fillWidth: true
                            }

                            Label {
                                visible: Boolean(outputFileRow.modelData.collision)
                                text: String(outputFileRow.modelData.action || "write")
                                color: content.theme.warning
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "carouselExportPartialFilesSection"
                Layout.fillWidth: true
                visible: content.carouselExportSheet.activePage === 2
                spacing: content.theme.space8
                Label {
                    text: "Partial files on stop"
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeControl
                    font.weight: Font.DemiBold
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space6
                    Repeater {
                        model: content.carouselDocument.exportPartialCleanupPolicyOptions
                        delegate: FilterChip {
                            required property string modelData
                            objectName: "carouselExportPartial_" + modelData
                            text: modelData === "remove" ? "Remove partial files" : "Keep partial files"
                            active: content.carouselDocument.exportPartialCleanupPolicy === modelData
                            onClicked: content.carouselDocument.setExportPartialCleanupPolicy(modelData)
                        }
                    }
                    ActionButton {
                        objectName: "carouselCleanPartialExportButton"
                        text: "Clean partial files"
                        compact: true
                        visible: Number(content.carouselExportSheet.progress.partialFileCount || 0) > 0 && !content.carouselDocument.exportBusy
                        onClicked: content.carouselDocument.cleanupPartialExport()
                    }
                }
            }

            ColumnLayout {
                objectName: "carouselExportPreflightSection"
                Layout.fillWidth: true

                visible: content.carouselExportSheet.activePage === 3
                spacing: content.theme.space8

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: "Preflight"
                        color: content.theme.textMain
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: content.carouselExportSheet.summary.blockingIssueCount > 0
                              ? content.carouselExportSheet.summary.blockingIssueCount + " blocking"
                              : "Ready"
                        color: content.carouselExportSheet.summary.blockingIssueCount > 0 ? content.theme.error : content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: content.carouselExportSheet.preflightIssues.length === 0
                    text: "No export issues detected."
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                }

                Repeater {
                    model: content.carouselExportSheet.preflightIssues

                    delegate: ExportPreflightIssueRow {
                        id: exportIssue
                        required property int index
                        required property var modelData
                        objectName: "carouselExportIssue_" + index
                        issue: modelData
                        showInspectAction: true
                        showTextResolutionActions: exportIssue.modelData.code === "text-overflow" ||
                                                   exportIssue.modelData.code === "text-too-small"
                        showIgnoreAction: showTextResolutionActions && severity !== "error"
                        onInspectRequested: {
                            content.carouselDocument.focusPreflightIssue(Number(exportIssue.modelData.boardIndex || 0), String(exportIssue.modelData.itemId || ""));
                            content.carouselExportSheet.close();
                        }
                        onResizeFrameRequested: content.carouselDocument.resolveTextPreflightIssue(
                                                    String(exportIssue.modelData.itemId || ""),
                                                    String(exportIssue.modelData.code || ""),
                                                    "resize-frame")
                        onFitTextRequested: content.carouselDocument.resolveTextPreflightIssue(
                                               String(exportIssue.modelData.itemId || ""),
                                               String(exportIssue.modelData.code || ""),
                                               "fit-text")
                        onIgnoreRequested: content.carouselDocument.resolveTextPreflightIssue(
                                              String(exportIssue.modelData.itemId || ""),
                                              String(exportIssue.modelData.code || ""),
                                              "ignore")
                        TapHandler {
                            onTapped: {
                                content.carouselDocument.focusPreflightIssue(
                                            Number(exportIssue.modelData.boardIndex || 0),
                                            String(exportIssue.modelData.itemId || ""))
                                content.carouselExportSheet.close()
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "carouselExportPresetSection"
                Layout.fillWidth: true
                visible: content.carouselExportSheet.activePage === 1
                spacing: content.theme.space12
                Label {
                    Layout.fillWidth: true
                    text: content.carouselExportSheet.presetState.hasActivePreset
                          ? String(content.carouselExportSheet.presetState.activePresetName) + " · v" + content.carouselExportSheet.presetState.activePresetVersion
                            + (content.carouselExportSheet.presetState.overrideCount > 0 ? " · " + content.carouselExportSheet.presetState.overrideCount + " overrides" : " · exact")
                          : "Custom export settings"
                    color: content.carouselExportSheet.presetState.overrideCount > 0 ? content.theme.warning : content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                ToneComboBox {
                    id: presetPicker
                    objectName: "carouselExportPresetBrowser"
                    Layout.fillWidth: true
                    accessibleLabel: "Delivery preset"
                    model: content.carouselDocument.exportPresets
                    textRole: "name"
                    valueRole: "presetId"
                    currentIndex: {
                        for (let index = 0; index < model.length; ++index) {
                            if (String(model[index].presetId) === String(content.carouselExportSheet.inspectedPreset.presetId || "")) return index;
                        }
                        return model.length ? 0 : -1;
                    }
                    onActivated: content.carouselExportSheet.inspectedPresetId = String(currentValue)
                }
                Label {
                    objectName: "carouselExportPresetCompatibility"
                    Layout.fillWidth: true
                    text: String(content.carouselExportSheet.inspectedPreset.compatibilityMessage || "")
                    color: content.carouselExportSheet.inspectedPreset.compatible ? content.theme.textMuted : content.theme.warning
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Label {
                    objectName: "carouselExportGuidanceDate"
                    Layout.fillWidth: true
                    visible: Boolean(content.carouselExportSheet.inspectedPreset.timeSensitive)
                    text: "Guidance reviewed " + String(content.carouselExportSheet.inspectedPreset.guidanceLastReviewed || "not dated")
                          + (content.carouselExportSheet.inspectedPreset.guidanceStale ? " · review overdue" : "")
                    color: content.carouselExportSheet.inspectedPreset.guidanceStale ? content.theme.warning : content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                ToneCheckBox {
                    objectName: "carouselExportPreserveOverridesToggle"
                    Layout.fillWidth: true
                    allowLabelWrap: true
                    text: "Preserve existing overrides"
                    checked: content.carouselExportSheet.preservePresetOverrides
                    onClicked: content.carouselExportSheet.preservePresetOverrides = checked
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space8
                    ActionButton {
                        objectName: "carouselApplyExportPresetButton"
                        text: "Apply preset"
                        compact: true
                        tone: "primary"
                        enabled: Boolean(content.carouselExportSheet.inspectedPreset.compatible)
                        disabledReason: String(content.carouselExportSheet.inspectedPreset.compatibilityMessage || "Choose a compatible preset.")
                        onClicked: {
                            if (content.carouselDocument.applyExportPreset(String(content.carouselExportSheet.inspectedPreset.presetId || ""), content.carouselExportSheet.preservePresetOverrides))
                                content.carouselExportSheet.activePage = 0;
                        }
                    }
                    ActionButton {
                        objectName: "carouselResetExportPresetOverridesButton"
                        text: "Reset overrides"
                        compact: true
                        enabled: Boolean(content.carouselExportSheet.presetState.hasActivePreset) && Number(content.carouselExportSheet.presetState.overrideCount || 0) > 0
                        disabledReason: "The active preset has no user overrides."
                        onClicked: content.carouselDocument.resetExportPresetOverrides()
                    }
                }
                ActionButton {
                    objectName: "carouselExportGuidanceDetailsButton"
                    text: content.guidanceExpanded ? "Hide preset guidance" : "Preset guidance"
                    iconName: content.guidanceExpanded ? "chevron-down" : "chevron-right"
                    compact: true
                    onClicked: content.guidanceExpanded = !content.guidanceExpanded
                }
                Rectangle {
                    objectName: "carouselExportGuidanceBody"
                    visible: content.guidanceExpanded
                    Layout.fillWidth: true
                    Layout.preferredHeight: exportPresetGuidance.implicitHeight + content.theme.space16
                    radius: content.theme.radius6
                    color: content.theme.alpha(content.theme.ink800, 0.72)
                    border.width: 1
                    border.color: Boolean(content.carouselExportSheet.inspectedPreset.guidanceStale)
                                  ? content.theme.alpha(content.theme.warning, 0.72)
                                  : content.theme.alpha(content.theme.lineSoft, 0.38)

                    ColumnLayout {
                        id: exportPresetGuidance
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: content.theme.space8
                        spacing: content.theme.space4

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                objectName: "carouselExportGuidanceHeading"
                                Layout.fillWidth: true
                                text: String(content.carouselExportSheet.inspectedPreset.name || "Preset") +
                                      " guidance · last reviewed " +
                                      String(content.carouselExportSheet.inspectedPreset.guidanceLastReviewed || "not dated")
                                color: content.theme.textMain
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                            }

                            Label {
                                visible: Boolean(content.carouselExportSheet.inspectedPreset.timeSensitive)
                                text: Boolean(content.carouselExportSheet.inspectedPreset.guidanceStale)
                                      ? "Review overdue" : "Review current"
                                color: Boolean(content.carouselExportSheet.inspectedPreset.guidanceStale) ? content.theme.warning : content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                            }
                        }

                        Label {
                            objectName: "carouselExportGuidanceSummary"
                            Layout.fillWidth: true
                            text: String(content.carouselExportSheet.inspectedPreset.guidanceSummary || "")
                            color: content.theme.textMuted
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        Label {
                            objectName: "carouselExportGuidanceDefaults"
                            Layout.fillWidth: true
                            text: String(content.carouselExportSheet.inspectedPreset.appChosenDefaults || "")
                            color: content.theme.textFaint
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: String(content.carouselExportSheet.inspectedPreset.guidanceSourceUrl || "").length > 0

                            Label {
                                objectName: "carouselExportGuidanceSourceDate"
                                Layout.fillWidth: true
                                text: "Source updated " +
                                      String(content.carouselExportSheet.inspectedPreset.guidanceSourceUpdated || "unknown") +
                                      " · review due " +
                                      String(content.carouselExportSheet.inspectedPreset.guidanceReviewDue || "not scheduled")
                                color: content.theme.textFaint
                                font.family: content.theme.fontMono
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                            }

                            ActionButton {
                                objectName: "carouselExportGuidanceSourceButton"
                                text: "Open First-Party Source"
                                compact: true
                                onClicked: Qt.openUrlExternally(
                                               String(content.carouselExportSheet.inspectedPreset.guidanceSourceUrl || ""))
                            }
                        }
                    }
                }
                Label {
                    text: "Current export settings"
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeSection
                    font.weight: Font.DemiBold
                }
                GridLayout {
                    objectName: "carouselExportPresetResolvedSettings"
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: content.theme.space14
                    rowSpacing: content.theme.space4

                    Repeater {
                        model: content.carouselExportSheet.presetState.resolvedRows || []

                        delegate: RowLayout {
                            id: resolvedRow
                            required property var modelData
                            required property int index
                            objectName: "carouselExportResolvedSetting_" + index
                            Layout.fillWidth: true
                            Layout.columnSpan: 2
                            spacing: content.theme.space8

                            Label {
                                Layout.preferredWidth: content.theme.scaled(138)
                                text: String(resolvedRow.modelData.label || "Setting")
                                color: content.theme.textMuted
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                            }

                            Label {
                                Layout.fillWidth: true
                                text: String(resolvedRow.modelData.value || "")
                                color: content.theme.textStrong
                                font.family: content.theme.fontMono
                                font.pixelSize: content.theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                            }

                            Label {
                                visible: Boolean(resolvedRow.modelData.overridden)
                                text: "Override"
                                color: content.theme.warning
                                font.family: content.theme.fontUi
                                font.pixelSize: content.theme.typeCaption
                            }
                        }
                    }
                }
            }
        }
    }
    ColumnLayout {
        objectName: "carouselExportDestination"
        Layout.fillWidth: true
        spacing: content.theme.space6
        Label {
            text: "Destination"
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: content.theme.space8
            ToneTextField {
                objectName: "carouselExportDestinationField"
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                accessibleLabel: "Export destination folder"
                text: content.carouselDocument.exportDirectory
                placeholderText: "Choose a folder"
                font.family: content.theme.fontMono
            }
            ActionButton {
                objectName: "carouselChooseExportDirectoryButton"
                text: "Choose folder"
                compact: true
                enabled: !content.carouselDocument.exportBusy
                onClicked: content.carouselDocument.chooseExportDirectory()
            }
        }
    }
    ColumnLayout {
        objectName: "carouselExportProgressSurface"
        Layout.fillWidth: true
        visible: content.jobState !== "idle"
        spacing: content.theme.space6
        Label {
            objectName: "carouselExportJobStatus"
            Layout.fillWidth: true
            text: (content.jobState === "completed" ? "Export complete" : content.jobState === "failed" ? "Export failed"
                   : content.jobState === "cancelled" ? "Export cancelled" : content.jobState === "cancelling" ? "Stopping export" : "Exporting")
                  + " · " + content.carouselExportSheet.progress.completedCount + " / " + content.carouselExportSheet.progress.totalCount + " boards"
                  + (content.jobState === "failed" && String(content.carouselExportSheet.progress.failureReason || "").length > 0
                     ? " · " + content.carouselExportSheet.progress.failureReason : "")
                  + (Number(content.carouselExportSheet.progress.partialFileCount || 0) > 0
                     ? " · " + content.carouselExportSheet.progress.partialFileCount + " partial files retained" : "")
            color: content.jobState === "failed" ? content.theme.error : content.jobState === "cancelled" ? content.theme.warning : content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        ProgressStrip {
            objectName: "carouselExportProgressBar"
            Layout.fillWidth: true
            visible: content.carouselDocument.exportBusy
            progress: Number(content.carouselExportSheet.progress.fraction || 0)
        }
    }
    RowLayout {
        objectName: "carouselExportFooter"
        Layout.fillWidth: true
        spacing: content.theme.space8
        Label {
            id: exportReadyStatus
            objectName: "carouselExportReadyStatus"
            Layout.fillWidth: true
            visible: !content.carouselDocument.exportBusy &&
                     (content.jobState === "idle" || content.carouselExportSheet.summary.blockingIssueCount > 0 ||
                      content.carouselDocument.exportDirectory.length === 0)
            text: content.carouselExportSheet.summary.blockingIssueCount > 0
                  ? content.carouselExportSheet.summary.blockingIssueCount + " blocking issue" + (content.carouselExportSheet.summary.blockingIssueCount === 1 ? "" : "s") + " · open Checks"
                  : content.carouselDocument.exportDirectory.length === 0 ? "Choose a destination to export."
                  : "Ready · " + content.carouselExportSheet.summary.boardCount + " board" + (content.carouselExportSheet.summary.boardCount === 1 ? "" : "s")
            color: content.carouselExportSheet.summary.blockingIssueCount > 0 ? content.theme.error : content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Item { Layout.fillWidth: true; visible: !exportReadyStatus.visible }
        ActionButton {
            objectName: "carouselCancelExportButton"
            text: "Stop export"
            compact: true
            visible: Boolean(content.carouselExportSheet.progress.canCancel)
            tone: "danger"
            onClicked: content.carouselDocument.cancelExport()
        }
        ActionButton {
            objectName: "carouselRevealExportButton"
            text: "Reveal output"
            compact: true
            visible: Boolean(content.carouselExportSheet.progress.canReveal)
            onClicked: content.carouselDocument.revealExportOutput()
        }
        ActionButton {
            objectName: "carouselRetryExportButton"
            text: "Retry export"
            iconName: "retry"
            tone: "primary"
            compact: true
            visible: Boolean(content.carouselExportSheet.progress.canRetry)
            enabled: content.carouselExportSheet.summary.blockingIssueCount === 0 && content.carouselDocument.exportDirectory.length > 0
            disabledReason: content.carouselExportSheet.summary.blockingIssueCount > 0 ? "Resolve blocking preflight issues first." : "Choose an export destination first."
            onClicked: content.carouselDocument.retryFailedExport()
        }
        ActionButton {
            objectName: "carouselStartExportButton"
            text: content.jobState === "completed" ? "Export again" : "Start export"
            tone: "primary"
            compact: true
            visible: !content.carouselDocument.exportBusy && !Boolean(content.carouselExportSheet.progress.canRetry)
            enabled: content.carouselExportSheet.summary.blockingIssueCount === 0 && content.carouselDocument.exportDirectory.length > 0
            disabledReason: content.carouselExportSheet.summary.blockingIssueCount > 0 ? "Resolve blocking preflight issues first." : "Choose an export destination first."
            onClicked: content.carouselDocument.startConfiguredExport()
        }
    }
}
