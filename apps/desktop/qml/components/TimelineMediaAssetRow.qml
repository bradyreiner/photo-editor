import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "timelineMediaAssetRow"

    required property string assetId
    required property string cameraLabel
    required property string filePath
    required property string thumbnailSource
    required property string mediaType
    required property string durationText
    required property real durationSeconds
    required property bool online
    required property string captureDate
    required property string lensModel
    required property string locationPath
    required property string projectSummary
    required property string importedAt
    required property var projectNames
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
    property int trailingScrollbarWidth: 0
    property var mediaMetadata: ({})
    readonly property string libraryType: "timeline-media"
    readonly property string titleText: cameraLabel.length > 0 ? cameraLabel : assetId
    readonly property int rowPadding: theme.scaled(theme.densityComfortable ? 8 : theme.densityCompact ? 4 : 6)
    readonly property int rowBaselineHeight: theme.scaled(theme.densityComfortable ? 88 : theme.densityCompact ? 68 : 78)
    readonly property int thumbnailHeight: theme.scaled(theme.densityComfortable ? 58 : theme.densityCompact ? 44 : 52)
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
    readonly property int previewColumnWidth: 84
    readonly property int actionMenuWidth: theme.scaled(34)
    readonly property bool hoverScrubActive: mediaType === "video" && durationSeconds > 0
                                               && previewDragArea.containsMouse
    readonly property string timelineMediaAssetRowContract: "timeline-media-asset-row:v5|preview:true|row-actions:more-menu|selection:click|double-click:preview-source|rating-favorite-reject-color-tags:contextual|search:controller-indexed-with-match-scope|hover-scrub:debounced-bounded-low-resolution-cache|filmstrip-density:5-9-15|keyboard:0-5-f-x|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|metadata-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v2|preview-zone:fixed-84|content:title-metadata-wrap|action-zone:more-menu-34|actions:source-insert-overwrite|selection:left-edge|hover:neutral|focus:ring"

    signal sourceRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal insertRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal overwriteRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal selectionRequested(string assetId, int modifiers)
    signal metadataActionRequested(string assetId, string action, string value)
    signal tagEditorRequested(string assetId)
    signal mediaDragMoved(var payload, real sceneX, real sceneY)
    signal mediaDragDropped(var payload, real sceneX, real sceneY)
    signal mediaDragCanceled()

    function dragPayload() {
        return {
            "assetId": root.assetId,
            "filePath": root.filePath,
            "titleText": root.titleText,
            "thumbnailSource": root.thumbnailSource,
            "mediaType": root.mediaType
        }
    }

    Theme { id: theme }

    visible: (mediaType === "video" || mediaType === "photo" || mediaType === "audio")
             && matchesSearch && binVisible && smartCollectionVisible
    width: ListView.view ? Math.max(0, ListView.view.width - trailingScrollbarWidth) : 360
    implicitHeight: Math.max(rowBaselineHeight, mediaRow.implicitHeight + rowPadding * 2)
    height: visible ? implicitHeight : 0
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
    Accessible.name: titleText
    Accessible.description: (mediaType === "audio" ? "Audio" : mediaType === "photo" ? "Photo" : "Video")
                            + (durationText.length > 0 ? ", " + durationText : "")
                            + ". Click to select; double-click or press Return to preview in Source. "
                            + "Use More for timeline actions."
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
    Keys.onEnterPressed: root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
    Keys.onSpacePressed: root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
    Keys.onPressed: function(event) {
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
    ToolTip.visible: rowHover.hovered && titleLabel.contentWidth > titleLabel.width + 1
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: titleText

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onTapped: root.selectionRequested(root.assetId, Qt.NoModifier)
        onDoubleTapped: root.sourceRequested(root.assetId, root.filePath,
                                             root.titleText, root.thumbnailSource)
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
                cursorShape: previewDragArea.drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            }

            MouseArea {
                id: previewDragArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                drag.target: mediaDragProxy
                drag.threshold: 3
                onPressed: {
                    mediaDragProxy.x = 0
                    mediaDragProxy.y = 0
                }
                onPositionChanged: function(mouse) {
                    if (!drag.active && root.mediaType === "video"
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
                    if (!drag.active)
                        return
                    const scenePoint = mapToItem(null, mouse.x, mouse.y)
                    root.mediaDragMoved(root.dragPayload(), scenePoint.x, scenePoint.y)
                }
                onExited: {
                    hoverScrubTimer.stop()
                    root.pendingHoverSeconds = -1
                    root.hoverFrameSource = ""
                }
                onReleased: function(mouse) {
                    const scenePoint = mapToItem(null, mouse.x, mouse.y)
                    root.mediaDragDropped(root.dragPayload(), scenePoint.x, scenePoint.y)
                    mediaDragProxy.Drag.drop()
                    mediaDragProxy.x = 0
                    mediaDragProxy.y = 0
                }
                onCanceled: root.mediaDragCanceled()
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
                    font.family: theme.fontMono
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
                Layout.fillWidth: true
                text: root.titleText
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
                    Layout.fillWidth: true
                    text: (root.mediaType === "audio" ? "Audio" : root.mediaType === "photo" ? "Photo"
                          : root.mediaType === "timeline" ? "Timeline" : root.mediaType === "carousel" ? "Carousel" : "Video")
                          + (root.durationText.length > 0 ? "  •  " + root.durationText : "")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeCaption
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
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
                font.family: theme.fontMono
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
            iconName: "menu"
            showText: false
            compact: true
            primaryOpensMenu: true
            secondaryActionMenu: true
            accessibleName: "Actions for " + root.titleText
            menuTooltipText: "Media actions for " + root.titleText
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
}
