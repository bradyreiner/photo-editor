pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "../components"

Rectangle {
    id: root
    color: "transparent"

    Theme { id: theme }

    property var currentClip: timelineDocument.currentClip
    property var sourceClip: timelineDocument.sourceClip
    property real playheadSeconds: timelineDocument.currentClipPlayheadSeconds
    property real sequencePlayheadSeconds: timelineDocument.sequencePlayheadSeconds
    property bool sequencePlaying: timelineDocument.sequencePlaying
    property real timelineZoom: 84
    property real minimumClipWidth: 48
    property real snapThresholdPixels: 10
    property bool snappingEnabled: true
    property bool rippleModeEnabled: true
    property int mediaPickerSelectedIndex: -1
    property string mediaPickerAssetId: ""
    property string mediaPickerFilePath: ""
    property string mediaPickerDisplayLabel: ""
    property string mediaPickerThumbnailSource: ""
    property string mediaPickerMediaType: ""
    property string mediaPickerTimelineDocumentPath: ""
    property string mediaPickerDurationText: ""
    property bool mediaPickerOnline: false
    property var mediaPickerAssets: []
    property string mediaPickerProjectName: ""
    property string mediaPickerScope: "all"
    property string setupTimelineName: ""
    property string setupProjectBinding: ""
    property string setupResolutionPreset: "1080 x 1350"
    property string setupWidth: "1080"
    property string setupHeight: "1350"
    property string setupFrameRate: "30"
    property string setupAudioSampleRate: "48000"
    property string setupColorSpace: "Rec.709"
    property string setupDataLevels: "Legal"
    property string setupExportContainer: "mp4"
    property string setupExportCodec: "h264"
    property string inspectorTab: "effects"
    property bool sourcePanelCollapsed: false
    property bool effectsPanelCollapsed: false
    property string sourceRailTab: "source"
    property real sourcePanelWidth: 320
    property real effectsPanelWidth: 344
    property bool marqueeActive: false
    property real marqueeStartX: 0
    property real marqueeStartY: 0
    property real marqueeCurrentX: 0
    property real marqueeCurrentY: 0
    property bool dragPreviewVisible: false
    property real dragPreviewX: 0
    property real dragPreviewY: 0
    property real dragPreviewWidth: 0
    property color dragPreviewColor: theme.alpha(theme.accentWarm300, 0.22)
    property bool snapGuideVisible: false
    property real snapGuideSeconds: 0
    property string snapGuideLabel: ""
    readonly property var sequenceTrackModel: timelineDocument.sequenceTracks ? timelineDocument.sequenceTracks : []
    readonly property var sequenceMarkerModel: timelineDocument.sequenceMarkers ? timelineDocument.sequenceMarkers : []
    readonly property var effectLibraryModel: [
        {"name": "Transform", "category": "Motion", "description": "Position, scale, and framing controls."},
        {"name": "Opacity", "category": "Composite", "description": "Animate transparency and blends."},
        {"name": "Crop", "category": "Motion", "description": "Trim frame edges."},
        {"name": "Color Balance", "category": "Color", "description": "Primary warmth and tint balance."},
        {"name": "Contrast", "category": "Color", "description": "Punch up midtones."},
        {"name": "Sharpen", "category": "Detail", "description": "Restore edge definition."},
        {"name": "Gaussian Blur", "category": "Stylize", "description": "Soft blur for focus pulls."},
        {"name": "Vignette", "category": "Stylize", "description": "Darken frame edges."},
        {"name": "Volume", "category": "Audio", "description": "Clip gain and loudness."},
        {"name": "Pan", "category": "Audio", "description": "Stereo position."}
    ]
    readonly property var transitionLibraryModel: [
        {"name": "Cut", "description": "Direct clip boundary."},
        {"name": "Cross Dissolve", "description": "Blend outgoing and incoming clips."},
        {"name": "Dip To Color", "description": "Fade through a solid color."},
        {"name": "Fade In", "description": "Bring in from black or silence."},
        {"name": "Fade Out", "description": "Exit to black or silence."},
        {"name": "Wipe", "description": "Directional reveal between clips."}
    ]
    readonly property real laneHeaderWidth: 128
    readonly property real laneHeight: 52
    readonly property real rulerHeight: 24
    readonly property real trackSpacing: 4
    readonly property real timelineContentWidth: Math.max(
                                                    timelineViewport.width,
                                                    Math.max(1, sequenceDurationSeconds()) * timelineZoom + 140)
    readonly property real timelineContentHeight: rulerHeight
                                                  + Math.max(1, sequenceTrackModel.length) * (laneHeight + trackSpacing)
    readonly property var resolutionPresetLabels: [
        "1080 x 1350",
        "1920 x 1080",
        "3840 x 2160",
        "1080 x 1920",
        "Custom"
    ]

    Settings {
        id: timelineWorkspaceSettings
        category: "TimelineWorkspace"
        property bool sourcePanelCollapsed: false
        property bool effectsPanelCollapsed: false
        property string sourceRailTab: "source"
        property string inspectorTab: "effects"
        property real sourcePanelWidth: 320
        property real effectsPanelWidth: 344
        property bool snappingEnabled: appShell.defaultTimelineSnapping
        property bool rippleModeEnabled: appShell.defaultTimelineRipple
    }

    Component.onCompleted: {
        sourcePanelCollapsed = timelineWorkspaceSettings.sourcePanelCollapsed
        effectsPanelCollapsed = timelineWorkspaceSettings.effectsPanelCollapsed
        sourceRailTab = timelineWorkspaceSettings.sourceRailTab
        inspectorTab = timelineWorkspaceSettings.inspectorTab
        sourcePanelWidth = Math.max(260, Number(timelineWorkspaceSettings.sourcePanelWidth || 320))
        effectsPanelWidth = Math.max(288, Number(timelineWorkspaceSettings.effectsPanelWidth || 344))
        snappingEnabled = timelineWorkspaceSettings.snappingEnabled
        rippleModeEnabled = timelineWorkspaceSettings.rippleModeEnabled
        beginTimelineSetup()
        timelinePlayback.attachVideoOutput(videoMonitor)
    }
    Component.onDestruction: timelinePlayback.attachVideoOutput(null)

    onSourcePanelCollapsedChanged: timelineWorkspaceSettings.sourcePanelCollapsed = sourcePanelCollapsed
    onEffectsPanelCollapsedChanged: timelineWorkspaceSettings.effectsPanelCollapsed = effectsPanelCollapsed
    onSourceRailTabChanged: timelineWorkspaceSettings.sourceRailTab = sourceRailTab
    onInspectorTabChanged: timelineWorkspaceSettings.inspectorTab = inspectorTab
    onSourcePanelWidthChanged: timelineWorkspaceSettings.sourcePanelWidth = sourcePanelWidth
    onEffectsPanelWidthChanged: timelineWorkspaceSettings.effectsPanelWidth = effectsPanelWidth
    onSnappingEnabledChanged: timelineWorkspaceSettings.snappingEnabled = snappingEnabled
    onRippleModeEnabledChanged: timelineWorkspaceSettings.rippleModeEnabled = rippleModeEnabled

    Connections {
        target: appShell

        function onTimelineDefaultsChanged() {
            root.snappingEnabled = appShell.defaultTimelineSnapping
            root.rippleModeEnabled = appShell.defaultTimelineRipple
        }

        function onTimelineWorkspaceSettingsReset() {
            root.restoreDefaultLayout()
        }
    }

    Connections {
        target: timelineDocument

        function onTimelineStateChanged() {
            if (!timelineDocument.hasActiveTimeline)
                root.beginTimelineSetup()
        }
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

    function clipSourceUrl() {
        return timelineDocument.playbackSourceUrl ? timelineDocument.playbackSourceUrl : ""
    }

    function canLoadPickedMedia() {
        return mediaPickerMediaType !== "timeline" && mediaPickerFilePath.length > 0 && mediaPickerOnline
    }

    function preferredMediaPickerProject() {
        if (timelineDocument.projectBinding.length > 0)
            return timelineDocument.projectBinding
        if (catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned")
            return catalogAssets.projectFilter
        return ""
    }

    function hasProjectScopedMediaPicker() {
        return preferredMediaPickerProject().length > 0
    }

    function refreshMediaPickerAssets() {
        mediaPickerProjectName = mediaPickerScope === "project" ? preferredMediaPickerProject() : ""
        mediaPickerAssets = catalogAssets.timelinePickerAssets(mediaPickerProjectName)
        if (mediaPickerAssets.length > 0) {
            const first = mediaPickerAssets[0]
            selectMediaPickerAsset(
                        0,
                        first.assetId || "",
                        first.filePath || "",
                        first.cameraLabel || "",
                        first.thumbnailSource || "",
                        first.mediaType || "",
                        first.timelineDocumentPath || "",
                        first.durationText || "",
                        Boolean(first.online))
        } else {
            clearMediaPickerSelection()
        }
    }

    function mediaPickerSubtitle() {
        if (mediaPickerScope === "project" && mediaPickerProjectName.length > 0)
            return "Showing timeline-ready media in " + mediaPickerProjectName
        return "Showing all timeline-ready media from the catalog"
    }

    function mediaPickerEmptyStateText() {
        if (catalogAssets.loading)
            return "Library is still loading."
        if (mediaPickerScope === "project" && mediaPickerProjectName.length > 0)
            return "No photo, video, or audio media is assigned to " + mediaPickerProjectName + "."
        return "No timeline-ready media is available yet. Import media in Library first."
    }

    function applyResolutionPreset() {
        switch (setupResolutionPreset) {
        case "1920 x 1080":
            setupWidth = "1920"
            setupHeight = "1080"
            break
        case "3840 x 2160":
            setupWidth = "3840"
            setupHeight = "2160"
            break
        case "1080 x 1920":
            setupWidth = "1080"
            setupHeight = "1920"
            break
        case "1080 x 1350":
            setupWidth = "1080"
            setupHeight = "1350"
            break
        default:
            break
        }
    }

    function beginTimelineSetup() {
        setupTimelineName = catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                ? catalogAssets.projectFilter + " Sequence"
                : "Untitled Timeline"
        setupProjectBinding = catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                ? catalogAssets.projectFilter
                : ""
        setupResolutionPreset = "1080 x 1350"
        setupWidth = "1080"
        setupHeight = "1350"
        setupFrameRate = "30"
        setupAudioSampleRate = "48000"
        setupColorSpace = "Rec.709"
        setupDataLevels = "Legal"
        setupExportContainer = "mp4"
        setupExportCodec = "h264"
    }

    function createTimelineFromSetup() {
        timelineDocument.createTimelineWithSettings({
            "timelineName": setupTimelineName,
            "projectBinding": setupProjectBinding,
            "width": Math.max(16, Number(setupWidth || 1080)),
            "height": Math.max(16, Number(setupHeight || 1350)),
            "frameRate": Math.max(1, Number(setupFrameRate || 30)),
            "audioSampleRate": Math.max(8000, Number(setupAudioSampleRate || 48000)),
            "colorSpace": setupColorSpace,
            "dataLevels": setupDataLevels,
            "exportContainer": setupExportContainer,
            "exportCodec": setupExportCodec
        })
    }

    function trimStartSeconds() {
        return Number(currentClip.trimStartSeconds || 0)
    }

    function trimEndSeconds() {
        return Number(currentClip.trimEndSeconds || 0)
    }

    function hasPlayableClip() {
        return clipSourceUrl().length > 0
    }

    function shouldShowVideoMonitor() {
        return hasPlayableClip() && (sequencePlaying || timelinePlayback.mediaReady)
    }

    function shouldShowTimelinePoster() {
        return timelineDocument.previewSource.length > 0 && (!hasPlayableClip() || !timelinePlayback.mediaReady)
    }

    function sequenceDurationSeconds() {
        return Number(timelineDocument.totalDurationSeconds || 0)
    }

    function formatSeconds(seconds) {
        const safe = Math.max(0, Number(seconds || 0))
        const total = Math.floor(safe)
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const remaining = total % 60
        if (hours > 0)
            return hours + ":" + String(minutes).padStart(2, "0") + ":" + String(remaining).padStart(2, "0")
        return minutes + ":" + String(remaining).padStart(2, "0")
    }

    function timecodeText(seconds) {
        const safe = Math.max(0, Number(seconds || 0))
        const fps = Math.max(1, Math.round(Number(timelineDocument.framesPerSecond || 30)))
        const totalFrames = Math.floor(safe * fps)
        const hours = Math.floor(totalFrames / (3600 * fps))
        const minutes = Math.floor((totalFrames % (3600 * fps)) / (60 * fps))
        const remainingSeconds = Math.floor((totalFrames % (60 * fps)) / fps)
        const frames = totalFrames % fps
        return String(hours).padStart(2, "0")
                + ":" + String(minutes).padStart(2, "0")
                + ":" + String(remainingSeconds).padStart(2, "0")
                + ":" + String(frames).padStart(2, "0")
    }

    function clipWidth(durationSeconds) {
        return Math.max(minimumClipWidth, Number(durationSeconds || 0) * timelineZoom)
    }

    function snapSequenceSeconds(proposedSeconds, excludedClipId) {
        return snapSequenceResult(proposedSeconds, excludedClipId).seconds
    }

    function snapSequenceResult(proposedSeconds, excludedClipId) {
        if (!snappingEnabled)
            return {
                "seconds": Math.max(0, Number(proposedSeconds || 0)),
                "snapped": false
            }

        const thresholdSeconds = snapThresholdPixels / Math.max(1, timelineZoom)
        let snappedSeconds = Math.max(0, Number(proposedSeconds || 0))
        let nearestDistance = thresholdSeconds + 1
        const snapPoints = [sequencePlayheadSeconds]

        for (let trackIndex = 0; trackIndex < sequenceTrackModel.length; ++trackIndex) {
            const track = sequenceTrackModel[trackIndex]
            if (!track || track.trackKind !== "video" || !track.clips)
                continue
            for (let clipIndex = 0; clipIndex < track.clips.length; ++clipIndex) {
                const clip = track.clips[clipIndex]
                if (!clip || clip.clipId === excludedClipId)
                    continue
                snapPoints.push(Number(clip.startSeconds || 0))
                snapPoints.push(Number(clip.endSeconds || 0))
            }
        }

        for (let index = 0; index < snapPoints.length; ++index) {
            const candidate = Math.max(0, Number(snapPoints[index] || 0))
            const distance = Math.abs(candidate - snappedSeconds)
            if (distance <= thresholdSeconds && distance < nearestDistance) {
                nearestDistance = distance
                snappedSeconds = candidate
            }
        }

        return {
            "seconds": snappedSeconds,
            "snapped": nearestDistance <= thresholdSeconds,
            "distance": nearestDistance
        }
    }

    function seekTimelineFromX(xPos) {
        timelinePlayback.pause()
        timelinePlayback.seekSequenceSeconds(Math.max(0, Math.min(sequenceDurationSeconds(), xPos / timelineZoom)))
    }

    function splitAtPlayhead() {
        if (!hasPlayableClip())
            return

        timelinePlayback.pause()

        if (timelineDocument.splitSelectedClip(playheadSeconds))
            timelineDocument.seekCurrentClipSeconds(trimStartSeconds())
    }

    function saveTimelineToLibrary() {
        if (!timelineDocument.saveTimeline())
            return false

        if (timelineDocument.projectBinding.length > 0 && timelineDocument.timelineAssetId.length > 0)
            catalogAssets.addAssetToProject(timelineDocument.timelineAssetId, timelineDocument.projectBinding)
        catalogAssets.refresh()
        return true
    }

    function toggleSourcePanel() {
        sourcePanelCollapsed = !sourcePanelCollapsed
    }

    function toggleEffectsPanel() {
        effectsPanelCollapsed = !effectsPanelCollapsed
    }

    function restoreDefaultLayout() {
        sourcePanelCollapsed = false
        effectsPanelCollapsed = false
        sourceRailTab = "source"
        inspectorTab = "effects"
        sourcePanelWidth = 320
        effectsPanelWidth = 344
        snappingEnabled = appShell.defaultTimelineSnapping
        rippleModeEnabled = appShell.defaultTimelineRipple
    }

    function programVideoFillMode() {
        switch (String(currentClip.frameMode || "fit")) {
        case "fill":
            return VideoOutput.PreserveAspectCrop
        case "stretch":
            return VideoOutput.Stretch
        default:
            return VideoOutput.PreserveAspectFit
        }
    }

    function programImageFillMode() {
        switch (String(currentClip.frameMode || "fit")) {
        case "fill":
            return Image.PreserveAspectCrop
        case "stretch":
            return Image.Stretch
        default:
            return Image.PreserveAspectFit
        }
    }

    function clearMediaPickerSelection() {
        mediaPickerSelectedIndex = -1
        mediaPickerAssetId = ""
        mediaPickerFilePath = ""
        mediaPickerDisplayLabel = ""
        mediaPickerThumbnailSource = ""
        mediaPickerMediaType = ""
        mediaPickerTimelineDocumentPath = ""
        mediaPickerDurationText = ""
        mediaPickerOnline = false
    }

    function openMediaPicker() {
        clearMediaPickerSelection()
        mediaPickerScope = hasProjectScopedMediaPicker() ? "project" : "all"
        refreshMediaPickerAssets()
        mediaPickerDialog.open()
    }

    function setMediaPickerScope(scope) {
        if (mediaPickerScope === scope)
            return
        mediaPickerScope = scope
        refreshMediaPickerAssets()
    }

    function selectMediaPickerAsset(index, assetId, filePath, displayLabel, thumbnailSource, mediaType, timelineDocumentPath, durationText, online) {
        mediaPickerSelectedIndex = index
        mediaPickerAssetId = assetId
        mediaPickerFilePath = filePath
        mediaPickerDisplayLabel = displayLabel
        mediaPickerThumbnailSource = thumbnailSource
        mediaPickerMediaType = mediaType
        mediaPickerTimelineDocumentPath = timelineDocumentPath
        mediaPickerDurationText = durationText
        mediaPickerOnline = online
    }

    function addPickedMedia() {
        if (!canLoadPickedMedia())
            return

        timelineDocument.loadSourceClipFromGallery(
                    mediaPickerAssetId,
                    mediaPickerFilePath,
                    mediaPickerDisplayLabel,
                    0,
                    mediaPickerThumbnailSource)
        mediaPickerDialog.close()
    }

    function insertPickedMedia() {
        addPickedMedia()
        timelineDocument.insertSourceClipAtPlayhead()
    }

    function overwritePickedMedia() {
        addPickedMedia()
        timelineDocument.overwriteSourceClipAtPlayhead()
    }

    function clipSequenceTime(modelData, localX) {
        return Number(modelData.startSeconds || 0) + Math.max(0, localX / timelineZoom)
    }

    function resetTimelinePreviewState() {
        dragPreviewVisible = false
        snapGuideVisible = false
    }

    function updateSnapGuide(seconds, snapped, labelPrefix) {
        if (snapped) {
            snapGuideVisible = true
            snapGuideSeconds = seconds
            snapGuideLabel = labelPrefix + " " + timecodeText(seconds)
        } else {
            snapGuideVisible = false
            snapGuideLabel = ""
        }
    }

    function applyEffectToClip(clipIndex, effectName) {
        if (clipIndex < 0 || !effectName || effectName.length === 0)
            return
        timelineDocument.selectClip(clipIndex)
        timelineDocument.addSelectedClipEffect(effectName)
        if (effectName === "Transform" || effectName === "Opacity" || effectName === "Crop")
            inspectorTab = "motion"
        else if (effectName === "Volume" || effectName === "Pan")
            inspectorTab = "audio"
        else
            inspectorTab = "effects"
    }

    function applyTransitionToClipEdge(clipIndex, edge, transitionName, durationSeconds) {
        if (clipIndex < 0 || !transitionName || transitionName.length === 0)
            return
        timelineDocument.selectClip(clipIndex)
        timelineDocument.assignSelectedClipTransition(edge, transitionName, durationSeconds)
        if (transitionName === "Fade In")
            timelineDocument.setSelectedClipFadeInSeconds(durationSeconds)
        else if (transitionName === "Fade Out")
            timelineDocument.setSelectedClipFadeOutSeconds(durationSeconds)
        inspectorTab = "transitions"
    }

    function selectClipFromMarquee() {
        const left = Math.min(marqueeStartX, marqueeCurrentX)
        const right = Math.max(marqueeStartX, marqueeCurrentX)
        const top = Math.min(marqueeStartY, marqueeCurrentY)
        const bottom = Math.max(marqueeStartY, marqueeCurrentY)
        let bestClipIndex = -1
        let bestTrackRow = -1

        for (let trackIndex = 0; trackIndex < sequenceTrackModel.length; ++trackIndex) {
            const track = sequenceTrackModel[trackIndex]
            if (!track || !track.clips)
                continue
            const laneTop = rulerHeight + trackIndex * (laneHeight + trackSpacing)
            const laneBottom = laneTop + laneHeight
            if (bottom < laneTop || top > laneBottom)
                continue

            for (let clipIndex = 0; clipIndex < track.clips.length; ++clipIndex) {
                const clip = track.clips[clipIndex]
                if (!clip || clip.trackKind !== "video")
                    continue
                const clipLeft = Number(clip.startSeconds || 0) * timelineZoom
                const clipRight = clipLeft + clipWidth(clip.trimmedDurationSeconds)
                if (right >= clipLeft && left <= clipRight && trackIndex >= bestTrackRow) {
                    bestTrackRow = trackIndex
                    bestClipIndex = Number(clip.index || -1)
                }
            }
        }

        if (bestClipIndex >= 0)
            timelineDocument.selectClip(bestClipIndex)
    }

    Dialog {
        id: mediaPickerDialog
        parent: Overlay.overlay
        modal: true
        width: Math.min(root.width - 32, 1080)
        height: Math.min(root.height - 32, 760)
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        padding: 0
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            radius: 22
            color: theme.ink900
            border.width: 1
            border.color: theme.alpha(theme.lineStrong, 0.46)
        }

        contentItem: RowLayout {
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: "Gallery"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 26
                                font.weight: Font.DemiBold
                            }

                            Label {
                                text: root.mediaPickerSubtitle()
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }
                        }

                        ActionButton {
                            text: "Current Project"
                            compact: true
                            active: root.mediaPickerScope === "project"
                            visible: root.hasProjectScopedMediaPicker()
                            onClicked: root.setMediaPickerScope("project")
                        }

                        ActionButton {
                            text: "All Media"
                            compact: true
                            active: root.mediaPickerScope === "all"
                            onClicked: root.setMediaPickerScope("all")
                        }

                        ActionButton {
                            text: "Close"
                            onClicked: mediaPickerDialog.close()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 18
                        color: theme.alpha(theme.ink950, 0.76)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.4)
                        clip: true

                        GridView {
                            id: mediaPickerGrid
                            anchors.fill: parent
                            anchors.margins: 12
                            clip: true
                            cellWidth: 176
                            cellHeight: 212
                            model: root.mediaPickerAssets

                            delegate: Rectangle {
                                required property int index
                                required property var modelData

                                readonly property string assetId: String(modelData.assetId || "")
                                readonly property string cameraLabel: String(modelData.cameraLabel || "")
                                readonly property string filePath: String(modelData.filePath || "")
                                readonly property string thumbnailSource: String(modelData.thumbnailSource || "")
                                readonly property string mediaType: String(modelData.mediaType || "")
                                readonly property string durationText: String(modelData.durationText || "")
                                readonly property string timelineDocumentPath: String(modelData.timelineDocumentPath || "")
                                readonly property bool online: Boolean(modelData.online)

                                radius: 14
                                color: root.mediaPickerAssetId === assetId ? theme.alpha(theme.accentWarm500, 0.16) : theme.alpha(theme.ink900, 0.88)
                                border.width: 1
                                border.color: root.mediaPickerAssetId === assetId
                                              ? theme.accentWarm300
                                              : theme.alpha(theme.lineSoft, 0.34)
                                opacity: online ? 1 : 0.56

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 132
                                        radius: 10
                                        color: "#0b1014"
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            source: thumbnailSource
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: false
                                            visible: source.length > 0
                                        }

                                        Label {
                                            anchors.centerIn: parent
                                            visible: thumbnailSource.length === 0
                                            text: mediaType === "timeline" ? "Timeline" : "Preview"
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: cameraLabel.length > 0 ? cameraLabel : assetId
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: mediaType === "photo"
                                                  ? "Photo"
                                                  : mediaType === "audio"
                                                    ? "Audio"
                                                  : mediaType === "timeline"
                                                    ? "Timeline"
                                                    : "Video"
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                        }

                                        Item { Layout.fillWidth: true }

                                        Label {
                                            text: durationText
                                            visible: durationText.length > 0
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.selectMediaPickerAsset(
                                                   index,
                                                   assetId,
                                                   filePath,
                                                   cameraLabel,
                                                   thumbnailSource,
                                                   mediaType,
                                                   timelineDocumentPath,
                                                   durationText,
                                                   online)
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.mediaPickerAssets.length === 0
                            text: root.mediaPickerEmptyStateText()
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 12
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                color: theme.alpha(theme.ink950, 0.84)
                border.width: 0

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    Label {
                        text: "Selection"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 170
                        radius: 14
                        color: "#0b1014"
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.34)
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: root.mediaPickerThumbnailSource
                            fillMode: Image.PreserveAspectFit
                            asynchronous: false
                            cache: false
                            visible: source.length > 0
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.mediaPickerThumbnailSource.length === 0
                            text: "Choose media"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.mediaPickerDisplayLabel.length > 0 ? root.mediaPickerDisplayLabel : "No media selected"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.mediaPickerMediaType === "video"
                              ? "Video clip ready for source, insert, or overwrite."
                              : root.mediaPickerMediaType === "audio"
                                ? "Standalone audio will route to the targeted audio lane."
                              : root.mediaPickerMediaType === "photo"
                                ? "Stills will load as timed clips on the targeted video lane."
                              : root.mediaPickerMediaType === "timeline"
                                  ? "Saved timelines stay visible in the project gallery."
                                  : "Select media from the gallery and click Add."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.mediaPickerDisplayLabel.length > 0 && !root.mediaPickerOnline
                        text: "This media is offline and cannot be loaded until the source path is available."
                        color: "#f4b7af"
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.mediaPickerDurationText
                        visible: text.length > 0
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Item { Layout.fillHeight: true }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Load To Source"
                        tone: "primary"
                        enabled: root.canLoadPickedMedia()
                        onClicked: root.addPickedMedia()
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Insert"
                        enabled: root.canLoadPickedMedia()
                        onClicked: root.insertPickedMedia()
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Overwrite"
                        enabled: root.canLoadPickedMedia()
                        onClicked: root.overwritePickedMedia()
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !timelineDocument.hasActiveTimeline
        spacing: 18

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: 1260
            spacing: 16

            BrandPanel {
                Layout.preferredWidth: 440
                Layout.fillHeight: false
                title: "Create Timeline"
                subtitle: "Start with sequence settings instead of an implicit default timeline."
                accent: theme.warning

                Label {
                    Layout.fillWidth: true
                    text: "This setup defines the sequence format, audio base, color pipeline defaults, and render container before you start cutting."
                    color: theme.textMain
                    wrapMode: Text.WordWrap
                }

                InspectorSection {
                    Layout.fillWidth: true
                    title: "Sequence"

                    ToneTextField {
                        Layout.fillWidth: true
                        placeholderText: "Timeline name"
                        text: root.setupTimelineName
                        onTextChanged: root.setupTimelineName = text
                    }

                    ToneTextField {
                        Layout.fillWidth: true
                        placeholderText: "Project binding"
                        text: root.setupProjectBinding
                        onTextChanged: root.setupProjectBinding = text
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: root.resolutionPresetLabels
                            currentIndex: Math.max(0, root.resolutionPresetLabels.indexOf(root.setupResolutionPreset))
                            onActivated: {
                                root.setupResolutionPreset = root.resolutionPresetLabels[currentIndex]
                                root.applyResolutionPreset()
                            }
                        }

                        ToneTextField {
                            Layout.preferredWidth: 110
                            text: root.setupWidth
                            onTextChanged: root.setupWidth = text
                        }

                        ToneTextField {
                            Layout.preferredWidth: 110
                            text: root.setupHeight
                            onTextChanged: root.setupHeight = text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["23.976", "24", "25", "29.97", "30", "60"]
                            currentIndex: Math.max(0, model.indexOf(root.setupFrameRate))
                            onActivated: root.setupFrameRate = model[currentIndex]
                        }

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["44100", "48000", "96000"]
                            currentIndex: Math.max(0, model.indexOf(root.setupAudioSampleRate))
                            onActivated: root.setupAudioSampleRate = model[currentIndex]
                        }
                    }
                }
            }

            BrandPanel {
                Layout.preferredWidth: 440
                Layout.fillHeight: false
                title: "Color + Delivery"
                subtitle: "Professional core defaults for sequence and export."
                accent: theme.accentCool400

                InspectorSection {
                    Layout.fillWidth: true
                    title: "Color Pipeline"

                    ToneComboBox {
                        Layout.fillWidth: true
                        model: ["Rec.709", "Display P3", "sRGB"]
                        currentIndex: Math.max(0, model.indexOf(root.setupColorSpace))
                        onActivated: root.setupColorSpace = model[currentIndex]
                    }

                    ToneComboBox {
                        Layout.fillWidth: true
                        model: ["Legal", "Full"]
                        currentIndex: Math.max(0, model.indexOf(root.setupDataLevels))
                        onActivated: root.setupDataLevels = model[currentIndex]
                    }
                }

                InspectorSection {
                    Layout.fillWidth: true
                    title: "Export Defaults"

                    RowLayout {
                        Layout.fillWidth: true

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["mp4", "mov"]
                            currentIndex: Math.max(0, model.indexOf(root.setupExportContainer))
                            onActivated: root.setupExportContainer = model[currentIndex]
                        }

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["h264", "prores"]
                            currentIndex: Math.max(0, model.indexOf(root.setupExportCodec))
                            onActivated: root.setupExportCodec = model[currentIndex]
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "Project-bound timelines save back into the active project by default. Media can be added after creation from the Timeline media browser."
                    color: theme.textMuted
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Use Active Project"
                        enabled: catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                        onClicked: root.setupProjectBinding = catalogAssets.projectFilter
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Create Timeline"
                        tone: "primary"
                        onClicked: root.createTimelineFromSetup()
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: timelineDocument.hasActiveTimeline
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: timelineNameField
                Layout.preferredWidth: 320
                Layout.fillWidth: true
                text: timelineDocument.timelineName
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 14
                font.weight: Font.DemiBold
                selectByMouse: true
                placeholderText: "Timeline name"
                onEditingFinished: timelineDocument.timelineName = text

                background: Rectangle {
                    radius: 8
                    color: theme.ink850
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.92)
                }
            }

            InfoPill {
                label: "Format"
                value: timelineDocument.sequenceFormatLabel
            }

            InfoPill {
                label: "Tracks"
                value: timelineDocument.videoTrackCount + "V / " + timelineDocument.audioTrackCount + "A"
            }

            ActionButton {
                text: "New"
                iconName: "plus"
                compact: true
                tooltipText: "Create a new timeline"
                onClicked: timelineDocument.closeTimeline()
            }

            ActionButton {
                text: "Media"
                iconName: "plus"
                compact: true
                tooltipText: "Open media browser"
                onClicked: root.openMediaPicker()
            }

            ActionButton {
                text: "Save"
                iconName: "save"
                compact: true
                tooltipText: "Save timeline to the library"
                tone: "primary"
                enabled: timelineDocument.clipCount > 0
                onClicked: root.saveTimelineToLibrary()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: timecodeText(sequencePlayheadSeconds) + " / " + formatSeconds(sequenceDurationSeconds())
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 11
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 14
                color: theme.alpha(theme.lineSoft, 0.8)
            }

            Label {
                visible: timelineDocument.hasSourceClip
                Layout.preferredWidth: 220
                Layout.maximumWidth: 260
                text: sourceClip.displayLabel
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            Rectangle {
                visible: timelineDocument.hasSourceClip
                Layout.preferredWidth: 1
                Layout.preferredHeight: 14
                color: theme.alpha(theme.lineSoft, 0.8)
            }

            Label {
                text: timelineDocument.activeTool.toUpperCase() + "  |  "
                      + timelineDocument.editMode.toUpperCase() + "  |  "
                      + (snappingEnabled ? "SNAP" : "FREE")
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
            }

            Item { Layout.fillWidth: true }

            Label {
                text: timelineDocument.dirty ? "Unsaved changes" : timelineDocument.status
                color: timelineDocument.dirty ? theme.accentWarm300 : theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            TimelineSourcePanel {
                id: sourceRail
                SplitView.preferredWidth: sourcePanelCollapsed ? 56 : sourcePanelWidth
                SplitView.minimumWidth: sourcePanelCollapsed ? 56 : 260
                SplitView.maximumWidth: sourcePanelCollapsed ? 56 : 460
                theme: theme
                timelineDocument: timelineDocument
                sourceClip: sourceClip
                catalogAssets: catalogAssets
                collapsed: sourcePanelCollapsed
                activeTab: sourceRailTab
                onToggleCollapseRequested: sourcePanelCollapsed = !sourcePanelCollapsed
                onTabChangeRequested: function(tab) { sourceRailTab = tab }
                onMediaPickerRequested: root.openMediaPicker()
                onWidthChanged: {
                    if (!sourcePanelCollapsed && width >= 260)
                        sourcePanelWidth = width
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 480
                radius: 12
                color: theme.alpha(theme.ink900, 0.82)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.55)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 12
                        color: "#0b1014"
                        clip: true

                        VideoOutput {
                            id: videoMonitor
                            anchors.centerIn: parent
                            width: parent.width - 18
                            height: parent.height - 18
                            fillMode: root.programVideoFillMode()
                            visible: root.shouldShowVideoMonitor()
                            transformOrigin: Item.Center
                            scale: currentClip.frameScale ? currentClip.frameScale : 1
                            anchors.horizontalCenterOffset: (currentClip.frameOffsetX ? currentClip.frameOffsetX : 0) * width * 0.18
                            anchors.verticalCenterOffset: (currentClip.frameOffsetY ? currentClip.frameOffsetY : 0) * height * 0.18
                        }

                        Image {
                            anchors.centerIn: parent
                            width: parent.width - 18
                            height: parent.height - 18
                            source: timelineDocument.previewSource
                            fillMode: root.programImageFillMode()
                            cache: false
                            asynchronous: false
                            visible: root.shouldShowTimelinePoster()
                            transformOrigin: Item.Center
                            scale: currentClip.frameScale ? currentClip.frameScale : 1
                            anchors.horizontalCenterOffset: (currentClip.frameOffsetX ? currentClip.frameOffsetX : 0) * width * 0.18
                            anchors.verticalCenterOffset: (currentClip.frameOffsetY ? currentClip.frameOffsetY : 0) * height * 0.18
                        }

                        Rectangle {
                            visible: !root.hasPlayableClip()
                                     && timelineDocument.previewSource.length === 0
                            anchors.centerIn: parent
                            radius: 10
                            color: theme.alpha(theme.ink950, 0.82)
                            border.width: 1
                            border.color: theme.alpha(theme.lineStrong, 0.55)
                            implicitWidth: emptyState.implicitWidth + 28
                            implicitHeight: emptyState.implicitHeight + 26

                            ColumnLayout {
                                id: emptyState
                                anchors.centerIn: parent
                                spacing: 8

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Program Monitor"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Load media to source, then insert or overwrite."
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                }
                                
                                ActionButton {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Add Media"
                                    tone: "primary"
                                    onClicked: root.openMediaPicker()
                                }
                            }
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            radius: 8
                            color: theme.alpha(theme.ink950, 0.82)
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.45)
                            implicitWidth: overlayReadout.implicitWidth + 16
                            implicitHeight: 24

                            Label {
                                id: overlayReadout
                                anchors.centerIn: parent
                                text: timecodeText(sequencePlayheadSeconds)
                                color: theme.textStrong
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }
                        }

                        Rectangle {
                            visible: timelinePlayback.errorString.length > 0
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 10
                            radius: 10
                            color: theme.alpha("#4f1713", 0.94)
                            border.width: 1
                            border.color: theme.alpha("#e58767", 0.58)
                            implicitHeight: playbackErrorLabel.implicitHeight + 24

                            Label {
                                id: playbackErrorLabel
                                anchors.fill: parent
                                anchors.margins: 12
                                text: "Playback failed: " + timelinePlayback.errorString
                                color: "#ffd7ca"
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 10
                        color: theme.alpha(theme.ink950, 0.78)
                        border.width: 1
                        border.color: theme.alpha(theme.lineStrong, 0.46)
                        implicitHeight: 64

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                ActionButton {
                                    iconName: sequencePlaying ? "pause" : "play"
                                    showText: false
                                    compact: true
                                    tooltipText: sequencePlaying ? "Pause playback" : "Play sequence"
                                    enabled: root.hasPlayableClip()
                                    onClicked: {
                                        if (sequencePlaying) {
                                            timelinePlayback.pause()
                                        } else {
                                            timelinePlayback.play()
                                        }
                                    }
                                }

                                ActionButton {
                                    iconName: "jump-start"
                                    showText: false
                                    compact: true
                                    tooltipText: "Jump to start"
                                    enabled: timelineDocument.clipCount > 0
                                    onClicked: timelinePlayback.seekSequenceSeconds(0)
                                }

                                ActionButton {
                                    iconName: "step-back"
                                    showText: false
                                    compact: true
                                    tooltipText: "Step back one frame"
                                    enabled: root.hasPlayableClip()
                                    onClicked: timelinePlayback.stepFrames(-1)
                                }

                                ActionButton {
                                    iconName: "step-forward"
                                    showText: false
                                    compact: true
                                    tooltipText: "Step forward one frame"
                                    enabled: root.hasPlayableClip()
                                    onClicked: timelinePlayback.stepFrames(1)
                                }

                                Rectangle {
                                    Layout.preferredWidth: 1
                                    Layout.preferredHeight: 18
                                    color: theme.alpha(theme.lineSoft, 0.78)
                                }

                                ActionButton {
                                    iconName: "cut"
                                    showText: false
                                    compact: true
                                    tooltipText: "Split at playhead"
                                    tone: "primary"
                                    enabled: root.hasPlayableClip()
                                    onClicked: splitAtPlayhead()
                                }

                                ActionButton {
                                    iconName: "insert"
                                    showText: false
                                    compact: true
                                    tooltipText: "Insert source at playhead"
                                    enabled: timelineDocument.hasSourceClip
                                    onClicked: timelineDocument.insertSourceClipAtPlayhead()
                                }

                                ActionButton {
                                    iconName: "overwrite"
                                    showText: false
                                    compact: true
                                    tooltipText: "Overwrite with source at playhead"
                                    enabled: timelineDocument.hasSourceClip
                                    onClicked: timelineDocument.overwriteSourceClipAtPlayhead()
                                }

                                Rectangle {
                                    Layout.preferredWidth: 1
                                    Layout.preferredHeight: 18
                                    color: theme.alpha(theme.lineSoft, 0.78)
                                }

                                ActionButton {
                                    iconName: "move-back"
                                    showText: false
                                    compact: true
                                    tooltipText: "Move clip earlier"
                                    enabled: timelineDocument.currentIndex > 0
                                    onClicked: timelineDocument.moveSelectedBackward()
                                }

                                ActionButton {
                                    iconName: "move-forward"
                                    showText: false
                                    compact: true
                                    tooltipText: "Move clip later"
                                    enabled: timelineDocument.currentIndex >= 0
                                             && timelineDocument.currentIndex < timelineDocument.clipCount - 1
                                    onClicked: timelineDocument.moveSelectedForward()
                                }

                                ActionButton {
                                    iconName: "delete"
                                    showText: false
                                    compact: true
                                    tooltipText: "Delete selected clip"
                                    enabled: timelineDocument.currentIndex >= 0
                                    onClicked: timelineDocument.removeSelectedClip()
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: timecodeText(sequencePlayheadSeconds)
                                    color: theme.accentWarm300
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                }
                            }

                            Slider {
                                id: transportSlider
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(0, sequenceDurationSeconds())
                                value: sequencePlayheadSeconds
                                enabled: timelineDocument.clipCount > 0
                                onPressedChanged: {
                                    if (pressed)
                                        timelinePlayback.pause()
                                }
                                onMoved: timelinePlayback.seekSequenceSeconds(value)
                            }
                        }
                    }
                }
            }

            TimelineEffectsPanel {
                id: effectsRail
                SplitView.preferredWidth: effectsPanelCollapsed ? 56 : effectsPanelWidth
                SplitView.minimumWidth: effectsPanelCollapsed ? 56 : 288
                SplitView.maximumWidth: effectsPanelCollapsed ? 56 : 460
                theme: theme
                timelineDocument: timelineDocument
                currentClip: currentClip
                collapsed: effectsPanelCollapsed
                activeTab: inspectorTab
                effectLibraryModel: effectLibraryModel
                transitionLibraryModel: transitionLibraryModel
                onToggleCollapseRequested: effectsPanelCollapsed = !effectsPanelCollapsed
                onTabChangeRequested: function(tab) { inspectorTab = tab }
                onWidthChanged: {
                    if (!effectsPanelCollapsed && width >= 288)
                        effectsPanelWidth = width
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 286
            radius: 12
            color: theme.alpha(theme.ink900, 0.84)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.55)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: "Timeline"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: timelineDocument.trackCount + " tracks"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Label {
                        text: sequenceMarkerModel.length + " markers"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Item { Layout.fillWidth: true }

                    ActionButton {
                        text: "Marker"
                        iconName: "plus"
                        compact: true
                        tooltipText: "Add marker at playhead"
                        onClicked: timelineDocument.addSequenceMarker()
                    }

                    ActionButton {
                        text: "Track"
                        iconName: "plus"
                        compact: true
                        tooltipText: "Add video track"
                        onClicked: timelineDocument.addVideoTrack()
                    }

                    Label {
                        text: timecodeText(sequencePlayheadSeconds)
                        color: theme.accentWarm300
                        font.family: theme.fontMono
                        font.pixelSize: 11
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    ActionButton {
                        iconName: "select"
                        showText: false
                        compact: true
                        tooltipText: "Select tool"
                        active: timelineDocument.activeTool === "select"
                        onClicked: timelineDocument.activeTool = "select"
                    }

                    ActionButton {
                        iconName: "cut"
                        showText: false
                        compact: true
                        tooltipText: "Razor tool"
                        active: timelineDocument.activeTool === "razor"
                        onClicked: timelineDocument.activeTool = "razor"
                    }

                    ActionButton {
                        iconName: "slip"
                        showText: false
                        compact: true
                        tooltipText: "Slip tool"
                        active: timelineDocument.activeTool === "slip"
                        onClicked: timelineDocument.activeTool = "slip"
                    }

                    ActionButton {
                        iconName: "roll"
                        showText: false
                        compact: true
                        tooltipText: "Roll tool"
                        active: timelineDocument.activeTool === "roll"
                        onClicked: timelineDocument.activeTool = "roll"
                    }

                    ActionButton {
                        text: "Ins"
                        compact: true
                        tooltipText: "Insert edit mode"
                        active: timelineDocument.editMode === "insert"
                        onClicked: timelineDocument.editMode = "insert"
                    }

                    ActionButton {
                        text: "Ovr"
                        compact: true
                        tooltipText: "Overwrite edit mode"
                        active: timelineDocument.editMode === "overwrite"
                        onClicked: timelineDocument.editMode = "overwrite"
                    }

                    ActionButton {
                        iconName: "link"
                        showText: false
                        compact: true
                        tooltipText: timelineDocument.linkedSelectionEnabled ? "Disable linked selection" : "Enable linked selection"
                        active: timelineDocument.linkedSelectionEnabled
                        onClicked: timelineDocument.linkedSelectionEnabled = !timelineDocument.linkedSelectionEnabled
                    }

                    ActionButton {
                        iconName: "snap"
                        showText: false
                        compact: true
                        tooltipText: snappingEnabled ? "Disable snapping" : "Enable snapping"
                        active: snappingEnabled
                        onClicked: snappingEnabled = !snappingEnabled
                    }

                    ActionButton {
                        iconName: "ripple"
                        showText: false
                        compact: true
                        tooltipText: rippleModeEnabled ? "Disable ripple trim" : "Enable ripple trim"
                        active: rippleModeEnabled
                        enabled: timelineDocument.currentIndex >= 0
                        onClicked: rippleModeEnabled = !rippleModeEnabled
                    }

                    ActionButton {
                        iconName: "cut"
                        showText: false
                        compact: true
                        tooltipText: "Split selected clip"
                        enabled: root.hasPlayableClip()
                        onClicked: splitAtPlayhead()
                    }

                    ActionButton {
                        iconName: "delete"
                        showText: false
                        compact: true
                        tooltipText: "Delete selected clip"
                        enabled: timelineDocument.currentIndex >= 0
                        onClicked: timelineDocument.removeSelectedClip()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Column {
                        id: trackHeaders
                        Layout.preferredWidth: laneHeaderWidth
                        spacing: trackSpacing

                        Rectangle {
                            width: laneHeaderWidth
                            height: rulerHeight
                            color: "transparent"
                        }

                        Repeater {
                            model: sequenceTrackModel

                            delegate: TimelineTrackHeader {
                                required property var modelData

                                width: laneHeaderWidth
                                height: laneHeight
                                theme: theme
                                timelineDocument: timelineDocument
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"
                        clip: true

                        Flickable {
                            id: timelineViewport
                            anchors.fill: parent
                            contentWidth: timelineContentWidth
                            contentHeight: timelineContentHeight
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ScrollBar.horizontal: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }

                            Rectangle {
                                width: timelineContentWidth
                                height: timelineContentHeight
                                color: "transparent"

                                MouseArea {
                                    id: timelineCanvasMouseArea
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    hoverEnabled: true
                                    onPressed: function(mouse) {
                                        if (mouse.button === Qt.RightButton) {
                                            timelineCanvasMenu.popup()
                                            mouse.accepted = true
                                            return
                                        }
                                        if (mouse.modifiers & Qt.ShiftModifier) {
                                            root.marqueeActive = true
                                            root.marqueeStartX = mouse.x
                                            root.marqueeStartY = mouse.y
                                            root.marqueeCurrentX = mouse.x
                                            root.marqueeCurrentY = mouse.y
                                            return
                                        }
                                        root.resetTimelinePreviewState()
                                        root.seekTimelineFromX(mouse.x)
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (root.marqueeActive) {
                                            root.marqueeCurrentX = mouse.x
                                            root.marqueeCurrentY = mouse.y
                                            return
                                        }
                                        if (pressed)
                                            root.seekTimelineFromX(mouse.x)
                                    }
                                    onReleased: function(mouse) {
                                        if (!root.marqueeActive)
                                            return
                                        root.marqueeCurrentX = mouse.x
                                        root.marqueeCurrentY = mouse.y
                                        root.selectClipFromMarquee()
                                        root.marqueeActive = false
                                    }
                                }

                                Menu {
                                    id: timelineCanvasMenu

                                    MenuItem {
                                        text: "Add Marker At Playhead"
                                        onTriggered: timelineDocument.addSequenceMarker()
                                    }

                                    MenuItem {
                                        text: "Insert Source Here"
                                        enabled: timelineDocument.hasSourceClip
                                        onTriggered: timelineDocument.insertSourceClipAtPlayhead()
                                    }

                                    MenuItem {
                                        text: "Overwrite Source Here"
                                        enabled: timelineDocument.hasSourceClip
                                        onTriggered: timelineDocument.overwriteSourceClipAtPlayhead()
                                    }
                                }

                                Repeater {
                                    model: Math.max(2, Math.ceil(sequenceDurationSeconds()) + 1)

                                    delegate: Item {
                                        required property int index

                                        x: index * timelineZoom
                                        y: 0
                                        width: 1
                                        height: timelineContentHeight

                                        Rectangle {
                                            anchors.top: parent.top
                                            width: 1
                                            height: timelineContentHeight
                                            color: index % 5 === 0 ? theme.alpha(theme.lineStrong, 0.34) : theme.alpha(theme.lineSoft, 0.18)
                                        }

                                        Label {
                                            anchors.top: parent.top
                                            anchors.topMargin: 4
                                            anchors.left: parent.left
                                            anchors.leftMargin: 4
                                            text: root.formatSeconds(index)
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                        }
                                    }
                                }

                                Repeater {
                                    model: sequenceMarkerModel

                                    delegate: Item {
                                        required property var modelData

                                        x: Number(modelData.seconds || 0) * timelineZoom
                                        y: 0
                                        width: 2
                                        height: timelineContentHeight

                                        Rectangle {
                                            anchors.top: parent.top
                                            width: 2
                                            height: timelineContentHeight
                                            color: modelData.color ? modelData.color : theme.accentWarm300
                                            opacity: 0.7
                                        }

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.topMargin: 2
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            radius: 6
                                            color: modelData.color ? modelData.color : theme.accentWarm300
                                            implicitWidth: markerLabel.implicitWidth + 12
                                            implicitHeight: markerLabel.implicitHeight + 6

                                            Label {
                                                id: markerLabel
                                                anchors.centerIn: parent
                                                text: modelData.label
                                                color: theme.ink950
                                                font.family: theme.fontMono
                                                font.pixelSize: 8
                                            }
                                        }
                                    }
                                }

                                Repeater {
                                    model: sequenceTrackModel

                                    delegate: Item {
                                        id: trackLane
                                        required property var modelData
                                        required property int index

                                        property int trackRowIndex: index

                                        x: 0
                                        y: rulerHeight + index * (laneHeight + trackSpacing)
                                        width: timelineContentWidth
                                        height: laneHeight

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 10
                                            color: theme.alpha(theme.ink950, 0.62)
                                            border.width: 1
                                            border.color: theme.alpha(theme.lineSoft, 0.32)
                                        }

                                        Repeater {
                                            model: modelData.clips

                                            delegate: Rectangle {
                                                id: clipBlock
                                                required property var modelData

                                                property real committedX: Number(modelData.startSeconds || 0) * timelineZoom + 3
                                                property real dragOffsetX: 0
                                                property real dragOffsetY: 0
                                                property real pressX: 0
                                                property real pressY: 0
                                                property real trimHandleWidth: modelData.trackKind === "video" ? 12 : 0
                                                property real fadeHandleWidth: 18

                                                x: committedX + dragOffsetX
                                                y: 6 + dragOffsetY
                                                width: root.clipWidth(modelData.trimmedDurationSeconds) - 6
                                                height: parent.height - 12
                                                radius: 8
                                                color: modelData.trackKind === "audio"
                                                       ? (modelData.silentPlaceholder
                                                          ? theme.alpha(theme.ink700, 0.78)
                                                          : theme.alpha(theme.accentCool400, 0.26))
                                                       : (modelData.selected
                                                          ? theme.alpha(theme.accentWarm500, Math.max(0.35, Number(modelData.opacity || 1) * 0.82))
                                                          : theme.alpha(theme.accentWarm400, Math.max(0.18, Number(modelData.opacity || 1) * 0.34)))
                                                border.width: modelData.selected ? 2 : 1
                                                border.color: modelData.selected
                                                              ? theme.textStrong
                                                              : theme.alpha(theme.lineStrong, 0.4)

                                                MouseArea {
                                                    id: clipMouseArea
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    cursorShape: modelData.trackKind !== "video"
                                                                 ? Qt.PointingHandCursor
                                                                 : timelineDocument.activeTool === "razor"
                                                                   ? Qt.CrossCursor
                                                                   : timelineDocument.activeTool === "slip"
                                                                     ? Qt.SizeHorCursor
                                                                   : Qt.OpenHandCursor
                                                    onPressed: function(mouse) {
                                                        if (mouse.button === Qt.RightButton) {
                                                            timelineDocument.selectClip(modelData.index)
                                                            clipMenu.popup()
                                                            mouse.accepted = true
                                                            return
                                                        }
                                                        if (modelData.trackKind === "video" && timelineDocument.activeTool === "razor") {
                                                            timelineDocument.selectClip(modelData.index)
                                                            timelineDocument.splitSelectedClip(root.clipSequenceTime(modelData, mouse.x))
                                                            return
                                                        }
                                                        clipBlock.pressX = mouse.x
                                                        clipBlock.pressY = mouse.y
                                                        timelineDocument.selectClip(modelData.index)
                                                    }
                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed || modelData.trackKind !== "video"
                                                                || trackLane.modelData.locked)
                                                            return
                                                        if (timelineDocument.activeTool === "select") {
                                                            clipBlock.dragOffsetX = mouse.x - clipBlock.pressX
                                                            clipBlock.dragOffsetY = mouse.y - clipBlock.pressY
                                                            const targetRow = Math.max(
                                                                        0,
                                                                        Math.min(
                                                                            sequenceTrackModel.length - 1,
                                                                            trackLane.trackRowIndex + Math.round(clipBlock.dragOffsetY / (laneHeight + trackSpacing))))
                                                            const snapResult = root.snapSequenceResult(
                                                                        Math.max(0, (clipBlock.committedX + clipBlock.dragOffsetX - 3) / timelineZoom),
                                                                        modelData.clipId)
                                                            root.dragPreviewVisible = true
                                                            root.dragPreviewWidth = clipBlock.width
                                                            root.dragPreviewX = snapResult.seconds * timelineZoom + 3
                                                            root.dragPreviewY = rulerHeight + targetRow * (laneHeight + trackSpacing) + 6
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, "Snap")
                                                        } else if (timelineDocument.activeTool === "slip") {
                                                            const deltaSeconds = (mouse.x - clipBlock.pressX) / timelineZoom
                                                            timelineDocument.slipSelectedClipBySeconds(deltaSeconds)
                                                            clipBlock.pressX = mouse.x
                                                            root.updateSnapGuide(
                                                                        Number(modelData.startSeconds || 0),
                                                                        false,
                                                                        "")
                                                        }
                                                    }
                                                    onReleased: {
                                                        if (modelData.trackKind === "video"
                                                                && timelineDocument.activeTool === "select"
                                                                && !trackLane.modelData.locked
                                                                && (Math.abs(clipBlock.dragOffsetX) > 4 || Math.abs(clipBlock.dragOffsetY) > 4)) {
                                                            const targetRow = Math.max(
                                                                        0,
                                                                        Math.min(
                                                                            sequenceTrackModel.length - 1,
                                                                            trackLane.trackRowIndex + Math.round(clipBlock.dragOffsetY / (laneHeight + trackSpacing))))
                                                            const snapResult = root.snapSequenceResult(
                                                                        Math.max(0, (clipBlock.committedX + clipBlock.dragOffsetX - 3) / timelineZoom),
                                                                        modelData.clipId)
                                                            timelineDocument.moveSelectedClipToTrackRow(targetRow, snapResult.seconds)
                                                        }
                                                        clipBlock.dragOffsetX = 0
                                                        clipBlock.dragOffsetY = 0
                                                        root.resetTimelinePreviewState()
                                                    }
                                                }

                                                DropArea {
                                                    anchors.fill: parent
                                                    keys: ["effect"]
                                                    onEntered: function(drag) {
                                                        if (drag.source && drag.source.libraryType === "effect")
                                                            clipBlock.border.color = theme.accentCool300
                                                    }
                                                    onExited: clipBlock.border.color = modelData.selected
                                                                                  ? theme.textStrong
                                                                                  : theme.alpha(theme.lineStrong, 0.4)
                                                    onDropped: function(drop) {
                                                        clipBlock.border.color = modelData.selected
                                                                              ? theme.textStrong
                                                                              : theme.alpha(theme.lineStrong, 0.4)
                                                        if (drop.source && drop.source.libraryType === "effect")
                                                            root.applyEffectToClip(modelData.index, drop.source.libraryName)
                                                    }
                                                }

                                                Menu {
                                                    id: clipMenu

                                                    MenuItem {
                                                        text: "Split At Playhead"
                                                        enabled: root.hasPlayableClip()
                                                        onTriggered: splitAtPlayhead()
                                                    }

                                                    MenuItem {
                                                        text: "Move Earlier"
                                                        enabled: timelineDocument.currentIndex > 0
                                                        onTriggered: timelineDocument.moveSelectedBackward()
                                                    }

                                                    MenuItem {
                                                        text: "Move Later"
                                                        enabled: timelineDocument.currentIndex >= 0
                                                                 && timelineDocument.currentIndex < timelineDocument.clipCount - 1
                                                        onTriggered: timelineDocument.moveSelectedForward()
                                                    }

                                                    MenuItem {
                                                        text: "Delete Clip"
                                                        enabled: timelineDocument.currentIndex >= 0
                                                        onTriggered: timelineDocument.removeSelectedClip()
                                                    }
                                                }

                                                Image {
                                                    anchors.fill: parent
                                                    anchors.margins: 4
                                                    source: modelData.trackKind === "audio" ? modelData.waveformSource : ""
                                                    fillMode: Image.Stretch
                                                    opacity: 0.42
                                                    visible: modelData.trackKind === "audio"
                                                             && !modelData.silentPlaceholder
                                                             && source.length > 0
                                                    cache: false
                                                    asynchronous: true
                                                }

                                                Rectangle {
                                                    visible: modelData.trackKind === "video"
                                                    anchors.left: parent.left
                                                    anchors.top: parent.top
                                                    anchors.bottom: parent.bottom
                                                    width: clipBlock.trimHandleWidth
                                                    radius: 8
                                                    color: theme.alpha(theme.textStrong, 0.16)

                                                    DropArea {
                                                        anchors.fill: parent
                                                        keys: ["transition"]
                                                        onDropped: function(drop) {
                                                            if (drop.source && drop.source.libraryType === "transition")
                                                                root.applyTransitionToClipEdge(
                                                                            modelData.index,
                                                                            "in",
                                                                            drop.source.libraryName,
                                                                            Number(drop.source.libraryDurationSeconds || 0.35))
                                                        }
                                                    }

                                                    MouseArea {
                                                        id: leftTrimHandle
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SizeHorCursor
                                                        preventStealing: true
                                                        onPressed: function(mouse) {
                                                            if (trackLane.modelData.locked)
                                                                return
                                                            timelinePlayback.pause()
                                                            timelineDocument.selectClip(modelData.index)
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const snapResult = root.snapSequenceResult(point.x / timelineZoom, modelData.clipId)
                                                            if (timelineDocument.activeTool === "roll")
                                                                timelineDocument.rollSelectedClipStartToSequenceSeconds(snapResult.seconds)
                                                            else
                                                                timelineDocument.trimSelectedClipStartToSequenceSeconds(snapResult.seconds, rippleModeEnabled)
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, timelineDocument.activeTool === "roll" ? "Roll" : "Trim")
                                                        }
                                                        onPositionChanged: function(mouse) {
                                                            if (!pressed || trackLane.modelData.locked)
                                                                return
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const snapResult = root.snapSequenceResult(point.x / timelineZoom, modelData.clipId)
                                                            if (timelineDocument.activeTool === "roll")
                                                                timelineDocument.rollSelectedClipStartToSequenceSeconds(snapResult.seconds)
                                                            else
                                                                timelineDocument.trimSelectedClipStartToSequenceSeconds(snapResult.seconds, rippleModeEnabled)
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, timelineDocument.activeTool === "roll" ? "Roll" : "Trim")
                                                        }
                                                        onReleased: {
                                                            root.resetTimelinePreviewState()
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    visible: modelData.trackKind === "video"
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.bottom: parent.bottom
                                                    width: clipBlock.trimHandleWidth
                                                    radius: 8
                                                    color: theme.alpha(theme.textStrong, 0.16)

                                                    DropArea {
                                                        anchors.fill: parent
                                                        keys: ["transition"]
                                                        onDropped: function(drop) {
                                                            if (drop.source && drop.source.libraryType === "transition")
                                                                root.applyTransitionToClipEdge(
                                                                            modelData.index,
                                                                            "out",
                                                                            drop.source.libraryName,
                                                                            Number(drop.source.libraryDurationSeconds || 0.35))
                                                        }
                                                    }

                                                    MouseArea {
                                                        id: rightTrimHandle
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SizeHorCursor
                                                        preventStealing: true
                                                        onPressed: function(mouse) {
                                                            if (trackLane.modelData.locked)
                                                                return
                                                            timelinePlayback.pause()
                                                            timelineDocument.selectClip(modelData.index)
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const snapResult = root.snapSequenceResult(point.x / timelineZoom, modelData.clipId)
                                                            if (timelineDocument.activeTool === "roll")
                                                                timelineDocument.rollSelectedClipEndToSequenceSeconds(snapResult.seconds)
                                                            else
                                                                timelineDocument.trimSelectedClipEndToSequenceSeconds(snapResult.seconds, rippleModeEnabled)
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, timelineDocument.activeTool === "roll" ? "Roll" : "Trim")
                                                        }
                                                        onPositionChanged: function(mouse) {
                                                            if (!pressed || trackLane.modelData.locked)
                                                                return
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const snapResult = root.snapSequenceResult(point.x / timelineZoom, modelData.clipId)
                                                            if (timelineDocument.activeTool === "roll")
                                                                timelineDocument.rollSelectedClipEndToSequenceSeconds(snapResult.seconds)
                                                            else
                                                                timelineDocument.trimSelectedClipEndToSequenceSeconds(snapResult.seconds, rippleModeEnabled)
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, timelineDocument.activeTool === "roll" ? "Roll" : "Trim")
                                                        }
                                                        onReleased: {
                                                            root.resetTimelinePreviewState()
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    visible: modelData.trackKind === "video"
                                                             && (leftTrimHandle.pressed || rightTrimHandle.pressed)
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    anchors.bottom: parent.top
                                                    anchors.bottomMargin: 6
                                                    radius: 10
                                                    color: theme.alpha(theme.ink950, 0.9)
                                                    border.width: 1
                                                    border.color: theme.alpha(theme.lineStrong, 0.46)
                                                    implicitWidth: trimOverlayLabel.implicitWidth + 16
                                                    implicitHeight: trimOverlayLabel.implicitHeight + 10

                                                    Label {
                                                        id: trimOverlayLabel
                                                        anchors.centerIn: parent
                                                        text: "In " + Number(modelData.trimStartSeconds || 0).toFixed(2)
                                                              + "  •  Out " + Number(modelData.trimEndSeconds || 0).toFixed(2)
                                                        color: theme.textStrong
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 9
                                                    }
                                                }

                                                Rectangle {
                                                    visible: modelData.transitionInName && modelData.transitionInName.length > 0
                                                    anchors.left: parent.left
                                                    anchors.top: parent.top
                                                    anchors.margins: 4
                                                    radius: 6
                                                    color: theme.alpha(theme.accentCool500, 0.9)
                                                    implicitWidth: transitionInLabel.implicitWidth + 10
                                                    implicitHeight: transitionInLabel.implicitHeight + 4

                                                    Label {
                                                        id: transitionInLabel
                                                        anchors.centerIn: parent
                                                        text: modelData.transitionInName
                                                        color: theme.ink950
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 8
                                                    }
                                                }

                                                Rectangle {
                                                    visible: modelData.transitionOutName && modelData.transitionOutName.length > 0
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.margins: 4
                                                    radius: 6
                                                    color: theme.alpha(theme.accentCool500, 0.9)
                                                    implicitWidth: transitionOutLabel.implicitWidth + 10
                                                    implicitHeight: transitionOutLabel.implicitHeight + 4

                                                    Label {
                                                        id: transitionOutLabel
                                                        anchors.centerIn: parent
                                                        text: modelData.transitionOutName
                                                        color: theme.ink950
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 8
                                                    }
                                                }

                                                Rectangle {
                                                    anchors.left: parent.left
                                                    anchors.leftMargin: 4
                                                    anchors.bottom: parent.bottom
                                                    anchors.bottomMargin: 4
                                                    width: Math.min(parent.width / 2 - 6, Math.max(12, Number(modelData.fadeInSeconds || 0) * timelineZoom))
                                                    height: 8
                                                    radius: 4
                                                    color: theme.alpha(theme.accentCool400, 0.82)
                                                    visible: modelData.selected || Number(modelData.fadeInSeconds || 0) > 0

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SizeHorCursor
                                                        enabled: timelineDocument.currentIndex >= 0
                                                        onPressed: timelineDocument.selectClip(modelData.index)
                                                        onPositionChanged: function(mouse) {
                                                            if (!pressed)
                                                                return
                                                            timelineDocument.selectClip(modelData.index)
                                                            const durationSeconds = Math.max(0, Math.min(Number(modelData.trimmedDurationSeconds || 0), mouse.x / timelineZoom))
                                                            timelineDocument.setSelectedClipFadeInSeconds(durationSeconds)
                                                            inspectorTab = "audio"
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    anchors.right: parent.right
                                                    anchors.rightMargin: 4
                                                    anchors.bottom: parent.bottom
                                                    anchors.bottomMargin: 4
                                                    width: Math.min(parent.width / 2 - 6, Math.max(12, Number(modelData.fadeOutSeconds || 0) * timelineZoom))
                                                    height: 8
                                                    radius: 4
                                                    color: theme.alpha(theme.accentCool400, 0.82)
                                                    visible: modelData.selected || Number(modelData.fadeOutSeconds || 0) > 0

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.SizeHorCursor
                                                        enabled: timelineDocument.currentIndex >= 0
                                                        onPressed: timelineDocument.selectClip(modelData.index)
                                                        onPositionChanged: function(mouse) {
                                                            if (!pressed)
                                                                return
                                                            timelineDocument.selectClip(modelData.index)
                                                            const durationSeconds = Math.max(0, Math.min(Number(modelData.trimmedDurationSeconds || 0), (parent.width - mouse.x) / timelineZoom))
                                                            timelineDocument.setSelectedClipFadeOutSeconds(durationSeconds)
                                                            inspectorTab = "audio"
                                                        }
                                                    }
                                                }

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 8 + clipBlock.trimHandleWidth
                                                    anchors.rightMargin: 8 + clipBlock.trimHandleWidth
                                                    anchors.topMargin: 6
                                                    anchors.bottomMargin: 6
                                                    spacing: 2

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.displayLabel
                                                        color: theme.textStrong
                                                        font.family: theme.fontUi
                                                        font.pixelSize: 10
                                                        elide: Text.ElideRight
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        text: modelData.trackKind === "audio"
                                                              ? (modelData.silentPlaceholder
                                                                 ? "Silent bed"
                                                                 : (Number(modelData.audioGainDb || 0).toFixed(1) + " dB  •  Fades "
                                                                    + Number(modelData.fadeInSeconds || 0).toFixed(1)
                                                                    + "/" + Number(modelData.fadeOutSeconds || 0).toFixed(1)))
                                                              : (modelData.trimmedDurationText
                                                                 + "  •  Op "
                                                                 + Math.round(Number(modelData.opacity || 1) * 100)
                                                                 + "%  •  " + Number(modelData.playbackRate || 1).toFixed(2) + "x")
                                                        color: theme.textMuted
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 9
                                                        elide: Text.ElideRight
                                                    }

                                                    Flow {
                                                        Layout.fillWidth: true
                                                        spacing: 4
                                                        visible: modelData.effectStack && modelData.effectStack.length > 0

                                                        Repeater {
                                                            model: modelData.effectStack ? modelData.effectStack : []

                                                            delegate: Rectangle {
                                                                required property var modelData
                                                                radius: 5
                                                                color: theme.alpha(theme.ink950, 0.74)
                                                                border.width: 1
                                                                border.color: theme.alpha(theme.lineStrong, 0.34)
                                                                implicitWidth: effectChipLabel.implicitWidth + 10
                                                                implicitHeight: effectChipLabel.implicitHeight + 4

                                                                Label {
                                                                    id: effectChipLabel
                                                                    anchors.centerIn: parent
                                                                    text: modelData
                                                                    color: theme.textMain
                                                                    font.family: theme.fontMono
                                                                    font.pixelSize: 8
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
                                    visible: root.dragPreviewVisible
                                    x: root.dragPreviewX
                                    y: root.dragPreviewY
                                    width: root.dragPreviewWidth
                                    height: laneHeight - 12
                                    radius: 8
                                    color: root.dragPreviewColor
                                    border.width: 1
                                    border.color: theme.alpha(theme.accentWarm300, 0.7)
                                }

                                Rectangle {
                                    visible: root.marqueeActive
                                    x: Math.min(root.marqueeStartX, root.marqueeCurrentX)
                                    y: Math.min(root.marqueeStartY, root.marqueeCurrentY)
                                    width: Math.abs(root.marqueeCurrentX - root.marqueeStartX)
                                    height: Math.abs(root.marqueeCurrentY - root.marqueeStartY)
                                    color: theme.alpha(theme.accentCool400, 0.14)
                                    border.width: 1
                                    border.color: theme.alpha(theme.accentCool300, 0.72)
                                }

                                Rectangle {
                                    visible: root.snapGuideVisible
                                    x: root.snapGuideSeconds * timelineZoom
                                    y: 0
                                    width: 2
                                    height: timelineContentHeight
                                    color: theme.accentCool400
                                }

                                Rectangle {
                                    visible: root.snapGuideVisible
                                    x: Math.max(0, root.snapGuideSeconds * timelineZoom - snapGuideBubble.implicitWidth / 2)
                                    y: 2
                                    radius: 8
                                    color: theme.alpha(theme.ink950, 0.94)
                                    border.width: 1
                                    border.color: theme.alpha(theme.accentCool400, 0.56)
                                    implicitWidth: snapGuideBubble.implicitWidth + 12
                                    implicitHeight: snapGuideBubble.implicitHeight + 6

                                    Label {
                                        id: snapGuideBubble
                                        anchors.centerIn: parent
                                        text: root.snapGuideLabel
                                        color: theme.textStrong
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                    }
                                }

                                Rectangle {
                                    x: sequencePlayheadSeconds * timelineZoom
                                    y: 0
                                    width: 2
                                    height: timelineContentHeight
                                    color: theme.accentWarm500
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: timelineDocument

        function onTimelineNameChanged() {
            timelineNameField.text = timelineDocument.timelineName
        }
    }
}
