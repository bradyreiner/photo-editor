import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: gridPresentation ? "timelineMediaAssetCard" : "timelineMediaAssetRow"

    required property string assetId
    required property int assetIndex
    required property int filteredIndex
    property var assetCatalog: null
    property var projectAsset: ({})
    property int catalogRevision: 0
    readonly property var catalogLibraryAsset: {
        catalogRevision
        return assetCatalog ? assetCatalog.assetById(assetId) : ({})
    }
    readonly property var catalogAsset: {
        const catalogEntry = catalogLibraryAsset
        if (String(projectAsset.filePath || "").length > 0) {
            const merged = Object.assign({}, projectAsset, catalogEntry || ({}))
            merged.assetId = assetId
            const sourcePathDiffers = String(projectAsset.filePath)
                    !== String(catalogEntry.filePath || "")
            merged.filePath = projectAsset.filePath
            merged.mediaType = String(projectAsset.mediaType || catalogEntry.mediaType || "")
            merged.online = projectAsset.online !== false
            merged.thumbnailSource = String(projectAsset.thumbnailSource || projectAsset.posterSource
                                            || (sourcePathDiffers ? ""
                                                : catalogEntry.thumbnailSource
                                                  || catalogEntry.posterSource || ""))
            merged.posterSource = String(projectAsset.posterSource
                                         || (sourcePathDiffers ? ""
                                             : catalogEntry.posterSource || ""))
            if (Number(projectAsset.durationSeconds) > 0)
                merged.durationSeconds = Number(projectAsset.durationSeconds)
            if (String(projectAsset.durationText || "").length > 0)
                merged.durationText = String(projectAsset.durationText)
            return merged
        }
        return catalogEntry || ({})
    }
    readonly property bool projectReferencePathDiffers: String(projectAsset.filePath || "").length > 0
            && String(projectAsset.filePath) !== String(catalogLibraryAsset.filePath || "")
    readonly property bool catalogBacked: String(catalogLibraryAsset.assetId || "").length > 0
                                              && !projectReferencePathDiffers
    property string cameraLabel: String(catalogAsset.cameraLabel || catalogAsset.displayLabel
                                        || catalogAsset.title || "")
    property string filePath: String(catalogAsset.filePath || "")
    property string thumbnailSource: String(catalogAsset.thumbnailSource || catalogAsset.posterSource || "")
    property string mediaType: String(catalogAsset.mediaType || "photo")
    property string durationText: String(catalogAsset.durationText || "")
    property real durationSeconds: Number(catalogAsset.durationSeconds || 0)
    property bool online: catalogAsset.online === undefined
                          ? filePath.length > 0 : catalogAsset.online === true
    property string captureDate: String(catalogAsset.captureDate || "")
    property string lensModel: String(catalogAsset.lensModel || "")
    property string locationPath: String(catalogAsset.locationPath || "")
    property string projectSummary: String(catalogAsset.projectSummary || "")
    property string importedAt: String(catalogAsset.importedAt || "")
    property var projectNames: catalogAsset.projectNames || []
    property string mediaSearchText: ""
    property var mediaSearchScopes: ["all"]
    property var documentController: null
    property int searchRevision: 0
    property int scrubSampleCount: 9
    property real pendingHoverSeconds: -1
    property string hoverFrameSource: ""
    property bool binVisible: true
    property bool smartCollectionVisible: true
    property bool selected: false
    property bool gridPresentation: false
    property real thumbnailTargetSize: 144
    property var mediaPanel: null
    property int trailingScrollbarWidth: 0
    property var mediaMetadata: ({})
    readonly property string libraryType: "timeline-media"
    readonly property string titleText: cameraLabel.length > 0 ? cameraLabel : assetId
    readonly property int rowPadding: theme.scaled(theme.densityComfortable ? 8 : theme.densityCompact ? 4 : 6)
    readonly property int rowBaselineHeight: theme.scaled(theme.densityComfortable ? 88 : theme.densityCompact ? 68 : 78)
    readonly property int thumbnailHeight: theme.scaled(theme.densityComfortable ? 58 : theme.densityCompact ? 44 : 52)
    readonly property string fileNameText: filePath.split(/[\\/]/).pop()
    readonly property string gridTitleText: fileNameText.length > 0 ? fileNameText : titleText
    readonly property int gridCardHeight: GridView.view ? Math.max(1, GridView.view.cellHeight - theme.space8) : 204
    readonly property var catalogSearchMetadata: ({
        "cameraLabel": cameraLabel,
        "filePath": filePath,
        "captureDate": captureDate,
        "lensModel": lensModel,
        "locationPath": locationPath,
        "projectSummary": projectSummary,
        "importedAt": importedAt,
        "projectNames": projectNames,
        "mediaType": mediaType,
        "durationText": durationText
    })
    readonly property var mediaSearchResult: {
        searchRevision
        if (mediaSearchText.length === 0)
            return ({"matches": true, "matchSummary": ""})
        if (documentController)
            return documentController.searchMediaAsset(
                        assetId, mediaSearchText, mediaSearchScopes, catalogSearchMetadata)
        return ({
                    "matches": titleText.toLowerCase().indexOf(mediaSearchText.toLowerCase()) >= 0,
                    "matchSummary": "Filename"
                })
    }
    readonly property bool matchesSearch: mediaSearchResult.matches === true
    readonly property int previewColumnWidth: gridPresentation ? Math.max(1, width - theme.space12) : 84
    readonly property int actionMenuWidth: theme.scaled(34)
    readonly property bool hoverScrubActive: mediaType === "video" && durationSeconds > 0
                                               && (gridPresentation
                                                   ? mediaCardDragArea.containsMouse
                                                   : previewDragArea.containsMouse)
    readonly property string timelineMediaAssetRowContract: "timeline-media-asset-row:v8|presentations:virtualized-grid-list|thumbnail-size:true|preview:true|row-actions:more-menu|selection:click|double-click:preview-source|rating-favorite-reject-color-tags:contextual|search:controller-indexed-with-match-scope|hover-scrub:debounced-bounded-low-resolution-cache|filmstrip-density:5-9-15|keyboard:0-5-f-x|hover:true|grid-list:filename-primary-camera-label-secondary|grid:compact-type-and-status-badges|captions:two-line-elide-tooltip|actions:hover-focus-selection|readable-text:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|metadata-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v5|grid:responsive-preview-card|list:fixed-84-filename-primary-more-menu|actions:source-insert-overwrite|selection:left-edge|hover:neutral|focus:ring|grid-border:selection-focus-only|metadata:grid-badges-only-list-camera-secondary"

    signal sourceRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal insertRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal overwriteRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal selectionRequested(string assetId, int modifiers)
    signal metadataActionRequested(string assetId, string action, string value)
    signal tagEditorRequested(string assetId)
    signal mediaDragMoved(var payload, real sceneX, real sceneY)
    signal mediaDragDropped(var payload, real sceneX, real sceneY)
    signal mediaDragCanceled()

    onSelectionRequested: function(assetId, modifiers) {
        if (mediaPanel)
            mediaPanel.selectMediaAsset(assetId, modifiers, filteredIndex)
    }
    onMetadataActionRequested: function(assetId, action, value) {
        if (mediaPanel && root.catalogBacked)
            mediaPanel.applyMediaMetadataAction(assetId, action, value)
    }
    onTagEditorRequested: function(assetId) {
        if (mediaPanel && root.catalogBacked)
            mediaPanel.openMediaTagEditor(assetId)
    }
    onSourceRequested: function(assetId, filePath, titleText, thumbnailSource) {
        if (mediaPanel)
            mediaPanel.loadIntoSource(assetId, filePath, titleText, thumbnailSource)
    }
    onInsertRequested: function(assetId, filePath, titleText, thumbnailSource) {
        if (mediaPanel && mediaPanel.documentController)
            mediaPanel.documentController.insertClipFromGallery(
                assetId, filePath, titleText, 0, 0, 0, thumbnailSource)
    }
    onOverwriteRequested: function(assetId, filePath, titleText, thumbnailSource) {
        if (mediaPanel && mediaPanel.documentController)
            mediaPanel.documentController.overwriteClipFromGallery(
                assetId, filePath, titleText, 0, 0, 0, thumbnailSource)
    }
    onMediaDragMoved: function(payload, sceneX, sceneY) {
        if (mediaPanel)
            mediaPanel.mediaDragMoved(payload, sceneX, sceneY)
    }
    onMediaDragDropped: function(payload, sceneX, sceneY) {
        if (mediaPanel)
            mediaPanel.mediaDragDropped(payload, sceneX, sceneY)
    }
    onMediaDragCanceled: if (mediaPanel) mediaPanel.mediaDragCanceled()

    function dragPayload() {
        return {
            "assetId": root.assetId,
            "filePath": root.filePath,
            "titleText": root.titleText,
            "thumbnailSource": root.thumbnailSource,
            "mediaType": root.mediaType
        }
    }

    function previewInSource() {
        if (!selected)
            selectionRequested(assetId, Qt.NoModifier)
        sourceRequested(assetId, filePath, titleText, thumbnailSource)
    }

    Theme { id: theme }

    visible: (mediaType === "video" || mediaType === "photo" || mediaType === "audio")
             && matchesSearch && binVisible && smartCollectionVisible
    width: gridPresentation && GridView.view
           ? Math.max(0, GridView.view.cellWidth - theme.space8)
           : ListView.view ? Math.max(0, ListView.view.width - trailingScrollbarWidth) : 360
    implicitHeight: gridPresentation ? gridCardHeight
                    : Math.max(rowBaselineHeight, mediaRow.implicitHeight + rowPadding * 2)
    height: visible ? (gridPresentation ? gridCardHeight : implicitHeight) : 0
    radius: theme.scaled(3)
    color: selected ? theme.alpha(theme.interactionFill, 0.52)
           : rowHover.hovered ? theme.alpha(theme.ink750, 0.28)
           : activeFocus ? theme.alpha(theme.ink800, 0.36)
           : "transparent"
    border.width: activeFocus ? theme.focusBorderWidth : selected ? theme.hairlineWidth : 0
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : selected ? theme.alpha(theme.accentCool400, 0.3)
                  : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: gridTitleText
    Accessible.description: (mediaType === "audio" ? "Audio" : mediaType === "photo" ? "Photo" : "Video")
                            + (durationText.length > 0 ? ", " + durationText : "")
                            + (titleText !== gridTitleText
                               ? ". Display label: " + titleText : "")
                            + (captureDate.length > 0 ? ". Captured " + captureDate : "")
                            + (lensModel.length > 0 ? ". Lens " + lensModel : "")
                            + (online ? ". Online" : ". Offline; relink before use")
                            + ". " + filePath + ". Click to select; double-click or press Return to preview in Source. "
                            + "Drag the preview to place it on the timeline. Use More for timeline actions."
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: root.previewInSource()
    Keys.onEnterPressed: root.previewInSource()
    Keys.onSpacePressed: root.previewInSource()
    Keys.onPressed: function(event) {
        if (!root.catalogBacked)
            return
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_5) {
            root.metadataActionRequested(root.assetId, "rating", String(event.key - Qt.Key_0))
            event.accepted = true
        } else if (event.key === Qt.Key_F) {
            root.metadataActionRequested(root.assetId, "disposition",
                                         root.mediaMetadata.favorite === true ? "none" : "favorite")
            event.accepted = true
        } else if (event.key === Qt.Key_X) {
            root.metadataActionRequested(root.assetId, "disposition",
                                         root.mediaMetadata.rejected === true ? "none" : "rejected")
            event.accepted = true
        } else if (event.key === Qt.Key_T) {
            root.tagEditorRequested(root.assetId)
            event.accepted = true
        }
    }
    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && !root.gridPresentation
                 && (rowHover.hovered && titleLabel.contentWidth > titleLabel.width + 1)
        delay: 350
        timeout: 3600
        text: root.gridTitleText
              + (root.titleText !== root.gridTitleText ? " · " + root.titleText : "")
              + (root.filePath.length > 0 ? " · " + root.filePath : "")
    }
    TapHandler {
        id: mediaCardTapHandler
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        enabled: root.gridPresentation ? !mediaCardDragArea.containsMouse
                                      : !previewDragArea.containsMouse
        onTapped: root.selectionRequested(root.assetId, point.modifiers)
        onDoubleTapped: root.previewInSource()
    }

    HoverHandler {
        id: rowHover
        cursorShape: Qt.PointingHandCursor
    }

    Connections {
        target: root.documentController
        enabled: root.documentController !== null

        function onMediaHoverFrameReady(assetId, sourceSeconds, frameSource) {
            if (String(assetId) === root.assetId
                    && Math.abs(Number(sourceSeconds) - root.pendingHoverSeconds) < 0.002) {
                root.hoverFrameSource = String(frameSource || "")
            }
        }
    }

    Timer {
        id: hoverScrubTimer
        interval: 80
        repeat: false
        onTriggered: {
            if (!root.hoverScrubActive || !root.documentController
                    || root.pendingHoverSeconds < 0)
                return
            const cached = root.documentController.requestMediaHoverFrame(
                               root.assetId, root.filePath, root.pendingHoverSeconds)
            if (cached && cached.length > 0)
                root.hoverFrameSource = cached
        }
    }

    Rectangle {
        visible: root.selected
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: theme.scaled(3)
        radius: width / 2
        color: theme.accentCool400
    }

    Rectangle {
        visible: !root.selected && !rowHover.hovered && !root.activeFocus
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.rowPadding
        anchors.rightMargin: root.rowPadding
        height: theme.hairlineWidth
        color: theme.alpha(theme.ink700, 0.56)
    }

    Item {
        id: mediaDragProxy
        width: 1
        height: 1
        Drag.active: previewDragArea.drag.active
        Drag.source: root
        Drag.hotSpot.x: root.previewColumnWidth / 2
        Drag.hotSpot.y: 32
        Drag.keys: ["timeline-media"]
    }

    RowLayout {
        id: mediaRow
        anchors.fill: parent
        anchors.margins: root.rowPadding
        spacing: theme.scaled(theme.densityComfortable ? 8 : theme.densityCompact ? 5 : 6)
        visible: !root.gridPresentation

        Rectangle {
            objectName: "timelineMediaAssetThumbnail"
            Layout.preferredWidth: root.previewColumnWidth
            Layout.minimumWidth: root.previewColumnWidth
            Layout.preferredHeight: root.thumbnailHeight
            Layout.minimumHeight: root.thumbnailHeight
            Layout.maximumHeight: root.thumbnailHeight
            radius: theme.radius4
            color: theme.mediaCanvas
            clip: true

            HoverHandler {
                cursorShape: previewDragArea.dragInProgress ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            }

            MouseArea {
                id: previewDragArea
                property bool dragWasStarted: false
                property bool dragInProgress: false
                property real dragPressX: 0
                property real dragPressY: 0
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: dragInProgress ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                drag.target: mediaDragProxy
                drag.threshold: 3
                onPressed: function(mouse) {
                    dragWasStarted = false
                    dragInProgress = false
                    dragPressX = mouse.x
                    dragPressY = mouse.y
                    mediaDragProxy.x = 0
                    mediaDragProxy.y = 0
                }
                onClicked: function(mouse) {
                    if (!dragWasStarted)
                        root.selectionRequested(root.assetId, mouse.modifiers)
                }
                onDoubleClicked: root.previewInSource()
                onPositionChanged: function(mouse) {
                    const leftPressed = (mouse.buttons & Qt.LeftButton) !== 0
                    if (leftPressed && !dragWasStarted) {
                        const deltaX = mouse.x - dragPressX
                        const deltaY = mouse.y - dragPressY
                        dragWasStarted = Math.sqrt(deltaX * deltaX + deltaY * deltaY)
                                >= Math.max(3, drag.threshold)
                        dragInProgress = dragWasStarted
                    }
                    if (dragInProgress) {
                        const scenePoint = mapToItem(null, mouse.x, mouse.y)
                        root.mediaDragMoved(root.dragPayload(), scenePoint.x, scenePoint.y)
                        return
                    }
                    if (!leftPressed && root.mediaType === "video"
                            && root.durationSeconds > 0) {
                        const sampleCount = Math.max(2, root.scrubSampleCount)
                        const fraction = Math.max(0, Math.min(1, mouse.x / Math.max(1, width)))
                        const sampleIndex = Math.round(fraction * (sampleCount - 1))
                        const scrubEnd = Math.max(0, root.durationSeconds - 0.04)
                        root.pendingHoverSeconds = scrubEnd
                                * sampleIndex / (sampleCount - 1)
                        hoverScrubTimer.restart()
                        return
                    }
                }
                onExited: {
                    hoverScrubTimer.stop()
                    root.pendingHoverSeconds = -1
                    root.hoverFrameSource = ""
                }
                onReleased: function(mouse) {
                    if (!dragInProgress) {
                        mediaDragProxy.x = 0
                        mediaDragProxy.y = 0
                        return
                    }
                    const scenePoint = mapToItem(null, mouse.x, mouse.y)
                    root.mediaDragDropped(root.dragPayload(), scenePoint.x, scenePoint.y)
                    mediaDragProxy.Drag.drop()
                    dragInProgress = false
                    mediaDragProxy.x = 0
                    mediaDragProxy.y = 0
                }
                onCanceled: {
                    dragInProgress = false
                    dragWasStarted = false
                    root.mediaDragCanceled()
                }
            }

            Image {
                anchors.fill: parent
                source: root.hoverScrubActive && root.hoverFrameSource.length > 0
                        ? root.hoverFrameSource : root.thumbnailSource
                fillMode: Image.PreserveAspectCrop
                cache: true
                asynchronous: true
                visible: source.toString().length > 0
            }

            Rectangle {
                visible: root.hoverScrubActive && root.pendingHoverSeconds >= 0
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: theme.space4
                width: hoverScrubTime.implicitWidth + theme.space8
                height: hoverScrubTime.implicitHeight + theme.space4
                radius: theme.radius4
                color: theme.alpha(theme.ink950, 0.84)

                Label {
                    id: hoverScrubTime
                    anchors.centerIn: parent
                    text: {
                        const total = Math.max(0, Math.floor(root.pendingHoverSeconds))
                        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0")
                    }
                    color: theme.textStrong
                    font.family: theme.fontNumeric
                    font.features: theme.numericFontFeatures
                    font.pixelSize: theme.typeCaption
                }
            }

            Rectangle {
                objectName: "timelineMediaAssetDragAffordance"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: theme.space4
                width: theme.space20
                height: theme.space20
                radius: theme.radius4
                color: theme.alpha(theme.ink950, 0.78)
                border.width: 0

                AppIcon {
                    anchors.centerIn: parent
                    name: "density"
                    color: theme.textMain
                    implicitWidth: theme.space12
                    implicitHeight: theme.space12
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space3

            Label {
                id: titleLabel
                objectName: "timelineMediaAssetTitle"
                Layout.fillWidth: true
                text: root.gridTitleText
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.Medium
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            RowLayout {
                Layout.fillWidth: true

                MediaTypeIcon {
                    mediaType: root.mediaType
                    color: theme.textMuted
                    Layout.preferredWidth: theme.scaled(14)
                    Layout.preferredHeight: theme.scaled(14)
                }

                Label {
                    objectName: "timelineMediaAssetSecondaryLabel"
                    Layout.fillWidth: true
                    text: (root.mediaType === "audio" ? "Audio" : root.mediaType === "photo" ? "Photo"
                          : root.mediaType === "timeline" ? "Timeline" : root.mediaType === "carousel" ? "Carousel" : "Video")
                          + (root.durationText.length > 0 ? "  •  " + root.durationText : "")
                          + (root.titleText !== root.gridTitleText ? "  •  " + root.titleText : "")
                    color: theme.textMuted
                    font.family: theme.fontNumeric
                    font.features: theme.numericFontFeatures
                    font.pixelSize: theme.typeCaption
                    elide: Text.ElideRight
                }

                StatusChip {
                    objectName: "timelineMediaAssetStatus"
                    text: root.online ? "Ready" : "Offline"
                    tone: root.online ? "neutral" : "error"
                    compact: true
                    minimumReadableWidth: 58
                    maximumReadableWidth: 72
                }
            }

            Label {
                objectName: "timelineMediaSearchMatchScope"
                Layout.fillWidth: true
                visible: root.mediaSearchText.length > 0 && root.matchesSearch
                         && String(root.mediaSearchResult.matchSummary || "").length > 0
                text: "Found in " + String(root.mediaSearchResult.matchSummary || "")
                color: theme.accentCool300
                font.family: theme.fontNumeric
                font.features: theme.numericFontFeatures
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            RowLayout {
                Layout.fillWidth: true
                visible: Number(root.mediaMetadata.rating || 0) > 0
                         || root.mediaMetadata.favorite === true
                         || root.mediaMetadata.rejected === true
                         || String(root.mediaMetadata.colorLabel || "none") !== "none"
                         || (root.mediaMetadata.tags || []).length > 0
                spacing: theme.space4

                RowLayout {
                    visible: Number(root.mediaMetadata.rating || 0) > 0
                    spacing: theme.space2
                    AppIcon {
                        name: "rating"
                        color: theme.accentWarm300
                        Layout.preferredWidth: theme.scaled(12)
                        Layout.preferredHeight: theme.scaled(12)
                    }
                    Label {
                        text: String(root.mediaMetadata.rating || 0)
                        color: theme.accentWarm300
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.DemiBold
                    }
                }

                StatusChip {
                    visible: root.mediaMetadata.favorite === true || root.mediaMetadata.rejected === true
                    text: root.mediaMetadata.favorite === true ? "Favorite" : "Rejected"
                    tone: root.mediaMetadata.favorite === true ? "success" : "error"
                    compact: true
                }

                Rectangle {
                    visible: String(root.mediaMetadata.colorLabel || "none") !== "none"
                    Layout.preferredWidth: theme.space8
                    Layout.preferredHeight: theme.space8
                    radius: width / 2
                    color: {
                        const label = String(root.mediaMetadata.colorLabel || "none")
                        if (label === "red") return theme.error
                        if (label === "orange") return theme.accentWarm400
                        if (label === "yellow") return theme.warning
                        if (label === "green") return theme.success
                        if (label === "blue") return theme.accentCool400
                        if (label === "purple") return "#ae7de0"
                        return "transparent"
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: (root.mediaMetadata.tags || []).length > 0
                    text: (root.mediaMetadata.tags || []).join(" · ")
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }
            }
        }

        MenuButton {
            objectName: "timelineMediaAssetActionsMenu"
            Layout.preferredWidth: root.actionMenuWidth
            Layout.minimumWidth: root.actionMenuWidth
            Layout.preferredHeight: theme.controlHeightDense
            Layout.alignment: Qt.AlignVCenter
            text: "More"
            iconName: "more"
            showText: false
            compact: true
            primaryOpensMenu: true
            secondaryActionMenu: true
            accessibleName: "Actions for " + root.gridTitleText
            menuTooltipText: "Media actions for " + root.gridTitleText
            tooltipText: "Preview, add, or overwrite this media."
            enabled: root.online
            disabledReason: "Relink this media before using media actions."
            menuItems: [
                {"key": "preview", "label": "Preview in Source", "iconName": "play"},
                {"key": "insert", "label": "Add to Timeline at Playhead", "iconName": "plus"},
                {"key": "overwrite", "label": "Overwrite at Playhead", "iconName": "edit", "danger": true}
            ]
            menuWidth: 238
            onMenuItemTriggered: function(key) {
                if (key === "preview")
                    root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
                else if (key === "insert")
                    root.insertRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
                else if (key === "overwrite")
                    root.overwriteRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
            }
        }
    }

    Rectangle {
        id: mediaCardSurface
        objectName: "timelineMediaCardSurface"
        anchors.fill: parent
        visible: root.gridPresentation
        radius: theme.radius6
        color: root.selected ? theme.alpha(theme.interactionFill, 0.58)
               : rowHover.hovered ? theme.alpha(theme.ink750, 0.36)
               : theme.alpha(theme.ink850, 0.42)
        border.width: root.activeFocus ? theme.focusBorderWidth
                      : root.selected ? theme.hairlineWidth : 0
        border.color: root.activeFocus ? theme.focusRing
                      : root.selected ? theme.alpha(theme.accentCool400, 0.52)
                      : "transparent"
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: theme.space6
            spacing: theme.space5

            Rectangle {
                id: mediaCardPreview
                objectName: "timelineMediaCardPreview"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(theme.scaled(72),
                                                 (root.width - theme.space16) * 0.74)
                Layout.minimumHeight: theme.scaled(72)
                radius: theme.radius4
                color: theme.mediaCanvas
                border.width: theme.hairlineWidth
                border.color: theme.alpha(theme.lineSoft, 0.28)
                clip: true

                Image {
                    id: mediaCardImage
                    objectName: "timelineMediaCardImage"
                    anchors.fill: parent
                    source: root.hoverScrubActive && root.hoverFrameSource.length > 0
                            ? root.hoverFrameSource : root.thumbnailSource
                    fillMode: Image.PreserveAspectCrop
                    cache: true
                    asynchronous: true
                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                    visible: source.toString().length > 0 && status === Image.Ready
                }

                AppIcon {
                    anchors.centerIn: parent
                    visible: mediaCardImage.status !== Image.Ready
                    name: root.mediaType === "audio" ? "audio"
                          : root.mediaType === "video" ? "video" : "image"
                    width: theme.scaled(24)
                    height: width
                    color: theme.textMuted
                }

                Rectangle {
                    visible: root.selected
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: theme.space5
                    width: theme.scaled(20)
                    height: width
                    radius: theme.radius4
                    color: theme.textStrong
                    border.width: theme.hairlineWidth
                    border.color: theme.textStrong

                    AppIcon {
                        anchors.centerIn: parent
                        name: "check"
                        implicitWidth: theme.scaled(12)
                        implicitHeight: theme.scaled(12)
                        color: theme.ink950
                    }
                }

                Rectangle {
                    objectName: "timelineMediaCardTypeBadge"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: theme.space5
                    visible: !root.selected
                    width: theme.scaled(24)
                    height: width
                    radius: width / 2
                    color: theme.alpha(theme.ink950, 0.8)
                    border.width: theme.hairlineWidth
                    border.color: theme.alpha(theme.textStrong, 0.2)

                    MediaTypeIcon {
                        anchors.centerIn: parent
                        mediaType: root.mediaType
                        width: theme.scaled(13)
                        height: width
                        color: theme.textStrong
                    }
                }

                Rectangle {
                    visible: root.hoverScrubActive && root.pendingHoverSeconds >= 0
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: theme.space5
                    width: mediaCardScrubTime.implicitWidth + theme.space8
                    height: mediaCardScrubTime.implicitHeight + theme.space4
                    radius: theme.radius4
                    color: theme.alpha(theme.ink950, 0.88)

                    Label {
                        id: mediaCardScrubTime
                        anchors.centerIn: parent
                        text: {
                            const total = Math.max(0, Math.floor(root.pendingHoverSeconds))
                            return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0")
                        }
                        color: theme.textStrong
                        font.family: theme.fontNumeric
                        font.features: theme.numericFontFeatures
                        font.pixelSize: theme.typeCaption
                    }
                }

                StatusChip {
                    objectName: "timelineMediaCardDurationBadge"
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: theme.space5
                    visible: (root.mediaType === "video" || root.mediaType === "audio")
                             && root.durationText.length > 0
                    text: root.durationText
                    tone: "neutral"
                    compact: true
                }

                RowLayout {
                    objectName: "timelineMediaCardMetadata"
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: theme.space5
                    spacing: theme.space4
                    visible: !root.online
                             || Number(root.mediaMetadata.rating || 0) > 0
                             || root.mediaMetadata.favorite === true
                             || root.mediaMetadata.rejected === true
                             || String(root.mediaMetadata.colorLabel || "none") !== "none"
                    Accessible.name: (root.online ? "" : "Offline")
                                     + (Number(root.mediaMetadata.rating || 0) > 0
                                        ? ", rated " + String(root.mediaMetadata.rating) : "")
                                     + (root.mediaMetadata.favorite === true ? ", favorite" : "")
                                     + (root.mediaMetadata.rejected === true ? ", rejected" : "")
                                     + (String(root.mediaMetadata.colorLabel || "none") !== "none"
                                        ? ", " + String(root.mediaMetadata.colorLabel) + " label" : "")

                    Label {
                        visible: Number(root.mediaMetadata.rating || 0) > 0
                        text: "★ " + String(root.mediaMetadata.rating || 0)
                        color: theme.accentWarm300
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.DemiBold
                    }

                    StatusChip {
                        visible: root.mediaMetadata.favorite === true || root.mediaMetadata.rejected === true
                        text: root.mediaMetadata.favorite === true ? "Favorite" : "Rejected"
                        tone: root.mediaMetadata.favorite === true ? "success" : "error"
                        compact: true
                    }

                    Rectangle {
                        visible: String(root.mediaMetadata.colorLabel || "none") !== "none"
                        Layout.preferredWidth: theme.scaled(8)
                        Layout.preferredHeight: theme.scaled(8)
                        radius: width / 2
                        color: {
                            const label = String(root.mediaMetadata.colorLabel || "none")
                            if (label === "red") return theme.error
                            if (label === "orange") return theme.accentWarm400
                            if (label === "yellow") return theme.warning
                            if (label === "green") return theme.success
                            if (label === "blue") return theme.accentCool400
                            if (label === "purple") return "#ae7de0"
                            return "transparent"
                        }
                    }

                    StatusChip {
                        visible: !root.online
                        text: "Offline"
                        tone: "error"
                        compact: true
                    }
                }

                MenuButton {
                    objectName: "timelineMediaAssetCardActionsMenu"
                    z: 2
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: theme.space4
                    implicitWidth: theme.controlHeightDense
                    implicitHeight: theme.controlHeightDense
                    text: "More"
                    iconName: "more"
                    showText: false
                    compact: true
                    visible: root.selected || rowHover.hovered || root.activeFocus
                    primaryOpensMenu: true
                    secondaryActionMenu: true
                    accessibleName: "Actions for " + root.gridTitleText
                    menuTooltipText: "Media actions for " + root.gridTitleText
                    tooltipText: "Preview, add, or overwrite this media."
                    enabled: root.online
                    disabledReason: "Relink this media before using media actions."
                    menuItems: [
                        {"key": "preview", "label": "Preview in Source", "iconName": "play"},
                        {"key": "insert", "label": "Add to Timeline at Playhead", "iconName": "plus"},
                        {"key": "overwrite", "label": "Overwrite at Playhead", "iconName": "edit", "danger": true}
                    ]
                    menuWidth: 238
                    onMenuItemTriggered: function(key) {
                        if (key === "preview")
                            root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
                        else if (key === "insert")
                            root.insertRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
                        else if (key === "overwrite")
                            root.overwriteRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
                    }
                }

                MouseArea {
                    id: mediaCardDragArea
                    objectName: "timelineMediaCardDragArea"
                    property bool dragWasStarted: false
                    property bool dragInProgress: false
                    property real dragPressX: 0
                    property real dragPressY: 0
                    z: 1
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: dragInProgress ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    drag.target: mediaDragProxy
                    drag.threshold: 3
                    onPressed: function(mouse) {
                        dragWasStarted = false
                        dragInProgress = false
                        dragPressX = mouse.x
                        dragPressY = mouse.y
                        mediaDragProxy.x = 0
                        mediaDragProxy.y = 0
                    }
                    onClicked: function(mouse) {
                        if (!dragWasStarted)
                            root.selectionRequested(root.assetId, mouse.modifiers)
                    }
                    onPositionChanged: function(mouse) {
                        const leftPressed = (mouse.buttons & Qt.LeftButton) !== 0
                        if (leftPressed && !dragWasStarted) {
                            const deltaX = mouse.x - dragPressX
                            const deltaY = mouse.y - dragPressY
                            dragWasStarted = Math.sqrt(deltaX * deltaX + deltaY * deltaY)
                                    >= Math.max(3, drag.threshold)
                            dragInProgress = dragWasStarted
                        }
                        if (dragInProgress) {
                            const scenePoint = mapToItem(null, mouse.x, mouse.y)
                            root.mediaDragMoved(root.dragPayload(), scenePoint.x, scenePoint.y)
                            return
                        }
                        if (!leftPressed && root.mediaType === "video"
                                && root.durationSeconds > 0) {
                            const sampleCount = Math.max(2, root.scrubSampleCount)
                            const fraction = Math.max(0, Math.min(1,
                                (mouse.x / Math.max(1, width))))
                            const sampleIndex = Math.round(fraction * (sampleCount - 1))
                            root.pendingHoverSeconds = Math.max(0, root.durationSeconds - 0.04)
                                    * sampleIndex / (sampleCount - 1)
                            hoverScrubTimer.restart()
                            return
                        }
                    }
                    onExited: {
                        hoverScrubTimer.stop()
                        root.pendingHoverSeconds = -1
                        root.hoverFrameSource = ""
                    }
                    onReleased: function(mouse) {
                        if (!dragInProgress) {
                            mediaDragProxy.x = 0
                            mediaDragProxy.y = 0
                            return
                        }
                        const scenePoint = mapToItem(null, mouse.x, mouse.y)
                        root.mediaDragDropped(root.dragPayload(), scenePoint.x, scenePoint.y)
                        mediaDragProxy.Drag.drop()
                        dragInProgress = false
                        mediaDragProxy.x = 0
                        mediaDragProxy.y = 0
                    }
                    onCanceled: {
                        dragInProgress = false
                        dragWasStarted = false
                        root.mediaDragCanceled()
                    }
                    onDoubleClicked: root.previewInSource()
                }

            }

            Label {
                id: mediaCardTitle
                objectName: "timelineMediaCardFilename"
                Layout.fillWidth: true
                text: root.gridTitleText
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                font.weight: root.selected ? Font.DemiBold : Font.Medium
                Layout.preferredHeight: theme.scaled(34)
                Layout.minimumHeight: theme.scaled(34)
                Layout.maximumHeight: theme.scaled(34)
                verticalAlignment: Text.AlignVCenter
                maximumLineCount: 2
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideRight

                AppToolTip {
                    targetItem: mediaCardTitle
                    visible: mediaCardTitle.visible
                             && (rowHover.hovered && mediaCardTitle.truncated)
                    delay: 350
                    timeout: 3600
                    text: root.gridTitleText
                          + (root.titleText !== root.gridTitleText
                             ? " · " + root.titleText : "")
                          + " · " + root.filePath
                }
            }

        }
    }
}
