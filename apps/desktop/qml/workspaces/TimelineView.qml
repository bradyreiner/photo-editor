pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia
import "../components"
import "../components" as Components

Rectangle {
    id: root
    objectName: "timelineWorkspace"
    color: "transparent"

    Theme { id: theme }

    readonly property string timelineVisibilityContract: "timeline-visibility:v1|always:header-program-monitor-timeline|contextual:source-panel-effects-panel-selected-clip-markers|one-expansion:tools-media-picker-effects-transitions-render-jobs|hover-focus:clip-actions-marker-actions-track-resize|diagnostic:render-errors-job-detail|preserve:functionality"
    readonly property string timelineSurfaceContract: "timeline-surfaces:v1|program-monitor:dominant|transport:compact|source-panel:collapsible|effects-panel:collapsible|tracks:stable-rows|clips:readable|markers:contextual-strip|waveform:track-contained|minimap:secondary|no-elide:true|no-overlap:true"
    readonly property string timelineWorkflowPolishContract: "timeline-workflow-polish:v1|first-glance:source-program-timeline-effects|close-inspection:active-tool-edit-mode-playhead-selection-markers-effects-render|clutter:panels-collapsible-details-contextual|interaction:load-source-insert-overwrite-play-trim-split-marker-effect-render|text:no-elide-no-overlap|functionality:preserved"
    readonly property string timelinePageCompletionContract: "page-completion:v1|timeline|sidebar:source-media-picker|content:program-monitor-transport-timeline-tracks-waveform|inspector:effects-transitions-selected-clip|toolbar:tools-search-marker-filter-zoom|states:setup-empty-source-selected-dragging-rendering-error"

    property var currentClip: timelineDocument.currentClip
    property var sourceClip: timelineDocument.sourceClip
    property real playheadSeconds: timelineDocument.currentClipPlayheadSeconds
    property real sequencePlayheadSeconds: timelineDocument.sequencePlayheadSeconds
    property bool sequencePlaying: timelineDocument.sequencePlaying
    property real timelineZoom: 84
    property real timelineZoomMin: 12
    property real timelineZoomMax: 600
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
    property var recentTimelineRecords: []
    property string timelineSearchQuery: ""
    property string markerStatusFilter: "all"
    property string markerEditorId: ""
    property string markerEditorLabel: ""
    property string markerEditorNote: ""
    property string markerEditorStatus: "todo"
    property bool markerEditorCompleted: false
    property string inspectorTab: "effects"
    property bool sourcePanelCollapsed: false
    property bool sourcePanelHidden: false
    property bool effectsPanelCollapsed: false
    property string sourceRailTab: "source"
    property real sourcePanelWidth: 320
    property real effectsPanelWidth: 344
    property bool marqueeActive: false
    property real marqueeStartX: 0
    property real marqueeStartY: 0
    property real marqueeCurrentX: 0
    property real marqueeCurrentY: 0
    property var dragPreviewState: ({"visible": false, "x": 0, "y": 0, "width": 0})
    property var snapGuideState: ({"visible": false, "seconds": 0, "label": ""})
    property var trimFeedbackState: ({"visible": false, "mode": "", "edge": "", "deltaSeconds": 0, "label": ""})
    property bool markerFeedbackVisible: false
    property string markerFeedbackText: ""
    property var cachedSnapPoints: []
    property real lastSnapCalcTime: 0
    readonly property var sequenceTrackModel: timelineDocument.sequenceTracks ? timelineDocument.sequenceTracks : []
    readonly property var sequenceMarkerModel: timelineDocument.sequenceMarkers ? timelineDocument.sequenceMarkers : []
    readonly property string markerFeedbackSignature: "timeline-marker-feedback:v1|visible:" +
                                                       String(markerFeedbackVisible) +
                                                       "|text:" + markerFeedbackText +
                                                       "|chip:immediate-playhead|no-elide:true"
    readonly property string trimFeedbackSignature: "timeline-trim-feedback:v1|visible:" +
                                                     String(trimFeedbackState.visible === true) +
                                                     "|mode:" + String(trimFeedbackState.mode || "") +
                                                     "|edge:" + String(trimFeedbackState.edge || "") +
                                                     "|delta:" + Number(trimFeedbackState.deltaSeconds || 0).toFixed(2) +
                                                     "|label:" + String(trimFeedbackState.label || "") +
                                                     "|snap-guide-during-drag:" + String(snapGuideState.visible === true) +
                                                     "|hover-handles:true|ghost-drag:true|no-elide:true"
    readonly property bool timelineSearchActive: timelineSearchQuery.trim().length > 0 || markerStatusFilter !== "all"
    readonly property var effectLibraryModel: [
        {"name": "Transform", "category": "Motion", "description": "Position, scale, and framing controls."},
        {"name": "Opacity", "category": "Composite", "description": "Animate transparency and blends."},
        {"name": "Crop", "category": "Motion", "description": "Trim frame edges."},
        {"name": "Ken Burns", "category": "Motion", "description": "Animated pan and zoom."},
        {"name": "Color Balance", "category": "Color", "description": "Primary warmth and tint balance."},
        {"name": "Contrast", "category": "Color", "description": "Punch up midtones."},
        {"name": "Levels", "category": "Color", "description": "Black, midtone, and white point control."},
        {"name": "HSL", "category": "Color", "description": "Hue, saturation, and luminance per channel."},
        {"name": "Color Wheels", "category": "Color", "description": "Lift, gamma, and gain three-way correction."},
        {"name": "Sharpen", "category": "Detail", "description": "Restore edge definition."},
        {"name": "Noise Reduction", "category": "Detail", "description": "Temporal and spatial denoise."},
        {"name": "Gaussian Blur", "category": "Stylize", "description": "Soft blur for focus pulls."},
        {"name": "Vignette", "category": "Stylize", "description": "Darken frame edges."},
        {"name": "Letterbox", "category": "Stylize", "description": "Cinematic black bars."},
        {"name": "Speed Ramp", "category": "Time", "description": "Variable speed with easing."},
        {"name": "Volume", "category": "Audio", "description": "Clip gain and loudness."},
        {"name": "Pan", "category": "Audio", "description": "Stereo position."},
        {"name": "EQ", "category": "Audio", "description": "Three-band equalizer."},
        {"name": "Compressor", "category": "Audio", "description": "Dynamic range compression."},
        {"name": "Echo", "category": "Audio", "description": "Delay-based audio effect."},
        {"name": "Reverb", "category": "Audio", "description": "Room ambience simulation."}
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
    readonly property real defaultLaneHeight: 52
    readonly property real rulerHeight: 24
    readonly property real trackSpacing: 4
    property var trackHeights: ({})
    readonly property real timelineContentWidth: Math.max(
                                                    timelineViewport.width,
                                                    Math.max(1, sequenceDurationSeconds()) * timelineZoom + 140)
    readonly property real timelineContentHeight: totalTrackHeight()

    function trackHeight(trackId) {
        return trackHeights[trackId] || defaultLaneHeight
    }

    function setTrackHeight(trackId, height) {
        const clamped = Math.max(28, Math.min(200, height))
        let updated = Object.assign({}, trackHeights)
        updated[trackId] = clamped
        trackHeights = updated
    }

    function trackTopY(trackIndex) {
        let y = rulerHeight
        for (let i = 0; i < trackIndex; i++) {
            const track = sequenceTrackModel[i]
            y += trackHeight(track ? track.trackId : "") + trackSpacing
        }
        return y
    }

    function totalTrackHeight() {
        let total = rulerHeight
        for (let i = 0; i < Math.max(1, sequenceTrackModel.length); i++) {
            const track = sequenceTrackModel[i]
            total += (track ? trackHeight(track.trackId) : defaultLaneHeight) + trackSpacing
        }
        return total
    }
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
        property bool sourcePanelHidden: false
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
        sourcePanelHidden = timelineWorkspaceSettings.sourcePanelHidden
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

    Timer {
        id: markerFeedbackTimer
        interval: 1800
        repeat: false
        onTriggered: root.markerFeedbackVisible = false
    }

    onSourcePanelCollapsedChanged: timelineWorkspaceSettings.sourcePanelCollapsed = sourcePanelCollapsed
    onSourcePanelHiddenChanged: timelineWorkspaceSettings.sourcePanelHidden = sourcePanelHidden
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
        return mediaPickerDialog.selectedMediaType !== "timeline"
                && mediaPickerDialog.selectedFilePath.length > 0
                && mediaPickerDialog.selectedOnline
    }

    function pickedMediaDisabledReason() {
        if (mediaPickerDialog.selectedMediaType === "timeline")
            return "Choose photo, video, or audio media instead of a timeline document."
        if (mediaPickerDialog.selectedFilePath.length === 0)
            return "Choose media from the gallery before loading it."
        if (!mediaPickerDialog.selectedOnline)
            return "This media is offline. Reconnect it in Library before using it in the timeline."
        return ""
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
        recentTimelineRecords = timelineDocument.recentTimelines()
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

    function timelineWorkflowPolishSignature() {
        const mode = timelineDocument.hasActiveTimeline ? "timeline" : "setup"
        const selected = timelineDocument.currentIndex >= 0
        const dragging = root.dragPreviewState && root.dragPreviewState.visible === true
        const snapVisible = root.snapGuideState && root.snapGuideState.visible === true
        return "timeline-workflow-polish:v1" +
               "|mode:" + mode +
               "|tool:" + String(timelineDocument.activeTool || "select") +
               "|edit-mode:" + String(timelineDocument.editMode || "insert") +
               "|timeline:" + String(timelineDocument.hasActiveTimeline) +
               "|clips:" + String(timelineDocument.clipCount) +
               "|tracks:" + String(root.sequenceTrackModel.length) +
               "|markers:" + String(root.sequenceMarkerModel.length) +
               "|selected:" + String(selected) +
               "|source:" + String(timelineDocument.hasSourceClip) +
               "|source-panel:" + String(!root.sourcePanelHidden) + "/" + String(!root.sourcePanelCollapsed) +
               "|effects-panel:" + String(!root.effectsPanelCollapsed) +
               "|playhead:" + root.timecodeText(root.sequencePlayheadSeconds) +
               "|duration:" + root.timecodeText(root.sequenceDurationSeconds()) +
               "|playing:" + String(root.sequencePlaying) +
               "|snapping:" + String(root.snappingEnabled) +
               "|ripple:" + String(root.rippleModeEnabled) +
               "|search:" + String(root.timelineSearchActive) +
               "|marker-editor:" + String(markerEditorPopup.visible) +
               "|drag:" + String(dragging) +
               "|snap-guide:" + String(snapVisible) +
               "|rendering:" + String(root.hasActiveTimelineRenderJobs()) +
               "|preserve:source-load-insert-overwrite-play-step-seek-select-razor-slip-roll-snap-ripple-split-move-lift-extract-marker-effect-transition-zoom-save-render"
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

    function playableClipDisabledReason() {
        if (timelineDocument.clipCount === 0)
            return "Add media to the timeline before using playback controls."
        if (timelineDocument.currentIndex < 0)
            return "Select a timeline clip before using this control."
        if (!hasPlayableClip())
            return "The selected clip has no playable source loaded."
        return ""
    }

    function sourceClipDisabledReason() {
        if (!timelineDocument.hasSourceClip)
            return "Load media to the source monitor before inserting or overwriting."
        return ""
    }

    function selectedClipDisabledReason() {
        if (timelineDocument.currentIndex < 0)
            return "Select a timeline clip first."
        return ""
    }

    function moveClipEarlierDisabledReason() {
        if (timelineDocument.currentIndex < 0)
            return "Select a timeline clip before moving it."
        if (timelineDocument.currentIndex === 0)
            return "The selected clip is already first in the sequence."
        return ""
    }

    function moveClipLaterDisabledReason() {
        if (timelineDocument.currentIndex < 0)
            return "Select a timeline clip before moving it."
        if (timelineDocument.currentIndex >= timelineDocument.clipCount - 1)
            return "The selected clip is already last in the sequence."
        return ""
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

    function shortcutsEnabled() {
        return timelineDocument.hasActiveTimeline
                && !timelineNameField.activeFocus
                && !timelineSearchField.activeFocus
                && !markerEditorPopup.visible
    }

    function normalizedSearchText(value) {
        return String(value === undefined || value === null ? "" : value).trim().toLowerCase()
    }

    function markerStatusLabel(status, completed) {
        if (completed)
            return "done"
        const normalized = normalizedSearchText(status)
        if (normalized === "review" || normalized === "approved" || normalized === "exported")
            return normalized
        return "todo"
    }

    function markerAccentColor(marker) {
        if (marker && marker.completed)
            return theme.accentCool400
        const status = markerStatusLabel(marker ? marker.status : "", false)
        if (status === "review")
            return theme.warning
        if (status === "approved")
            return theme.accentCool400
        if (status === "exported")
            return theme.accentCool500
        return marker && marker.color ? marker.color : theme.accentWarm300
    }

    function markerStatusTone(marker) {
        const status = markerStatusLabel(marker ? marker.status : "", marker ? marker.completed : false)
        if (status === "done" || status === "approved" || status === "exported")
            return "success"
        if (status === "review")
            return "warning"
        return "accent"
    }

    function markerMatchesFilters(marker) {
        if (!marker)
            return false
        const filter = normalizedSearchText(markerStatusFilter)
        if (filter === "open" && marker.completed)
            return false
        if (filter !== "" && filter !== "all" && filter !== "open" && normalizedSearchText(marker.status) !== filter)
            return false

        const query = normalizedSearchText(timelineSearchQuery)
        if (query.length === 0)
            return true
        const haystack = [
            marker.label || "",
            marker.note || "",
            marker.status || "",
            marker.timecode || ""
        ].join(" ").toLowerCase()
        return haystack.indexOf(query) >= 0
    }

    function clipMatchesSearch(clip) {
        const query = normalizedSearchText(timelineSearchQuery)
        if (query.length === 0)
            return true
        if (!clip)
            return false
        const effectStack = clip.effectStack && clip.effectStack.join ? clip.effectStack.join(" ") : ""
        const haystack = [
            clip.displayLabel || "",
            clip.trackLabel || "",
            clip.mediaType || "",
            clip.transitionInName || "",
            clip.transitionOutName || "",
            effectStack
        ].join(" ").toLowerCase()
        return haystack.indexOf(query) >= 0
    }

    function searchMatchCount() {
        let total = 0
        for (let markerIndex = 0; markerIndex < sequenceMarkerModel.length; ++markerIndex) {
            if (markerMatchesFilters(sequenceMarkerModel[markerIndex]))
                total += 1
        }
        for (let trackIndex = 0; trackIndex < sequenceTrackModel.length; ++trackIndex) {
            const track = sequenceTrackModel[trackIndex]
            if (!track || !track.clips)
                continue
            for (let clipIndex = 0; clipIndex < track.clips.length; ++clipIndex) {
                if (clipMatchesSearch(track.clips[clipIndex]))
                    total += 1
            }
        }
        return total
    }

    function collectSearchResultTimes() {
        let values = []
        for (let markerIndex = 0; markerIndex < sequenceMarkerModel.length; ++markerIndex) {
            const marker = sequenceMarkerModel[markerIndex]
            if (markerMatchesFilters(marker))
                values.push(Number(marker.seconds || 0))
        }
        for (let trackIndex = 0; trackIndex < sequenceTrackModel.length; ++trackIndex) {
            const track = sequenceTrackModel[trackIndex]
            if (!track || !track.clips)
                continue
            for (let clipIndex = 0; clipIndex < track.clips.length; ++clipIndex) {
                const clip = track.clips[clipIndex]
                if (clipMatchesSearch(clip))
                    values.push(Number(clip.startSeconds || 0))
            }
        }
        values.sort(function(left, right) { return left - right })
        let deduped = []
        for (let index = 0; index < values.length; ++index) {
            if (deduped.length === 0 || Math.abs(values[index] - deduped[deduped.length - 1]) > 0.001)
                deduped.push(values[index])
        }
        return deduped
    }

    function jumpToSearchResult(direction) {
        const values = collectSearchResultTimes()
        if (values.length === 0)
            return

        const current = Number(sequencePlayheadSeconds || 0)
        if (direction >= 0) {
            for (let index = 0; index < values.length; ++index) {
                if (values[index] > current + 0.001) {
                    timelinePlayback.seekSequenceSeconds(values[index])
                    return
                }
            }
            timelinePlayback.seekSequenceSeconds(values[0])
            return
        }

        for (let reverseIndex = values.length - 1; reverseIndex >= 0; --reverseIndex) {
            if (values[reverseIndex] < current - 0.001) {
                timelinePlayback.seekSequenceSeconds(values[reverseIndex])
                return
            }
        }
        timelinePlayback.seekSequenceSeconds(values[values.length - 1])
    }

    function openMarkerEditor(marker) {
        if (!marker)
            return
        markerEditorId = marker.markerId || ""
        markerEditorLabel = marker.label || ""
        markerEditorNote = marker.note || ""
        markerEditorStatus = marker.status || "todo"
        markerEditorCompleted = Boolean(marker.completed)
        markerEditorPopup.open()
    }

    function saveMarkerEditor() {
        if (!markerEditorId || markerEditorId.length === 0)
            return
        timelineDocument.renameSequenceMarker(markerEditorId, markerEditorLabel)
        timelineDocument.setSequenceMarkerNote(markerEditorId, markerEditorNote)
        timelineDocument.setSequenceMarkerStatus(markerEditorId, markerEditorStatus)
        timelineDocument.setSequenceMarkerCompleted(markerEditorId, markerEditorCompleted)
        markerEditorPopup.close()
    }

    function deleteSelectedTimelineClip() {
        if (timelineDocument.selectedClipCount > 1) {
            if (rippleModeEnabled)
                timelineDocument.extractSelectedClips()
            else
                timelineDocument.liftSelectedClips()
            return
        }
        if (rippleModeEnabled)
            timelineDocument.extractSelectedClip()
        else
            timelineDocument.liftSelectedClip()
    }

    function clipWidth(durationSeconds) {
        return Math.max(minimumClipWidth, Number(durationSeconds || 0) * timelineZoom)
    }

    function buildSnapPointsCache(excludedClipId) {
        const points = [sequencePlayheadSeconds]
        for (let trackIndex = 0; trackIndex < sequenceTrackModel.length; ++trackIndex) {
            const track = sequenceTrackModel[trackIndex]
            if (!track || track.trackKind !== "video" || !track.clips)
                continue
            for (let clipIndex = 0; clipIndex < track.clips.length; ++clipIndex) {
                const clip = track.clips[clipIndex]
                if (!clip || clip.clipId === excludedClipId)
                    continue
                points.push(Number(clip.startSeconds || 0))
                points.push(Number(clip.endSeconds || 0))
            }
        }
        cachedSnapPoints = points
    }

    function snapSequenceSeconds(proposedSeconds, excludedClipId) {
        return snapSequenceResult(proposedSeconds, excludedClipId).seconds
    }

    function snapSequenceResult(proposedSeconds, excludedClipId, useCache) {
        if (!snappingEnabled)
            return {
                "seconds": Math.max(0, Number(proposedSeconds || 0)),
                "snapped": false
            }

        const thresholdSeconds = snapThresholdPixels / Math.max(1, timelineZoom)
        let snappedSeconds = Math.max(0, Number(proposedSeconds || 0))
        let nearestDistance = thresholdSeconds + 1
        let snapPoints

        if (useCache && cachedSnapPoints.length > 0) {
            snapPoints = cachedSnapPoints
        } else {
            snapPoints = [sequencePlayheadSeconds]
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

    function setTimelineZoom(newZoom, anchorX) {
        const oldZoom = timelineZoom
        const clampedZoom = Math.max(timelineZoomMin, Math.min(timelineZoomMax, newZoom))
        if (Math.abs(clampedZoom - oldZoom) < 0.01)
            return
        const ratio = clampedZoom / oldZoom
        timelineZoom = clampedZoom
        timelineViewport.contentX = Math.max(0, anchorX * ratio - (anchorX - timelineViewport.contentX))
    }

    function zoomToFit() {
        const duration = sequenceDurationSeconds()
        if (duration <= 0)
            return
        const availableWidth = timelineViewport.width - 40
        timelineZoom = Math.max(timelineZoomMin, Math.min(timelineZoomMax, availableWidth / duration))
        timelineViewport.contentX = 0
    }

    function rulerInterval() {
        const fps = Math.max(1, Math.round(Number(timelineDocument.framesPerSecond || 30)))
        if (timelineZoom >= 300) return 1.0 / fps
        if (timelineZoom >= 120) return 0.5
        if (timelineZoom >= 60)  return 1
        if (timelineZoom >= 30)  return 5
        if (timelineZoom >= 15)  return 10
        return 30
    }

    function rulerLabel(seconds) {
        const interval = rulerInterval()
        const fps = Math.max(1, Math.round(Number(timelineDocument.framesPerSecond || 30)))
        if (interval < 1) {
            const totalFrames = Math.round(seconds * fps)
            const s = Math.floor(totalFrames / fps)
            const f = totalFrames % fps
            const m = Math.floor(s / 60)
            return String(m).padStart(2, "0") + ":" + String(s % 60).padStart(2, "0") + ":" + String(f).padStart(2, "0")
        }
        return formatSeconds(seconds)
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

    function timelineHeaderMenuItems() {
        return [
            {
                key: "save",
                label: root.hasActiveTimelineRenderJobs() ? "Rendering" : "Save Timeline",
                icon: "save",
                enabled: timelineDocument.clipCount > 0 && !root.hasActiveTimelineRenderJobs(),
                disabledReason: root.hasActiveTimelineRenderJobs()
                                ? "Wait for the active render to finish before saving."
                                : "Add clips to the timeline before saving.",
                tooltip: "Save the timeline to the library."
            },
            {
                key: "jobs",
                label: "Open Export Jobs",
                icon: "export",
                enabled: jobQueue && jobQueue.totalJobCountForKind("timeline_render") > 0,
                disabledReason: "No timeline render jobs are available yet.",
                tooltip: "Open the export queue and render jobs."
            }
        ]
    }

    function handleTimelineHeaderMenuAction(key) {
        if (key === "save") {
            root.saveTimelineToLibrary()
            return
        }
        if (key === "jobs")
            root.openJobsWorkspace()
    }

    function timelineToolMenuItems() {
        return [
            {
                key: "tool-select",
                label: "Select Tool",
                icon: "select",
                checked: timelineDocument.activeTool === "select",
                tooltip: "Select, move, and trim clips."
            },
            {
                key: "tool-razor",
                label: "Razor Tool",
                icon: "cut",
                checked: timelineDocument.activeTool === "razor",
                tooltip: "Split clips at the pointer."
            },
            {
                key: "tool-slip",
                label: "Slip Tool",
                icon: "slip",
                checked: timelineDocument.activeTool === "slip",
                tooltip: "Slip a clip's source timing."
            },
            {
                key: "tool-roll",
                label: "Roll Tool",
                icon: "roll",
                checked: timelineDocument.activeTool === "roll",
                tooltip: "Roll adjacent edit points."
            },
            {
                key: "mode-insert",
                label: "Insert Mode",
                icon: "plus",
                checked: timelineDocument.editMode === "insert",
                tooltip: "Insert source media at the playhead."
            },
            {
                key: "mode-overwrite",
                label: "Overwrite Mode",
                icon: "media",
                checked: timelineDocument.editMode === "overwrite",
                tooltip: "Overwrite timeline media at the playhead."
            },
            {
                key: "toggle-linked",
                label: timelineDocument.linkedSelectionEnabled ? "Linked Selection On" : "Linked Selection Off",
                icon: "link",
                checked: timelineDocument.linkedSelectionEnabled,
                tooltip: "Toggle linked audio/video selection."
            },
            {
                key: "toggle-snapping",
                label: snappingEnabled ? "Snapping On" : "Snapping Off",
                icon: "snap",
                checked: snappingEnabled,
                tooltip: "Toggle snapping."
            },
            {
                key: "toggle-ripple",
                label: rippleModeEnabled ? "Ripple Trim On" : "Ripple Trim Off",
                icon: "ripple",
                checked: rippleModeEnabled,
                enabled: timelineDocument.currentIndex >= 0,
                disabledReason: "Select a timeline clip before changing ripple trim.",
                tooltip: "Toggle ripple trim for selected clips."
            },
            {
                key: "split",
                label: "Split At Playhead",
                icon: "cut",
                enabled: root.hasPlayableClip(),
                disabledReason: "Load a playable clip and place the playhead before splitting.",
                tooltip: "Split the selected clip at the playhead."
            },
            {
                key: "delete",
                label: rippleModeEnabled ? "Extract Selected Clip" : "Lift Selected Clip",
                icon: "delete",
                destructive: true,
                separatorBefore: true,
                enabled: timelineDocument.currentIndex >= 0,
                disabledReason: "Select a timeline clip before removing it.",
                tooltip: rippleModeEnabled ? "Remove the selected clip and close the gap." : "Remove the selected clip and leave the gap."
            }
        ]
    }

    function handleTimelineToolMenuAction(key) {
        if (key === "tool-select") {
            timelineDocument.activeTool = "select"
            return
        }
        if (key === "tool-razor") {
            timelineDocument.activeTool = "razor"
            return
        }
        if (key === "tool-slip") {
            timelineDocument.activeTool = "slip"
            return
        }
        if (key === "tool-roll") {
            timelineDocument.activeTool = "roll"
            return
        }
        if (key === "mode-insert") {
            timelineDocument.editMode = "insert"
            return
        }
        if (key === "mode-overwrite") {
            timelineDocument.editMode = "overwrite"
            return
        }
        if (key === "toggle-linked") {
            timelineDocument.linkedSelectionEnabled = !timelineDocument.linkedSelectionEnabled
            return
        }
        if (key === "toggle-snapping") {
            snappingEnabled = !snappingEnabled
            return
        }
        if (key === "toggle-ripple") {
            if (timelineDocument.currentIndex >= 0)
                rippleModeEnabled = !rippleModeEnabled
            return
        }
        if (key === "split") {
            root.splitAtPlayhead()
            return
        }
        if (key === "delete")
            root.deleteSelectedTimelineClip()
    }

    function hasActiveTimelineRenderJobs() {
        return jobQueue && jobQueue.activeJobCountForKind("timeline_render") > 0
    }

    function timelineRenderStatus() {
        return jobQueue
                ? jobQueue.statusForKind("timeline_render", timelineDocument.status)
                : timelineDocument.status
    }

    function openJobsWorkspace() {
        const appWindow = root.Window.window
        if (appWindow)
            appWindow.currentWorkspace = appWindow.exportWorkspaceIndex
    }

    function toggleSourcePanel() {
        sourcePanelCollapsed = !sourcePanelCollapsed
    }

    function toggleEffectsPanel() {
        effectsPanelCollapsed = !effectsPanelCollapsed
    }

    function restoreDefaultLayout() {
        sourcePanelCollapsed = false
        sourcePanelHidden = false
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

    function openMediaPicker() {
        mediaPickerScope = hasProjectScopedMediaPicker() ? "project" : "all"
        refreshMediaPickerAssets()
        mediaPickerDialog.openDialog()
    }

    function setMediaPickerScope(scope) {
        if (mediaPickerScope === scope)
            return
        mediaPickerScope = scope
        refreshMediaPickerAssets()
    }

    function addPickedMedia() {
        if (!canLoadPickedMedia())
            return

        timelineDocument.loadSourceClipFromGallery(
                    mediaPickerDialog.selectedAssetId,
                    mediaPickerDialog.selectedFilePath,
                    mediaPickerDialog.selectedDisplayLabel,
                    0,
                    mediaPickerDialog.selectedThumbnailSource)
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
        dragPreviewState = {"visible": false, "x": 0, "y": 0, "width": 0}
        snapGuideState = {"visible": false, "seconds": 0, "label": ""}
        trimFeedbackState = {"visible": false, "mode": "", "edge": "", "deltaSeconds": 0, "label": ""}
    }

    function updateSnapGuide(seconds, snapped, labelPrefix) {
        if (snapped) {
            snapGuideState = {"visible": true, "seconds": seconds, "label": labelPrefix + " " + timecodeText(seconds)}
        } else {
            snapGuideState = {"visible": false, "seconds": 0, "label": ""}
        }
    }

    function signedSecondsLabel(value) {
        const seconds = Number(value || 0)
        if (Math.abs(seconds) < 0.005)
            return "0.00s"
        return (seconds > 0 ? "+" : "") + seconds.toFixed(2) + "s"
    }

    function updateTrimFeedback(mode, edge, baselineSeconds, nextSeconds) {
        const normalizedMode = String(mode || "trim")
        const normalizedEdge = String(edge || "")
        const delta = Number(nextSeconds || 0) - Number(baselineSeconds || 0)
        const title = normalizedMode === "slip" ? "Slip"
                    : normalizedMode === "roll" ? "Roll"
                    : normalizedEdge === "out" ? "Trim Out"
                    : "Trim In"
        trimFeedbackState = {
            "visible": true,
            "mode": normalizedMode,
            "edge": normalizedEdge,
            "deltaSeconds": delta,
            "label": title + " " + root.signedSecondsLabel(delta)
        }
    }

    function addMarkerAtPlayhead() {
        timelineDocument.addSequenceMarker()
        markerFeedbackText = "Marker " + root.timecodeText(sequencePlayheadSeconds)
        markerFeedbackVisible = true
        markerFeedbackTimer.restart()
        return markerFeedbackSignature
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

    function clipMetadataText(clip) {
        if (!clip)
            return ""
        if (clip.trackKind === "audio") {
            if (clip.silentPlaceholder)
                return "Silent bed"
            return Number(clip.audioGainDb || 0).toFixed(1) + " dB  -  Fades "
                    + Number(clip.fadeInSeconds || 0).toFixed(1)
                    + "/" + Number(clip.fadeOutSeconds || 0).toFixed(1)
        }
        return String(clip.trimmedDurationText || "")
                + "  -  Op "
                + Math.round(Number(clip.opacity || 1) * 100)
                + "%  -  " + Number(clip.playbackRate || 1).toFixed(2) + "x"
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
            const laneTop = root.trackTopY(trackIndex)
            const laneBottom = laneTop + root.trackHeight(track.trackId)
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

    GalleryPickerDialog {
        id: mediaPickerDialog
        theme: theme
        dialogTitle: "Gallery"
        subtitle: root.mediaPickerSubtitle()
        emptyStateText: root.mediaPickerEmptyStateText()
        selectionHintText: "Choose media"
        showProjectScopeToggle: root.hasProjectScopedMediaPicker()
        scope: root.mediaPickerScope
        assets: root.mediaPickerAssets
        onProjectScopeRequested: root.setMediaPickerScope("project")
        onAllMediaScopeRequested: root.setMediaPickerScope("all")
        onAssetActivated: {
            if (root.canLoadPickedMedia())
                root.addPickedMedia()
        }
        sidePanelFooter: Component {
            ColumnLayout {
                width: parent ? parent.width : undefined
                spacing: 8

                ActionButton {
                    Layout.fillWidth: true
                    text: "Load To Source"
                    tone: "primary"
                    enabled: root.canLoadPickedMedia()
                    disabledReason: root.pickedMediaDisabledReason()
                    onClicked: root.addPickedMedia()
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: "Insert"
                    enabled: root.canLoadPickedMedia()
                    disabledReason: root.pickedMediaDisabledReason()
                    onClicked: root.insertPickedMedia()
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: "Overwrite"
                    enabled: root.canLoadPickedMedia()
                    disabledReason: root.pickedMediaDisabledReason()
                    onClicked: root.overwritePickedMedia()
                }
            }
        }
    }

    Popup {
        id: markerEditorPopup
        modal: true
        focus: true
        width: 360
        x: (root.width - width) / 2
        y: Math.max(24, (root.height - height) / 2)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 12
            color: theme.alpha(theme.glassBase, 0.92)
            border.width: 1
            border.color: theme.alpha(theme.glassEdge, 0.46)

            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.alpha(theme.polar, 0.09) }
                GradientStop { position: 1.0; color: theme.alpha(theme.ink950, 0.34) }
            }
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: "Edit Marker"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            ToneTextField {
                id: markerLabelField
                Layout.fillWidth: true
                placeholderText: "Marker label"
                text: root.markerEditorLabel
                onTextChanged: root.markerEditorLabel = text
            }

            ToneTextField {
                Layout.fillWidth: true
                placeholderText: "Review note"
                text: root.markerEditorNote
                onTextChanged: root.markerEditorNote = text
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: ["todo", "review", "approved", "exported"]

                    delegate: FilterChip {
                        required property var modelData
                        text: String(modelData).charAt(0).toUpperCase() + String(modelData).slice(1)
                        active: root.markerEditorStatus === modelData
                        onClicked: root.markerEditorStatus = modelData
                    }
                }
            }

            ActionButton {
                text: root.markerEditorCompleted ? "Completed" : "Mark Complete"
                compact: true
                active: root.markerEditorCompleted
                onClicked: root.markerEditorCompleted = !root.markerEditorCompleted
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: markerEditorPopup.close()
                }

                ActionButton {
                    text: "Save"
                    tone: "primary"
                    compact: true
                    onClicked: root.saveMarkerEditor()
                }
            }
        }
    }

    ColumnLayout {
        objectName: "timelineSetupSurface"
        anchors.fill: parent
        visible: !timelineDocument.hasActiveTimeline
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
                implicitHeight: newTimelineColumn.implicitHeight + 32
                radius: 12
                color: theme.alpha(theme.ink850, 0.92)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    id: newTimelineColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    SectionHeader {
                        title: "New Timeline"
                        accent: theme.warning
                    }

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
                        spacing: 6

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
                            Layout.preferredWidth: 80
                            text: root.setupWidth
                            onTextChanged: root.setupWidth = text
                        }

                        Label {
                            text: "x"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 11
                        }

                        ToneTextField {
                            Layout.preferredWidth: 80
                            text: root.setupHeight
                            onTextChanged: root.setupHeight = text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "FPS"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["23.976", "24", "25", "29.97", "30", "60"]
                            currentIndex: Math.max(0, model.indexOf(root.setupFrameRate))
                            onActivated: root.setupFrameRate = model[currentIndex]
                        }

                        Label {
                            text: "Audio"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["44100", "48000", "96000"]
                            currentIndex: Math.max(0, model.indexOf(root.setupAudioSampleRate))
                            onActivated: root.setupAudioSampleRate = model[currentIndex]
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "Color"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["Rec.709", "Display P3", "sRGB"]
                            currentIndex: Math.max(0, model.indexOf(root.setupColorSpace))
                            onActivated: root.setupColorSpace = model[currentIndex]
                        }

                        ToneComboBox {
                            Layout.preferredWidth: 100
                            model: ["Legal", "Full"]
                            currentIndex: Math.max(0, model.indexOf(root.setupDataLevels))
                            onActivated: root.setupDataLevels = model[currentIndex]
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "Export"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

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

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 8

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

            Rectangle {
                Layout.preferredWidth: 340
                Layout.minimumWidth: 260
                Layout.fillWidth: true
                implicitHeight: recentColumn.implicitHeight + 32
                Layout.alignment: Qt.AlignTop
                radius: 12
                color: theme.alpha(theme.ink850, 0.92)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    id: recentColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    SectionHeader {
                        title: "Recent Timelines"
                        accent: theme.accentCool400
                    }

                    ListView {
                        id: recentTimelinesList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(320, Math.max(48, count * 52))
                        clip: true
                        model: root.recentTimelineRecords
                        spacing: 4

                        delegate: RecentDocumentRow {
                            required property var modelData
                            required property int index

                            width: recentTimelinesList.width
                            height: 60
                            iconName: "timeline"
                            title: modelData.name || "Untitled"
                            detail: modelData.updatedAt || modelData.createdAt || ""
                            accent: theme.accentCool400
                            actionable: modelData.documentPath && modelData.documentPath.length > 0
                            onActivated: {
                                if (modelData.documentPath && modelData.documentPath.length > 0)
                                    timelineDocument.loadTimeline(modelData.documentPath)
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: recentTimelinesList.count === 0
                        text: "No saved timelines yet."
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

    ColumnLayout {
        anchors.fill: parent
        visible: timelineDocument.hasActiveTimeline
        spacing: 8

        Shortcut {
            sequence: "V"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.activeTool = "select"
        }
        Shortcut {
            sequence: "C"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.activeTool = "razor"
        }
        Shortcut {
            sequence: "Y"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.activeTool = "slip"
        }
        Shortcut {
            sequence: "N"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.activeTool = "roll"
        }
        Shortcut {
            sequence: "S"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: snappingEnabled = !snappingEnabled
        }
        Shortcut {
            sequence: "B"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled() && root.hasPlayableClip()
            onActivated: splitAtPlayhead()
        }
        Shortcut {
            sequence: "M"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: root.addMarkerAtPlayhead()
        }
        Shortcut {
            sequences: [StandardKey.Delete]
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled() && timelineDocument.currentIndex >= 0
            onActivated: root.deleteSelectedTimelineClip()
        }
        Shortcut {
            sequence: "Ctrl+F"
            context: Qt.WindowShortcut
            enabled: timelineDocument.hasActiveTimeline && !markerEditorPopup.visible
            onActivated: timelineSearchField.forceActiveFocus()
        }
        Shortcut {
            sequence: "Space"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled() && root.hasPlayableClip()
            onActivated: sequencePlaying ? timelinePlayback.pause() : timelinePlayback.play()
        }
        Shortcut {
            sequence: "J"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled() && root.hasPlayableClip()
            onActivated: timelinePlayback.stepFrames(-1)
        }
        Shortcut {
            sequence: "K"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelinePlayback.pause()
        }
        Shortcut {
            sequence: "L"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled() && root.hasPlayableClip()
            onActivated: timelinePlayback.play()
        }
        Shortcut {
            sequence: "Alt+Right"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.seekNextEditPoint()
        }
        Shortcut {
            sequence: "Alt+Left"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.seekPreviousEditPoint()
        }
        Shortcut {
            sequence: "Alt+Down"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.seekNextSequenceMarker()
        }
        Shortcut {
            sequence: "Alt+Up"
            context: Qt.WindowShortcut
            enabled: root.shortcutsEnabled()
            onActivated: timelineDocument.seekPreviousSequenceMarker()
        }

        RowLayout {
            objectName: "timelineHeaderBar"
            readonly property string visibilityRole: "always-visible|timeline-name-format-media-save"
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

            MenuButton {
                text: root.hasActiveTimelineRenderJobs() ? "Rendering" : "Save"
                iconName: "save"
                compact: true
                tooltipText: "Save timeline to the library"
                tone: "primary"
                enabled: timelineDocument.clipCount > 0 ||
                         (jobQueue && jobQueue.totalJobCountForKind("timeline_render") > 0)
                menuItems: root.timelineHeaderMenuItems()
                menuWidth: 190
                onClicked: {
                    if (timelineDocument.clipCount > 0 && !root.hasActiveTimelineRenderJobs())
                        root.saveTimelineToLibrary()
                }
                onMenuItemTriggered: root.handleTimelineHeaderMenuAction(key)
            }

            ActionButton {
                text: "Jobs"
                compact: true
                visible: jobQueue && jobQueue.totalJobCountForKind("timeline_render") > 0
                onClicked: root.openJobsWorkspace()
            }
        }

        RowLayout {
            objectName: "timelineStatusBar"
            readonly property string visibilityRole: "always-visible|timecode-source-tool-mode-render-status"
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
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 8
                elide: Text.ElideNone
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

            ActionButton {
                text: sourcePanelHidden ? "Show Source" : "Hide Source"
                compact: true
                tooltipText: sourcePanelHidden ? "Show source panel" : "Hide source panel"
                active: !sourcePanelHidden
                onClicked: sourcePanelHidden = !sourcePanelHidden
            }

            Label {
                text: timelineDocument.dirty ? "Unsaved changes" : root.timelineRenderStatus()
                color: timelineDocument.dirty ? theme.accentWarm300 : theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
            }
        }

        SplitView {
            objectName: "timelineMainSplit"
            readonly property string visibilityRole: "media-surface|source-program-effects"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            TimelineSourcePanel {
                id: sourceRail
                objectName: "timelineSourceRail"
                visible: !sourcePanelHidden
                SplitView.preferredWidth: sourcePanelHidden ? 0 : (sourcePanelCollapsed ? 56 : sourcePanelWidth)
                SplitView.minimumWidth: sourcePanelHidden ? 0 : (sourcePanelCollapsed ? 56 : 260)
                SplitView.maximumWidth: sourcePanelHidden ? 0 : (sourcePanelCollapsed ? 56 : 460)
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
                objectName: "timelineProgramPanel"
                readonly property string surfaceContract: "timeline-program-panel:v1|monitor-dominant:true|transport-compact:true|empty-action:media-picker|error-nonblocking:true|no-overlap:true"
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
                        objectName: "timelineProgramMonitor"
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
                            asynchronous: true
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
                        objectName: "timelineTransportBar"
                        readonly property string transportContract: "timeline-transport:v1|playback:first|edit-actions:compact|timecode:stable|slider:stable|disabled-reasons:true|no-elide:true|no-overlap:true"
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
                                    objectName: "timelinePlayPauseButton"
                                    iconName: sequencePlaying ? "pause" : "play"
                                    showText: false
                                    compact: true
                                    tooltipText: sequencePlaying ? "Pause playback" : "Play sequence"
                                    enabled: root.hasPlayableClip()
                                    disabledReason: root.playableClipDisabledReason()
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
                                    disabledReason: "Add clips to the timeline before seeking."
                                    onClicked: timelinePlayback.seekSequenceSeconds(0)
                                }

                                ActionButton {
                                    iconName: "step-back"
                                    showText: false
                                    compact: true
                                    tooltipText: "Step back one frame"
                                    enabled: root.hasPlayableClip()
                                    disabledReason: root.playableClipDisabledReason()
                                    onClicked: timelinePlayback.stepFrames(-1)
                                }

                                ActionButton {
                                    iconName: "step-forward"
                                    showText: false
                                    compact: true
                                    tooltipText: "Step forward one frame"
                                    enabled: root.hasPlayableClip()
                                    disabledReason: root.playableClipDisabledReason()
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
                                    disabledReason: root.playableClipDisabledReason()
                                    onClicked: splitAtPlayhead()
                                }

                                ActionButton {
                                    objectName: "timelineInsertSourceButton"
                                    iconName: "insert"
                                    showText: false
                                    compact: true
                                    tooltipText: "Insert source at playhead"
                                    enabled: timelineDocument.hasSourceClip
                                    disabledReason: root.sourceClipDisabledReason()
                                    onClicked: timelineDocument.insertSourceClipAtPlayhead()
                                }

                                ActionButton {
                                    objectName: "timelineOverwriteSourceButton"
                                    iconName: "overwrite"
                                    showText: false
                                    compact: true
                                    tooltipText: "Overwrite with source at playhead"
                                    enabled: timelineDocument.hasSourceClip
                                    disabledReason: root.sourceClipDisabledReason()
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
                                    disabledReason: root.moveClipEarlierDisabledReason()
                                    onClicked: timelineDocument.moveSelectedBackward()
                                }

                                ActionButton {
                                    iconName: "move-forward"
                                    showText: false
                                    compact: true
                                    tooltipText: "Move clip later"
                                    enabled: timelineDocument.currentIndex >= 0
                                             && timelineDocument.currentIndex < timelineDocument.clipCount - 1
                                    disabledReason: root.moveClipLaterDisabledReason()
                                    onClicked: timelineDocument.moveSelectedForward()
                                }

                                ActionButton {
                                    iconName: "delete"
                                    showText: false
                                    compact: true
                                    tooltipText: rippleModeEnabled ? "Extract selected clip and close gap" : "Lift selected clip and leave gap"
                                    enabled: timelineDocument.currentIndex >= 0
                                    disabledReason: root.selectedClipDisabledReason()
                                    onClicked: root.deleteSelectedTimelineClip()
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: timecodeText(sequencePlayheadSeconds)
                                    color: theme.accentWarm300
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                }
                            }

                            ToolValueSlider {
                                id: transportSlider
                                Layout.fillWidth: true
                                label: "Transport"
                                valueText: timecodeText(sequencePlayheadSeconds) + " / " + timecodeText(sequenceDurationSeconds())
                                tooltipText: "Timeline transport - " + valueText
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
                objectName: "timelineEffectsRail"
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
            objectName: "timelineSurfacePanel"
            readonly property string surfaceContract: "timeline-surface:v1|toolbar:compact|search-filter:contextual|tools:one-expansion|tracks:stable|clips:readable|markers:contextual|minimap:secondary|waveform:track-contained|no-overlap:true"
            Layout.fillWidth: true
            Layout.preferredHeight: 378
            radius: 12
            color: theme.alpha(theme.ink900, 0.84)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.55)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    objectName: "timelineSurfaceHeader"
                    readonly property string visibilityRole: "always-visible|track-count-marker-count-add-marker-add-track-timecode"
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

                    StatusChip {
                        objectName: "timelineMarkerFeedbackChip"
                        visible: root.markerFeedbackVisible
                        text: root.markerFeedbackText
                        tone: "accent"
                        iconName: "star"
                        compact: true
                        minimumReadableWidth: 118
                        maximumReadableWidth: 180
                        tooltipText: "Marker added at the current playhead."
                    }

                    Item { Layout.fillWidth: true }

                    ActionButton {
                        text: "Marker"
                        iconName: "plus"
                        compact: true
                        tooltipText: "Add marker at playhead"
                        onClicked: root.addMarkerAtPlayhead()
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

                RowLayout {
                    objectName: "timelineSearchBar"
                    readonly property string visibilityRole: "contextual|clip-marker-search-filter-navigation-clear"
                    Layout.fillWidth: true
                    spacing: 6

                    ToneTextField {
                        id: timelineSearchField
                        Layout.fillWidth: true
                        implicitHeight: theme.controlHeightDense
                        placeholderText: "Search clips and markers"
                        text: root.timelineSearchQuery
                        onTextChanged: root.timelineSearchQuery = text
                        onAccepted: root.jumpToSearchResult(1)
                    }

                    Label {
                        text: root.searchMatchCount() + " matches"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    ActionButton {
                        text: "Prev"
                        compact: true
                        enabled: root.searchMatchCount() > 0
                        onClicked: root.jumpToSearchResult(-1)
                    }

                    ActionButton {
                        text: "Next"
                        compact: true
                        enabled: root.searchMatchCount() > 0
                        onClicked: root.jumpToSearchResult(1)
                    }

                    ActionButton {
                        text: "Clear"
                        compact: true
                        enabled: root.timelineSearchActive
                        onClicked: {
                            root.timelineSearchQuery = ""
                            root.markerStatusFilter = "all"
                        }
                    }
                }

                Flow {
                    objectName: "timelineMarkerFilterBar"
                    readonly property string visibilityRole: "contextual|marker-status-filter"
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: [
                            {"label": "All", "value": "all"},
                            {"label": "Open", "value": "open"},
                            {"label": "Todo", "value": "todo"},
                            {"label": "Review", "value": "review"},
                            {"label": "Approved", "value": "approved"},
                            {"label": "Exported", "value": "exported"}
                        ]

                        delegate: FilterChip {
                            required property var modelData
                            text: modelData.label
                            active: root.markerStatusFilter === modelData.value
                            onClicked: root.markerStatusFilter = modelData.value
                        }
                    }
                }

                Flow {
                    objectName: "timelineToolBar"
                    readonly property string visibilityRole: "one-expansion|tool-switch-edit-mode-snapping-ripple-zoom-split-delete"
                    Layout.fillWidth: true
                    spacing: 6

                    MenuButton {
                        text: "Tools"
                        iconName: timelineDocument.activeTool === "razor" ? "cut"
                                  : timelineDocument.activeTool === "slip" ? "slip"
                                  : timelineDocument.activeTool === "roll" ? "roll"
                                  : "select"
                        compact: true
                        tooltipText: "Switch timeline tools and edit modes."
                        menuItems: root.timelineToolMenuItems()
                        menuWidth: 218
                        onClicked: timelineDocument.activeTool = "select"
                        onMenuItemTriggered: root.handleTimelineToolMenuAction(key)
                    }

                    Components.ToolButton {
                        iconName: "select"
                        compact: true
                        label: "Select tool"
                        shortcut: "V"
                        active: timelineDocument.activeTool === "select"
                        onClicked: timelineDocument.activeTool = "select"
                    }

                    Components.ToolButton {
                        iconName: "cut"
                        compact: true
                        label: "Razor tool"
                        shortcut: "C"
                        active: timelineDocument.activeTool === "razor"
                        onClicked: timelineDocument.activeTool = "razor"
                    }

                    Components.ToolButton {
                        iconName: "slip"
                        compact: true
                        label: "Slip tool"
                        shortcut: "Y"
                        active: timelineDocument.activeTool === "slip"
                        onClicked: timelineDocument.activeTool = "slip"
                    }

                    Components.ToolButton {
                        iconName: "roll"
                        compact: true
                        label: "Roll tool"
                        shortcut: "N"
                        active: timelineDocument.activeTool === "roll"
                        onClicked: timelineDocument.activeTool = "roll"
                    }

                    Components.ToolButton {
                        label: "Insert"
                        iconText: "Ins"
                        showLabel: true
                        compact: true
                        active: timelineDocument.editMode === "insert"
                        onClicked: timelineDocument.editMode = "insert"
                    }

                    Components.ToolButton {
                        label: "Overwrite"
                        iconText: "Ovr"
                        showLabel: true
                        compact: true
                        active: timelineDocument.editMode === "overwrite"
                        onClicked: timelineDocument.editMode = "overwrite"
                    }

                    Components.ToolButton {
                        iconName: "link"
                        compact: true
                        label: timelineDocument.linkedSelectionEnabled ? "Disable linked selection" : "Enable linked selection"
                        active: timelineDocument.linkedSelectionEnabled
                        onClicked: timelineDocument.linkedSelectionEnabled = !timelineDocument.linkedSelectionEnabled
                    }

                    Components.ToolButton {
                        iconName: "snap"
                        compact: true
                        label: snappingEnabled ? "Disable snapping" : "Enable snapping"
                        shortcut: "S"
                        active: snappingEnabled
                        onClicked: snappingEnabled = !snappingEnabled
                    }

                    Components.ToolButton {
                        iconName: "ripple"
                        compact: true
                        label: rippleModeEnabled ? "Disable ripple trim" : "Enable ripple trim"
                        active: rippleModeEnabled
                        enabled: timelineDocument.currentIndex >= 0
                        disabledReason: "Select a timeline clip before changing ripple trim."
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
                        tooltipText: rippleModeEnabled ? "Extract selected clip and close gap" : "Lift selected clip and leave gap"
                        enabled: timelineDocument.currentIndex >= 0
                        onClicked: root.deleteSelectedTimelineClip()
                    }

                    Rectangle {
                        width: 1
                        height: 18
                        color: theme.alpha(theme.lineSoft, 0.78)
                    }

                    ToolValueSlider {
                        id: zoomSlider
                        implicitWidth: 120
                        label: "Timeline zoom"
                        valueText: Math.round(root.timelineZoom / 84 * 100) + "%"
                        tooltipText: "Timeline zoom - " + valueText
                        from: Math.log2(root.timelineZoomMin)
                        to: Math.log2(root.timelineZoomMax)
                        value: Math.log2(root.timelineZoom)
                        onMoved: root.setTimelineZoom(Math.pow(2, value), timelineViewport.contentX + timelineViewport.width / 2)
                    }

                    Label {
                        text: Math.round(root.timelineZoom / 84 * 100) + "%"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 9
                    }

                    ActionButton {
                        text: "Fit"
                        compact: true
                        tooltipText: "Zoom to fit all clips"
                        onClicked: root.zoomToFit()
                    }
                }

                Rectangle {
                    objectName: "timelineMarkerStrip"
                    readonly property string visibilityRole: "contextual|marker-review-jump-edit-complete"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 88
                    radius: 10
                    color: theme.alpha(theme.ink950, 0.56)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.3)
                    visible: sequenceMarkerModel.length > 0

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 8
                        orientation: ListView.Horizontal
                        spacing: 8
                        clip: true
                        model: sequenceMarkerModel

                        delegate: TimelineMarkerCard {
                            required property var modelData
                            visible: root.markerMatchesFilters(modelData)
                            width: visible ? 254 : 0
                            height: parent ? parent.height - 4 : 0
                            objectName: "timelineMarkerCard"
                            markerId: String(modelData.markerId || "")
                            title: String(modelData.label || "Marker")
                            note: String(modelData.note || "")
                            timecode: String(modelData.timecode || "")
                            statusText: root.markerStatusLabel(modelData.status, modelData.completed)
                            statusTone: root.markerStatusTone(modelData)
                            completed: Boolean(modelData.completed)
                            accent: root.markerAccentColor(modelData)
                            onJumpRequested: function(markerId) {
                                timelineDocument.seekSequenceMarker(markerId)
                            }
                            onEditRequested: function(markerId) {
                                root.openMarkerEditor(modelData)
                            }
                            onCompletionToggled: function(markerId, completed) {
                                timelineDocument.setSequenceMarkerCompleted(markerId, completed)
                            }
                        }
                    }
                }

                Rectangle {
                    id: minimapBar
                    objectName: "timelineMinimapBar"
                    readonly property string visibilityRole: "secondary|overview-scroll-playhead"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    Layout.leftMargin: laneHeaderWidth
                    radius: 4
                    color: theme.alpha(theme.ink950, 0.78)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.32)
                    clip: true
                    visible: sequenceDurationSeconds() > 0

                    readonly property real barDuration: Math.max(1, sequenceDurationSeconds())
                    readonly property real barScale: (width - 4) / barDuration

                    Repeater {
                        model: sequenceTrackModel

                        delegate: Repeater {
                            id: minimapTrackRepeater
                            required property var modelData
                            required property int index
                            model: modelData.clips

                            delegate: Rectangle {
                                required property var modelData

                                x: 2 + Number(modelData.startSeconds || 0) * minimapBar.barScale
                                y: 2
                                width: Math.max(2, Number(modelData.trimmedDurationSeconds || 0) * minimapBar.barScale)
                                height: minimapBar.height - 4
                                radius: 2
                                color: modelData.trackKind === "audio"
                                       ? theme.alpha(theme.accentCool400, 0.5)
                                       : theme.alpha(theme.accentWarm400, 0.5)
                            }
                        }
                    }

                    Rectangle {
                        x: 2 + (timelineViewport.contentX / timelineZoom) * minimapBar.barScale
                        y: 0
                        width: Math.max(20, (timelineViewport.width / timelineZoom) * minimapBar.barScale)
                        height: parent ? parent.height : 0
                        radius: 3
                        color: theme.alpha(theme.textStrong, 0.12)
                        border.width: 1
                        border.color: theme.alpha(theme.textStrong, 0.4)
                    }

                    Rectangle {
                        x: 2 + sequencePlayheadSeconds * minimapBar.barScale
                        y: 0
                        width: 1
                        height: parent ? parent.height : 0
                        color: theme.accentWarm500
                    }

                    MouseArea {
                        anchors.fill: parent
                        onPressed: function(mouse) {
                            timelineViewport.contentX = Math.max(0, (mouse.x - 2) / minimapBar.barScale * timelineZoom - timelineViewport.width / 2)
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            timelineViewport.contentX = Math.max(0, (mouse.x - 2) / minimapBar.barScale * timelineZoom - timelineViewport.width / 2)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Item {
                        objectName: "timelineTrackHeaderColumn"
                        Layout.preferredWidth: laneHeaderWidth
                        Layout.fillHeight: true
                        clip: true

                        Column {
                            id: trackHeaders
                            y: -timelineViewport.contentY
                            spacing: trackSpacing

                            Rectangle {
                                width: laneHeaderWidth
                                height: rulerHeight
                                color: "transparent"
                            }

                            Repeater {
                                model: sequenceTrackModel

                                delegate: Column {
                                    required property var modelData
                                    required property int index

                                    width: laneHeaderWidth
                                    spacing: 0

                                    Rectangle {
                                        visible: index > 0 && modelData.trackKind === "audio"
                                                 && index < sequenceTrackModel.length
                                                 && sequenceTrackModel[index - 1] !== undefined
                                                 && sequenceTrackModel[index - 1].trackKind === "video"
                                        width: parent.width
                                        height: 2
                                        color: theme.alpha(theme.accentWarm400, 0.5)
                                    }

                                    TimelineTrackHeader {
                                        trackData: parent.modelData
                                        width: laneHeaderWidth
                                        height: root.trackHeight(trackData.trackId)
                                        theme: theme
                                        timelineDocument: timelineDocument
                                        onResizeRequested: function(delta) {
                                            root.setTrackHeight(trackData.trackId, height + delta)
                                        }
                                    }
                                }
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
                            objectName: "timelineViewport"
                            anchors.fill: parent
                            contentWidth: timelineContentWidth
                            contentHeight: timelineContentHeight
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ScrollBar.horizontal: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }

                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }

                            WheelHandler {
                                target: null
                                enabled: timelineDocument.hasActiveTimeline
                                onWheel: function(event) {
                                    if (event.modifiers & Qt.ControlModifier || event.modifiers & Qt.MetaModifier) {
                                        const step = event.angleDelta.y > 0 ? 1.15 : (1.0 / 1.15)
                                        root.setTimelineZoom(timelineZoom * step, timelineViewport.contentX + event.x)
                                        event.accepted = true
                                    }
                                }
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
                                        if (mouse.y < rulerHeight) {
                                            root.seekTimelineFromX(mouse.x)
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
                                        timelineDocument.selectClip(-1)
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (root.marqueeActive) {
                                            root.marqueeCurrentX = mouse.x
                                            root.marqueeCurrentY = mouse.y
                                            return
                                        }
                                        if (pressed && mouse.y < rulerHeight)
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

                                AppMenu {
                                    id: timelineCanvasMenu
                                    objectName: "timelineCanvasMenu"
                                    accent: theme.accentWarm500

                                    AppMenuItem {
                                        text: "Add Marker At Playhead"
                                        iconName: "star"
                                        accent: timelineCanvasMenu.accent
                                        onTriggered: root.addMarkerAtPlayhead()
                                    }

                                    AppMenuItem {
                                        text: "Insert Source Here"
                                        iconName: "plus"
                                        accent: timelineCanvasMenu.accent
                                        enabled: timelineDocument.hasSourceClip
                                        onTriggered: timelineDocument.insertSourceClipAtPlayhead()
                                    }

                                    AppMenuItem {
                                        text: "Overwrite Source Here"
                                        iconName: "overwrite"
                                        accent: timelineCanvasMenu.accent
                                        enabled: timelineDocument.hasSourceClip
                                        onTriggered: timelineDocument.overwriteSourceClipAtPlayhead()
                                    }
                                }

                                Rectangle {
                                    width: timelineContentWidth
                                    height: rulerHeight
                                    color: theme.alpha(theme.ink950, 0.62)

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: theme.alpha(theme.lineSoft, 0.55)
                                    }

                                    Repeater {
                                        readonly property real interval: root.rulerInterval()
                                        model: interval > 0 ? Math.max(2, Math.ceil(sequenceDurationSeconds() / interval) + 1) : 0

                                        delegate: Item {
                                            required property int index

                                            readonly property real seconds: index * parent.interval
                                            readonly property bool isMajor: {
                                                const iv = parent.interval
                                                if (iv < 1) return (index % Math.max(1, Math.round(Number(timelineDocument.framesPerSecond || 30)))) === 0
                                                if (iv <= 1) return index % 5 === 0
                                                if (iv <= 5) return index % 2 === 0
                                                return index % 3 === 0
                                            }

                                            x: seconds * timelineZoom
                                            y: 0
                                            width: 1
                                            height: rulerHeight

                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                width: 1
                                                height: isMajor ? rulerHeight - 4 : rulerHeight / 2 - 2
                                                color: isMajor ? theme.alpha(theme.lineStrong, 0.46) : theme.alpha(theme.lineSoft, 0.28)
                                            }

                                            Label {
                                                visible: isMajor
                                                anchors.top: parent.top
                                                anchors.topMargin: 3
                                                anchors.left: parent.left
                                                anchors.leftMargin: 4
                                                text: root.rulerLabel(seconds)
                                                color: theme.textMuted
                                                font.family: theme.fontMono
                                                font.pixelSize: 9
                                            }
                                        }
                                    }

                                    Canvas {
                                        id: playheadTriangle
                                        x: sequencePlayheadSeconds * timelineZoom - 6
                                        y: 0
                                        width: 13
                                        height: rulerHeight
                                        onXChanged: requestPaint()
                                        onPaint: {
                                            var ctx = getContext("2d")
                                            ctx.reset()
                                            ctx.fillStyle = theme.accentWarm500
                                            ctx.beginPath()
                                            ctx.moveTo(0, 0)
                                            ctx.lineTo(13, 0)
                                            ctx.lineTo(6.5, rulerHeight - 2)
                                            ctx.closePath()
                                            ctx.fill()
                                        }
                                    }
                                }

                                Repeater {
                                    readonly property real interval: root.rulerInterval()
                                    model: interval >= 1 ? Math.max(2, Math.ceil(sequenceDurationSeconds() / interval) + 1) : 0

                                    delegate: Rectangle {
                                        required property int index

                                        readonly property real seconds: index * parent.interval
                                        readonly property bool isMajor: {
                                            const iv = parent.interval
                                            if (iv <= 1) return index % 5 === 0
                                            if (iv <= 5) return index % 2 === 0
                                            return index % 3 === 0
                                        }

                                        x: seconds * timelineZoom
                                        y: rulerHeight
                                        width: 1
                                        height: timelineContentHeight - rulerHeight
                                        color: isMajor ? theme.alpha(theme.lineStrong, 0.22) : theme.alpha(theme.lineSoft, 0.12)
                                    }
                                }

                                Repeater {
                                    model: sequenceMarkerModel

                                    delegate: TimelineMarkerPin {
                                        required property var modelData
                                        visible: root.markerMatchesFilters(modelData)

                                        x: Number(modelData.seconds || 0) * timelineZoom
                                        y: 0
                                        height: timelineContentHeight
                                        markerId: String(modelData.markerId || "")
                                        label: String(modelData.label || "Marker")
                                        accent: root.markerAccentColor(modelData)
                                        onJumpRequested: function(markerId) {
                                            timelineDocument.seekSequenceMarker(markerId)
                                        }
                                        onEditRequested: function(markerId) {
                                            root.openMarkerEditor(modelData)
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
                                        y: root.trackTopY(index)
                                        width: timelineContentWidth
                                        height: root.trackHeight(modelData.trackId)

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
                                                readonly property bool searchMatch: root.clipMatchesSearch(modelData)

                                                property real committedX: Number(modelData.startSeconds || 0) * timelineZoom + 3
                                                property real dragOffsetX: 0
                                                property real dragOffsetY: 0
                                                property real pressX: 0
                                                property real pressY: 0
                                                property real trimHandleWidth: modelData.trackKind === "video" ? 18 : 0
                                                property real fadeHandleWidth: 18

                                                x: committedX + dragOffsetX
                                                y: 6 + dragOffsetY
                                                width: root.clipWidth(modelData.trimmedDurationSeconds) - 6
                                                height: parent ? parent.height - 12 : 0
                                                radius: 8
                                                color: {
                                                    if (modelData.trackKind === "audio") {
                                                        if (modelData.silentPlaceholder)
                                                            return theme.alpha(theme.ink700, 0.78)
                                                        return modelData.selected
                                                            ? theme.alpha(theme.accentCool400, 0.42)
                                                            : theme.alpha(theme.accentCool400, 0.26)
                                                    }
                                                    const baseColor = modelData.mediaType === "photo"
                                                        ? theme.accentCool500
                                                        : modelData.mediaType === "audio"
                                                            ? theme.warning
                                                            : theme.accentWarm500
                                                    const opacityScale = Math.max(0.18, Number(modelData.opacity || 1))
                                                    return modelData.selected
                                                        ? theme.alpha(baseColor, opacityScale * 0.82)
                                                        : theme.alpha(baseColor, opacityScale * 0.34)
                                                }
                                                border.width: modelData.selected ? 2 : 1
                                                border.color: modelData.selected
                                                              ? theme.textStrong
                                                              : clipBlock.searchMatch && root.timelineSearchQuery.trim().length > 0
                                                                ? theme.accentWarm300
                                                              : theme.alpha(theme.lineStrong, 0.4)
                                                opacity: root.timelineSearchActive ? (clipBlock.searchMatch ? 1 : 0.22) : 1

                                                MouseArea {
                                                    id: clipMouseArea
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    preventStealing: true
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
                                                            if (!timelineDocument.splitSelectedClip(root.clipSequenceTime(modelData, mouse.x)))
                                                                splitFailFlash.restart()
                                                            return
                                                        }
                                                        clipBlock.pressX = mouse.x
                                                        clipBlock.pressY = mouse.y
                                                        if (mouse.modifiers & Qt.ControlModifier || mouse.modifiers & Qt.MetaModifier) {
                                                            timelineDocument.toggleClipSelection(modelData.index)
                                                            return
                                                        }
                                                        if (mouse.modifiers & Qt.ShiftModifier) {
                                                            timelineDocument.selectClipRange(timelineDocument.currentIndex, modelData.index)
                                                            return
                                                        }
                                                        timelineDocument.selectClip(modelData.index)
                                                        if (modelData.trackKind === "video" && timelineDocument.activeTool === "select")
                                                            root.buildSnapPointsCache(modelData.clipId)
                                                        if (modelData.trackKind === "video" && timelineDocument.activeTool === "slip")
                                                            timelineDocument.beginContinuousEdit()
                                                    }
                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed || modelData.trackKind !== "video"
                                                                || trackLane.modelData.locked)
                                                            return
                                                        if (timelineDocument.activeTool === "select") {
                                                            clipBlock.dragOffsetX = mouse.x - clipBlock.pressX
                                                            clipBlock.dragOffsetY = mouse.y - clipBlock.pressY
                                                            const now = Date.now()
                                                            if (now - root.lastSnapCalcTime < 16)
                                                                return
                                                            root.lastSnapCalcTime = now
                                                            const targetRow = Math.max(
                                                                        0,
                                                                        Math.min(
                                                                            sequenceTrackModel.length - 1,
                                                                            trackLane.trackRowIndex + Math.round(clipBlock.dragOffsetY / (root.trackHeight(trackLane.modelData.trackId) + trackSpacing))))
                                                            const snapResult = root.snapSequenceResult(
                                                                        Math.max(0, (clipBlock.committedX + clipBlock.dragOffsetX - 3) / timelineZoom),
                                                                        modelData.clipId, true)
                                                            root.dragPreviewState = {
                                                                "visible": true,
                                                                "width": clipBlock.width,
                                                                "x": snapResult.seconds * timelineZoom + 3,
                                                                "y": root.trackTopY(targetRow) + 6
                                                            }
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, "Snap")
                                                        } else if (timelineDocument.activeTool === "slip") {
                                                            const deltaSeconds = (mouse.x - clipBlock.pressX) / timelineZoom
                                                            timelineDocument.slipSelectedClipBySeconds(deltaSeconds)
                                                            root.updateTrimFeedback("slip", "body", 0, deltaSeconds)
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
                                                                            trackLane.trackRowIndex + Math.round(clipBlock.dragOffsetY / (root.trackHeight(trackLane.modelData.trackId) + trackSpacing))))
                                                            const snapResult = root.snapSequenceResult(
                                                                        Math.max(0, (clipBlock.committedX + clipBlock.dragOffsetX - 3) / timelineZoom),
                                                                        modelData.clipId, false)
                                                            timelineDocument.moveSelectedClipToTrackRow(targetRow, snapResult.seconds)
                                                        }
                                                        if (modelData.trackKind === "video" && timelineDocument.activeTool === "slip")
                                                            timelineDocument.endContinuousEdit()
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

                                                AppMenu {
                                                    id: clipMenu
                                                    objectName: "timelineClipMenu"
                                                    accent: theme.accentWarm500

                                                    AppMenuItem {
                                                        text: "Split At Playhead"
                                                        iconName: "cut"
                                                        accent: clipMenu.accent
                                                        enabled: root.hasPlayableClip()
                                                        onTriggered: splitAtPlayhead()
                                                    }

                                                    AppMenuItem {
                                                        text: "Move Earlier"
                                                        iconName: "chevron-left"
                                                        accent: clipMenu.accent
                                                        enabled: timelineDocument.currentIndex > 0
                                                        onTriggered: timelineDocument.moveSelectedBackward()
                                                    }

                                                    AppMenuItem {
                                                        text: "Move Later"
                                                        iconName: "chevron-right"
                                                        accent: clipMenu.accent
                                                        enabled: timelineDocument.currentIndex >= 0
                                                                 && timelineDocument.currentIndex < timelineDocument.clipCount - 1
                                                        onTriggered: timelineDocument.moveSelectedForward()
                                                    }

                                                    AppMenuItem {
                                                        text: rippleModeEnabled ? "Extract Clip" : "Lift Clip"
                                                        iconName: "delete"
                                                        danger: true
                                                        accent: clipMenu.accent
                                                        enabled: timelineDocument.currentIndex >= 0
                                                        onTriggered: root.deleteSelectedTimelineClip()
                                                    }
                                                }

                                                Row {
                                                    id: posterStrip
                                                    anchors.fill: parent
                                                    anchors.margins: 2
                                                    clip: true
                                                    visible: modelData.trackKind === "video"
                                                             && (modelData.posterSource || "").length > 0
                                                    z: -1
                                                    spacing: 1

                                                    Repeater {
                                                        model: Math.max(1, Math.floor(clipBlock.width / Math.max(1, clipBlock.height - 4)))

                                                        delegate: Image {
                                                            required property int index
                                                            width: clipBlock.height - 4
                                                            height: posterStrip.height
                                                            source: modelData.posterSource || ""
                                                            fillMode: Image.PreserveAspectCrop
                                                            asynchronous: true
                                                            cache: true
                                                            opacity: 0.28
                                                        }
                                                    }
                                                }

                                                Image {
                                                    anchors.fill: parent
                                                    anchors.margins: 4
                                                    source: (modelData.waveformSource || "").length > 0 ? modelData.waveformSource : ""
                                                    fillMode: Image.Stretch
                                                    opacity: modelData.trackKind === "audio" ? 0.42 : 0.22
                                                    visible: !modelData.silentPlaceholder
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
                                                    color: leftTrimHandle.containsMouse || leftTrimHandle.pressed
                                                           ? theme.alpha(theme.accentWarm300, 0.32)
                                                           : theme.alpha(theme.textStrong, 0.16)

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
                                                        hoverEnabled: true
                                                        onPressed: function(mouse) {
                                                            if (timelineDocument.activeTool === "razor") {
                                                                mouse.accepted = false
                                                                return
                                                            }
                                                            if (trackLane.modelData.locked)
                                                                return
                                                            timelinePlayback.pause()
                                                            timelineDocument.selectClip(modelData.index)
                                                            timelineDocument.beginContinuousEdit()
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const snapResult = root.snapSequenceResult(point.x / timelineZoom, modelData.clipId)
                                                            root.updateTrimFeedback(timelineDocument.activeTool === "roll" ? "roll" : "trim",
                                                                                    "in",
                                                                                    Number(modelData.startSeconds || 0),
                                                                                    snapResult.seconds)
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
                                                            root.updateTrimFeedback(timelineDocument.activeTool === "roll" ? "roll" : "trim",
                                                                                    "in",
                                                                                    Number(modelData.startSeconds || 0),
                                                                                    snapResult.seconds)
                                                            if (timelineDocument.activeTool === "roll")
                                                                timelineDocument.rollSelectedClipStartToSequenceSeconds(snapResult.seconds)
                                                            else
                                                                timelineDocument.trimSelectedClipStartToSequenceSeconds(snapResult.seconds, rippleModeEnabled)
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, timelineDocument.activeTool === "roll" ? "Roll" : "Trim")
                                                        }
                                                        onReleased: {
                                                            timelineDocument.endContinuousEdit()
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
                                                    color: rightTrimHandle.containsMouse || rightTrimHandle.pressed
                                                           ? theme.alpha(theme.accentWarm300, 0.32)
                                                           : theme.alpha(theme.textStrong, 0.16)

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
                                                        hoverEnabled: true
                                                        onPressed: function(mouse) {
                                                            if (timelineDocument.activeTool === "razor") {
                                                                mouse.accepted = false
                                                                return
                                                            }
                                                            if (trackLane.modelData.locked)
                                                                return
                                                            timelinePlayback.pause()
                                                            timelineDocument.selectClip(modelData.index)
                                                            timelineDocument.beginContinuousEdit()
                                                            const point = parent.mapToItem(trackLane, mouse.x, mouse.y)
                                                            const snapResult = root.snapSequenceResult(point.x / timelineZoom, modelData.clipId)
                                                            root.updateTrimFeedback(timelineDocument.activeTool === "roll" ? "roll" : "trim",
                                                                                    "out",
                                                                                    Number(modelData.endSeconds || 0),
                                                                                    snapResult.seconds)
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
                                                            root.updateTrimFeedback(timelineDocument.activeTool === "roll" ? "roll" : "trim",
                                                                                    "out",
                                                                                    Number(modelData.endSeconds || 0),
                                                                                    snapResult.seconds)
                                                            if (timelineDocument.activeTool === "roll")
                                                                timelineDocument.rollSelectedClipEndToSequenceSeconds(snapResult.seconds)
                                                            else
                                                                timelineDocument.trimSelectedClipEndToSequenceSeconds(snapResult.seconds, rippleModeEnabled)
                                                            root.updateSnapGuide(snapResult.seconds, snapResult.snapped, timelineDocument.activeTool === "roll" ? "Roll" : "Trim")
                                                        }
                                                        onReleased: {
                                                            timelineDocument.endContinuousEdit()
                                                            root.resetTimelinePreviewState()
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    objectName: "timelineTrimFeedbackOverlay"
                                                    readonly property string feedbackContract: root.trimFeedbackSignature
                                                    visible: modelData.trackKind === "video"
                                                             && ((root.trimFeedbackState.visible === true && modelData.selected)
                                                                 || leftTrimHandle.pressed || rightTrimHandle.pressed)
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
                                                        objectName: "timelineTrimFeedbackLabel"
                                                        anchors.centerIn: parent
                                                        text: root.trimFeedbackState.visible === true
                                                              && String(root.trimFeedbackState.label || "").length > 0
                                                              ? String(root.trimFeedbackState.label)
                                                              : "In " + Number(modelData.trimStartSeconds || 0).toFixed(2)
                                                                + "  •  Out " + Number(modelData.trimEndSeconds || 0).toFixed(2)
                                                        color: theme.textStrong
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 9
                                                        elide: Text.ElideNone
                                                        fontSizeMode: Text.HorizontalFit
                                                        minimumPixelSize: 7
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
                                                    visible: modelData.trackKind === "video"
                                                             && Math.abs(Number(modelData.playbackRate || 1) - 1.0) > 0.01
                                                    anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.margins: 4
                                                    radius: 6
                                                    color: theme.alpha(theme.warning, 0.9)
                                                    implicitWidth: speedBadgeLabel.implicitWidth + 10
                                                    implicitHeight: speedBadgeLabel.implicitHeight + 4

                                                    Label {
                                                        id: speedBadgeLabel
                                                        anchors.centerIn: parent
                                                        text: Number(modelData.playbackRate || 1).toFixed(1) + "x"
                                                        color: theme.ink950
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 8
                                                        font.weight: Font.Bold
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

                                                Rectangle {
                                                    id: splitFailOverlay
                                                    anchors.fill: parent
                                                    color: theme.alpha(theme.error, 0.3)
                                                    radius: clipBlock.radius
                                                    visible: false
                                                    opacity: 0

                                                    SequentialAnimation {
                                                        id: splitFailFlash
                                                        PropertyAction { target: splitFailOverlay; property: "visible"; value: true }
                                                        NumberAnimation { target: splitFailOverlay; property: "opacity"; from: 0.6; to: 0; duration: 400 }
                                                        PropertyAction { target: splitFailOverlay; property: "visible"; value: false }
                                                    }
                                                }

                                                TimelineClipContent {
                                                    anchors.fill: parent
                                                    title: String(modelData.displayLabel || "Clip")
                                                    metadata: root.clipMetadataText(modelData)
                                                    effects: modelData.effectStack ? modelData.effectStack : []
                                                    horizontalInset: clipBlock.trimHandleWidth
                                                    selected: Boolean(modelData.selected)
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: root.dragPreviewState.visible
                                    x: root.dragPreviewState.x
                                    y: root.dragPreviewState.y
                                    width: root.dragPreviewState.width
                                    height: defaultLaneHeight - 12
                                    radius: 8
                                    color: theme.alpha(theme.accentWarm300, 0.22)
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
                                    visible: root.snapGuideState.visible
                                    x: root.snapGuideState.seconds * timelineZoom
                                    y: 0
                                    width: 2
                                    height: timelineContentHeight
                                    color: theme.accentCool400
                                }

                                Rectangle {
                                    visible: root.snapGuideState.visible
                                    x: Math.max(0, root.snapGuideState.seconds * timelineZoom - snapGuideBubble.implicitWidth / 2)
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
                                        text: root.snapGuideState.label
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
